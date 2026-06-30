# QSWAP LP-Incentives — build & test status

## Bottom line
Designed, integrated into a real QSWAP contract, compiled clean (**0 warnings/errors from our code**), and
**run green: all 16 `ContractSwap.*` tests PASS** (our 6 + the 10 existing, no regressions), and the
additions **PASS the official Qubic Contract Verification Tool** (`Contract compliance check PASSED`).
Per-test breakdown in [`TEST-RESULTS.md`](TEST-RESULTS.md).

## Artifacts
- **Proposal (source of truth):** `%USERPROFILE%\Qubic Devving\Qswap-Incentives-Proposal\`
  - `PROPOSAL.md`, `README.md`, `EVIDENCE.md`, `DESIGN.md`, `CONTRACT-NOTES.md`, `REVIEW.md`,
    `TEST-RESULTS.md`, `reference\qswap_incentives.h`, `reference\qswap_incentives_check.h`,
    `submission\` (changed files + diff + SUBMISSION.md).
- **Build/test working copy:** `%USERPROFILE%\qsi\`
  - A clean copy of `core-main-June-23-2026` **at a short path on purpose** — the deep original path
    (240+ chars) trips the Windows 260-char `MAX_PATH` limit and the MSVC linker can't open the gtest lib.
  - Modified: `src\contracts\Qswap.h` (new state fields; per-swap volume accrual at the 4 swap sites; the
    incentive debt-twin on AddLiquidity + auto-claim on RemoveLiquidity; a new `END_EPOCH`; `ClaimIncentive`
    procedure (12) + `GetClaimableIncentive` function (9); registration + `INITIALIZE` default),
    `test\contract_qswap.cpp` (6 new tests + helpers).
- **Subset-check extract:** `reference\qswap_incentives_check.h` (our additions in a minimal contract).
- **Clean reference (untouched):** `…\Smart Contract Development\core-main-June-23-2026\`.

## Result
- ✅ Contract + tests **compile with 0 warnings/errors from our code** against the real June-23 source
  (the only build warning is a pre-existing `C4244` in the unrelated `contract_clkndgr.cpp`).
- ✅ **ALL 16 `ContractSwap.*` tests PASS** (our 6 + the 10 existing) — full build ~3.5 min, test run ~3.3 min.
- ✅ **Subset compliance PASS** — `contractverify.exe reference\qswap_incentives_check.h` →
  `Contract compliance check PASSED` (the same checker the `qubic/core` PR CI runs).

## Rebuild + run (gtest suite)
1. **Build:**
   `cmd /c "\"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat\" && msbuild %USERPROFILE%\qsi\test\test.vcxproj /p:Configuration=Debug /p:Platform=x64 /m"`
2. **Run:**
   `%USERPROFILE%\qsi\test\x64\Debug\test.exe --gtest_filter=ContractSwap.*`  → `[ PASSED ] 16 tests.`

   Our 6 (the rest are pre-existing QSWAP tests):
   - `Incentive_VolumeTrackingAndReset` · `Incentive_SweepWeightedDistribution_BottomExcluded` ·
     `Incentive_BacklogSweptOnFirstEpoch` · `Incentive_ClaimPaysOutAndZeroes` ·
     `Incentive_AutoClaimOnRemove` · `Incentive_StacksAcrossEpochs`

## Rebuild + run (Qubic Contract Verification Tool)
The `qubic/core` PR-CI subset checker. It parses
for forbidden constructs (no `/`/`%`, no `[]`, no floats/pointers/strings/globals); it does not compile/link.
0. `git clone https://github.com/Franziska-Mueller/qubic-contract-verify.git %USERPROFILE%\qcv`
1. `cd /d %USERPROFILE%\qcv && git -c url."https://github.com/".insteadOf="git@github.com:" submodule update --init --recursive`
2. `set CMAKE="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"`
   `%CMAKE% -S deps\CppParser -B deps\CppParser\builds -G "Visual Studio 17 2022" -A x64 -DFLEX="%USERPROFILE%/qcv/deps/CppParser/cppparser/third_party/flex_tp/flex.exe"`
   `%CMAKE% --build deps\CppParser\builds --config Release`
3. `%CMAKE% -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_CONTRACTVERIFY_TESTS:BOOL=OFF -Dcppparser_DIR="%USERPROFILE%/qcv/deps/CppParser/builds"`
   `%CMAKE% --build build --config Release`
4. `build\src\Release\contractverify.exe "%USERPROFILE%\Qubic Devving\Qswap-Incentives-Proposal\reference\qswap_incentives_check.h"`  → expect `Contract compliance check PASSED`
   (The full `Qswap.h` is checked by this same tool in the PR CI; the local parser can't handle the full
   2,500-line file in reasonable time.)

## Subset review — CONFIRMED by the tool
The official tool returns **`Contract compliance check PASSED`** on `reference\qswap_incentives_check.h`
(the additions extracted into a minimal `QSWAP` contract). Manual review concurs: every division is
`div()`; every array access is `Array.get/.set`; the `<<64` uses `uint128.high/low`; there are no
`/`/`%`/`[]`/floats/pointers/strings/globals in the contract code. The `double`/`EXPECT_NEAR` usages are in
the **test** file only, not the contract.

> Harness note: the extract's wrapper struct is named `QSWAP` (matching the real contract) so its global
> `QSWAP_*` constants satisfy the tool's "global names start with the state-struct name" rule — exactly as
> the live `Qswap.h` does.

## Setup notes
- VS Build Tools 2022 (MSVC 14.44, Windows SDK), GoogleTest restored via nuget (present in the clean copy).
- VS bundles CMake at `…\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
