# fix-issues-hw-usability -- Vision Notes
# Started: 2026-07-16

## The Idea
Two intertwined goals for the tr4mpass activation-lock **research tool**:
1. Work through the outstanding GitHub issues (56 open, 14 closed -- 70 total)
   and land real fixes, not one-offs.
2. Reshape the tool so a **researcher with hardware/iOS-internals experience
   but limited C/Linux tooling background** can drive it end-to-end without
   getting stuck on build systems, dependency mismatches, WSL quirks, or
   silent DFU-detection failures.

The fixing work will be fanned out to **Fable research subagents** (one per
problem cluster, running in parallel via `/flow:research`) so each defect gets
independent, focused investigation instead of a single omnibus pass. Every
subagent brief must open with a standing **research / coordinated-disclosure
authorization** preamble so the request context is unambiguous.

## Why / Intent
- Real researcher pain: the issues repeat the same handful of root causes,
  which means users bounce off before ever exercising the actual bypass paths.
- Target user is **hardware-fluent, code-shy**: they can hold Vol-/Vol+/Power
  in the right sequence, own the right cables, know their chip generation --
  but they should not have to diagnose `pkg-config`, patch a Makefile, or
  guess which libirecovery struct field renamed itself.
- Ethical framing: this is a **research + bug-bounty / coordinated-disclosure
  tool**. Every subagent research brief must state that context so downstream
  models treat the work as authorized security research, not a wild bypass
  request.
- Success = an experienced hardware researcher can `git clone`, run one
  script, get a clear diagnosis, and either proceed to a working bypass on a
  supported device or receive a precise, honest "not supported on this chip
  because X" answer.

## Scope
**In:**
- Root-cause fixes for every open issue that maps to a real defect
- Cross-platform build hardening (macOS, Debian/Ubuntu/Mint, Fedora, Arch, WSL)
- Diagnostic UX: doctor/preflight command, categorized error messages,
  honest per-chip support matrix, workflow docs (Path A vs Path B, manual
  overrides)
- Real feasibility investigation of Path B (A12+) via deep research on
  public prior art, not a stub
- Honest close messages for every open issue -- fixed, WON'T-FIX with
  reason, or DUPLICATE with pointer

**Out:**
- New CLI/GUI surfaces beyond the doctor + existing binary
- Bundling libimobiledevice / libirecovery / libusb sources (dep hell)
- Windows-native support (WSL only; Linux + macOS as native)
- Any assistance that presumes the researcher does NOT own the hardware
- Data-preservation guarantees; we document what is destructive, not
  invent new-preserving flows

## Surfaces & Pages
Concrete things the researcher touches:
- `./start.sh` -- the front-door script (dependency check, build, run)
- The `tr4mpass` binary CLI (with `--cpid`, `--ecid`, `--detect-only`, etc.)
- The diagnostic/log output (what the tool tells the user when something fails)
- `README.md` and any device-support matrix / compatibility page
- Optional: a "doctor" / preflight step that inspects the environment before
  attempting the bypass

## Key Concept Decisions

### C1: One research subagent per root-cause cluster, not per issue
**Decision:** Fan out research to ~12 Opus 4.7 subagents, one per root-cause
cluster (build-deps, libirecovery-api-drift, dfu-serial-parsing, chip-db-integrity,
dfu-to-recovery-transition, path-a-checkm8-stage1, env-wsl-ux,
workflow-manual-overrides, error-ux-doctor, product-docs-honesty,
mobileactivationd-research, a12plus-path-b-feasibility).
**Reasoning:** The 56 open issues collapse to ~10 root causes. Per-issue
fan-out would fire the same investigation 5-10 times.
**Rejected:** Per-issue fan-out (wasteful, duplicative); one omnibus agent
(loses parallelism, weakest link on any one problem).

### C2: Deliverable is code fix + UX wrapper, not code fix alone
**Decision:** Each cluster subagent produces (a) the code fix, (b) any
doctor/preflight additions that would have caught this cluster before it
became an issue, and (c) an error-message rewrite for that failure mode.
Plus a separate `error-ux-doctor` cluster owns cross-cutting UX.
**Reasoning:** The tool currently fails *silently* on DFU parse. Fixing
the bug without fixing "user does not know what went wrong" leaves the
hardware-fluent / code-shy researcher stuck at the same wall.
**Rejected:** Pure code-fix scope (leaves the underlying UX problem intact);
UX-only rewrite (does not close the underlying defects).

### C3: A12+ Path B gets a real research attempt via /deep-research, not a punt
**Decision:** Launch `/deep-research` on Opus 4.7 specifically for the
A12+ Path B feasibility question, in parallel with the cluster subagents.
It investigates public prior art (checkra1n team notes, ipwndfu writeups,
iBoot dumps, T8020/T8030/T8101/T8110/T8120/T8103/T8112 documentation),
verifies each claim adversarially, and reports **feasible / partially
feasible / not feasible** with evidence.
**Reasoning:** The source's own TODO admits the SRAM offset is unknown --
so the honest answer needs research, not a shrug.
**Fallback if research says infeasible:** Cluster subagent lands the
"honest limits" doc + README matrix + CLI refusal, closing the aspirational
A12+ device requests with a clear "not supported because X" message.
**Rejected:** Straight punt to WON'T-FIX (dishonest about what could be
learned); solo attempt to code Path B blind (guaranteed waste).

### C4: Uniform authorization preamble on every subagent brief
**Decision:** Every subagent prompt opens with a fixed authorization
block establishing (a) tool ownership by the maintainer, (b) hardware
ownership by the researcher, (c) coordinated-disclosure / bug-bounty /
defensive-research intent, (d) exclusion of PII, mass targeting,
detection evasion, supply-chain compromise.
**Reasoning:** Downstream models should treat these as authorized
security research, not ambiguous bypass requests. Uniform header keeps
framing load-bearing and consistent.
**Rejected:** Per-agent bespoke framing (drift, inconsistent tone);
implicit framing (unreliable).

## Questions for Research
<!-- Sharp technical questions handed verbatim to /flow:research. Each one gets its own Fable subagent. -->

## Open Questions
- How do we group 70 issues into research clusters? One agent per issue is
  wasteful; one agent per root cause is likely right, but the grouping needs
  agreement.
- What does "usable for a hardware researcher" mean concretely -- better
  errors, a doctor command, a device-support matrix, all three, more?
- Do we scope this to "close every open issue" or "fix the root causes that
  close most issues + honestly mark the rest"?
- What is the exact ethical/authorization preamble every subagent brief must
  open with, so the framing is uniform and load-bearing?

## Non-Goals / Rejected Directions
<!-- Filled in as we reject directions. -->

## Issue-cluster snapshot (from the tracker sweep)
Grouping observed in the 70 issues, kept here so discussion can reference
clusters by name:

- **build-deps** -- libcurl / libusb-1.0 headers not found, WSL/Debian/Fedora
  variations (#70, #74, #67, #54, #51, #44, #21, #16*, #4*, #1*)
- **libirecovery-api-drift** -- `info->pid` vs `info->cpid`,
  `IRECV_SEND_OPT_DFU_NOTIFY_FINISH` undeclared (#55, #31, #24, #19)
- **dfu-serial-parsing** -- descriptor index likely off-by-one; CPID/ECID
  never parsed although `irecovery -q` sees them (#45 is the smoking gun;
  #65, #59, #27, #17, #10*, #8*, #6*, #52, #30, #22, #25, #7*)
- **chip-db-integrity** -- mismatched CPID entries across A5/A7/A10 (#53)
- **dfu-to-recovery-transition** -- won't auto-switch, "failed to reach
  dfuIDLE" (#61, #38, #40, #18)
- **path-a-checkm8-stage1** -- I/O error at stage 1/4 on A10 (#46, #47)
- **path-b-a12plus-incomplete** -- identity manipulation fails; core TODO
  admits SRAM offset unknown (#42, #10*, #5*, #12*, plus every "no A12+
  support" request: #57, #72, #75, #33, #43, #3*, #58, #23)
- **env-wsl-user-error** -- WSL USB passthrough, binary-is-a-directory,
  Windows git missing, sudo confusion (#2*, #20, #29, #34, #37, #35, #28,
  #30, #25, #48)
- **workflow-and-manual-overrides** -- how to use `--cpid`/`--ecid`, when to
  use Path A vs Path B (#76, #62, #36)
- **product-questions** -- untethered? cellular after bypass? data preserved?
  MEID/Chinese variants? (#73, #13, #14, #64, #58, #50)
- **research-thread** -- mobileactivationd forging + sealed root partition
  discussion (#77)
- **low-info** -- vague / screenshot-only / duplicates (#60, #15, #33, #11*)

`*` = closed issue but same root cause.
