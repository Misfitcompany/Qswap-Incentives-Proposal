# QSWAP — LP Incentives from recaptured SC dividends — PROPOSAL DRAFT

**Status:** DRAFT (Den of Misfits). **Not submitted.**
**Target:** Qubic core / `QSWAP` (contract index **13**).
**Motivation:** dividend QU that belongs to QCAP/QMINE held inside QSWAP pools is currently **stranded in
the contract, unattributed and growing** — see [`EVIDENCE.md`](EVIDENCE.md) for the live on-chain proof
(**≈ 4,646,728 QU** as of 2026-06-29, accruing ~1M+ QU/epoch). This turns that quiet leak into a
transparent, on-chain **LP incentive** for the pools that keep QSWAP liquid.

> ⚠️ Process note: QSWAP is a **core-protocol** contract, not independently deployable. Nothing here goes
> live by our hand — this is a *proposal* for the Qubic core team, adopted (if accepted) via a network
> upgrade at an epoch boundary. Submitting it means engaging the **public** Qubic repo/process.

> 🙏 We're outside contributors, not core maintainers — you know the storage budget, the gas envelope of a
> per-epoch hook, and the upgrade mechanics far better than we do. Please treat this as a worked-out
> **starting point, not a finished answer** — we'd happily be steered to a lighter capture path, a
> different selection rule, or a different design entirely.

> 📄 **Reference implementation:** [`reference/qswap_incentives.h`](reference/qswap_incentives.h)
> — the additions reconciled against the live QSWAP source. Subset-check extract:
> [`reference/qswap_incentives_check.h`](reference/qswap_incentives_check.h).
>
> ✅ **Validated, not just sketched.** Integrated into the real contract (`core-main`, June 2026),
> compiled clean (0 warnings/errors from our code), and run against GoogleTest: **all 16 `ContractSwap`
> tests pass** — our 6 new incentive tests (volume tracking, the weighted sweep with bottom-% exclusion,
> the recovered-backlog first run, claim payout, auto-claim-on-remove, cross-epoch stacking) plus the 10
> existing QSWAP tests (no regressions). Breakdown in [`TEST-RESULTS.md`](TEST-RESULTS.md).
>
> 🛡️ **Subset-compliant.** The additions also **PASS the official [Qubic Contract Verification
> Tool](https://github.com/Franziska-Mueller/qubic-contract-verify)** — the same forbidden-construct
> checker the `qubic/core` CI runs (`Contract compliance check PASSED`).

---

## 0. The leak, in one paragraph (for everyone)

QCAP is paid dividends by **QVAULT**; QMINE is paid dividends by **qRWA**. Both pay **by possession** —
they walk every holder of the token and transfer each one its share. When you deposit QCAP or QMINE into a
**QSWAP liquidity pool**, the pool contract becomes the holder of record, so the dividend is transferred
**to the QSWAP contract** — which has no code to account for or forward it. The QU just **piles up inside
QSWAP, belonging to no one**. Today that pile is **~4.65M QU and grows every epoch**. This proposal hands
it back to LPs — specifically, to the LPs of the pools doing the most volume, since they're the ones
providing the liquidity that makes QSWAP useful.

---

## 1. Summary

Add a once-per-epoch routine to QSWAP that:

1. **Recaptures** the QU that has quietly arrived in the contract (the QCAP/QMINE dividends, plus — on the
   first run — the entire historical backlog), by comparing the contract's balance to what it has accounted
   for. Balance-based, so it needs no per-event history and catches stray QU no matter who sent it or when.
2. **Measures** each pool's swap volume for the epoch (new lightweight per-pool counter).
3. **Selects** the active pools: drops the **bottom 30% by volume**, keeps the **top 70%**.
4. **Distributes** the recaptured pot to those pools **weighted by volume**, crediting each pool's LPs via a
   per-LP accumulator they can **claim** any time (or that **auto-pays on remove**).

LPs experience it like a second, bonus fee stream — except it's funded by value that is currently leaking
away entirely.

---

## 2. The problem, on-chain (evidence)

Measured live against `rpc.qubic.org` on 2026-06-29 (full method + repro in [`EVIDENCE.md`](EVIDENCE.md)):

| Source | What | Stranded QU | Span |
|---|---|---:|---|
| QVAULT (idx 10) | QCAP dividends on pool-held QCAP | 476,695 | epoch 207–218 |
| qRWA (idx 20) | QMINE dividends on pool-held QMINE | 4,170,033 | epoch 213–219 |
| | **Total stranded** | **≈ 4,646,728** | growing ~1M+/epoch |

Grounded in the deployed contracts: QVAULT pays QCAP possessors and includes pooled QCAP in circulating
supply (`QVAULT.h:3329, 3344-3353`); qRWA pays QMINE possessors, excluding only its own treasury /
fundraising / exchange addresses — **not** QSWAP (`qRWA.h:1571-1587, 1661-1700`); QSWAP takes possession of
deposited tokens (`Qswap.h:1198-1205`) but has **no `POST_INCOMING_TRANSFER`**, tracks pool QU in an
explicit `reservedQuAmount` (so stray QU is invisible to both pricing and LP accounting), and `END_TICK`
distributes only its four *tracked* fee buckets — nothing sweeps the raw balance. ⇒ the dividend QU has no
exit.

---

## 3. The mechanism (what we add)

### New state
- `PoolBasicState` += `uint64 volumeQuEpoch` + `uint128 accIncentivePerLPX64` (mirrors the existing
  `accFeePerLPX64` reward-per-share).
- `LiquidityInfo` += `uint128 incentiveDebtX64` + `uint64 accumulatedIncentive` (mirrors `feeDebtX64` /
  `accumulatedFee`).
- `StateData` += `mIncentiveUnclaimed`, `mIncentiveLifetimeDistributed`, `mIncentiveBottomPctExcluded` (=30).

### Capture — a new `END_EPOCH` (QSWAP has none today)
Read SELF balance via `qpi.getEntity(SELF, …)`; subtract the summed pool reserves, the four undistributed
fee buckets, and the already-earmarked incentive. The remainder is **stray QU** = recaptured dividends.
Because it's balance-derived, the **~4.65M backlog is swept automatically on the first run** — no special
case. (Stray QU that arrives after QSWAP's `END_EPOCH` in a given epoch — e.g. from qRWA, a higher contract
index — is simply caught the next epoch. Order-independent.)

### Measure
Each swap adds its QU side to the pool's `volumeQuEpoch`; `END_EPOCH` reads it, then resets it.

### Select + distribute
Rank participating pools (volume > 0) by volume via a fixed 64-bucket histogram; the **bottom
`mIncentiveBottomPctExcluded`% get nothing**; the rest split the pot **weighted by volume**
(`poolShare = stray × poolVolume / Σ qualifying volume`). Each winning pool's share is credited to
`accIncentivePerLPX64` (`+= poolShare<<64 / totalLiquidity`), exactly like a fee — so LPs realize it
through the same machinery.

---

## 4. The LP experience (confirmed semantics)

- **Don't claim → rewards stack, no expiry.** Claimable = `accumulatedIncentive + liquidity ×
  (accIncentivePerLP − yourDebt)`; the accumulator grows each epoch.
- **`ClaimIncentive(issuer, name)`** (procedure, index 12) — pays your accrued incentive to your wallet,
  liquidity untouched (claim while you keep providing).
- **Remove liquidity → auto-claim.** Any `RemoveLiquidity` (partial or full) pays your accrued incentive
  to your wallet as part of the withdrawal — a full exit never strands it.
- **Add liquidity → stacks** (settled into your accumulator, never a surprise payout).
- **`GetClaimableIncentive(issuer, name, account)`** (function, index 9) — read your claimable QU, the
  pool's incentive-per-LP, and the pool's current epoch volume.

---

## 5. Worked example

Epoch with 5 active pools; recaptured pot = 1,000,000 QU; volumes A=500k, B=300k, C=150k, D=40k, E=10k.
- Bottom 30% of 5 pools = `floor(5 × 0.30) = 1` excluded → **E (10k) gets nothing**.
- Qualifying volume = 500+300+150+40 = 990k. Shares: A=505,050; B=303,030; C=151,515; D=40,404 (∝ volume).
- Each pool's share lands in `accIncentivePerLPX64`; an LP holding 25% of pool A's liquidity can later
  claim ≈ 126,262 QU (or it auto-pays when they remove). A's LPs collectively get ~the full 505,050
  (minus the dust on the pool's permanently-locked `MIN_LIQUIDITY`).

---

## 6. New state & storage (stated honestly)

- `LiquidityInfo` grows by **+24 B/record**. `mLiquidities` capacity = `QSWAP_MAX_POOL ×
  QSWAP_MAX_USER_PER_POOL = 2^21` records → **≈ +48 MiB × X_MULTIPLIER always-on** (record size only; the
  capacity stays the power-of-two the `Collection<>` requires). **This is the number to weigh.**
- `PoolBasicState` grows by +24 B/pool × `QSWAP_MAX_POOL` (8192 × X_MULTIPLIER) ≈ **+192 KiB × X_MULTIPLIER** (trivial).
- *Lighter fallback if +48 MiB is too much:* drop `accumulatedIncentive` and require claim-before-any-
  liquidity-change (≈ +16 MiB) — at the cost of the clean stack-on-add behavior. Open question Q1.

---

## 7. Edge cases / notes

- **Pool with zero volume this epoch** → not "participating", excluded entirely (gets nothing) — not part
  of the bottom-30% count either.
- **Rounding dust** (`stray − Σ distributed`) stays in the contract and is swept again next epoch — never lost.
- **The `MIN_LIQUIDITY` lock** (1000 units to SELF at pool creation) accrues a tiny **unclaimable**
  incentive share each distribution (its `MIN_LIQUIDITY ⁄ totalLiquidity` fraction — well under 1% for a
  typical pool), exactly as it does for fees. That slice effectively re-strands, and it makes the
  `mIncentiveUnclaimed` tally drift slightly upward over time (so the sweep gets *more* conservative — safe,
  never over-pays). A refinement could distribute over `(totalLiquidity − MIN_LIQUIDITY)` to exclude the
  lock; acceptable as-is for v1.
- **All integer / subset-safe:** every division is `div()`, every array access is `Array.get/.set`, the
  `<<64` uses `uint128.high/low`; no `/`/`%`/`[]`/floats/pointers. (Subset-check extract included.)
- **Gas:** `END_EPOCH` scans the `QSWAP_MAX_POOL`-slot pool array (8192 × X_MULTIPLIER) a few times once per epoch. Bounded + deterministic,
  but it's a new per-epoch cost — Open question Q2.

---

## 8. Backwards compatibility

Additive. `AddLiquidity` / `RemoveLiquidity` / swaps / quotes / `GetLiquidityOf` / `GetPoolBasicState`
behavior is unchanged (verified: the 10 existing tests pass). New state defaults to 0 for existing records;
the new endpoints (fn 9, proc 12) are independent. Existing pools simply start earning incentives once they
have volume.

> **Index note:** a separate QSWAP proposal of ours also targets function indices 9/10; if both are
> adopted, the indices need coordinating. This proposal uses the next free slots on the live contract —
> **fn 9** (`GetClaimableIncentive`) and **proc 12** (`ClaimIncentive`).

---

## 9. Open questions for the Qubic / QSWAP core team

1. **Storage:** is **+48 MiB × X_MULTIPLIER** always-on for the two `LiquidityInfo` fields acceptable, or
   should we take the +16 MiB fallback (§6)?
2. **Gas:** is a per-epoch full-pool-array scan in `END_EPOCH` within budget, or would you prefer a
   maintained running reserve-total + active-pool index?
3. **Selection:** bottom-% via a 64-bucket histogram is approximate (bucket-granular). Acceptable, or do
   you want an exact rank? Is **30% / top-70%** the right split — and should it be governable?
4. **Volume metric:** this-epoch QU volume, or a trailing-N-epoch / EMA to blunt wash-trade gaming? (The
   0.3% + flat fee per swap already taxes wash trading, but a cap on any single pool's share is an option.)
5. **Migration:** the upgrade must zero-extend the existing `LiquidityInfo` and `PoolBasicState` records so
   the new fields default to 0 (the legacy handling in §7 depends on it).
6. **The backlog:** is sweeping the existing ~4.65M as the *first* live distribution the right call, or
   should it be handled separately (e.g. spread over N epochs)?

---

## 10. Scope reminder

The fix is self-contained in `QSWAP`. It touches no other contract, changes no existing behavior, and turns
a real, measured, growing leak into a transparent reward for the LPs who keep QSWAP's most-used pools deep.
If only one thing ships, it's the **capture + distribute** loop; the claim ergonomics (stack / claim /
auto-claim-on-remove) are the LP-facing polish on top.
