using namespace QPI;

// Minimal harness to run the Qubic Contract Verification Tool against ONLY the additions proposed for
// QSWAP LP-incentives: the two new PoolBasicState fields, the two new LiquidityInfo fields, the new
// END_EPOCH sweep + volume-weighted distribution, the new ClaimIncentive procedure and
// GetClaimableIncentive function, and the per-op maintenance statements (volume accrual + incentive
// debt-twin + auto-claim). Bodies are verbatim from src/contracts/Qswap.h; only the surrounding
// 2,500-line contract is trimmed so the subset parser returns quickly. The tool parses for forbidden
// C++ constructs (no '/'/'%', no '[]', no floats/pointers/strings/globals) — it does not compile/link,
// so undefined state members are fine.

constexpr uint64 QSWAP_MAX_POOL = 8192;
constexpr uint64 QSWAP_INCENTIVE_HIST_BUCKETS = 64;

struct QSWAP : public ContractBase
{
public:
    struct PoolBasicState
    {
        id poolID;
        sint64 reservedQuAmount;
        sint64 reservedAssetAmount;
        sint64 totalLiquidity;
        uint128 accFeePerLPX64;
        uint64 volumeQuEpoch;
        uint128 accIncentivePerLPX64;
    };

    struct LiquidityInfo
    {
        sint64 liquidity;
        uint128 feeDebtX64;
        uint64 accumulatedFee;
        uint128 incentiveDebtX64;
        uint64 accumulatedIncentive;
    };

    struct FindPoolSlotReadOnly_input { id assetIssuer; uint64 assetName; };
    struct FindPoolSlotReadOnly_output { sint64 poolSlot; };

    // ---------------- GetClaimableIncentive (function index 9) ----------------
    struct GetClaimableIncentive_input { id assetIssuer; uint64 assetName; id account; };
    struct GetClaimableIncentive_output { sint64 claimableQu; uint64 accIncentivePerLP; uint64 poolVolumeEpoch; };
    struct GetClaimableIncentive_locals
    {
        id poolID; id liqPov; id r;
        sint64 liqElementIndex; sint64 poolSlot;
        PoolBasicState pbs; LiquidityInfo li;
        uint128 pendingIncX64;
        FindPoolSlotReadOnly_input  fsRoIn;
        FindPoolSlotReadOnly_output fsRoOut;
    };
    PUBLIC_FUNCTION_WITH_LOCALS(GetClaimableIncentive)
    {
        output.claimableQu = 0;
        output.accIncentivePerLP = 0;
        output.poolVolumeEpoch = 0;

        locals.poolID = input.assetIssuer;
        locals.poolID.u64._3 = input.assetName;

        locals.fsRoIn.assetIssuer = input.assetIssuer;
        locals.fsRoIn.assetName = input.assetName;
        CALL(FindPoolSlotReadOnly, locals.fsRoIn, locals.fsRoOut);
        locals.poolSlot = locals.fsRoOut.poolSlot;
        if (locals.poolSlot == NULL_INDEX)
        {
            return;
        }
        locals.pbs = state.get().mPoolBasicStates.get(locals.poolSlot);
        output.accIncentivePerLP = locals.pbs.accIncentivePerLPX64.high;
        output.poolVolumeEpoch = locals.pbs.volumeQuEpoch;

        locals.liqPov = liquidityPov(locals.poolID, input.account, locals.r);
        locals.liqElementIndex = state.get().mLiquidities.headIndex(locals.liqPov, 0);
        if (locals.liqElementIndex == NULL_INDEX)
        {
            return;
        }
        locals.li = state.get().mLiquidities.element(locals.liqElementIndex);
        locals.pendingIncX64 = uint128(locals.li.liquidity) * (locals.pbs.accIncentivePerLPX64 - locals.li.incentiveDebtX64);
        output.claimableQu = sint64(locals.li.accumulatedIncentive + locals.pendingIncX64.high);
    }

    // ---------------- ClaimIncentive (procedure index 12) ----------------
    struct ClaimIncentive_input { id assetIssuer; uint64 assetName; };
    struct ClaimIncentive_output { sint64 claimedQu; };
    struct ClaimIncentive_locals
    {
        id poolID; id liqPov; id r;
        sint64 liqElementIndex; sint64 poolSlot;
        PoolBasicState pbs; LiquidityInfo li;
        uint128 pendingIncX64; sint64 payout;
        FindPoolSlotReadOnly_input  fsRoIn;
        FindPoolSlotReadOnly_output fsRoOut;
    };
    PUBLIC_PROCEDURE_WITH_LOCALS(ClaimIncentive)
    {
        output.claimedQu = 0;

        if (qpi.invocationReward() > 0)
        {
            qpi.transfer(qpi.invocator(), qpi.invocationReward());
        }

        locals.poolID = input.assetIssuer;
        locals.poolID.u64._3 = input.assetName;

        locals.fsRoIn.assetIssuer = input.assetIssuer;
        locals.fsRoIn.assetName = input.assetName;
        CALL(FindPoolSlotReadOnly, locals.fsRoIn, locals.fsRoOut);
        locals.poolSlot = locals.fsRoOut.poolSlot;
        if (locals.poolSlot == NULL_INDEX)
        {
            return;
        }
        locals.pbs = state.get().mPoolBasicStates.get(locals.poolSlot);

        locals.liqPov = liquidityPov(locals.poolID, qpi.invocator(), locals.r);
        locals.liqElementIndex = state.get().mLiquidities.headIndex(locals.liqPov, 0);
        if (locals.liqElementIndex == NULL_INDEX)
        {
            return;
        }
        locals.li = state.get().mLiquidities.element(locals.liqElementIndex);

        locals.pendingIncX64 = uint128(locals.li.liquidity) * (locals.pbs.accIncentivePerLPX64 - locals.li.incentiveDebtX64);
        locals.payout = sint64(locals.li.accumulatedIncentive + locals.pendingIncX64.high);
        if (locals.payout > 0)
        {
            qpi.transfer(qpi.invocator(), locals.payout);
            if (state.get().mIncentiveUnclaimed >= uint64(locals.payout))
            {
                state.mut().mIncentiveUnclaimed -= uint64(locals.payout);
            }
            else
            {
                state.mut().mIncentiveUnclaimed = 0;
            }
        }
        locals.li.accumulatedIncentive = 0;
        locals.li.incentiveDebtX64 = locals.pbs.accIncentivePerLPX64;
        state.mut().mLiquidities.replace(locals.liqElementIndex, locals.li);

        output.claimedQu = locals.payout;
    }

    // ---------------- END_EPOCH: recapture stray QU + volume-weighted distribution ----------------
    struct END_EPOCH_locals
    {
        Entity entity;
        PoolBasicState p;
        Array<uint32, QSWAP_INCENTIVE_HIST_BUCKETS> bucketCount;
        uint64 balance; uint64 totalReserved; uint64 undistFees; uint64 stray;
        uint64 maxVol; uint64 qualVol; uint64 distributed; uint64 poolShare;
        uint64 target; uint64 cum; uint64 vbucket;
        uint128 num;
        sint64 i; sint64 participating;
        uint32 b; uint32 cutoff;
    };
    END_EPOCH_WITH_LOCALS()
    {
        qpi.getEntity(SELF, locals.entity);
        locals.balance = locals.entity.incomingAmount - locals.entity.outgoingAmount;

        locals.totalReserved = 0;
        locals.participating = 0;
        locals.maxVol = 0;
        for (locals.i = 0; locals.i < (sint64)QSWAP_MAX_POOL; locals.i++)
        {
            locals.p = state.get().mPoolBasicStates.get(locals.i);
            if (locals.p.poolID == NULL_ID)
            {
                continue;
            }
            locals.totalReserved += locals.p.reservedQuAmount;
            if (locals.p.volumeQuEpoch > 0 && locals.p.totalLiquidity > 0)
            {
                locals.participating++;
                if (locals.p.volumeQuEpoch > locals.maxVol)
                {
                    locals.maxVol = locals.p.volumeQuEpoch;
                }
            }
        }

        locals.undistFees = (state.get().shareholderEarnedFee - state.get().shareholderDistributedAmount)
            + (state.get().investRewardsEarnedFee - state.get().investRewardsDistributedAmount)
            + (state.get().qxEarnedFee - state.get().qxDistributedAmount)
            + (state.get().burnEarnedFee - state.get().burnedAmount);

        locals.stray = 0;
        if (locals.balance > locals.totalReserved + locals.undistFees + state.get().mIncentiveUnclaimed)
        {
            locals.stray = locals.balance - locals.totalReserved - locals.undistFees - state.get().mIncentiveUnclaimed;
        }

        if (locals.stray > 0 && locals.participating > 0 && locals.maxVol > 0)
        {
            for (locals.b = 0; locals.b < QSWAP_INCENTIVE_HIST_BUCKETS; locals.b++)
            {
                locals.bucketCount.set(locals.b, 0);
            }
            for (locals.i = 0; locals.i < (sint64)QSWAP_MAX_POOL; locals.i++)
            {
                locals.p = state.get().mPoolBasicStates.get(locals.i);
                if (locals.p.poolID == NULL_ID || locals.p.volumeQuEpoch == 0 || locals.p.totalLiquidity <= 0)
                {
                    continue;
                }
                locals.vbucket = div(uint128(locals.p.volumeQuEpoch) * uint128(QSWAP_INCENTIVE_HIST_BUCKETS), uint128(locals.maxVol)).low;
                if (locals.vbucket >= QSWAP_INCENTIVE_HIST_BUCKETS)
                {
                    locals.vbucket = QSWAP_INCENTIVE_HIST_BUCKETS - 1;
                }
                locals.bucketCount.set(locals.vbucket, uint32(locals.bucketCount.get(locals.vbucket) + 1));
            }

            locals.target = div(uint64(locals.participating) * uint64(state.get().mIncentiveBottomPctExcluded), 100ULL);
            locals.cum = 0;
            locals.cutoff = 0;
            for (locals.b = 0; locals.b < QSWAP_INCENTIVE_HIST_BUCKETS; locals.b++)
            {
                if (locals.cum >= locals.target)
                {
                    locals.cutoff = locals.b;
                    break;
                }
                locals.cum += locals.bucketCount.get(locals.b);
            }

            locals.qualVol = 0;
            for (locals.i = 0; locals.i < (sint64)QSWAP_MAX_POOL; locals.i++)
            {
                locals.p = state.get().mPoolBasicStates.get(locals.i);
                if (locals.p.poolID == NULL_ID || locals.p.volumeQuEpoch == 0 || locals.p.totalLiquidity <= 0)
                {
                    continue;
                }
                locals.vbucket = div(uint128(locals.p.volumeQuEpoch) * uint128(QSWAP_INCENTIVE_HIST_BUCKETS), uint128(locals.maxVol)).low;
                if (locals.vbucket >= QSWAP_INCENTIVE_HIST_BUCKETS)
                {
                    locals.vbucket = QSWAP_INCENTIVE_HIST_BUCKETS - 1;
                }
                if (locals.vbucket >= locals.cutoff)
                {
                    locals.qualVol += locals.p.volumeQuEpoch;
                }
            }

            if (locals.qualVol > 0)
            {
                locals.distributed = 0;
                for (locals.i = 0; locals.i < (sint64)QSWAP_MAX_POOL; locals.i++)
                {
                    locals.p = state.get().mPoolBasicStates.get(locals.i);
                    if (locals.p.poolID == NULL_ID || locals.p.volumeQuEpoch == 0 || locals.p.totalLiquidity <= 0)
                    {
                        continue;
                    }
                    locals.vbucket = div(uint128(locals.p.volumeQuEpoch) * uint128(QSWAP_INCENTIVE_HIST_BUCKETS), uint128(locals.maxVol)).low;
                    if (locals.vbucket >= QSWAP_INCENTIVE_HIST_BUCKETS)
                    {
                        locals.vbucket = QSWAP_INCENTIVE_HIST_BUCKETS - 1;
                    }
                    if (locals.vbucket < locals.cutoff)
                    {
                        continue;
                    }
                    locals.poolShare = div(uint128(locals.stray) * uint128(locals.p.volumeQuEpoch), uint128(locals.qualVol)).low;
                    if (locals.poolShare > 0)
                    {
                        locals.num.high = locals.poolShare;
                        locals.num.low = 0;
                        locals.p.accIncentivePerLPX64 += div(locals.num, uint128(locals.p.totalLiquidity));
                        state.mut().mPoolBasicStates.set(locals.i, locals.p);
                        locals.distributed += locals.poolShare;
                    }
                }
                state.mut().mIncentiveUnclaimed += locals.distributed;
                state.mut().mIncentiveLifetimeDistributed += locals.distributed;
            }
        }

        for (locals.i = 0; locals.i < (sint64)QSWAP_MAX_POOL; locals.i++)
        {
            locals.p = state.get().mPoolBasicStates.get(locals.i);
            if (locals.p.poolID == NULL_ID)
            {
                continue;
            }
            if (locals.p.volumeQuEpoch != 0)
            {
                locals.p.volumeQuEpoch = 0;
                state.mut().mPoolBasicStates.set(locals.i, locals.p);
            }
        }
    }

    // ---------------- per-op maintenance statements (volume accrual + incentive debt-twin + auto-claim) ----------------
    struct IncentiveMaintenanceDemo_input { sint64 quAmount; sint64 increaseLiquidity; sint64 burnLiquidity; };
    struct IncentiveMaintenanceDemo_output { sint64 dummy; };
    struct IncentiveMaintenanceDemo_locals
    {
        PoolBasicState pbs; LiquidityInfo tmpLiquidity; LiquidityInfo userLiquidity;
        uint128 pendingIncX64; sint64 incentivePayout;
    };
    PUBLIC_PROCEDURE_WITH_LOCALS(IncentiveMaintenanceDemo)
    {
        // swap site: accrue QU volume
        locals.pbs.volumeQuEpoch += locals.quAmount;

        // add (new record): start at current accumulator
        locals.tmpLiquidity.incentiveDebtX64 = locals.pbs.accIncentivePerLPX64;

        // add (existing record): settle pending on OLD liquidity, then re-snapshot
        locals.pendingIncX64 = uint128(locals.tmpLiquidity.liquidity) * (locals.pbs.accIncentivePerLPX64 - locals.tmpLiquidity.incentiveDebtX64);
        locals.tmpLiquidity.accumulatedIncentive += locals.pendingIncX64.high;
        locals.tmpLiquidity.liquidity += input.increaseLiquidity;
        locals.tmpLiquidity.incentiveDebtX64 = locals.pbs.accIncentivePerLPX64;

        // remove: auto-claim the accrued incentive
        locals.pendingIncX64 = uint128(locals.userLiquidity.liquidity) * (locals.pbs.accIncentivePerLPX64 - locals.userLiquidity.incentiveDebtX64);
        locals.incentivePayout = sint64(locals.userLiquidity.accumulatedIncentive + locals.pendingIncX64.high);
        if (locals.incentivePayout > 0)
        {
            qpi.transfer(qpi.invocator(), locals.incentivePayout);
            if (state.get().mIncentiveUnclaimed >= uint64(locals.incentivePayout))
            {
                state.mut().mIncentiveUnclaimed -= uint64(locals.incentivePayout);
            }
            else
            {
                state.mut().mIncentiveUnclaimed = 0;
            }
        }
        locals.userLiquidity.accumulatedIncentive = 0;
        locals.userLiquidity.incentiveDebtX64 = locals.pbs.accIncentivePerLPX64;
    }

    REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
    {
        REGISTER_USER_FUNCTION(GetClaimableIncentive, 9);
        REGISTER_USER_PROCEDURE(ClaimIncentive, 12);
        REGISTER_USER_PROCEDURE(IncentiveMaintenanceDemo, 1);
    }
};
