# QSWAP — contract reference notes (grounded study)

> **Source of truth:** `core-main-June-23-2026/core-main/src/contracts/Qswap.h` (2,492 lines), read in
> full, plus `qpi.h` (`Collection` / `Array` / `uint128` / `div` / `getEntity` / `Entity` /
> `POST_INCOMING_TRANSFER`). Written so we never re-derive QSWAP from memory. **If this disagrees with the
> source, the source wins.**

## 1. What QSWAP is
- A Uniswap-V2-style constant-product AMM on Qubic. **Contract index 13** (`contract_def.h:145`).
- **Core-protocol** — not independently deployable; changes ship only via a network upgrade at an epoch
  boundary.

## 2. State (`StateData`)
- Fee config: `swapFeeRate = 30` (0.3%, base 10000); split `shareholderFeeRate = 27`,
  `investRewardsFeeRate = 3`, `qxFeeRate = 5`, `burnFeeRate = 1` (base 100 → LPs keep the remaining 64%).
- Four **earned-vs-distributed** fee accumulators (shareholders, invest-rewards, QX, burn).
- `mPoolBasicStates` — `Array<PoolBasicState, QSWAP_MAX_POOL = 8192 × X_MULTIPLIER>`.
- `mLiquidities` — `Collection<LiquidityInfo, QSWAP_MAX_POOL * QSWAP_MAX_USER_PER_POOL = 2^21>`.

## 3. The two records
- `PoolBasicState { id poolID; sint64 reservedQuAmount, reservedAssetAmount, totalLiquidity; uint128 accFeePerLPX64; }`
- `LiquidityInfo { sint64 liquidity; uint128 feeDebtX64; uint64 accumulatedFee; }` — one per (pool, account).

## 4. Fee accounting — the mechanism we mirror for incentives
Classic *scaled reward-per-share*:
- `accFeePerLPX64` = per-pool running "fee per unit of LP", X64 fixed point.
- Each swap routes the LP slice: `accFeePerLPX64 += div(scaledLpFee, totalLiquidity)` (sites at **1640 /
  1797 / 2012 / 2225** — the four swap procedures).
- A position's earned fee = `accumulatedFee + (liquidity * (accFeePerLPX64 − feeDebtX64)).high`; `feeDebtX64`
  is the accumulator snapshot at its last touch (set at the 3 add sites + remove).
- **LP fees stay in the reserves** (no separate claim). ⟶ our incentive accumulator is a *parallel,
  separate* accumulator (`accIncentivePerLPX64`) whose QU is the swept stray dividends, paid out by an
  explicit claim / auto-claim — distinct from fees.

## 5. Where the QU is, and why stray QU is invisible
- Pool QU is tracked in the explicit `reservedQuAmount` field, updated only in add/remove/swap — **not** read
  from the contract's balance. So QU that arrives via a bare `qpi.transfer` (the QCAP/QMINE dividends) does
  **not** touch reserves, pricing, or any fee bucket — it just raises the contract's raw balance. That gap
  is exactly what `END_EPOCH` recaptures: `balance − Σ reserves − undistributedFees − earmarkedIncentive`.

## 6. Lifecycle hooks (before this proposal)
- `INITIALIZE` — fee rates + `investRewardsId`.
- `BEGIN_EPOCH` — caches QX issuance/transfer fees.
- `END_TICK` — distributes the four tracked fee buckets (I&R transfer, QX donation, shareholder
  `distributeDividends`, burn). **Distributes only tracked fees — never the raw balance.**
- `PRE_ACQUIRE_SHARES` → `allowTransfer = true`. **No `END_EPOCH`** (the clean hook we add).

## 7. Self-balance + incoming-transfer APIs (used by the fix)
- `qpi.getEntity(SELF, entity)` → `entity.incomingAmount − entity.outgoingAmount` = the contract's QU
  balance (used in QEARN/GGWP/QReservePool; `qpi.h:2525`).
- `POST_INCOMING_TRANSFER` (`qpi.h:2906`, `PostIncomingTransfer_input{ id sourceId; sint64 amount; uint8
  type; }`) — **qRWA already uses it to catch incoming SC dividends** (`qRWA.h:2655`). This is the
  alternative capture mechanism; the fix uses the balance-sweep instead.

## 8. Function / procedure indices
- Functions used 1–8 → **9 free** (we add `GetClaimableIncentive`).
- Procedures used 1–11 → **12 free** (we add `ClaimIncentive`).
- `END_EPOCH` is wired by defining `END_EPOCH_WITH_LOCALS()` (no explicit REGISTER, like BEGIN_EPOCH/END_TICK).

## 9. Math / safety idioms (we follow these)
- `uint128` multiply-before-divide; floor `div()` (never `/`/`%`); `Array.get/.set` (never `[]`); build a
  `value << 64` by setting `uint128.high = value; .low = 0` (the `scaledLpFee` idiom).
- The `Collection` capacity must stay a power of two (`static_assert`); we change record **size**, not
  capacity, so `2^21` is preserved.
