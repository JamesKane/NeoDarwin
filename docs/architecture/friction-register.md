<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Friction register

_Output of the platform API study (epic P4-05; graphics-desktop.md §6.1). Evidence, SQL and per-entry write-ups are in the study repository `../NeoDarwin-api-study` (`friction/F-NNN-*.md`, `reports/friction-triage-{graphics,system}.md`), pinned at the commit named in its `reports/STATUS.md`._

Each entry is a place where applications fight the platform to get performance or correct behaviour. An entry is admitted only when there is evidence from **at least three independent projects**, taken from code and comments at cited sites, git history and backend churn in a 44-project corpus. The corpus covers engines, translation layers, GPU compute and interactive tools, and each entry's sites were read by hand.

Each entry names the NeoDarwin layer that should own the fix. Where a fix needs something from the window protocol (§2 of graphics-desktop.md), it is an extension proposal. The protocol is not reopened.

The heritage study (`reports/heritage.md` in the study repository: AmigaOS, Atari TOS/GEM, consoles with open SDKs) confirms several of these entries by showing a simpler system that did not have the problem. That is noted in the last column.

## Entries

| Id | Friction | Projects | Owning layer | Heritage precedent |
|---|---|---:|---|---|
| F-101 | Present timing is unknowable or approximate | 8 | wsys fast path (+P7 for Vulkan) | vblank is a waitable event on every console; the flip is latched at vblank |
| F-102 | Frame pacing source and queue depth differ on every platform | 8 | wsys fast path | `WaitTOF`, `Vsync()`, vblank callbacks |
| F-103 | Pipeline compilation at first use; caches that don't survive drivers | 8 | P7 (+build/packaging) | microcode shipped precompiled; state changes were register writes |
| F-104 | Engines keep 3–5 graphics backends in step | 6 | P7 | — |
| F-105 | One compute backend per vendor stack | 4 | P7 | — |
| F-106 | Every engine rebuilds barrier and layout tracking | 8 | P7 | manual cache flush before DMA on every console (the ancestor) |
| F-107 | Sharing a GPU buffer across APIs, processes and the compositor | 7 | P7 (+wsys surface kind) | — |
| F-108 | Ordinary process memory as GPU memory without a copy | 7 | kernel scheduler/VM | unified memory on N64, Switch, 3DS |
| F-109 | GPU memory budget and residency are guessed, not negotiated | 7 | kernel scheduler/VM | exact, known budgets (1 MB PS1 VRAM, 4 MB GS, 2048 polys/frame on DS) |
| F-110 | Per-vendor, per-version driver quirk tables everywhere | 9 | P7 | — |
| F-111 | Shader IR and binding models translated per backend | 8 | P7 (+build/packaging) | — |
| F-201 | The platform owns the UI thread and its wait | 11 | toolkit (+kqueue readiness on 9P fids) | Exec `Wait(sigmask)`; GEM `evnt_multi`; Horizon waits on 64 handles |
| F-202 | System modal loops freeze the app during move, resize and menus | 11 | wsys fast path (+toolkit rule) | anti-pattern: GEM `form_do` and `BEG_UPDATE`, kept by XaAES |
| F-203 | No precise deadline sleep; apps raise global timer resolution and spin | 12 | kernel scheduler/VM (+toolkit) | Amiga `timer.device`: absolute deadline delivered into the one wait |
| F-204 | Threads cannot state intent (heterogeneous cores) | 9 | kernel scheduler/VM | Switch reserved system core and fixed priorities |
| F-205 | Per-output scale and display topology | 8 | wsys fast path | — |
| F-206 | Negotiated window geometry, state and popup placement | 6 | wsys fast path | — |
| F-207 | Custom title bars and decoration ownership | 11 | wsys fast path | — |
| F-208 | Keeping content in step with the window during live resize | 8 | wsys fast path | — |
| F-209 | Window visibility is implicit; hidden windows stall or waste frames | 6 | wsys fast path | — |
| F-210 | Keyboard layout, modifier and repeat model | 10 | wsys fast path (+toolkit shortcut matching) | — |
| F-211 | IME composition routing | 8 | wsys fast path (+toolkit text model) | — |
| F-212 | Pen input has no single self-describing stream | 6 | wsys fast path (+kernel HID) | — |
| F-213 | Relative motion and pointer lock emulated by warping | 5 | wsys fast path | — |
| F-214 | Gamepads bypass the platform; apps ship HID drivers | 5 | kernel HID class (+wsys delivery, toolkit mapping) | consoles: one controller API per system |
| F-215 | Real-time audio needs three unrelated mechanisms | 5 | kernel scheduler/VM (+toolkit audio stream) | dedicated sound processors; a system DSP mixer on a fixed period (3DS, Switch) |
| F-216 | Audio period, latency, clock and device identity have no single contract | 6 | toolkit audio stream (+audio service) | — |
| F-217 | Async asset I/O exists on one platform per project | 4 | kernel scheduler/VM (via libdispatch and toolkit) | Exec IORequest: one async request/reply for every device (like 9P) |
| F-218 | Reserving address space, aliased views and JIT W^X differ per OS | 3 | kernel scheduler/VM | — |
| F-219 | Every project builds its own loader for optional platform libraries | 9 | build/packaging | — |

## By owning layer

| Layer | Entries | Existing epic | Gap |
|---|---|---|---|
| **wsys fast path**, as window-protocol extension proposals | F-101, F-102, F-202, F-205–F-213 (12) | P4-17 (protocol revision 2), then P4-03 wsys, P4-02 inputd | — |
| **toolkit** | F-201, F-216, plus the toolkit halves of F-203, F-210, F-211, F-215 | P4-12 toolkit v1 (via the charter) | — |
| **P7 GPU/compute** | F-103–F-107, F-110, F-111 (7) | P7-01 GPU driver + Mesa; P7-07 (Vulkan profile and conformance gate); P7-06 (buffer object) | — |
| **kernel scheduler/VM** | F-108, F-109, F-203, F-204, F-215, F-217, F-218 (7) | P4-16 (scheduling contract), P7-06 (GPU budget and sharing) | F-217 (async I/O queue) and F-218 (address-space views) are not yet scoped into an epic. |
| **kernel HID** | F-214 | P3-07 USB + HID dexts, P4-02 inputd | A controller class and database is not scoped. |
| **audio service** | F-216 (+F-215) | P4-18 | — |
| **build/packaging** | F-219, and the cache halves of F-103 and F-111 | P2-01 ndpkg | Optional-library and weak-linking policy. |

## Backlog additions

Accepted 2026-09-28 and added to `roadmap/backlog.yaml`:
- **P4-16:** item 1
- **P4-17:** item 2 (P4-03 now depends on it)
- **P4-18:** item 3 (P4-12 now depends on P4-16 and P4-18)
- **P7-06:** item 4
- **P7-07:** item 5

1. **Scheduling contract** (kernel, before P4-12 freezes the toolkit loop):
   - one absolute-deadline wait with leeway on every waitable source;
   - thread intent (interactive, throughput, background, real-time audio) mapped onto XNU QoS and core placement;
   - real-time admission for audio, modelled on Plan 9 EDF `admit`;
   - kqueue readiness on 9P fids.
   - Exit: an S7 prototype measures p99 wake error on NeoDarwin.
   - Covers F-201 (kernel half), F-203, F-204, F-215 and F-217.
2. **Window-protocol revision 2** (before P4-03 freezes): the wsys extension proposals above, each with a `deskconform` check. Covers F-101, F-102, F-202 and F-205–F-213.
3. **Audio service and toolkit audio stream:**
   - a system mixer on a fixed period, with streams deadline-scheduled automatically;
   - one contract for period, latency, clock and device identity;
   - AUD.voice-style submission.
   - Covers F-215 and F-216.
4. **GPU memory and sharing** (P7, with the VM):
   - one buffer-object type (a Mach memory entry with a format and a fence);
   - host-pointer import;
   - a per-task GPU budget with a pressure signal.
   - Covers F-107, F-108 and F-109.
5. **Vulkan profile and conformance gate** (P7):
   - one guaranteed profile;
   - a shared quirk database;
   - pipeline caches built at package time.
   - Covers F-103, F-104, F-106, F-110 and F-111.

## Maintenance

New entries are admitted in the study repository under the same rule, at least three independent projects with sites read, and are then copied here as one row. When an owning epic lands a fix, the S7 prototype check in the entry records the result, and this table gains a "resolved in" column.
