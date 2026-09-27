<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Toolkit API charter (draft for review)

_Status: **S6 draft**, 2026-09-27. Output of the platform API study (epic P4-05, graphics-desktop.md §6.1). It becomes normative when reviewed. P4-12 (toolkit v1) builds against it, and the S7 prototypes test it._

**Evidence** is in the study repository `../NeoDarwin-api-study`. References in brackets point there:

| Tag | Source |
|---|---|
| [Q1] | `reports/concept-matrix.md`, `reports/rules-consistency.md` §b: where the platforms converge |
| [Q2] | `reports/q2-shapes.md`: shapes the cross-platform layers converged on; R1–R14 are its recommendations |
| [Q5] | `reports/q5-compat.md`: compatibility layers by reach per effort |
| [Q6] | `reports/heritage.md`: AmigaOS, TOS/GEM, consoles |
| [F-nnn] | `docs/architecture/friction-register.md` in this repository |

## 0. What the charter decides

graphics-desktop.md §6.1 asks for four things. This document answers each:

| Asked for | Where |
|---|---|
| The surface area | §3 |
| The ownership and threading model | §4, §5 |
| The language bindings that matter | §7 |
| The compatibility layers worth shipping | §8 |

**Already fixed below the toolkit, and not reopened:**
- the window protocol (`window-protocol.md`, `desktop.h`);
- the theme classes a widget draws through (`theme-engine.md`, `ui-configuration.md`);
- the agent protocol (`agent-protocol.md`).

Where the toolkit needs something from them, §9 lists it as a proposal against that document.

**Not in scope:** the chrome (P4-08), the file viewer and prefs panel (P4-09), and the GPU driver stack (P7). The charter only says what the toolkit needs from them.

## 1. Who it is for, and the bar it must clear

**Audience:** hackers, indie game developers, and people who build tools, demos, editors and shells (graphics-desktop.md §6.1). They mostly program against SDL, GLFW, sokol, raylib, winit/wgpu, ImGui/egui or their own OS layer today [Q2 §1]. The toolkit must be at least as direct as those, while solving the friction they cannot solve because the platform owns it [F-201–F-219].

**The bar:**
- **Calls:** a program that opens a window, draws a frame, reads input and plays a sound needs **no more calls than the AmigaOS or Switch equivalent: about 12–13** [Q6 §3]. S7 measures this against SDL3 and against native Win32, AppKit and Wayland.
- **Size:** the ~100-line, allocator-free `sysinfo` UI program from the plan-neo pilot stays about that size.

## 2. Principles

Each principle names its evidence. The status column says whether the principle carries over from graphics-desktop.md §6.2, is modified, or is new.

| # | Principle | Evidence | Status vs §6.2 |
|---|---|---|---|
| P1 | **The application owns the loop, and the loop waits once.** One wait covers window events, the frame event, timers, audio-contract changes, I/O completions, a cross-thread wake and the app's own descriptors. Wait and poll modes both exist, with an absolute deadline. | [Q2 C2, R2]; [F-201], [F-203]; [Q6 §4]: Exec `Wait(sigmask)`, GEM `evnt_multi`, Horizon's 64-handle wait | modifies "reactive and frame-synchronised loops" |
| P2 | **No main thread and no modal calls.** Any thread may create and drive windows; serialisation is per window. No toolkit call runs a nested loop: dialogs, menus, drag and file choosers are asynchronous objects that deliver events. | [F-201], [F-202]; [Q2 R4, R13]; `EVT.modal` is used by 21 corpus projects [Q1]; GEM `form_do`/`BEG_UPDATE` as the anti-pattern [Q6 §5] | new |
| P3 | **The frame clock belongs to wsys.** Apps request a frame (coalesced). The frame event carries the previous frame's *actual* presentation time and the next target. Queue depth is one number. Hidden windows get a throttled clock, never a withheld one. | [F-101], [F-102], [F-209]; [Q2 R5]; vblank as a waitable event on every console [Q6] | new |
| P4 | **Logical units; resize and scale are one ordered event.** Layout is in logical points. Resize and scale arrive together with a configuration sequence, and presents carry that sequence, so a resize is never an error. | [F-205], [F-208]; [Q2 C7, R6] | modifies "two-pass measure/arrange" (units) |
| P5 | **Handles, not pointers, at every boundary.** Windows, UI nodes, surfaces, audio streams and GPU buffers are index + generation at the C ABI, in `/n/app` and in agent messages. An invalid handle is a diagnosed no-op. | [Q2 C9, R8]: sokol, Godot RID, bevy, gpui converge; language-policy §4.3 | carries over |
| P6 | **Two memories.** A per-frame arena (T2, allocation-free Swift) for layout, draw and element scratch, reporting its high-water mark. A retained node table with slot reuse for everything that outlives a frame. | [Q2 §6.1, R9]: no layer uses an arena-only retained tree | modifies "arena-only allocation" |
| P7 | **Widgets hold no colour, font or self-chosen size.** Style comes from the theme by class; a theme reload is a repaint. | [Q2 C11]; graphics-desktop.md §4.3 | carries over |
| P8 | **Retained widgets with an immediate layer.** Widgets are retained and built in one expression. An immediate builder rebuilds a subtree per frame into the arena, keyed by stable ids, and diffs it into the node table. | [Q2 R11]: ImGui, egui, gpui `render()`; libintuition's `Icanvas` | modifies "declarative tree"; adds "diffs as the update protocol" |
| P9 | **No present without a change.** One present per frame, with a damage rectangle. Partial redraw inside a frame is a CPU-path optimisation, not a contract. | [Q2 §6.1]; graphics-desktop.md §2 frame rules | carries over |
| P10 | **Timers are absolute deadlines with leeway, inside the one wait.** There is no resolution setting anywhere. | [F-203]; Amiga `timer.device` [Q6 §4] | new |
| P11 | **Budgets are stated, not guessed.** The system reports the GPU memory budget, audio period and latency, and frame-queue depth exactly. | [F-109], [F-216]; console budgets [Q6 §4] | new |
| P12 | **Errors are values.** Swift uses typed throws; the C ABI uses codes plus thread-local detail. Asynchronous failures are events. Nothing panics on a recoverable condition. | [Q2 R12]; language-policy §4.6 | new |
| P13 | **The first version is permanent, so evolve additively.** Every record is versioned and size-asserted; capability queries exist from v1; new features add calls and event types and never change existing meaning. | FreeMiNT/XaAES retrofit lessons [Q6 §6]; [Q2 §6.2] | extends "versioned data ABIs" |
| P14 | **One truth, many views.** The node table is also what `/n/app` exports and what accessibility reads. | graphics-desktop.md §6.2; [Q2 §6.2] (a pointer graph cannot do this) | carries over |

## 3. Surface area

Modules, the concepts each covers from the study's taxonomy [Q1], and what is deliberately excluded. The concepts listed are the common core: offered by three or more OS families and used by ten or more corpus projects, unless marked.

| Module | Covers (taxonomy concepts) | Shape | Excluded, and why |
|---|---|---|---|
| **Loop** | EVT.wait, EVT.pump, EVT.wake, EVT.user, EVT.fd, EVT.lifecycle, EVT.system, TIM.timer | `wait(until:leeway:)`, `poll()`, `post(to:)`, `wake()`, `addSource(fd:)`, `timer(at:leeway:)`. A thin callback driver (`init / iterate / event / quit`, SDL3-shaped) sits on top [Q2 R3]. | EVT.runloop and EVT.mainthread: no platform run loop to hand over to, no main thread (P2). EVT.modal: none (P2). |
| **Events** | the event record for everything below | One fixed-size record: type, window handle, 64-bit monotonic ns timestamp, sequence, typed payload, and a payload span valid until the next wait (text, IME preedit, drop paths). It maps 1:1 from `Deskevent` plus the extended records of §9. | No object per event; no callback per event kind [Q2 R1]. |
| **Windows and outputs** | WIN.window.create/destroy/show/geometry/constraints/fullscreen/maximize/title/parent/focus/state/attention, WIN.display.enumerate/scale/mode/color, WIN.cursor, WIN.clipboard (snarf), WIN.dnd, WIN.workspace (app-facing) | Handles plus asynchronous requests acknowledged by sequenced events (F-206). Decorations and menus are the server's (F-207). The app may request `decor none` plus hit-test regions. | Client-side decorations as the default [F-207]; app-driven stacking layers beyond `above` (Wayland lacks them [Q1]). |
| **Input** | INP.key.event, INP.key.map, INP.text.input, INP.text.ime, INP.pointer.event/wheel/relative/lock/capture, INP.pen, INP.touch, INP.gesture, INP.gamepad.* | Key events carry scancode, keysym, text, modifiers and a repeat flag from a server-side keymap (F-210). IME uses a text-model adapter in the toolkit's text field (F-211). Pen fields sit on the pointer record (F-212). Relative motion has its own stream (F-213). Gamepads use one mapped API from the kernel HID class (F-214). | INP.raw for apps: HID access stays in drivers and dexts. INP.inject is agent-protocol-gated, not a toolkit call. |
| **Frames and surfaces** | PRS.present, PRS.damage, PRS.timing.pacing, PRS.timing.feedback, PRS.latency, PRS.vsync, PRS.vrr, PRS.hdr, WIN.surface.software, PRS.surface.bind | `requestFrame()`; `present(damage:, seq:)`. A **CPU surface** is the shared-memory `surface`, copy or flip. A **GPU surface** is the same object handed to Vulkan WSI or `webgpu.h` through a NeoDarwin window-handle bag [Q2 R7]. | App-run display links (P3). Swapchain micromanagement: one latency number instead. |
| **Drawing** | WIN.draw2d, WIN.font; GPU.* through the GPU module | 2D through Vesper and libstyle, where the theme's materials are available to widgets. | A toolkit-specific 3D API (§6). |
| **GPU** | PRS.surface.bind, GPU.interop.external, GPU.mem.residency (budget) | **Vulkan is the interop layer** (surface for a window, buffer import/export, budget). **The toolkit's own GPU drawing API is `webgpu.h`** (Dawn, SPIR-V accepted). See §6. | Metal (no corpus need [Q5]); an SDL_gpu-shaped API (one corpus user [Q5]). |
| **UI** | WIN.widget.tree, WIN.menu (as data to the server), WIN.dialog (async), WIN.accessibility | Class-named, style-free widgets; flex measure/arrange in logical units; one-expression construction; an immediate builder; node table with handles (P5–P8, P14). The text field ships the IME adapter. | System widgets drawn by the server; the server draws only chrome. |
| **Audio** | AUD.stream.open/callback/push/capture, AUD.latency, AUD.clock, AUD.device.enumerate, AUD.session, AUD.voice, AUD.format | **Tier 1, a stream:** pull callback on a toolkit-created real-time thread with admitted period and computation, or push; format and rate conversion; a contract record (period, rate, latency, device-time ↔ `mach_absolute_time`, device identity) whose changes are events in the one wait. **Tier 2, voices** on the system mixer [Q2 R10; F-215, F-216]. | AUD.exclusive (a flag on open, if at all); AUD.plugin (third-party standards); AUD.midi (deferred: [Q1] shows 4 OS families, but little corpus use). |
| **Time and threads** | TIM.monotonic, TIM.sleep (deadline), THR.create, THR.name, THR.priority as **intent** (interactive / throughput / background / audio), THR.pool (libdispatch), THR.perfmode | Thread intent maps onto XNU QoS and core placement (F-204). No affinity API for apps. | TIM.resolution: none exists (P10). THR.hetero as a separate call: intent covers it. |
| **Files and I/O** | IO.file.async, IO.path.known, IO.watch | Asynchronous reads complete into the one wait, through libdispatch (F-217). Known folders are resolved from the namespace. | IO.gpu.direct (no corpus demand [F graphics triage]). |
| **Agent and export** | (NeoDarwin-specific) | The node table is exported as `/n/app/…` and served to `/n/agent/APPID` (agent-protocol.md). | — |

## 4. Threading model

- **Loops.**
  - A loop is an object owned by one thread at a time.
  - Windows attach to a loop, and their events arrive in that loop's wait.
  - An app may run several loops (one per window, as Haiku does, or one for all), because nothing is thread-affine (P2).
- **Cross-thread interaction:**
  - `post(to: loop, message)`: a copyable message, delivered in the wait.
  - `wake(loop)`: coalesced, with no payload.
- **Window ctl.**
  - Any thread may issue window ctl requests; they are serialised per window by the server.
  - The toolkit does not lock around them.
- **Audio.**
  - The real-time callback runs on a toolkit-created thread with the audio intent (P10, F-215).
  - The callback path takes no locks.
  - State reaches it through single-producer, single-consumer rings.
- **The draw path is T2** (allocation-free and lock-free by construction; language-policy §2):
  - event decode, layout, draw into the frame arena, present.

## 5. Ownership and memory

| Kind | Owner | Lifetime | Across the C ABI |
|---|---|---|---|
| Windows, nodes, surfaces, streams, GPU buffers | toolkit tables | until destroyed; handles are generation-checked | `u64` handle |
| Per-frame scratch (layout, element trees, draw lists, event payload spans) | the loop's frame arena | until the next `wait` or frame | `Span` / pointer + length, valid until the next wait |
| Strings and text given to widgets | copied into the node table unless passed as a `borrowing` view for one call | explicit | pointer + length, copied |
| Pixels | shared-memory surface | the surface's | a Mach port behind the surface handle |
| Audio buffers | the stream's ring | the stream's | pointer + frame count inside the callback only |

- **Allocator hook:** a user allocator hook exists at the C ABI only [Q2 R9].
- **Swift ownership:** Swift code uses `~Copyable` owners and `Span` views (language-policy §4).

## 6. The GPU question

[Q5] ranks the options by corpus reach per unit of effort. The decision is:

1. **Every GPU API compiles to Vulkan.**
   - Mesa supplies Vulkan (the NeoDarwin profile, SPIR-V only), GL/GLES through Zink, WebGPU through Dawn or wgpu, and D3D later through DXVK/vkd3d.
   - Engines keep their own backends (F-104). NeoDarwin guarantees one Vulkan profile, gates drivers on conformance, and keeps one shared quirk database (F-110).
2. **The toolkit exposes Vulkan only as an interop seam:**
   - a surface for a window (the handle bag);
   - buffer import and export: the GPU buffer object of F-107/F-108, a Mach memory entry plus format plus fence;
   - the budget (F-109).
3. **The toolkit's own GPU drawing API is `webgpu.h`**, backed by Dawn, rather than a new API:
   - It answers F-106 (barriers) and F-111 (binding and IR translation) with an API the corpus already uses: bevy, zed and egui through wgpu [Q5].
   - Swift bindings are generated from the C header.
   - This is **provisional** until S7's game-loop and compute-to-display prototypes run on it.
4. **Not offered:**
   - Metal: no project in the corpus needs it; all 11 Metal users also have Vulkan or GL [Q5].
   - An SDL_gpu-shaped toolkit API: one corpus user [Q5]; SDL_gpu itself still runs on NeoDarwin's Vulkan.

## 7. Language bindings

| Binding | How | Status |
|---|---|---|
| **Swift** | Native. The toolkit is T1, with T2 on the event, layout, draw and audio paths (language-policy §2). | normative |
| **C ABI** | `@_cdecl` entry points over the same tables. Records are plain fixed-width structs, handles are `u64`, errors are codes plus thread-local detail. | normative; the stable ABI |
| **Zig, Rust, others** | Generated from the C header **outside the tree**. The language policy forbids Rust and Zig in the tree; the generators and published bindings live in separate repositories. | the charter ships the header and a machine-readable description of it |

## 8. Compatibility layers

This is the first-release set from [Q5], which runs 28 of the 37 applicable corpus projects (32 with optional Xwayland):

| Layer | What it gives | Notes |
|---|---|---|
| POSIX / libSystem, plus Linux shims | everything | NeoDarwin must **not** define `__APPLE__`: the corpus has 1,045 `__APPLE__` guards against 18 `__MACH__` [Q5] |
| **Wayland core+** | toolkits, terminals, browsers | The planned five interfaces are not enough. Clients use 16 extensions, each in 6–12 projects; `linux-dmabuf` needs the F-107 buffer object. Wayland semantics are translated *into* wsys, never the reverse. |
| **Mesa Vulkan** (profile) | engines, compute | P7 |
| **GL/GLES/EGL through Zink** | 7 projects that are GL-only, and 11 that fall back to GL | zero new code through the Wayland door; a native wsys EGL platform later |
| **WebGPU packages** (wgpu, Dawn) | bevy, zed, egui, and more | also the toolkit's own drawing API (§6) |
| **SDL3 built for NeoDarwin** | SDL games; the callback driver maps 1:1 | audio and gamepad first; a native video backend on wsys later |
| **PulseAudio-protocol door** | Linux audio clients | in front of the audio service |
| Later: Wine + DXVK/vkd3d | Windows games | needs Vulkan 1.3 with DXVK's feature set, F-218, and x86 emulation on arm64 |

**Mesa and Rust:** Mesa now contains Rust (NVK, the Panfrost compilers, Rusticl). This does not conflict with the language policy, which governs first-party code only; vendored upstream is built in its own language (language-policy.md §1, scope). The practical consequence is a pinned rustc in the build, as a dependency of the vendored Mesa component, when those drivers are built from source.

## 9. What the toolkit needs from other layers

These are proposals against other documents and epics, as listed in `friction-register.md`:

| Needed | From | For |
|---|---|---|
| **Frame event with presentation feedback:** actual present time, target time, sequence, `latency n`, and a throttled state for hidden windows | window-protocol.md (revision 2), wsys P4-03 | P3; F-101, F-102, F-209 |
| **Resize/scale events with logical and pixel sizes, a rational scale and a configuration sequence; presents tagged with the sequence** | window-protocol.md | P4; F-205, F-208 |
| **Sequenced ctl acknowledgements; anchored popups; `decor none` with hit-test regions; `DS_INTERACTIVE`** | window-protocol.md | F-202, F-206, F-207 |
| **Channels for key and keymap, IME, pen, relative pointer and gamepad** | window-protocol.md, inputd P4-02 | F-210–F-214 |
| **An event mask wider than 32 types, or masking by class** (`desktop.h:398`: 19 types used, 11 more proposed) | desktop.h, before v1 freezes | P13 |
| **An extended event record with a 64-bit ns timestamp and a payload span** | desktop.h | §3 Events |
| **Readiness of 9P fids through kqueue; EVFILT_USER wake; absolute-deadline timers with leeway** | kernel scheduling contract (new epic) | P1, P10; F-201, F-203 |
| **Thread intent → QoS and placement; real-time admission for audio** | kernel scheduling contract | F-204, F-215 |
| **A GPU buffer object (Mach memory entry + format + fence), host-pointer import, a per-task GPU budget with a pressure signal** | P7 with the VM | F-107–F-109 |
| **An audio service: system mixer on a fixed period, a stream contract, voices** | audio service (new epic) | F-215, F-216 |

## 10. How the charter is validated (S7)

The four prototypes are each written three ways: against the candidate API, against SDL3, and against native Win32, AppKit and Wayland. The candidate API runs first on macOS through a shim over AppKit and Metal, then on wsys.

| Prototype | Exit criteria |
|---|---|
| **Game loop:** fixed step, interpolation, raw input, gamepad, audio | ≤ 13 calls from start to window + frame + input + sound (§1). Frame pacing error p99 ≤ 1 ms on NeoDarwin. No timer-resolution call anywhere. |
| **Text editor:** IME, large document, 120 Hz scroll | Zero dropped frames at 120 Hz during scroll on the reference machine. IME composition works in the toolkit text field with no app code beyond the adapter. Live resize without artefacts (the configuration sequence). |
| **Synth UI:** real-time audio + UI reading audio state lock-free | No locks on the callback path (checked by the T2 diagnostics). Period and latency are reported exactly. No underruns at a 128-frame period under UI load. |
| **Compute to display:** GPU compute writes a surface, presented without a copy | Zero copies between compute and composition (buffer object). The budget is reported by the system. |
| All four | Nothing presented when idle (`stats`: zero composites). Every event source in one wait. No modal call exists to be tested. |

## 11. Open questions for review

1. **Drawing API:** is `webgpu.h` right, or is a smaller 2D-plus-mesh API over Vulkan better for the audience? The evidence favours `webgpu.h` for reach [Q5]; the heritage evidence favours smaller [Q6]. S7 decides.
2. **Immediate layer:** does the immediate builder (P8) ship in v1, or after the retained core?
3. **Wayland extensions:** which of the 16 are in core+ for the first release [Q5 lists them with project counts]?
4. **One loop or loop per window by default:** Haiku's per-window threads made single-threaded engine ports marshal every event [Q6; Haiku notes]. The proposal is one loop per app by default, with more loops allowed.
