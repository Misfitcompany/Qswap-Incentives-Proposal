# On-chain evidence — QU dividends stranded in the QSWAP contract

> **Measured live** against `rpc.qubic.org` on **2026-06-29** (QSWAP balance valid at tick **63,162,682**).
> Every number here is reproducible from the public event archive with the scripts in §6. If this
> disagrees with a fresh pull, the chain wins — re-run and update.

## 1. The one-paragraph finding

QCAP earns dividends from **QVAULT** (contract index 10) and QMINE earns dividends from **qRWA** (index
20). Both contracts pay **by possession** — they iterate every holder of the token and `qpi.transfer`
each one its per-share cut. When QCAP or QMINE is deposited into a **QSWAP** liquidity pool (index 13),
the pool contract becomes the *possessor of record*, so the dividend is transferred **to the QSWAP
contract itself**. QSWAP has no code that accounts for or forwards received QU, so it simply **accumulates
in the contract's balance, unattributed and unclaimable** — value that quietly leaks out of LPs' hands and
sits inert. As of 2026-06-29 that pile is **≈ 4,646,728 QU**, and it grows on **every epoch boundary**.

## 2. Headline numbers

| Source | What it is | Stranded QU | Transfers | Span |
|---|---|---:|:---:|---|
| **QVAULT** (idx 10) | QCAP dividends on pool-held QCAP | **476,695** | 9 | epoch 207 → 218 (Apr 8 → Jun 24, 2026) |
| **qRWA** (idx 20) | QMINE dividends on pool-held QMINE | **4,170,033** | 10 | epoch 213 → 219 (May 15 → Jun 26, 2026) |
| | **TOTAL STRANDED** | **≈ 4,646,728** | 19 | |

QSWAP contract (`NAAA…MAML`) live balance at measurement: **49,014,250,263 QU** (lifetime incoming
636,169,771,521 / outgoing 587,155,521,258; 1,097,199 incoming transfers, 1,607,567 outgoing). The
stranded pile is ~0.0095% of the balance today — small, but **monotonically growing** (§3) and buried
with no way out under current code.

## 3. It compounds every epoch (and is accelerating)

```
qRWA → QSWAP  (QMINE dividends), QU per epoch
  ep213     50,582
  ep214    709,196
  ep215    555,288
  ep216    979,697
  ep218  1,055,642
  ep219    819,628        ← ~0.8–1.0M QU/epoch and rising as QMINE pools deepen

QVAULT → QSWAP  (QCAP dividends), QU per epoch
  ep207         32
  ep211    115,653
  ep213     32,450
  ep215     92,260
  ep218    236,300        ← climbing
```

Combined, **~1M+ QU is being stranded per epoch (~weekly) and accelerating** as QCAP/QMINE liquidity
grows. Left alone, this is tens of millions of QU per year accruing to no one.

## 4. Why this is real and not double-counted

- **They are dividends, not trades.** Every QVAULT→QSWAP and qRWA→QSWAP event is a pure QU transfer
  (`logType 0`), one cluster per epoch, with **zero asset legs** — the exact signature of the `END_EPOCH`
  dividend push, not a swap (a swap would carry a paired `logType 2` asset leg).
- **They do not flow back out.** The reverse direction shows heavy QSWAP→QVAULT traffic (8,551 transfers,
  ~16.4M QU) — but that is a **separate, opposite relationship**: QVAULT *earning* income from QSWAP as a
  shareholder / IPO participant. It matches no QSWAP outflow tied to the QCAP/QMINE dividends, which sit
  put. (qRWA has **zero** QSWAP→qRWA back-flow.)
- **The capture is complete.** The compound `source`+`destination` archive filter returned **9/9** and
  **10/10** rows with no truncation, starting at the natural first-pooled epochs (QCAP first drew a
  dividend at epoch 207, QMINE at 213 — i.e. when those tokens were first added to QSWAP pools).

## 5. The mechanism, in code (June-23 core snapshot)

- **QVAULT pays QCAP holders by possession** — `QVAULT.h:3344-3353`: iterate every QCAP possessor,
  `qpi.transfer(possessor, perShare × shares)`, skipping only `SELF`. Pool-held QCAP's possessor is
  QSWAP → QSWAP is paid. Its circulating-supply figure (`QVAULT.h:3329`) even includes the pooled QCAP.
- **qRWA pays QMINE holders by possession** — `qRWA.h:1571-1587` (begin snapshot) and `1661-1700` (end
  snapshot) iterate every QMINE possessor, excluding only `SELF`, the fundraising address, and the
  safe.trade exchange address — **not** the QSWAP contract; payouts via `qpi.transfer` (`qRWA.h:2192…`).
- **QSWAP takes possession of deposited tokens** — `Qswap.h:1198-1205`:
  `transferShareOwnershipAndPossession(..., invocator, invocator, amount, SELF)`. So the pool contract is
  the possessor the dividend is sent to.
- **QSWAP has nowhere to put received QU** — it has **no `POST_INCOMING_TRANSFER`** handler; pool QU
  reserves are tracked in an explicit `reservedQuAmount` state field (`Qswap.h:76,177`), updated only by
  add/remove/swap, so a bare transfer is invisible to both pricing and LP accounting; and `END_TICK`
  (`Qswap.h:2433-2486`) distributes only the four *tracked* fee accumulators — nothing sweeps the raw
  balance. ⇒ the dividend QU has no exit. **Stranded.**

This is the gap the proposal closes.

## 6. Reproduce

All read-only, public RPC. Identities were resolved from the contract index via Qubic's own
KangarooTwelve and **verified** against the four known contract identities (QX→`…RMID`, QUTIL→`…VWRF`,
QBAY→`…WLWD`, QSWAP→`…MAML`) before use:

| Contract | Index | Identity |
|---|---:|---|
| QSWAP | 13 | `NAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAMAML` |
| QVAULT | 10 | `KAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAXIUO` |
| qRWA | 20 | `UAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAHQEE` |

Method (the key trick): the public event archive caps a page at 1,000 rows with **no offset**, so a bare
`destination=QSWAP` filter truncates (QSWAP has >10k events). But a **compound `{source, destination}`
filter is AND-ed** by the archive (verified: `{source:QSWAP,destination:QSWAP}` → 0), so
`{source: QVAULT, destination: QSWAP}` returns the *complete* dividend set — a handful of rows, no
truncation.

```
POST https://rpc.qubic.org/query/v1/getEventLogs
  body: {"pagination":{"size":1000},"filters":{"source":"<QVAULT id>","destination":"<QSWAP id>"}}
  → sum quTransfer.amount over logType 0   ⇒  476,695 QU (9 rows)
  same with source=<qRWA id>               ⇒  4,170,033 QU (10 rows)
GET  https://rpc.qubic.org/v1/balances/<QSWAP id>   → current contract balance
```

The figures were produced with small **read-only** Node scripts (the compound-filter sums plus the
per-epoch and back-flow checks) and a KangarooTwelve identity resolver that self-tests against the four
known contract IDs — all hitting only the public RPC. The manual commands above reproduce them directly.

---

*This file is the empirical basis for the QSWAP LP-Incentives proposal. The proposal's job is to turn this
inert, growing pile — and the ongoing stream — into a transparent, on-chain reward for the LPs who keep
QSWAP's most-used pools liquid.*
