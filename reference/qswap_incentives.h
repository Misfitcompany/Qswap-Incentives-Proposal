// ============================================================================================
// QSWAP LP-Incentives — REFERENCE IMPLEMENTATION (annotated)
//
// This is the reviewer's map of the additions, reconciled against the live source
// (core-main-June-23-2026/core-main/src/contracts/Qswap.h, 2,492 lines). The FULL integrated
// contract is submission/Qswap.h; the subset-check extract is reference/qswap_incentives_check.h.
// Everything below is ADDITIVE — no existing line is removed (diff: ~560 added / 0 removed).
//
// Design = "separate incentive accumulator": a parallel reward-per-share
// distinct from the fee accumulator, funded by the QU recaptured each epoch, paid by explicit
// claim / auto-claim. No price distortion.
// Subset-safe throughout: div() not '/'/'%', Array.get/.set not '[]', uint128.high/low for <<64,
// no floats/pointers/strings/globals.
// ============================================================================================

// ---- constant (near the other QSWAP_* constants, ~line 22) ----
constexpr uint64 QSWAP_INCENTIVE_HIST_BUCKETS = 64;   // volume-histogram resolution for the bottom-% cutoff

// ---- (1) STATE ADDITIONS ----------------------------------------------------------------------
// PoolBasicState (live ~73-80) gains, after accFeePerLPX64:
//     uint64  volumeQuEpoch;          // QU swap volume this epoch (reset in END_EPOCH)
//     uint128 accIncentivePerLPX64;   // running incentive-per-LP, X64 (mirrors accFeePerLPX64)
// LiquidityInfo (live ~82-87) gains, after accumulatedFee:
//     uint128 incentiveDebtX64;       // accIncentivePerLPX64 snapshot at this position's last touch
//     uint64  accumulatedIncentive;   // settled-but-unclaimed incentive (QU)
// StateData (live ~89-117) gains, at the end (zero-extends on upgrade):
//     uint64 mIncentiveUnclaimed;            // earmarked-but-unclaimed; EXCLUDED from the sweep
//     uint64 mIncentiveLifetimeDistributed;  // stat
//     uint32 mIncentiveBottomPctExcluded;    // = 30 (INITIALIZE)

// ---- (2) PER-SWAP VOLUME (the 4 swap sites: live 1640 / 1797 / 2012 / 2225) -------------------
// Immediately after each `accFeePerLPX64 += div(scaledLpFee, ...)` (QU-in swaps) or next to the
// totalFee line (QU-out swaps), on the same locals.poolBasicState that is written back:
//     locals.poolBasicState.volumeQuEpoch += <quAmountIn | quAmountOut | input.quAmountOut>;

// ---- (3) ADD-LIQUIDITY incentive debt-twin (live ~1314-1328) ----------------------------------
// New record: init so a fresh LP can't claim a pool's prior distributions:
//     locals.tmpLiquidity.incentiveDebtX64 = locals.poolBasicState.accIncentivePerLPX64;
// Existing record (top-up): settle pending on the OLD liquidity (rewards STACK, never pay on add),
// alongside the existing fee settle, then re-snapshot:
//     locals.pendingIncX64 = uint128(locals.tmpLiquidity.liquidity) * (locals.poolBasicState.accIncentivePerLPX64 - locals.tmpLiquidity.incentiveDebtX64);
//     locals.tmpLiquidity.accumulatedIncentive += locals.pendingIncX64.high;
//     /* ...existing liquidity += ... ; feeDebt = ... ; */
//     locals.tmpLiquidity.incentiveDebtX64 = locals.poolBasicState.accIncentivePerLPX64;

// ---- (4) REMOVE-LIQUIDITY auto-claim (live ~1488, before the liquidity decrement) -------------
//     locals.pendingIncX64 = uint128(locals.userLiquidity.liquidity) * (locals.poolBasicState.accIncentivePerLPX64 - locals.userLiquidity.incentiveDebtX64);
//     locals.incentivePayout = sint64(locals.userLiquidity.accumulatedIncentive + locals.pendingIncX64.high);
//     if (locals.incentivePayout > 0) {
//         qpi.transfer(qpi.invocator(), locals.incentivePayout);
//         if (state.get().mIncentiveUnclaimed >= uint64(locals.incentivePayout)) state.mut().mIncentiveUnclaimed -= uint64(locals.incentivePayout);
//         else state.mut().mIncentiveUnclaimed = 0;
//     }
//     locals.userLiquidity.accumulatedIncentive = 0;
//     locals.userLiquidity.incentiveDebtX64 = locals.poolBasicState.accIncentivePerLPX64;
//     /* then the existing `liquidity -= burnLiquidity` and the full/partial-exit record handling */
// → pays on BOTH partial and full exit; full exit pays BEFORE the record is remove()d, so nothing strands.

// ---- (5) END_EPOCH: recapture + bottom-30%-excluded, top-70% volume-weighted distribution ------
// (NEW hook — QSWAP had none. Full body in submission/Qswap.h and reference/qswap_incentives_check.h.)
// Algorithm:
//   getEntity(SELF) → balance = incomingAmount - outgoingAmount
//   pass 1: totalReserved = Σ pool.reservedQuAmount ; participating = pools with volume>0 & liq>0 ; maxVol
//   undistFees = Σ (earned - distributed) over the 4 fee buckets
//   stray = balance - totalReserved - undistFees - mIncentiveUnclaimed        // = recaptured dividends (+backlog on 1st run)
//   if stray>0 && participating>0 && maxVol>0:
//     pass 2: histogram volumes into 64 buckets (bucket = div(vol*64, maxVol), clamp to 63)
//     cutoff = first bucket where cum-count >= div(participating * mIncentiveBottomPctExcluded, 100)   // bottom % excluded
//     pass 3: qualVol = Σ volume of pools with bucket >= cutoff
//     pass 4: for each qualifying pool: poolShare = div(stray * vol, qualVol);
//             num.high = poolShare; num.low = 0;                              // poolShare << 64 (X64)
//             pool.accIncentivePerLPX64 += div(num, totalLiquidity);          // credit the pool's LPs
//             mIncentiveUnclaimed += poolShare; mIncentiveLifetimeDistributed += poolShare
//     // rounding dust (stray - Σ poolShare) stays in the contract → swept again next epoch
//   pass 5: reset every pool's volumeQuEpoch = 0
// Balance-based ⇒ catches the ~4.65M backlog on the first run, and stray QU regardless of source/timing.

// ---- (6) GetClaimableIncentive — read-only, FUNCTION INDEX 9 -----------------------------------
//   input  { id assetIssuer; uint64 assetName; id account; }
//   output { sint64 claimableQu; uint64 accIncentivePerLP; uint64 poolVolumeEpoch; }
//   claimableQu = accumulatedIncentive + (liquidity * (accIncentivePerLPX64 - incentiveDebtX64)).high
//   clean zeros for non-pool / non-LP (mirrors GetLiquidityOf). Full body: submission/Qswap.h.

// ---- (7) ClaimIncentive — PROCEDURE INDEX 12 ---------------------------------------------------
//   input { id assetIssuer; uint64 assetName; }   output { sint64 claimedQu; }
//   refund any invocationReward (claiming is free); find pool + the caller's record;
//   payout = accumulatedIncentive + pending; if (payout>0) { qpi.transfer(invocator, payout); mIncentiveUnclaimed -= payout; }
//   accumulatedIncentive = 0; incentiveDebtX64 = accIncentivePerLPX64; replace record. Liquidity untouched.

// ---- (8) REGISTRATION + INITIALIZE -------------------------------------------------------------
//   REGISTER_USER_FUNCTION(GetClaimableIncentive, 9);
//   REGISTER_USER_PROCEDURE(ClaimIncentive, 12);
//   END_EPOCH is wired by defining END_EPOCH_WITH_LOCALS() (no explicit REGISTER).
//   INITIALIZE: state.mut().mIncentiveBottomPctExcluded = 30;
//
// Tests: all 8 construct families above are exercised by the 6 ContractSwap.Incentive_* tests (TEST-RESULTS.md),
// green alongside the 10 pre-existing QSWAP tests.
