# QSWAP — LP Incentives (from recaptured stranded dividends)

Dividend QU owed to QCAP/QMINE that sits inside QSWAP liquidity pools is **stranded in the contract today —
unattributed and growing** (**≈ 4,646,728 QU** as of 2026-06-29, +~1M QU/epoch; full on-chain proof in
[`EVIDENCE.md`](EVIDENCE.md)). This proposal adds a once-per-epoch routine to the QSWAP contract
(index 13) that **recaptures** that QU and pays it out as an **LP incentive** to the pools doing the most
volume — bottom 30% by volume get nothing, the top 70% split it volume-weighted — with the existing ~4.65M
backlog swept as the first distribution. Motivated by the
[Den of Misfits LP & NFT tracker](https://denofmisfits.com).

**Status:** DRAFT · not submitted · integrated + compiled against real Qubic core · **16/16 `ContractSwap`
tests pass** (6 new + 10 existing, no regressions) · **passes the official Qubic Contract Verification
Tool** (subset compliance).

## Contents

| Document | What's in it |
|---|---|
| 📋 [**The proposal →**](PROPOSAL.md) | The full write-up: the leak, the capture→select→distribute mechanism, the LP experience, worked example, storage, and open questions for the core team |
| 🔗 [On-chain evidence](EVIDENCE.md) | The 4.65M measured live: QVAULT/qRWA split, per-epoch trend, identities (K12-verified), compound-filter method, repro |
| 📄 [Reference implementation](reference/qswap_incentives.h) | The additions, annotated and reconciled against the live QSWAP source |
| 🛡️ [Subset-check extract](reference/qswap_incentives_check.h) | The additions in a minimal contract for the official Qubic Contract Verification Tool |
| 🔎 [Review findings](REVIEW.md) | Self-review of the change (correctness, accounting, gas, gaming) |
| 🧪 [Test results](TEST-RESULTS.md) | 16/16 ContractSwap, with the full breakdown |
| 🏗️ [Build & repro](BUILD-AND-TEST-STATUS.md) | How to rebuild the test suite and the verification tool |
| 📝 [Contract notes](CONTRACT-NOTES.md) | Grounded study of the QSWAP internals the fix hooks into |
| 🧭 [Design spec](DESIGN.md) | The internal implementation spec (state, END_EPOCH algorithm, claim semantics) |
| 📦 [Submission package](submission/SUBMISSION.md) | The changed files + diff, and how to open the PR |

## The ask, in one line

Add a per-epoch `END_EPOCH` to QSWAP that sweeps the contract's unaccounted QU (the stranded QCAP/QMINE
dividends + the backlog) and credits it to the **top-70%-by-volume** pools' LPs via a new
`accIncentivePerLPX64` accumulator — claimable via **`ClaimIncentive`** (proc 12), readable via
**`GetClaimableIncentive`** (fn 9), auto-paid on remove. Full case in **[PROPOSAL.md](PROPOSAL.md)**.
