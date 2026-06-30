# QSWAP LP-Incentives — self-review

Adversarial pass over our own change before asking the core team to read it. Each item is either
**confirmed safe** (with the reason / the test that locks it) or **flagged** for the core team.

## Correctness & accounting

- **R1 — Sweep isolates only true stray QU. ✅** `stray = balance − Σ reserves − undistributedFees −
  mIncentiveUnclaimed`. Swap inputs become reserves; the four fee buckets are tracked and subtracted;
  already-earmarked incentive is subtracted. Test `Incentive_SweepWeightedDistribution_BottomExcluded`
  injects a known 6,000,000 and sees ~exactly that distributed despite 4 setups + 4 fee-bearing swaps.
  *Flag:* if a **future** fee bucket is added to QSWAP, `undistFees` must be extended to include it, or the
  sweep would mis-capture it. (One-line coupling to note in the contract.)
- **R2 — No double-distribution across epochs. ✅** `mIncentiveUnclaimed` is excluded from the sweep, so
  last epoch's still-unclaimed incentive isn't re-swept. Test `Incentive_StacksAcrossEpochs` confirms epoch
  2 adds to (does not re-grant) epoch 1.
- **R3 — Solvency: payouts never exceed captured QU. ✅** Every per-pool credit and per-LP extraction is
  floored (rounding always favors the contract), and incentive is only ever earmarked from QU physically
  swept into the balance — so the contract always holds at least what it owes; every claim/auto-claim pays
  ≤ that. Decrements are underflow-guarded (`>= ? -= : =0`). Test `Incentive_ClaimPaysOutAndZeroes` checks
  the wallet rises by exactly `claimableQu` and `mIncentiveUnclaimed` falls by the same.
  - *Independent-audit correction:* `mIncentiveUnclaimed` actually drifts slightly **upward** over time —
    it's bumped by the **pre-floor** `distributed`, while real claims are ≤ that (floor dust + the SELF
    `MIN_LIQUIDITY` lock's share, which is never claimed). This is the **safe** direction: the sweep
    `stray = balance − … − mIncentiveUnclaimed` only ever gets *more* conservative — it never over-sweeps
    and never over-pays. The cost is that a small slice of each distribution (mainly the SELF lock's
    `MIN_LIQUIDITY ⁄ totalLiquidity` fraction — well under 1% for a typical pool) effectively re-strands.
    Future refinement: distribute over `(totalLiquidity − MIN_LIQUIDITY)` to skip the unclaimable lock. Not
    a correctness defect.
- **R4 — No retroactive claim. ✅** A new `LiquidityInfo` initializes `incentiveDebtX64 =
  accIncentivePerLPX64`, so a fresh LP can't claim a pool's pre-existing distributions. (Mirrors the fee
  `feeDebtX64` init.)
- **R5 — Auto-claim-on-remove can't strand. ✅** Pending is settled on the **old** liquidity and paid
  **before** the record is `remove()`d on full exit; partial exit pays then keeps the record with the
  incentive zeroed. Test `Incentive_AutoClaimOnRemove`.
- **R6 — Div-by-zero guarded. ✅** Distribution only runs when `participating > 0 && maxVol > 0 &&
  qualVol > 0`, and a pool is credited only when `totalLiquidity > 0`. The histogram divides by `maxVol`
  (guarded > 0).

## Selection & economics (flagged for the core team)

- **R7 — Histogram percentile is bucket-granular (approximate).** 64 log-ish buckets → the bottom-30%
  cutoff is approximate, and if *all* participating pools land in one bucket, none are excluded (they split
  evenly) — a reasonable degenerate, but worth a decision. An exact rank needs a sort (not subset-friendly
  at 8,192 pools). *Open question Q3.*
- **R8 — Gas: a per-epoch full-array scan.** `END_EPOCH` scans the 8,192-slot pool array ~5×, once per
  epoch. Bounded + deterministic, but a new cost. A maintained running reserve-total + active-pool index
  would cut it to O(active pools). *Open question Q2.*
- **R9 — Wash-trade gaming.** Volume can be inflated by self-swapping, but every swap pays the 0.3% + flat
  fee, so inflating a pool's share costs real QU each round — net-negative unless the captured incentive
  exceeds the fees burned. A per-pool share cap (e.g. ≤ X% of the pot) is a cheap hardening if wanted.
  *Open question Q4.*

## Storage (the headline ask)

- **R10 — +48 MiB × X_MULTIPLIER always-on** for the two `LiquidityInfo` fields (record size only;
  capacity stays 2^21). This is the real cost to weigh; a +16 MiB fallback exists
  (PROPOSAL §6). *Open question Q1.*

## Verdict
The accounting is closed (sweep ↔ earmark ↔ claim balances, proven by the tests), the change is additive
(10 existing tests green), and the open items are **design choices for the core team** (storage budget, gas
of the scan, exactness of the percentile, anti-gaming) — not correctness defects.
