# QSWAP LP-Incentives — implementation design spec (authoritative)

> Working spec for the chain fix. Line numbers are against
> `core-main-June-23-2026/core-main/src/contracts/Qswap.h` (2,492 lines). Mechanism = **separate
> incentive accumulator** — a parallel reward-per-share kept separate from swap fees, so the incentive is
> cleanly auditable and pool prices are never distorted. Everything here is held to Qubic subset rules:
> `div()`/`mod()` not `/`/`%`, no `[]` (use `.get/.set`), `uint128` multiply-before-divide, locals only via
> `_WITH_LOCALS`, no floats/pointers/strings/globals.

## 0. One-line behavior

Each epoch, sweep the QU that has quietly landed in the contract (recaptured SC dividends + the ~4.65M
backlog on first run), and credit it as a **claimable LP incentive** to the LPs of the **most-active 70%
of pools, weighted by that epoch's swap volume** — bottom 30% of participating pools get nothing.

## 1. State additions

### `PoolBasicState` (Qswap.h:73-80) — +24 B/pool × QSWAP_MAX_POOL (8192 × X_MULTIPLIER) ≈ +192 KiB × X_MULTIPLIER (trivial)
```cpp
uint64  volumeQuEpoch;          // QU-denominated swap volume accrued this epoch; reset in END_EPOCH
uint128 accIncentivePerLPX64;   // running incentive-per-LP, X64 fixed point (mirrors accFeePerLPX64)
```

### `LiquidityInfo` (Qswap.h:82-87) — +24 B/record × 2^21 ≈ +48 MiB always-on  ← the storage ask
```cpp
uint128 incentiveDebtX64;       // accIncentivePerLPX64 snapshot at the position's last touch
uint64  accumulatedIncentive;   // settled-but-unclaimed incentive (QU), realized by ClaimIncentive
```
*(Honest fallback if +48 MiB is too much: drop `accumulatedIncentive`, require claim-before-any-
liquidity-change, ≈ +16 MiB. Documented as an open question.)*

### `StateData` (Qswap.h:89-117)
```cpp
uint64 mIncentiveUnclaimed;          // total earmarked-but-unclaimed incentive — EXCLUDED from the sweep
uint64 mIncentiveLifetimeDistributed;// stat: cumulative incentive ever credited
uint32 mIncentiveBottomPctExcluded;  // = 30 (INITIALIZE); the bottom-volume %ile that gets nothing
```

## 2. Volume tracking — the 4 swap sites

`accFeePerLPX64 += div(scaledLpFee, …)` happens at **1640 / 1797 / 2012 / 2225** (the four swaps:
SwapExactQuForAsset, SwapQuForExactAsset, SwapExactAssetForQu, SwapAssetForExactQu). Each operates on a
single `locals.poolBasicState` and writes it back. Immediately adjacent, accumulate the **QU side** of the
trade (every QSWAP trade has a QU side — natural common-denominator volume):
```cpp
locals.poolBasicState.volumeQuEpoch = sadd(locals.poolBasicState.volumeQuEpoch, <quAmountOfThisSwap>);
```
`<quAmountOfThisSwap>` = `quAmountIn` for QU-in swaps, `quAmountOut` for QU-out swaps (the values already
computed locally at each site). Use a saturating add helper or guard overflow (volumes are far below 2^64
per epoch, but be safe).

## 3. Incentive debt settle — add (3 sites) + remove (1 site)

Mirror the existing fee-debt pattern exactly. The fee version (subsequent-add existing record) at
**1317-1326**, and remove at **1474-1483**, do:
```cpp
pendingFeeX64 = uint128(liquidity) * (accFeePerLPX64 - feeDebtX64);
accumulatedFee += pendingFeeX64.high;
feeDebtX64 = accFeePerLPX64;
```
Alongside each, add the incentive twin:
```cpp
pendingIncX64 = uint128(liquidity) * (accIncentivePerLPX64 - incentiveDebtX64);
accumulatedIncentive += pendingIncX64.high;
incentiveDebtX64 = accIncentivePerLPX64;
```
- **First-mint user record** (~1225-1229) and **new record on subsequent add**: init
  `incentiveDebtX64 = poolBasicState.accIncentivePerLPX64; accumulatedIncentive = 0` (so they don't
  retroactively claim pre-existing incentive). The SELF `MIN_LIQUIDITY` lock record: same init (its dust
  incentive is unclaimable — acceptable, like its forfeited fees).

**Claim semantics (confirmed with owner):**
- **Never claiming ⇒ rewards STACK, no expiry.** claimable = `accumulatedIncentive + liquidity ×
  (accIncentivePerLPX64 − incentiveDebtX64)`; `accIncentivePerLPX64` grows every epoch.
- **Add liquidity STACKS (no payout).** Settle pending → `accumulatedIncentive`, re-snapshot debt
  (MasterChef). Topping up never triggers a surprise payout.
- **Remove liquidity (partial OR full) AUTO-CLAIMS.** Settle pending → `accumulatedIncentive`, then pay the
  whole claimable (`accumulatedIncentive` + pending) inline via `qpi.transfer`, set `accumulatedIncentive
  = 0`, `incentiveDebtX64 = accIncentivePerLPX64`, `mIncentiveUnclaimed -= paid`. Partial: record persists
  with claimable zeroed; full exit: record `remove()`d after payout, so nothing is lost. (Guard `paid > 0`
  before transferring.)
- **`ClaimIncentive` pays WITHOUT removing** — same settle+pay+reset, but liquidity is untouched (claim
  while still providing).

## 4. END_EPOCH (NEW hook — QSWAP has none today)

```
END_EPOCH_WITH_LOCALS():
  getEntity(SELF, entity)
  balance = entity.incomingAmount - entity.outgoingAmount

  // pass 1: total reserved QU + participating-pool stats
  totalReserved = 0; participating = 0; maxVol = 0
  for i in 0..QSWAP_MAX_POOL-1:
     p = mPoolBasicStates.get(i)
     if p.poolID == NULL_ID: continue
     totalReserved = sadd(totalReserved, p.reservedQuAmount)
     if p.volumeQuEpoch > 0 && p.totalLiquidity > 0:
        participating++; if p.volumeQuEpoch > maxVol: maxVol = p.volumeQuEpoch

  undistFees = (shareholderEarnedFee - shareholderDistributedAmount)
             + (investRewardsEarnedFee - investRewardsDistributedAmount)
             + (qxEarnedFee - qxDistributedAmount)
             + (burnEarnedFee - burnedAmount)
  stray = balance - totalReserved - undistFees - mIncentiveUnclaimed
  // (clamp: if any subtraction underflows or stray <= 0, skip to volume reset)

  if stray > 0 && participating > 0 && maxVol > 0:
     // pass 2: histogram of volumes into NB=64 buckets, bucket(v) = div(uint128(v)*NB, maxVol+1).low
     reset bucketCount[0..NB-1] = 0
     for each participating pool: bucketCount[bucket(vol)]++
     // cutoff = smallest bucket index b such that count of pools in buckets[0..b-1] >= 30% of participating
     target = div(participating * mIncentiveBottomPctExcluded, 100)   // # excluded
     cum = 0; cutoff = 0
     for b in 0..NB-1: if cum >= target: { cutoff = b; break }; cum += bucketCount[b]
     // pass 3: sum qualifying volume (pools whose bucket >= cutoff)
     qualVol = 0
     for each participating pool: if bucket(vol) >= cutoff: qualVol = sadd(qualVol, vol)
     // pass 4: credit each qualifying pool, volume-weighted
     if qualVol > 0:
        distributed = 0
        for each participating pool with bucket(vol) >= cutoff && totalLiquidity > 0:
           poolShare = div(uint128(stray) * vol, uint128(qualVol)).low     // mul before div
           if poolShare > 0:
              accIncentivePerLPX64 += div(uint128(poolShare) << 64, uint128(totalLiquidity))
              mPoolBasicStates.set(i, p)   // write back
              distributed = sadd(distributed, poolShare)
        mIncentiveUnclaimed       = sadd(mIncentiveUnclaimed, distributed)
        mIncentiveLifetimeDistributed = sadd(mIncentiveLifetimeDistributed, distributed)
        // rounding dust (stray - distributed) stays in balance → swept again next epoch

  // pass 5: reset volumes
  for each pool with poolID != NULL_ID: volumeQuEpoch = 0; write back
```
- **Backlog (the ~4.65M):** it is exactly `balance - totalReserved - undistFees` already, so the **first**
  END_EPOCH after upgrade sweeps it automatically into the first distribution. No special-case code.
- **Cost:** up to ~5 passes over the QSWAP_MAX_POOL-slot pool array (8192 × X_MULTIPLIER) per epoch. Bounded,
  deterministic. Flag as an open question for core (gas budget of a per-epoch full-array scan).
- **NB=64 histogram** ⇒ the 30% cutoff is bucket-granular (approximate). Acceptable + documented; an exact
  rank would need a sort (not subset-friendly at 8192). Open question.

## 5. `ClaimIncentive` (NEW procedure, index 12)

```
input { id assetIssuer; uint64 assetName; }   output { sint64 claimedQu; sint32 returnCode; }
ClaimIncentive:
  refund qpi.invocationReward() to invocator (this op carries no fee)
  poolSlot = FindPoolSlot(issuer,name); if none → returnCode=NO_POOL; return
  pov = liquidityPov(poolID, invocator); idx = mLiquidities.headIndex(pov,0); if none → claimedQu=0; return
  li = mLiquidities.element(idx)
  pendingX64 = uint128(li.liquidity) * (poolBasicState.accIncentivePerLPX64 - li.incentiveDebtX64)
  total = li.accumulatedIncentive + pendingX64.high
  if total > 0:
     qpi.transfer(invocator, total)
     mIncentiveUnclaimed -= total        // (guard underflow)
  li.accumulatedIncentive = 0
  li.incentiveDebtX64 = poolBasicState.accIncentivePerLPX64
  mLiquidities.replace(idx, li)
  claimedQu = total; returnCode = SUCCESS
```

## 6. `GetClaimableIncentive` (NEW read-only function, index 9)

```
input { id assetIssuer; uint64 assetName; id account; }
output { sint64 claimableQu; uint64 accIncentivePerLP; uint64 poolVolumeEpoch; }
  // claimableQu = accumulatedIncentive + (liquidity*(accIncentivePerLPX64 - incentiveDebtX64)).high
  // clean zeros for non-pool / non-LP (mirrors GetLiquidityOf)
```

## 7. Registration

- Live contract: functions 1-8 used → **9 free** (GetClaimableIncentive). Procedures 1-11 used → **12 free**
  (ClaimIncentive). (A separate QSWAP proposal of ours also targets 9/10 — if both are adopted, coordinate
  indices; see PROPOSAL §8.)
- In `REGISTER_USER_FUNCTIONS_AND_PROCEDURES`: `REGISTER_USER_FUNCTION(GetClaimableIncentive, 9);`
  `REGISTER_USER_PROCEDURE(ClaimIncentive, 12, …);` (match the arity/flags of the existing registrations).
- `END_EPOCH` is wired by defining `END_EPOCH_WITH_LOCALS()` (no explicit REGISTER, same as BEGIN_EPOCH /
  END_TICK).
- `INITIALIZE`: set `mIncentiveBottomPctExcluded = 30` (and zero the new scalar counters; new record/pool
  fields zero-init on creation; existing records zero-extend at upgrade — the standard upgrade
  zero-extension question).

## 8. Storage & open questions (for the PROPOSAL)

1. `LiquidityInfo` +24 B × 2^21 ≈ **+48 MiB always-on** — acceptable? (lighter +16 MiB fallback in §1).
2. Per-epoch full-array pool scan in END_EPOCH — gas budget OK, or maintain a running reserve total +
   active-pool index instead?
3. Bottom-30% via bucket histogram (approximate) vs an exact rank — preference? Is 30% / NB=64 right?
4. Volume metric = this-epoch QU volume; a trailing-N-epoch or EMA would smooth gaming — want that?
5. State migration must zero-extend existing `LiquidityInfo`/`PoolBasicState` records at the upgrade.
6. Anti-gaming: wash-trading to inflate a pool's volume costs the 0.3% + flat fee each swap; is the
   fee drag a sufficient deterrent, or cap a single pool's share?

## 9. Build/verify targets (MSVC + GoogleTest)

- Short-path working copy `%USERPROFILE%\qsi\` (clone of core-main-June-23-2026; MAX_PATH).
- `msbuild …\qsi\test\test.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`; run
  `…\qsi\test\x64\Debug\test.exe --gtest_filter=ContractSwap.*`.
- Subset check: extract additions → `reference/qswap_incentives_check.h`; run `contractverify.exe` →
  `Contract compliance check PASSED`.
