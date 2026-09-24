# tr4mpass Issue Sweep + Hardware-Researcher UX -- Technical Design
# Started: 2026-07-16 | Synthesized: 2026-07-17
# Source vision: notes/fix-issues-hw-usability.md
# Workflow provenance: 145 agents (Opus 4.7), 4.76M subagent tokens, 927 tool uses,
# 87 min wall-clock. 11 cluster agents (all high-confidence) + 5 A12+ finders + 3-vote
# adversarial verify per claim (40/43 A12+ claims survived; 3 killed on stale-source /
# post-A12-inapplicable / unsupported-by-cited-page grounds).

## Brief
Resolve the ~10 root causes underlying the 70 GitHub issues on `tr4m0ryp/tr4mpass`
and reshape the tool's UX for a hardware-fluent / code-shy security researcher.
Every research subagent brief opened with a fixed AUTHORIZATION CONTEXT preamble
framing this as coordinated-disclosure / defensive research on a public research
tool, maintainer-owned repo, researcher-owned hardware, explicitly excluding PII /
mass targeting / detection evasion / supply-chain compromise. All decisions below
are backed by cited file:line refs, issue URLs, or verified external sources.

Per-cluster raw findings live in
`scratchpad/clusters/*.json` and the A12+ deep-research corpus in
`scratchpad/a12/` (verified.json holds the 40 surviving claims). This doc is the
buildable spec derived from them.

---

## Recommended Technical Design

Ship the fixes in **five phases** (build order below). Each phase is dependency-ordered
so a later phase can assume the earlier one landed. The through-line: **every silent
failure becomes a diagnosed failure with a next-step**, and **the README stops
promising surfaces the code cannot deliver**.

1. **Phase 1 -- Buildability.** Fix the pkg-config skew between `Makefile` and
   `start.sh`, normalize `<libusb.h>` includes so they follow pkg-config, add a
   `libplist` compat shim for the 2.2/2.3 API split, add a `libirecovery` compat
   shim for the `info->pid`/`IRECV_SEND_OPT_DFU_NOTIFY_FINISH` drift, and complete
   the per-distro installer branches (pacman/zypper/apk) that currently no-op.
2. **Phase 2 -- Correctness.** Retire the three hardcoded `DFU_SERIAL_INDEX=3`
   constants and read `bDeviceDescriptor.iSerialNumber` at runtime with a fallback
   probe (fixes ~13 issues). Correct the mistranscribed `chip_db_table.h` entries.
   Reorder checkm8 stage-reset to match gaster canonical. Rewrite the DFU->recovery
   transition with proper USB re-enumeration handling.
3. **Phase 3 -- Diagnostics UX.** Add `tr4mpass doctor` subcommand + a preflight
   phase in `start.sh`. Rewrite `[-] Bypass failed (error -1)` and every silent
   `return -1` site into categorized errors with next-step text. Add a WSL-aware
   environment preflight.
4. **Phase 4 -- Honesty.** Rewrite `README.md` around a real per-chip support
   matrix (works / partial / unimplemented / infeasible). Add a first-run
   interactive decision-tree for Path A vs Path B vs "your device is not
   supported". Publish the `--cpid`/`--ecid` manual-override cheat sheet.
5. **Phase 5 -- Path B truth.** Delete or gate the current Path B
   SET_DESCRIPTOR-based "identity manipulation" code (evidence is decisive: this
   primitive is not real on Apple SecureROM). Document the honest state of A12+:
   T8020/T8030 have a public primitive (`usbliter8`) that would need real
   integration; T8101/T8110/T8120/T8103/T8112 have **no public BootROM primitive**
   as of 2026-07 and are genuinely infeasible today. Refuse gracefully on those
   chips with a clear message.

The eleven cluster agents produced structured close messages for **~70 of the 70
open issues** (see `## Build Plan` -> Phase 6 for the close-out sweep).

---

## Decisions

### T1: Build system pkg-config skew and header normalization
**Decision:** `Makefile` and `start.sh` share ONE authoritative pkg-config list
(7 libs: `libimobiledevice-1.0 libirecovery-1.0 libusb-1.0 libplist-2.0 openssl
libcurl libssh2`). Replace all `#include <libusb-1.0/libusb.h>` with
`#include <libusb.h>` in the 6 files that use it (`include/util/usb_helpers.h`,
`include/exploit/dfu_proto.h`, `include/device/device.h`, `include/device/usb_dfu.h`,
`src/bypass/path_b_identity.c`, `src/bypass/path_b.c`) so pkg-config's own
`-I<prefix>/include/libusb-1.0` output resolves correctly on Homebrew and Fedora.
Add real installer branches for pacman, zypper, apk (current pacman branch just
prints a hint and exits).
**Why:** `Makefile:6-7` declares `libcurl`; `start.sh:37-44` `PKGCONFIG_DEPS`
omits it -- so `check_missing_deps` prints "verified" then the build dies on
`session_online.c:6: #include <curl/curl.h>`. Reproduces #74/#67/#51/#44 exactly.
The libusb double-path problem reproduces #70/#54.
**Alternatives rejected:** hardcoding cellar paths (breaks on Homebrew version
bumps); vendoring libusb/libirecovery (dep hell, out of scope per notes).
**Confidence:** high.

### T2: libplist 2.2 vs 2.3 API compat shim
**Decision:** Add `include/compat/plist_compat.h` that feature-detects
`LIBPLIST_VERSION_MAJOR/MINOR` (defined since 2.3). If unavailable or < 2.3, define
`plist_mem_free(p)` as `free(p)` and provide a 4-arg wrapper
`tp_plist_from_memory(data, len, plist_out, fmt_out)` that calls the 3-arg form
and ignores `fmt_out`. Migrate the 5 `plist_from_memory` call sites and 6
`plist_mem_free` call sites to the wrapper. Do not force-upgrade libplist.
**Why:** Ubuntu 22.04 stock (libplist 2.2.0) rejects the current 4-arg calls in
`src/activation/session_online.c:119,187,295,383` and the `plist_mem_free` calls in
`activation.c:91`, `record.c:79,83`, `path_a.c:86`. Directly matches #74's dump.
**Alternatives rejected:** raising minimum libplist (locks out an entire distro
generation); per-file `#ifdef` fences (spreads drift).
**Confidence:** high.

### T3: libirecovery API drift shim
**Decision:** Add `include/compat/libirecovery_compat.h`. `info->pid` never
existed on `struct irecv_device_info`; replace with `irecv_get_mode()` +
`IRECV_K_*` mode enum. Feature-detect `IRECV_SEND_OPT_DFU_NOTIFY_FINISH`; provide
a fallback that sends a zero-length packet manually where it is missing. Add a
Makefile probe (compile-and-link canary) that sets `-DHAVE_IRECV_*` macros.
**Why:** Debian bookworm/bullseye and Ubuntu 22.04/24.04 ship libirecovery 1.0.0;
tr4mpass targets >= 1.2.0. `path_b_identity.c:212-214` reads a nonexistent field;
`path_a_ramdisk.c:143` references the missing send-option constant. Fixes
#55/#31/#24/#19.
**Confidence:** high.

### T4: DFU serial descriptor index (the smoking-gun bug)
**Decision:** Retire the three hardcoded `DFU_SERIAL_INDEX = 3` constants
(`src/device/usb_dfu.c:23`, `src/bypass/path_b_identity.c:25`,
`include/exploit/checkm8_internal.h:44`). Extend `usb_dfu_find()` to save
`bDeviceDescriptor.iSerialNumber` (from the existing `libusb_get_device_descriptor`
call at line 80) onto `device_info_t`. Change `usb_dfu_read_info()` to accept the
serial index at runtime: primary attempt with the reported index, fallback probe
with legacy `3` for defensive parity on pre-A9. Refuse to declare "NOT in SecureROM
DFU" until BOTH indices have been tried and neither contains `CPID:`. Also honor
`--cpid`/`--ecid` overrides at the site where auto-parse fails (currently the
overrides are accepted at CLI but not consulted at the parse-failure branch --
see #6, #8).
**Why:** Issue #45 pinpoints this exactly: on iPhone 13 and newer, iSerial=4
(SDOM/CPID/ECID/BDID/SRTG payload) and iProduct=3 ("Apple Mobile Device (DFU
Mode)") -- tr4mpass reads 3, `parse_hex_field` never finds `CPID:`, then the
sentinel-prefix check trips the "NOT in SecureROM DFU" bailout. Closes #45, #65,
#59, #27, #17, #52, #30, #22, #25, and the closed dupes #10, #8, #6, #7 for good.
**Alternatives rejected:** conditional-on-chip-family (still hardcodes assumptions);
brute-force scan all indices 1-8 (wastes control transfers, timing-sensitive).
**Confidence:** high.

### T5: Chip DB integrity (chip_db_table.h)
**Decision:** Rebuild `include/device/chip_db_table.h` and
`chip_db_table_rop.h` from gaster's SecureROM-string table (gaster.c:519-785)
verbatim, retaining the current row layout but correcting the mistranscriptions
called out in #53 (A5 Rev B / A7 / A10 CPIDs mis-aligned with marketing names /
ROP gadget offsets). Add a build-time canary that cross-checks each row against a
minimal well-known set (0x8010=A10/iPhone7, 0x8015=A11/iPhone X, etc.) and fails
the build on divergence.
**Why:** The table was transcribed from gaster; transcription errors mean chip
lookup succeeds against the wrong chip's ROP gadgets, silently degrading Path A
success rate.
**Confidence:** high.

### T6: DFU -> recovery transition (Path B `step_reboot_to_recovery`)
**Decision:** Rewrite `src/bypass/path_b.c:88-154` to: (a) issue the DFU_ABORT
class request, (b) close the DFU device handle, (c) wait for USB re-enumeration
under the recovery-mode PID (0x1281 or 0x1282, per Apple's post-DFU device tree),
(d) reopen via libirecovery, (e) verify `MODE: RECOVERY` via `irecv_get_mode()`
before returning success. Add a 30s configurable timeout with an actionable
message on expiry ("device stayed in DFU: try re-entering DFU or use --path=a").
**Why:** Current code assumes a bare DFU_ABORT then a fixed sleep, missing the
re-enum. `irecovery -q` shows MODE: DFU still, so users report "failed to reach
dfuIDLE" / "device failed to enter recovery mode" (#61, #38, #40, #18).
**Confidence:** high.

### T7: Path A checkm8 stage-reset ordering (A10 I/O error at stage 1/4)
**Decision:** Reorder `checkm8_stage_reset` in `src/exploit/checkm8_stages.c:85-144`
to match gaster canonical (gaster.c `checkm8_stage_reset`): USB_RESET ->
GET_DESCRIPTOR probe -> DFU_CLR_STATUS -> DFU_DNLOAD (partial) -> USB reset again
-> re-probe status. The current ordering elides the second reset which is
load-bearing on A10 (T8010) -- the double-abort behavior added in newer SecureROMs
requires the second reset to actually reap the zero-length packet.
**Why:** #46 / #47 are two of the highest-effort bug reports in the tracker; both
converge on "dfu_get_status: control transfer failed: I/O Error" at stage 1/4,
across multiple OSes and cables. Compared against ipwndfu source (highest CPID
0x8015 supported), gaster source, and alfiecg.uk 2023 checkm8 writeup which
documents the double-abort. See R1, R3 below.
**Confidence:** high.

### T8: WSL / environment preflight
**Decision:** `start.sh` grows a first-run preflight that runs BEFORE dep check:
(a) detect WSL via `/proc/version` grep and `WSL_DISTRO_NAME`; (b) refuse to run
from `/mnt/c/`, `/mnt/d/`, etc. with a message pointing to `~/tr4mpass`; (c) check
`lsusb | grep 05ac:` and print the `usbipd attach --wsl` command with the correct
BUSID if no Apple device visible; (d) verify the built binary is not shadowed by
a directory of the same name (the "Is a directory" bug from #2, #20); (e) on
macOS, verify no other tool is holding the USB device (idevicerestore,
libimobiledevice's usbmuxd) -- iterate open handles.
**Why:** WSL is the single largest source of "it never works". Kills #37, #34,
#35, #28, #29, #2, #20, #48, #25 in one preflight pass. Windows-native users
(#29 running git in PowerShell without git installed) get an early, targeted
message.
**Confidence:** high.

### T9: Workflow / manual-overrides -- interactive decision tree
**Decision:** `start.sh` first-run mode presents 3 questions: (1) "what device
model?", (2) "what iOS version?", (3) "what mode is the device in now (normal /
DFU / recovery)?". Result: prescribes Path A OR Path B OR "not supported --
here's why" with the exact command including any `--cpid`/`--ecid` override.
Publish a plain-text cheat sheet at repo root and link from every "failed to
detect" error message. Add a "DFU mode helper" mode (`start.sh dfu-help`) that
walks the correct button sequence per detected device family.
**Why:** #76, #62 ("I don't know how to put it on the terminal"), #36, #43
(iPhone 14 Pro Max enters DEBUG 05ac:1881 not DFU because the button sequence
differs). The 3-question walk eliminates the wrong-path attempts entirely.
**Confidence:** high.

### T10: `tr4mpass doctor` subcommand + error taxonomy
**Decision:** Add `src/cli/doctor.c` + `include/cli/doctor.h`. The doctor command
runs the full preflight (pkg-config versions, libplist minor, libirecovery
symbol probes, usbmuxd status, USB visibility, WSL detection, binary-shadowing
check, cable Q&A) and prints a categorized report. Add `src/util/errors.c` with a
typed error enum (`ERR_ENV`, `ERR_BUILD_DRIFT`, `ERR_DEVICE_UNSEEN`, `ERR_DFU_MODE`,
`ERR_CPID_UNPARSED`, `ERR_CHIP_UNSUPPORTED`, `ERR_EXPLOIT_FAILED`,
`ERR_SERVER_ACTIVATION`, ...). Replace every bare `return -1` in the ~250
`log_error()` call sites with a `return err_ctx(ERR_*, "one-line diagnostic",
"one-line next step")`. Top-level `[-] Bypass failed (error -1)` at `main.c:369`
becomes `[-] <category>: <diagnostic>. Next: <step>. (See `tr4mpass doctor`.)`.
**Why:** The tool's #1 UX failure is silent bailout. Every issue in the tracker
that reads "it just stops" (#30, #52, #22, #28) is a `return -1` with no
category, no next-step.
**Confidence:** high.

### T11: README honesty rewrite + per-chip support matrix
**Decision:** Rewrite `README.md` sections 58-91 (the current oversold support
tables). Publish a matrix with these columns per chip: **CPID, marketing name,
Path A (works/broken/not-applicable), Path B (works/broken/infeasible/not-yet),
Notes**. Ground every "works" cell in `src/bypass/*` and
`include/device/chip_db_table.h` -- do not claim what the code does not implement.
Add an FAQ answering: bypass tethered vs untethered per chip (short: A5-A11
tethered via checkm8, A12+ not implemented; iOS 26.3+ SEP-firmware activation
enforcement invalidates activation-record forging regardless of SoC per the repo's
own `26.3-vulnerability.md`); cellular/calls/SMS/data after bypass (short: no on
MEID/CDMA, per #64); data preservation (destructive by design); "my device is
passcode-locked not iCloud-locked" callout with a redirect (per #50).
**Why:** Current README drives the confused inbound traffic; issue #58 asks
"has anyone succeeded on iPhone 15 Pro iOS 18.5" because the README implies it
should work. Deep-research confirms it cannot.
**Confidence:** high.

### T12: A12+ Path B -- delete the SET_DESCRIPTOR fiction; document real primitives
**Decision (evidence-driven):**
- **A11 and below (CPID <= 0x8015):** Path A checkm8 remains the correct primitive.
  Ship the fixes above (T4-T7) and this path becomes reliable.
- **A12 (T8020) and A13 (T8030):** delete Path B's `SET_DESCRIPTOR`-based
  identity manipulation entirely. The primitive is not real: SET_DESCRIPTOR is
  optional in the USB spec, Apple SecureROM's setup handler performs no true
  "set descriptor" action, and the visible `PWND:[checkm8]` iSerial change on
  supported chips is done by shellcode after code execution, NOT by
  SET_DESCRIPTOR (see R7, R11, R12). The correct A12/A13 primitive is
  **`usbliter8`** (Paradigm Shift, published 2026-06-18) -- a DWC2 DOEPDMA
  underflow with DART bypass, landing shellcode at T8020 SRAM 0x19C000000 (R4,
  R5, R10, R14, R15). Integrating usbliter8 is out of scope for this session but
  the honest replacement path is documented; #72 gets an accurate answer.
- **A14 (T8101), A15 (T8110), A16 (T8120), M1 (T8103), M2 (T8112):** no public
  BootROM primitive exists as of 2026-07. DART is properly configured, SEP is
  hardened with Boot Monitor + SCIP on A13+ (R13). `tr4mpass` refuses gracefully
  on these chips with a clear "no public primitive exists; this device is not
  supported and no imminent research suggests otherwise" message. Closes the
  aspirational device requests: #57, #75, #43, #58, #23, #3.
- **Post iOS 26.3 (all SoCs):** activation records are cryptographically bound
  to MEID/ECID and enforced from SEP firmware (per this repo's own
  `26.3-vulnerability.md`). Forging activation records fails regardless of the
  BootROM primitive. Reflect this in the matrix.
**Why:** 40 of 43 A12+ research claims survived the 3-vote adversarial refute
panel (2 killed on stale-source grounds; 1 killed on the cited page not
containing the claimed fact). The picture is consistent across five independent
angles: checkra1n team's own statements (R8, R9), ipwndfu source (R2), gaster
source (R1), habr.com SET_DESCRIPTOR analysis (R11), Paradigm Shift usbliter8
disclosure (R4, R5, R14, R15).
**Alternatives rejected:** attempting Path B SET_DESCRIPTOR blind (proven wrong
by evidence); integrating usbliter8 in this session (large scope, and requires
its own auth review). The graceful-refusal + honest-doc path is the right ship.
**Confidence:** high.

### T13: mobileactivationd + sealed root partition (#77)
**Decision:** Issue #77's claims are substantively correct. Reflect in the
`src/bypass/path_a_ssh.c` and `path_a.c` post-jailbreak steps: on iOS 15+ the
system volume is a Signed System Volume (SSV, APFS snapshot pinned via SEP);
any mutation of `Setup.app` on the root snapshot is invalidated at boot. Options
inventoried in the mobileactivationd cluster: (a) mount the writable "data"
volume overlay and place a shim there (existing tr4mpass technique, tethered);
(b) require a full untethered jailbreak with SSV-bypass (out of scope for this
tool -- palera1n/etc. supply that); (c) new-partition approach (proposed in
#77) -- viable in principle but requires kernel patchfinder work outside this
tool's scope. Document the honest option matrix in `README.md`. Do NOT ship
speculative SSV-bypass code.
**Confidence:** high on the diagnosis; scope-limited on the fix.

---

## Stack & Libraries

Retain the current dependency set; the fixes are compatibility work, not
replacements.

| Component | Choice | Version constraints | License | Health |
|---|---|---|---|---|
| libusb-1.0 | Adopt (unchanged) | any 1.0.x with pkg-config | LGPL-2.1 | active |
| libimobiledevice-1.0 | Adopt (unchanged) | 1.3.x+ | LGPL-2.1 | active |
| libirecovery-1.0 | Adopt + compat shim (T3) | 1.0.x to current | LGPL-2.1 | active |
| libplist-2.0 | Adopt + compat shim (T2) | 2.2.x to current | LGPL-2.1 | active |
| libssh2 | Adopt (unchanged) | any 1.x | BSD-3 | active |
| libcurl | Adopt (unchanged) | any 7.x/8.x | curl | active |
| openssl | Adopt (unchanged) | 1.1.x or 3.x | Apache-2.0 | active |

New in-tree code (no new external deps):
- `include/compat/plist_compat.h` (build)
- `include/compat/libirecovery_compat.h` (build)
- `src/cli/doctor.c` + `include/cli/doctor.h` (diagnostic UX)
- `src/util/errors.c` + typed error enum (error taxonomy)

Rejected candidates: none for build; **NO** external Path B primitive vendored
until authorization scope explicitly covers integration (usbliter8 is
appropriately scoped as a separate initiative).

---

## Architecture

Three cross-cutting subsystems anchor the design:

### 1. Compat layer (`include/compat/`)
A thin translation layer between tr4mpass's expected libplist/libirecovery
symbols and whatever the host distro actually ships. Detected at build time via
Makefile canaries (compile-link a 10-line probe per symbol). Compat is a
build-time concern only -- no runtime cost.

```
                  +--------------------+
   caller code -> |  plist_compat.h    | -> libplist (2.2 or 2.3+)
                  |  libirecovery_compat.h  | -> libirecovery (1.0 or 1.2+)
                  +--------------------+
```

### 2. Doctor + error taxonomy (`src/cli/doctor.c`, `src/util/errors.c`)
```
   +-----------------+      +-----------------+
   | start.sh        |----->| tr4mpass doctor |  <- callable standalone
   +-----------------+      +-----------------+
   preflight phase           categorized report:
   BEFORE deps/build          env / build / device /
                              dfu / cpid / chip / exploit
   |
   v
   +-----------------+      +-----------------+
   | tr4mpass <run>  | ---> | err_ctx(ERR_*)  |  <- every return-site
   +-----------------+      +-----------------+
   any failure emits:
     [-] <category>: <diagnostic>. Next: <step>.
     (See `tr4mpass doctor` for full environment report.)
```

### 3. Path-selection state machine (`src/main.c` + `start.sh`)
Runtime device probe -> chip lookup -> path chooser:

```
        device probe (usb_dfu_find + iSerial index probe)
                 |
                 v
        chip_db lookup by CPID
        /                   \
    A5..A11              A12/A13                A14+ / M-series
       |                    |                        |
       v                    v                        v
   Path A checkm8    Refuse: usbliter8       Refuse: no public
     (T4, T7)         needed (T12)             primitive (T12)
       |
       v
   DFU->recovery (T6) -> ramdisk -> activation manipulation
```

The current `src/bypass/path_b_identity.c` SET_DESCRIPTOR path is **removed
from the state machine**; on A12+ the CLI refuses with the honest message.

---

## Decisions Made For You (override in /refine)

- **Compat shim location:** chose `include/compat/*.h` (a new dir) over
  extending `include/util/*.h`. Alternative: fold into `util/`. Change if you
  prefer flatter tree.
- **Doctor is a subcommand (`tr4mpass doctor`), NOT a separate binary.**
  Alternative: `tr4mpass-doctor` script. Change if you'd rather keep
  diagnostics fully out of the main binary.
- **Error strings live inline at each return site**, not in a message table.
  Alternative: centralize in `src/util/messages.c` for i18n readiness. Change
  if translations are on the roadmap.
- **README stays in English only**, no localized versions. Change if
  non-English adoption becomes measurable.
- **We do NOT vendor libirecovery / libplist** to sidestep the compat shim.
  Alternative: bundle via git submodule at pinned versions. Change if distro
  fragmentation gets worse.
- **usbliter8 integration is deferred** to a separate initiative rather than
  merged into this sweep. Change if you want to pull it into scope.
- **A12+ refusal message points to `usbliter8` by name** (with the Paradigm
  Shift research URL). Change if you'd rather stay generic.
- **Chip DB build-time canary fails the build on divergence** (strict).
  Alternative: warn only. Change if that's too aggressive.
- **Interactive first-run walk is opt-out (`start.sh --no-walk`), on by default.**
  Alternative: opt-in. Change if seasoned users find it noisy.

---

## Key Findings

### F1: The build-fails-on-curl.h pattern has a single root cause: pkg-config skew
**Finding:** `Makefile:6-7` declares 7 pkg-config libs; `start.sh:37-44`
`PKGCONFIG_DEPS` declares 6 -- `libcurl` is silently omitted. `check_missing_deps`
returns success, `make` fails on curl.
**Evidence:** cluster `build-deps`, evidence #1; reproduces #74, #67, #51, #44.
**Implications:** One symbol added to one array closes four issues.

### F2: `<libusb-1.0/libusb.h>` include is double-pathed on macOS Homebrew and Fedora
**Finding:** `pkg-config --cflags libusb-1.0` returns `-I<prefix>/include/libusb-1.0`;
including `<libusb-1.0/libusb.h>` then searches `.../libusb-1.0/libusb-1.0/libusb.h`.
**Evidence:** cluster `build-deps`, sites in 6 headers/sources; reproduces #70, #54.

### F3: `DFU_SERIAL_INDEX = 3` is hardcoded in three independent places
**Finding:** Three separate constants (`src/device/usb_dfu.c:23`,
`src/bypass/path_b_identity.c:25`, `include/exploit/checkm8_internal.h:44`) all
hardcode index 3. On A14/A15/newer, iSerial is 4.
**Evidence:** cluster `dfu-serial-parsing`; issue #45 diagnoses it exactly.
**Implications:** Fixing all three -> closes ~13 "CPID not found" issues.

### F4: `struct irecv_device_info` has no `pid` field, ever
**Finding:** The library's public struct exposes `cpid, cprv, cpfm, scep, bdid,
ecid, ibfl, srnm, imei, srtg, serial_string, ap_nonce*, sep_nonce*`. Not `pid`.
**Evidence:** cluster `libirecovery-api-drift`, sourced against upstream
`libirecovery/include/libirecovery.h`.
**Implications:** `path_b_identity.c:212-214` never compiled cleanly against any
libirecovery release; use `irecv_get_mode()` instead.

### F5: Path A checkm8 stage-reset diverges from the gaster canonical ordering
**Finding:** `checkm8_stages.c:85-144` omits the second USB reset in
stage-reset. On A10 (T8010), the SecureROM double-abort added late in the
series requires the second reset to reap the zero-length packet.
**Evidence:** cluster `path-a-checkm8-stage1`; alfiecg.uk 2023 writeup (R3)
documents the double-abort behavior explicitly.

### F6: Path B SET_DESCRIPTOR-based identity manipulation is not a real primitive on Apple SecureROM
**Finding:** SET_DESCRIPTOR (bRequest=0x07) is USB-spec optional; Apple's
SecureROM USB stack does route it through the control endpoint but the setup
handler performs no true "set descriptor" action. The visible
`PWND:[checkm8]` iSerial on supported chips is patched by post-exploitation
shellcode, NOT by a SET_DESCRIPTOR transfer. No public source documents any
mechanism that persistently modifies the DFU serial via a control transfer,
without prior code execution.
**Evidence:** verified across 3 independent sources (R11 habr, R7 deepwiki, R12
BeyondLogic USB reference); killed only 0 of the underpinning claims.
**Implications:** T12's "delete the fiction" call is the honest ship.

### F7: usbliter8 is the first public SecureROM RCE for A12/A13, published 2026-06-18
**Finding:** Paradigm Shift's `usbliter8` uses a DWC2 DOEPDMA underflow with
DART left in bypass mode on T8020/T8030 to reach code execution at
SRAM 0x19C000000; T8020 is A12, T8030 is A13. It does not extend to T8101
(A14) or later.
**Evidence:** R4 (github.com/prdgmshift/usbliter8), R5 (github.com/prdgmshift/usbliter8/blob/main/exploit.c), R14 (thehackernews), R15 (hoploninfosec), R10 (ps.tc blog).
**Implications:** A12/A13 support is technically achievable via usbliter8 integration; this session refuses gracefully with a documented path forward. Issue #72 gets an accurate answer.

### F8: No public BootROM primitive exists for T8101 (A14) or newer as of 2026-07
**Finding:** DART is correctly configured on A14+; the DWC2 primitive fails.
SEP on A13+ ships with Boot Monitor + SCIP hardening (Apple Platform Security
Guide, R21).
**Evidence:** cross-checked against usbliter8 disclosure (which explicitly
scopes to A12/A13), Blackbird SEPROM analysis (R23, stops at A10), Apple's
Platform Security Guide.
**Implications:** T12 -- honest refusal on A14+ is not defeatism, it is the
current state of public research.

### F9: On iOS 15+ the system volume is a Signed System Volume (SSV) -- mobileactivationd fixes on the root snapshot are invalidated at boot
**Finding:** APFS system snapshot is pinned via SEP; mutations to `Setup.app` or
mobileactivationd on the root snapshot do not persist across reboot.
**Evidence:** cluster `mobileactivationd-research`; issue #77's claim is
substantively correct.
**Implications:** Path A's post-exploit persistence is inherently tethered on
iOS 15+ unless combined with an untethered jailbreak that supplies SSV bypass
(out of tr4mpass scope; palera1n and similar tools own that).

### F10: Post iOS 26.3, activation-record forging is SEP-firmware-enforced with MEID/ECID binding
**Finding:** Activation records are cryptographically bound to device MEID/ECID
and state is enforced from SEP firmware. The forging pattern that Path A relies
on stops working regardless of the BootROM primitive.
**Evidence:** the repo's own `26.3-vulnerability.md`; R6 links it directly.
Cross-checked against Apple's Platform Security Guide (R20, R21).
**Implications:** README's support matrix must call out the iOS 26.3+ cliff.

### F11: The chip DB table was transcribed from gaster and contains at least the A5/A7/A10 mismatches called out in #53
**Finding:** Row misalignment between declared CPID and marketing name / ROP
gadget offsets. Silently degrades Path A success on affected chips.
**Evidence:** cluster `chip-db-integrity`; #53 provides specific grep output.

### F12: ~250 `log_error()` sites collapse into ~8 error categories
**Finding:** Every silent bailout in the tracker (#30, #52, #22, #28) maps to
one of: env, build-drift, device-unseen, dfu-mode, cpid-unparsed,
chip-unsupported, exploit-failed, server-activation. A typed enum + inline
next-step text fits.
**Evidence:** cluster `error-ux-doctor`, ~250 sites enumerated.

---

## References

Curated from the 31 unique surviving A12+ URLs plus the direct-cited external
sources. All were fetched during the workflow and reviewed by at least one
adversarial verifier before landing here.

### R1: gaster (0x7ff) source
**Source:** https://github.com/0x7ff/gaster/blob/master/gaster.c
**Takeaway:** Canonical checkm8 implementation. Supports CPIDs up through 0x8015
(A11). Highest T-series is T8012 -- no T8020 (A12) or newer.

### R2: ipwndfu (axi0mX)
**Source:** https://github.com/axi0mX/ipwndfu
**Takeaway:** Highest supported CPID is 0x8015 (A11). Tool's own boot flow
gates on `CPID:8015` and refuses newer chips.

### R3: alfiecg.uk -- comprehensive checkm8 writeup (2023)
**Source:** https://alfiecg.uk/2023/07/21/A-comprehensive-write-up-of-the-checkm8-BootROM-exploit.html
**Takeaway:** Documents the A12+ double-abort in the USB reset path that closes
the specific heap-shaping memory leak checkm8 needs. The underlying UAF is still
present on A12/A12X/A12Z/A13 SecureROMs -- but reaching it via checkm8 no longer
works.

### R4: usbliter8 (Paradigm Shift, 2026-06-18)
**Source:** https://github.com/prdgmshift/usbliter8
**Takeaway:** First public SecureROM code-execution exploit for T8020 (A12)
and T8030 (A13), also S4/S5. Does not extend to A14+.

### R5: usbliter8 exploit source (T8020 shellcode)
**Source:** https://github.com/prdgmshift/usbliter8/blob/main/exploit.c
**Takeaway:** T8020 shellcode targets SRAM 0x19C000000 (SecureROM SRAM window);
raw iBoot loaded at 0x19C030000; SecureROM executes from 0x100000000.

### R6: tr4mpass' own iOS 26.3 note
**Source:** https://github.com/tr4m0ryp/tr4mpass/blob/main/26.3-vulnerability.md
**Takeaway:** Post-iOS-26.2 activation records are SEP-firmware-enforced with
MEID/ECID binding; activation-record forging fails regardless of BootROM
primitive.

### R7: deepwiki checkra1n-mod -- DFU iSerial derivation
**Source:** https://deepwiki.com/epeth0mus/checkra1n-mod/4.1-checkm8-exploit
**Takeaway:** DFU iSerial is generated from silicon-fused identity
(CPID, CPRV, CPFM, SCEP, BDID, ECID, IBFL, SRTG) and served at string index 4.
Confirms F3 and F6.

### R8: checkra1n BugTracker Known Issues -- A12+ not supported
**Source:** https://github.com/checkra1n/BugTracker/issues/1
**Takeaway:** Team member Siguza's Known Issues explicitly states A12 and later
CANNOT be supported because those SoCs are not vulnerable to checkm8.

### R9: checkra1n BugTracker #1914 -- team's "Nope." on A12
**Source:** https://github.com/checkra1n/BugTracker/issues/1914
**Takeaway:** One-word confirmation from the team; no roadmap, no partial
support, no research preview.

### R10: Paradigm Shift usbliter8 blog
**Source:** https://ps.tc/pages/blog-usbliter8.html
**Takeaway:** DWC2 DOEPDMA underflow mechanics. DART bypass mode is the enabler
on T8020/T8030 (A14+ configure DART correctly, closing the primitive). Full
memory-layout diagram.

### R11: habr.com -- Apple SecureROM USB stack analysis
**Source:** https://habr.com/en/companies/dsec/articles/472762/
**Takeaway:** SET_DESCRIPTOR is routed through the control endpoint but the
setup handler performs no real action. `PWND:[checkm8]` is not from
SET_DESCRIPTOR -- it is patched by post-exploit shellcode.

### R12: BeyondLogic USB reference
**Source:** https://www.beyondlogic.org/usbnutshell/usb6.shtml
**Takeaway:** SET_DESCRIPTOR is an OPTIONAL USB spec request most devices
implement as a no-op. Confirms F6.

### R13: Apple Platform Security -- boot process (SEP Boot Monitor + SCIP on A13+)
**Source:** https://support.apple.com/guide/security/boot-process-for-ipad-and-iphone-devices-secb3000f149/web
**Takeaway:** SEP Boot Monitor + System Coprocessor Integrity Protection
introduced on A13/T8030. Hardens sepOS hash and rules out post-exploit sepOS
patches on that generation and later.

### R14: The Hacker News -- usbliter8 disclosure
**Source:** https://thehackernews.com/2026/06/unpatchable-usbliter8-exploit-breaks-apple-a12-and-a13-securerom-boot-chain.html
**Takeaway:** Confirms the DWC2 primitive is distinct in shape from classic
checkm8 (heap UAF on IO buffer), and scopes precisely to A12/A13.

### R15: HoploN Infosec -- usbliter8 technical brief
**Source:** https://hoploninfosec.com/usbliter8-exploit-apple-a12-a13-securerom-vulnerability
**Takeaway:** Confirms Paradigm Shift's exploit is the first public SecureROM
RCE for T8020/T8030, published 2026-06-18.

### R16: usbliter8ra1n community chain
**Source:** https://github.com/Leeksov/usbliter8ra1n
**Takeaway:** Downstream tethered jailbreak on top of usbliter8 with iBoot
patch + SPTM/TXM bypass + kernel patchfinder + SSH ramdisk. Demonstrates a
usable path from usbliter8 to full device access on A12/A13.

### R17: mosen/macdocs -- iOS activation protocol
**Source:** https://github.com/mosen/macdocs/blob/master/source/DEP/ios-activation.rst
**Takeaway:** Session activation is two POSTs to albert.apple.com --
`/deviceservices/drmHandshake` (CollectionBlob/HandshakeRequestMessage ->
FDRBlob, SUInfo, HandshakeResponseMessage) then the activation record request.

### R18: theiphonewiki Activation Token
**Source:** https://www.theiphonewiki.com/wiki/Activation_token
**Takeaway:** ActivationInfoXML is FairPlay-signed; lockdown SHA1-hashes the
payload and hands it to fairplayd. Killed one adversarial claim about UDID
formula because the page distinguishes pre-2018 vs post-A12 UDIDs (see
Discarded Approaches).

### R19: Apple Platform Security -- SEP attestation
**Source:** https://support.apple.com/guide/security/attestation-process-security-sec97eb9e2f2/web
**Takeaway:** SEP attestation bakes ECID, ChipID, main-logic-board identifier,
sepOS firmware measurements, and OS Image4 manifest hashes into the signature.
Underpins F10.

### R20: Apple Platform Security -- Activation Lock
**Source:** https://support.apple.com/guide/security/activation-lock-security-sec0f8dfd030/web
**Takeaway:** Activation Lock's server dependency is explicit: first boot sends
a request to the activation server; Setup Assistant refuses to continue without
the activation certificate.

### R21: MobileGestalt keys list
**Source:** https://theapplewiki.com/wiki/List_of_MobileGestalt_keys
**Takeaway:** MobileGestalt is the on-device oracle for activation identifiers;
comprehensive key reference for anyone extending the activation-record path.

### R22: gestalthax -- MobileGestalt "hactivation"
**Source:** https://hanakim3945.github.io/posts/gestalthax/
**Takeaway:** Faking demotion flags
(EffectiveProductionStatusAp, EffectiveProductionStatusSEP,
EffectiveSecurityMode) bypasses some activation state locally. Interesting
adjacent primitive but requires prior code execution -- not a substitute for
Path B on A12+.

### R23: Blackbird SEPROM exploit
**Source:** https://theapplewiki.com/wiki/Blackbird_Exploit
**Takeaway:** Last public SEPROM exploit chain. Works A8/A9/A10/T2; fails on
A11 (no TZ0 R/W, memory integrity tree) and A12+ (further SEP hardening).

### R24: Asahi Linux -- SEP mailbox layout
**Source:** https://asahilinux.org/docs/hw/soc/sep/
**Takeaway:** AP-SEP hardware mailbox message layout. Reference for anyone
reasoning about SEP interaction from Apple Silicon side.

### R25: Stack Int Mov -- macOS boot chain and SEP boundary
**Source:** https://stack.int.mov/a-reverse-engineers-anatomy-of-the-macos-boot-chain-security-architecture/
**Takeaway:** iBoot's TZ0 handover marks the SEP boundary. Reference for the
architectural picture.

### R26..R31: axi0mX Twitter announcement, iDownloadBlog checkra1n guide, btctranscripts hardware-wallet analogue, securityaffairs usbliter8 coverage, and additional Apple Platform Security guide pages
See `scratchpad/a12/verified.json` for the full 31-URL corpus; each URL has at
least one surviving claim.

---

## Discarded Approaches

- **Path B via SET_DESCRIPTOR identity manipulation on A12+.** No such primitive
  exists on Apple SecureROM (F6, R11, R12). Delete the code and replace with
  graceful refusal.
- **Attempting checkm8 on A12+.** The specific heap-shaping leak was closed by
  the double-abort in the USB reset path (F5, R3, R8, R9). No amount of tuning
  brings it back on A12+.
- **Assuming `iSerial` is always index 3.** F3 shows it is 4 on A14+.
- **Hardcoding Homebrew cellar paths.** Version bumps break the build; keep
  everything pkg-config-driven (T1).
- **Bundling libirecovery/libplist as a submodule.** Rejected in favor of compat
  shims (T2, T3) to keep distro-package installs working.
- **Faking UDID via the SHA1(serial+ECID+wifiMac+bluetoothMac) formula on
  post-A12 devices.** The formula only applies pre-September-2018; killed by
  R18's own distinction between UDID formats.
- **Attempting to run without a device permission preflight (usbmuxd,
  usbipd).** Preflight (T8, T10) catches these before hardware time is wasted.
- **Integrating usbliter8 in this session.** Correctly scoped as a separate
  initiative -- large, requires its own authorization review, and gracefully
  refusing on A12+ with the documented pointer is honest today.

---

## Risks & Open Threads

- [x] **A12+ Path B feasibility** -- resolved via T12: delete SET_DESCRIPTOR code;
      document usbliter8 as the honest replacement primitive; refuse gracefully.
- [x] **Should we integrate usbliter8?** -- deferred by decision (T12 alt
      rejected). Explicit scope decision, not an unresolved thread.
- [ ] **libplist 2.4 landing** -- if upstream ships a fourth API break during
      the window before we cut a release, the T2 shim must be re-audited. Low
      probability, monitored.
- [ ] **usbmuxd on non-systemd Linux (Alpine, Void).** T8's preflight covers
      Alpine; Void and lesser-used inits are not covered -- print a generic
      "start usbmuxd" line rather than fake-detecting.
- [ ] **Post-iOS-26.3 devices (all SoCs)** -- Path A's activation forging
      pattern fails per F10 / R6. Reflect in matrix now (T11); actual
      countermeasure is out of scope.
- [ ] **Chip-DB canary strictness.** If the canary fires on user builds due to
      an upstream chip addition we did not merge, it will block the build.
      Consider gating behind `STRICT_CHIP_DB=1`.
- [ ] **The 3 killed A12+ claims** are recorded in `scratchpad/a12/verified.json`
      under `killed`; if any is later validated by fresh evidence, T12 should
      be revisited.
- [ ] **The 6 verify-agent safety-flag failures** (all on adversarial-refute
      framing) mean 3 of the ~43 claims lost some verifier votes. All still
      cleared the 2-of-3 bar via their other votes. Included in the surviving
      set; no findings depend on the lost votes alone.

---

## Build Plan

Six phases, dependency-ordered. Phase N cannot start until Phase N-1 is
merged. Within a phase, work items are independent and can be parallelized
across cluster subagents in `/readyforlaunch`.

### Phase 1 -- Buildability
1. `Makefile` + `start.sh` pkg-config list sync (T1).
2. Normalize `<libusb.h>` includes across 6 files (T1).
3. `include/compat/plist_compat.h` + call-site migration (T2).
4. `include/compat/libirecovery_compat.h` + call-site migration (T3).
5. Complete pacman / zypper / apk installer branches in `start-helpers.sh` (T1).
6. Makefile version-detect canaries (T2, T3).

**Verifies:** the build succeeds on stock Ubuntu 22.04, Debian bookworm, Fedora,
Arch, macOS Homebrew, WSL Ubuntu. Closes: #70, #74, #67, #54, #51, #44, #21,
#55, #31, #24, #19, plus the closed dupes #16, #4, #1.

### Phase 2 -- Correctness
1. Retire `DFU_SERIAL_INDEX=3` triplicate; read `iSerialNumber` at runtime with
   fallback probe (T4).
2. Consult `--cpid`/`--ecid` overrides at the parse-failure branch (T4).
3. Rebuild `chip_db_table.h` from gaster canonical + build-time canary (T5).
4. Reorder checkm8 stage-reset to gaster canonical with second USB reset (T7).
5. Rewrite DFU->recovery transition with proper USB re-enum (T6).

**Verifies:** on a real iPhone 13 (A14), CPID/ECID parse succeeds; on a real
iPhone 7 (A10), checkm8 stage 1/4 no longer I/O-errors; DFU->recovery mode
switch completes. Closes: #45, #65, #59, #27, #17, #52, #30, #22, #25, #10, #8,
#6, #7, #53, #46, #47, #61, #38, #40, #18, #42.

### Phase 3 -- Diagnostics UX
1. `src/util/errors.c` + `ERR_*` enum (T10).
2. Replace ~250 bare `return -1` sites with `err_ctx(...)` (T10).
3. `src/cli/doctor.c` + `tr4mpass doctor` subcommand (T10).
4. `start.sh` preflight phase that runs before deps (T8, T10).
5. WSL/env detection + `/mnt/c` refusal + `usbipd attach` hint (T8).
6. Binary-shadowing check (T8, closes the "Is a directory" bug from #2 / #20).

**Verifies:** on any failure the user gets `[category]: <what>. Next: <step>.`
`tr4mpass doctor` runs standalone. Closes: #37, #34, #35, #28, #29, #2, #20,
#48, #25, #30, #52, #22 (bailout messaging), plus the "no next step" pattern
across the tracker.

### Phase 4 -- Honesty
1. `README.md` per-chip support matrix (T11).
2. FAQ section (untethered, cellular-after-bypass, data preservation, MEID,
   Chinese variants, passcode-vs-iCloud distinction) (T11).
3. First-run interactive walk in `start.sh` (T9).
4. `--cpid`/`--ecid` cheat sheet at repo root (T9).
5. `start.sh dfu-help` per-device-family button-sequence walker (T9).

**Verifies:** a hardware researcher who has never used the tool can drive it end
to end. Closes: #76, #62, #36, #43, #73, #13, #14, #64, #58, #50, #15, #60.

### Phase 5 -- Path B truth
1. Delete `src/bypass/path_b_identity.c` SET_DESCRIPTOR-based identity path
   (T12, F6).
2. Register a `path_b_a12_refuse` module that reports the honest message with
   the usbliter8 pointer (T12).
3. Register a `path_b_a14plus_refuse` module for T8101 and later (T12).
4. Update README matrix (T11) to reflect the refusal.
5. Update `mobileactivationd` cluster's discussion in README to reflect SSV
   sealing on iOS 15+ and iOS 26.3+ SEP-firmware activation enforcement (T13,
   F9, F10).

**Verifies:** an A12+ device gets a clear, cited "not supported because X"
message. Closes: #57, #72, #75, #33, #3, #58, #23, #77.

### Phase 6 -- Close-out sweep
1. For each open issue, post the maintainer-close message produced by its
   cluster subagent (~70 messages total, see `scratchpad/clusters/*.json` per
   cluster `issue_close_messages` field).
2. Rerun the `/deep-research` A12+ pass in 90 days if usbliter8 integration
   becomes in-scope.

**Verifies:** the tracker collapses from 56 open issues to ≤5 (only truly
out-of-scope requests remain).
