# Submission package — QSWAP LP-Incentives

Everything needed to open the PR against `qubic/core`. **Purely additive: ~560 lines added, 0 removed.**

## Files
| File | What it is |
|---|---|
| `Qswap.h` | The **full integrated contract** (`src/contracts/Qswap.h`) — clean June-23 source + our additions. Drop-in. |
| `contract_qswap.cpp` | The **full test file** (`test/contract_qswap.cpp`) — 10 existing + our 6 new `ContractSwap` tests. |
| `CHANGES.diff` | Unified diff of both files vs the clean `core-main-June-23-2026`. |

## What changed in `Qswap.h` (all additive)
- **State:** `PoolBasicState` += `volumeQuEpoch`, `accIncentivePerLPX64`; `LiquidityInfo` += `incentiveDebtX64`,
  `accumulatedIncentive`; `StateData` += `mIncentiveUnclaimed`, `mIncentiveLifetimeDistributed`,
  `mIncentiveBottomPctExcluded`.
- **Per-swap:** accrue `volumeQuEpoch` at the 4 swap sites (next to the existing `accFeePerLPX64` update).
- **AddLiquidity:** init `incentiveDebtX64` on new records; settle pending → `accumulatedIncentive` on top-ups
  (rewards stack).
- **RemoveLiquidity:** auto-claim accrued incentive to the LP on any remove (partial or full).
- **New `END_EPOCH`:** sweep unaccounted QU → bottom-30%-excluded, top-70% volume-weighted credit to
  `accIncentivePerLPX64`; reset volumes.
- **New endpoints:** `GetClaimableIncentive` (function **9**), `ClaimIncentive` (procedure **12**); registered;
  `INITIALIZE` sets `mIncentiveBottomPctExcluded = 30`.

## How to open the PR
1. Fork `qubic/core`; branch from the same base as the deployed contract.
2. Replace `src/contracts/Qswap.h` and `test/contract_qswap.cpp` with the files here (or apply `CHANGES.diff`).
3. Confirm the suite: build `test/test.vcxproj` (Debug x64) and run
   `test.exe --gtest_filter=ContractSwap.*` → `[ PASSED ] 16 tests.`
4. The PR CI runs the Qubic Contract Verification Tool over the full file; locally we verify the extract
   `reference/qswap_incentives_check.h` (see [`../BUILD-AND-TEST-STATUS.md`](../BUILD-AND-TEST-STATUS.md)).
5. In the PR description, lead with the on-chain leak ([`../EVIDENCE.md`](../EVIDENCE.md)) and the open
   questions ([`../PROPOSAL.md`](../PROPOSAL.md) §9) — storage budget, gas of the per-epoch scan,
   percentile exactness, anti-gaming, migration, and how to handle the ~4.65M backlog.

> ⚠️ QSWAP is core-protocol: this proposes a change for the core team; it would go live only via a network
> upgrade at an epoch boundary. Nothing here deploys by our hand. **Open the PR only when you're ready to
> engage the public Qubic process.**
