# QSWAP LP-Incentives — TEST RESULTS ✅

## Result: ALL PASS — 16 / 16  +  subset-compliance PASS
Built and run against the **real Qubic core** (`core-main-June-23-2026`) with MSVC (VS Build Tools 2022,
MSVC 14.44) + GoogleTest. Our 6 new `ContractSwap` tests exercise the whole mechanism — volume tracking,
the END_EPOCH sweep, the bottom-30%/top-70% volume-weighted split, the recovered-backlog first run, claim
payout, auto-claim-on-remove, and cross-epoch stacking — plus the 10 pre-existing QSWAP tests (no regressions).

```
[==========] Running 16 tests from 1 test case.
[  PASSED  ] 16 tests.
EXIT: 0
[----------] 16 tests from ContractSwap (197618 ms total)
```

### Our 6 new tests — all OK

- ✅ **`Incentive_VolumeTrackingAndReset`** — each swap accrues its QU volume into the pool
  (`poolVolumeEpoch` 0 → 50k → 80k), and `END_EPOCH` resets it to 0. No stray injected ⇒ nothing distributed.
- ✅ **`Incentive_SweepWeightedDistribution_BottomExcluded`** — **the core test.** 4 pools with volumes
  300k / 200k / 100k / 10k; inject 6,000,000 QU of stray dividends; run `END_EPOCH`. The bottom pool
  (10k) receives **exactly 0**; the top three split the pot **3 : 2 : 1 by volume**
  (`clA/clC ≈ 3.0`, `clB/clC ≈ 2.0`); ~all 6,000,000 is distributed (rounding dust < #pools); volumes reset.
- ✅ **`Incentive_BacklogSweptOnFirstEpoch`** — injects the measured on-chain backlog **4,646,728 QU**
  before any epoch has run; the first `END_EPOCH` sweeps ~all of it to the single active pool's LPs
  (the proposed "first live distribution"). Proves the balance-based sweep needs no per-event history.
- ✅ **`Incentive_ClaimPaysOutAndZeroes`** — `ClaimIncentive` transfers exactly `claimableQu` to the LP's
  wallet, decrements `mIncentiveUnclaimed` by the same amount, and a second claim pays **0**.
- ✅ **`Incentive_AutoClaimOnRemove`** — `RemoveLiquidity` (partial) auto-pays the accrued incentive to the
  LP's wallet (wallet rises by ≥ the incentive); claimable is **0** afterward. (Owner-requested semantics.)
- ✅ **`Incentive_StacksAcrossEpochs`** — with no claim in between, a second epoch's distribution **stacks**
  on the first (claimable roughly doubles), confirming rewards accrue indefinitely until claimed/removed.

### 10 existing QSWAP tests — all still OK (no regressions)
InvestRewardsInfoTest · QuoteTest · IssueAssetAndTransferShareManagementRights · SwapExactQuForAsset ·
SwapQuForExactAsset · SwapExactAssetForQu · SwapAssetForExactQu · CreatePool · LiqTest1 · LiqTest2.
→ The additions (new state fields, per-swap volume accrual, the incentive debt-twin on add, auto-claim on
remove, the new `END_EPOCH`, and the two new endpoints at fn 9 / proc 12) leave existing
add / remove / swap / quote behavior unchanged.

### Build & subset compliance
- ✅ Contract + tests **compile with 0 warnings / 0 errors from our code** against the real June-23 source
  (the only warning in the build is a pre-existing `C4244` in an unrelated test file, `contract_clkndgr.cpp`).
- ✅ **Official Qubic Contract Verification Tool: `Contract compliance check PASSED`** on the extracted
  additions (`reference/qswap_incentives_check.h`) — the same subset checker the `qubic/core` CI runs.
- Compile + test run reproduced from a clean short-path copy `%USERPROFILE%\qsi\`.

## Reproduce
1. Build: `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild %USERPROFILE%\qsi\test\test.vcxproj /p:Configuration=Debug /p:Platform=x64 /m"`
2. Run: `%USERPROFILE%\qsi\test\x64\Debug\test.exe --gtest_filter=ContractSwap.*`  → `[ PASSED ] 16 tests.`

## What this proves
The fix is integrated into the real contract, compiles clean, and passes a 16-test behavioral suite that
nails the headline behaviors: the recaptured dividends (incl. the exact on-chain backlog) reach LPs of the
most-active pools, volume-weighted, with the bottom % excluded — and LPs can read, claim, stack, and
auto-collect-on-remove their incentive.
