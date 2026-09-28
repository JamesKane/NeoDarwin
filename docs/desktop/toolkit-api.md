<!-- SPDX-License-Identifier: BSD-2-Clause -->
# The NeoDarwin toolkit API

| | |
|---|---|
| **Status** | draft |
| **Version** | 1 |
| **Epic** | P4-12 (toolkit v1), formalising `toolkit-charter.md` (P4-05) |
| **Evidence** | study repository `../NeoDarwin-api-study` at commit `77e9b08`: `prototypes/ndtk/API.md` (with its changelog), `prototypes/ndtk/SHIM-NOTES.md`, `prototypes/ndtk/Sources/NDTK`, `prototypes/ndtk/Sources/NDTKUI`, `prototypes/ndtk/include/ndtk.h`, `prototypes/minimal/ndtk/CALLS.md`; `reports/s7-prototypes.md` §1, §3, §4; `reports/q2-shapes.md` §6, §7 (R1–R14); `friction/F-101` … `F-219` |
| **Supersedes** | the S7 candidate API (`prototypes/ndtk/API.md`, 2026-09-27), which was never normative. §11 lists what changed |
| **Header** | [`ndtk.h`](ndtk.h), `NDTK_API_VERSION 1`, C23 and C++20; layouts checked by [`ndtk_layout.c`](ndtk_layout.c) and [`ndtk_cxx.cpp`](ndtk_cxx.cpp) |
| **Swift interface** | [`NDToolkit.swift`](NDToolkit.swift), type-checked with [`NDToolkitMinimal.swift`](NDToolkitMinimal.swift) by [`BUILD.bazel`](BUILD.bazel) (`bazel test //docs/desktop/...`) |

This document fixes the programming interface of the NeoDarwin toolkit: the one wait and its callback driver, the event record, windows and outputs, input, frames and surfaces, 2D drawing, the GPU seam and its one-call helper over `webgpu.h`, the retained UI (node table, flex layout, theme-owned style, shaped text, the text view's IME adapter), audio streams and voices, time and threads, asynchronous files, and the export of the node table to agents and accessibility. It fixes the C ABI (`ndtk.h`) and the Swift surface (`NDToolkit.swift`), and for every concept it names the lower-layer requirements it stands on: the window protocol (WP, `window-protocol.md` version 2), the scheduling contract (SC, `docs/kernel/scheduling-contract.md`) and the audio service (AU, `docs/audio/audio-service.md`). Those documents own their records and semantics; this one cites them and never restates them (spec-conventions.md §7). It deliberately leaves open: the immediate builder (charter P8, deferred), Wayland extensions (P4-04), the GPU buffer object and budget (P7), the rasteriser's internals (Vesper and libstyle, P4-06, P4-14), and every compatibility layer (charter §8).

## Contents

0. Status (above)
1. [Scope and non-goals](#1-scope-and-non-goals)
2. [Terms](#2-terms)
3. [Model](#3-model)
4. [Interfaces](#4-interfaces)
   - 4.0 [Rules for every module](#40-rules-for-every-module): handles, errors, the two memories, threading, T2 marking, the Swift interface
   - 4.1 [Loop](#41-loop) · 4.2 [Callback driver](#42-callback-driver) · 4.3 [Events](#43-events) · 4.4 [Windows and outputs](#44-windows-and-outputs) · 4.5 [Input](#45-input) · 4.6 [Frames and surfaces](#46-frames-and-surfaces) · 4.7 [Drawing](#47-drawing) · 4.8 [GPU](#48-gpu) · 4.9 [UI](#49-ui) · 4.10 [Audio](#410-audio) · 4.11 [Time and threads](#411-time-and-threads) · 4.12 [Files and I/O](#412-files-and-io) · 4.13 [Agent export and accessibility](#413-agent-export-and-accessibility)
5. [Versioning and capabilities](#5-versioning-and-capabilities)
6. [Security and capabilities](#6-security-and-capabilities)
7. [Performance contract](#7-performance-contract)
8. [Conformance](#8-conformance)
9. [Rationale and evidence](#9-rationale-and-evidence)
10. [Open issues](#10-open-issues)
11. [Changelog](#11-changelog)

**Reading the requirements.** MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as in RFC 2119 and RFC 8174, and are normative only in capitals. Every normative sentence ends with its requirement id, `[TK-AREA-NNN]`. A lower-layer id in the text (`WP-…`, `SC-…`, `AU-…`) is a citation: the requirement belongs to that document, and the toolkit relies on it. **[T2]** marks a function on a T2 path (§4.0.5); **[RT]** marks one that an audio render callback may call.

## 1. Scope and non-goals

**In scope:**
- the toolkit's C ABI, `ndtk.h`: the stable, cross-language surface (charter §7);
- the Swift surface, `NDToolkit.swift`: the native binding, T1 with T2 paths (language-policy.md §2);
- the semantics of every call, record and event, and how each maps onto WP, SC and AU;
- the performance contract and the conformance list, whose vehicle is the S7 prototype programs ported from the host shim to this API.

**Non-goals:**
- **New protocol.** The toolkit adds no message to wsys, audiod or the kernel. Where it needs one, §10 records a proposal against the owning document (spec-conventions.md §7, "a change needed in a lower layer is made there first").
- **The immediate builder** (charter P8, §11 question 2): deferred until after the retained core; `NDTK_CAP_IMMEDIATE` is reserved (§10).
- **Wayland extensions** (charter §11 question 3): the Wayland front door is P4-04's, and it translates *into* wsys, not into this API.
- **A toolkit 3D API, Metal, or an SDL_gpu-shaped API** (charter §6).
- **Server-drawn widgets, client-side decorations as the default, app-driven stacking layers** (charter §3).
- **Raw HID, input injection, MIDI, audio plug-ins, exclusive audio devices** (charter §3 exclusions).
- **Thread affinity, priorities and timer resolution** (SC-PLACE-001, SC-RES-001).

## 2. Terms

| Term | Meaning |
|---|---|
| **toolkit** | the library that implements this document: `libndtk` (Swift, with `@_cdecl` entry points for `ndtk.h`) |
| **loop** | a toolkit object owning one kqueue and one frame arena; the one wait of charter P1 |
| **loop thread** | the thread currently inside, or last inside, `ndtk_wait`/`ndtk_poll` on a loop |
| **source** | anything registered in a loop's kqueue: a window's `events` fid, a desktop `events` fid, the wake knote, an app descriptor or Mach port, the audio session descriptor, a completion source |
| **handle** | a `uint64_t`: kind (bits 56–63), generation (bits 32–55, never 0), slot index (bits 0–31) |
| **retained table** | a slot table with free-list reuse whose slots outlive frames: windows, nodes, timers, sources, surfaces, GPU contexts, I/O requests, dialogs, threads |
| **frame arena** | a loop's bump allocator, reset at the start of every wait or poll |
| **SC clock** | `mach_absolute_time` in nanoseconds (SC-TIME-001); every toolkit time and deadline |
| **configuration** | a window's `Deskconfig` (WP §4.8), with its config seq |
| **CPU surface** | the shared-memory buffer behind `windows/N/surface` (WP-PAINT-011), acquired and presented through the toolkit |
| **GPU context** | the result of the one-call helper: a `webgpu.h` instance, adapter, device, queue and configured surface for one window |
| **node** | one entry of a UI's node table: a container, widget or canvas |
| **UI** | the node table of one window, with its layout, style resolution and painter |
| **stream, buffer, voice** | the audio service's objects (AU §3.1), named by their AU handles |
| **T2** | allocation-free and lock-free on the path (language-policy.md §2); §4.0.5 |
| **S7 program** | one of the study's prototypes: minimal (Swift and C), game-loop, text-editor, synth-ui, compute-display, and the `ndtk-selftest` |

## 3. Model

**Objects and lifetimes.**

| Object | Created by | Owned by | Ends | Handle kind |
|---|---|---|---|---|
| loop | `ndtk_loop_create`, `Loop()` | the creating code; waited on by one thread at a time | `ndtk_loop_destroy`, `Loop` deinit | `LOOP` 0x40 |
| window | `ndtk_window_open` | its loop (attach can move it) | `ndtk_window_close`; the server ending the stream | `WINDOW` 0x41 |
| timer | `ndtk_timer_at` | its loop | fires (one-shot), `ndtk_timer_cancel`, loop end | `TIMER` 0x42 |
| source | `ndtk_watch_fd`, `ndtk_watch_port`, `ndtk_watch_path` | its loop | `ndtk_unwatch`, loop end | `SOURCE` 0x43 |
| output | the server (WP-OUT-004) | the toolkit's output table | unplug | `OUTPUT` 0x44 |
| gamepad | the server (WP-PAD-004) | the toolkit's pad table | disconnection | `GAMEPAD` 0x45 |
| CPU surface | `ndtk_cpu_surface_acquire` | the caller until presented or released | present, release, window close | `SURFACE` 0x46 |
| GPU context | `ndtk_gpu_open` | the caller | `ndtk_gpu_close`, window close | `GPU` 0x47 |
| UI | `ndtk_ui_create` | its window's loop thread | `ndtk_ui_destroy`, window close | `UI` 0x48 |
| node | `ndtk_node_create` | its UI | `ndtk_node_destroy` (subtree), UI end | `NODE` 0x49 |
| I/O request | `ndtk_io_read`, `ndtk_io_write` | its loop | its completion event is returned | `IO` 0x4a |
| dialog | `ndtk_dialog_open` | its loop | its answer event is returned | `DIALOG` 0x4b |
| thread | `ndtk_thread_spawn` | the caller | `ndtk_thread_join` | `THREAD` 0x4c |
| stream, buffer, voice | `ndtk_audio_open`, `ndtk_audio_buffer`, `ndtk_audio_play` | the toolkit's audio session | AU §3.1 | AU kinds 1–5 |

**One wait.** A loop is a kqueue plus a table. Everything that can wake the application is a source in that kqueue, and `ndtk_wait` is exactly one `nd_wait` (SC-LIB-001) followed by decoding into the frame arena:

```
                      ┌──────────────────────── loop (one kqueue) ────────────────────────┐
 wsys windows/N/events ─ Tready/Rready (SC §4.2) ─┐                                       │
 wsys desktop events   ───────────────────────────┤                                       │
 audio session fd      ─ AU-LIB-003 ──────────────┤  nd_wait(kq, min(deadline, timers),   │
 app fds, Mach ports   ───────────────────────────┼─ leeway)  ── decode (T2) ─▶ ndtk_event[] in the arena
 EVFILT_USER wake      ─ post ring + trigger ─────┤                                       │
 I/O completions       ─ post ring + trigger ─────┤                                       │
 agent 9P server fd    ───────────────────────────┘                                       │
                      └───────────────────────────────────────────────────────────────────┘
```

**Loop states.**

```
   create ─▶ IDLE ──wait/poll──▶ WAITING ──events, deadline or wake──▶ DISPATCHING (arena valid)
               ▲                                                            │
               └─────────── next wait/poll resets the arena ◀───────────────┘
   destroy (from IDLE or DISPATCHING) ─▶ windows closed, timers and sources cancelled, handles stale
```

**The frame cycle, from the application's side** (the server's side is WP §3):

```
 NDTK_EV_CONFIGURE seq=7 ──▶ acquire surface of config 7 ──▶ draw (T2, arena) ──▶ present(damage, config 7)
                                                                                   │ never blocks (WP-PAINT-015)
 NDTK_EV_FRAME presented=T target=T+r ◀──────────────────────── server shows it ◀──┘
 nothing requested and nothing presented ──▶ no NDTK_EV_FRAME, no wakeup (WP-FRAME-006, TK-LOOP-013)
```

**The two memories** (charter P6). The frame arena holds per-frame scratch: the event array, event payload bytes, layout scratch, draw lists and canvases. Retained tables hold everything that outlives a frame. Nothing retained points into the arena, and the arena never holds a handle's object.

## 4. Interfaces

### 4.0 Rules for every module

#### 4.0.1 Handles

- Every object that a caller names after the call that created it MUST be named at the C ABI by a `uint64_t` handle with the layout of §2 and the `NDTK_HANDLE_*` accessors of `ndtk.h` [TK-HDL-001].
- Toolkit kinds MUST lie in 0x40–0x7F; audio streams, buffers and voices MUST be the audio service's AU handles, passed through unchanged, so the kind byte always says which table owns a handle [TK-HDL-002].
- When an object ends, its slot's generation MUST advance before the slot is reused, so a stale handle never names a new object [TK-HDL-003].
- A call given a stale, foreign, wrong-kind or zero handle MUST change nothing, MUST return its failure value with `NDTK_E_HANDLE`, and MUST count the event in the owning table's statistics; it MUST NOT trap and MUST NOT have undefined behaviour [TK-HDL-004]. This is P5's "diagnosed no-op".
- Handles MUST be valid process-wide: any thread may pass any handle to any call, subject to the threading rules of §4.0.4 [TK-HDL-005].
- The same `u64` values MUST be the ones that appear in the `/n/app` export and in agent messages (§4.13) [TK-HDL-006].

In Swift, handles are `Hashable, Sendable` wrappers of `raw: UInt64` (`Window`, `NodeID`, `TimerID`, …). Owners of resources are `~Copyable` structs whose `deinit` releases them (`Loop`, `CPUSurface`, `GPUContext`, `GPUFrame`, `AudioStream`, `AudioBuffer`, `IOBuffer`, `ThreadHandle`).

| TK | Relies on |
|---|---|
| TK-HDL-001, TK-HDL-003, TK-HDL-004 | (toolkit tables); the same rule below it: AU-ABI-001, SC-RT-011 |
| TK-HDL-002 | AU-ABI-001 (AU handle layout, kinds 1–5) |
| TK-HDL-005 | WP-ACK-012 (any thread may write a window's ctl) |
| TK-HDL-006 | (agent-protocol.md §1, no ids; §10 item 11) |

#### 4.0.2 Errors

- Calls that create an object MUST return its handle, or 0 on failure; other calls MUST return `bool`, a count or an `ndtk_error` as declared in `ndtk.h` [TK-ERR-001].
- After every failure, `ndtk_last_error()` MUST return the code and `ndtk_error_detail()` a non-empty UTF-8 reason, both thread-local and valid until the thread's next `ndtk_` call; after a success they MUST return `NDTK_OK` and `""` [TK-ERR-002].
- A failure that comes from a lower layer MUST be mapped by the table below, and its detail MUST contain the lower layer's own text verbatim: the error string of a refused ctl write (WP-ACK-004), `nd_sched_error_detail()` (SC-RT-020), `au_error_detail()` (AU-ABI-002) [TK-ERR-003].
- In Swift, a call that creates an object or starts an operation that can fail recoverably MUST be `throws(ToolkitError)`; a call on an existing handle MUST NOT throw, and reports as the C ABI does, readable through `lastError()` [TK-ERR-004]. `ToolkitError.refusal` carries the `sc_rt_refusal` fields for `.admission`.
- Asynchronous failures (a window deleted by the server, a stream lost, a device removed, a GPU device lost, an I/O error) MUST be delivered as events to the loop that owns the object, never as a signal, a trap or an abort [TK-ERR-005].
- A toolkit call MUST NOT terminate the process or trap on a recoverable condition, including loss of the connection to wsys or audiod: such a call MUST fail with `NDTK_E_SERVICE` [TK-ERR-006].
- The GPU helper MUST install `webgpu.h`'s uncaptured-error and device-lost callbacks, and report through `NDTK_E_GPU` and `NDTK_EV_GPU`, so that no default handler panics (R12) [TK-ERR-007].

| Lower code | `ndtk_error` |
|---|---|
| WP `badmsg`, `badarg`, `noconfig`, `short` (WP-ACK-004) | `NDTK_E_ARG` |
| WP `busy` / `nofocus` / `nopress` / `gone` / `perm` / `unsupported` | `NDTK_E_BUSY` / `NDTK_E_NOFOCUS` / `NDTK_E_NOPRESS` / `NDTK_E_GONE` / `NDTK_E_PERMISSION` / `NDTK_E_UNSUPPORTED` |
| `SC_E_INVAL`, `SC_E_HANDLE`, `SC_E_REFUSED`, `SC_E_PERM`, `SC_E_BUSY`, `SC_E_NOTSUP`, `SC_E_INTR`, `SC_E_NOMEM` | `NDTK_E_ARG`, `NDTK_E_HANDLE`, `NDTK_E_ADMISSION`, `NDTK_E_PERMISSION`, `NDTK_E_BUSY`, `NDTK_E_UNSUPPORTED`, `NDTK_E_INTR`, `NDTK_E_NOMEM` |
| `AU_ERR_INVALID_HANDLE`, `_INVALID_ARG`, `_FORMAT`, `_PERIOD`, `_ADMISSION`, `_PERMISSION`, `_NO_DEVICE`, `_DEVICE_LOST`, `_LIMIT`, `_BUSY`, `_STATE`, `_SERVICE`, `_VERSION`, `_NOMEM`, `_TIMEOUT`, `_UNSUPPORTED` | `NDTK_E_HANDLE`, `_ARG`, `_FORMAT`, `_FORMAT`, `_ADMISSION`, `_PERMISSION`, `_DEVICE`, `_DEVICE`, `_LIMIT`, `_BUSY`, `_STATE`, `_SERVICE`, `_ARG`, `_NOMEM`, `_TIMEOUT`, `_UNSUPPORTED` |

| TK | Relies on |
|---|---|
| TK-ERR-002, TK-ERR-003 | WP-ACK-004, WP-ACK-005, SC-LIB-008, SC-RT-020, AU-ABI-002 |
| TK-ERR-005 | WP-EV-019, AU-ABI-003, AU-STREAM-017, AU-DRV-003 |
| TK-ERR-006 | WP-MODAL-001, AU-ABI-003 |
| TK-ERR-007 | (webgpu.h; no NeoDarwin id) |

#### 4.0.3 The two memories

- Each loop MUST own one frame arena, reset at the start of every `ndtk_wait` and `ndtk_poll`, holding the event array, event payload bytes, and the layout scratch, draw lists and canvases of work done on that loop's thread between two waits; a reset MUST NOT return memory to the system [TK-MEM-001].
- Everything that outlives a frame MUST live in retained tables with free-list slot reuse (TK-HDL-003), and a retained object MUST NOT point into the arena [TK-MEM-002].
- Memory returned in the arena (the event array, `ndtk_event.data`, `Span8`, a `Canvas`) MUST stay valid, and MUST NOT be reused by the toolkit, until the next wait or poll on the same loop; callers MUST NOT use it afterwards [TK-MEM-003]. This rests on the window protocol's rule that record data belongs to the reader (WP §4.5, lifetime).
- When a frame needs more arena than the arena has, the toolkit MUST complete the work from the heap, count one spill, and grow the arena at the next reset; it MUST NOT fail the work [TK-MEM-004]. Capacity, high-water mark and spills are reported (`ndtk_ui_stats`, P11).
- Bytes passed into the toolkit (titles, node text, menus, clipboard text, requests, agent JSON) MUST be copied before the call returns, except the buffer lent to an I/O request (TK-IO-002) and the pixels of a CPU surface or image passed to a drawing call during that call [TK-MEM-005].
- `ndtk_set_allocator`, called before any other `ndtk_` call, MUST route every heap allocation of the toolkit's tables and arenas through the hook; called later it MUST fail with `NDTK_E_STATE` [TK-MEM-006]. The hook exists at the C ABI only (R9); Swift code has none.

| TK | Relies on |
|---|---|
| TK-MEM-003 | WP §4.5 "Lifetime and ownership" (no id), WP-EXT-010 |
| TK-MEM-001, TK-MEM-002, TK-MEM-004 … TK-MEM-006 | (toolkit) |

#### 4.0.4 Threading

- A toolkit call MUST NOT depend on the identity of the calling thread: there is no main thread, and any thread MAY create loops and windows [TK-THR-001].
- A loop is waited on by one thread at a time: a wait or poll entered while another thread is inside a wait or poll on the same loop MUST fail at once with `NDTK_E_BUSY`; ownership passes to whichever thread waits next [TK-THR-002].
- Window requests (§4.4) MAY be made from any thread; the toolkit MUST NOT hold a process-wide lock around them, relying on the server's per-window serialisation [TK-THR-003].
- A UI, its nodes, and canvases belong to the thread of their window's loop; a UI call from another thread MUST fail with `NDTK_E_STATE` and change nothing [TK-THR-004].
- A toolkit call MUST NOT run a nested event loop or wait for user input or for another client: every call MUST complete in time independent of user input (P2) [TK-THR-005]. Dialogs, drags, menus and popups are asynchronous objects that answer with events.
- The toolkit MUST NOT create a thread to wait on, poll or deliver any event source; the only threads it creates are the audio service's stream threads (AU-STREAM-004), the workers of asynchronous I/O (§4.12), and threads the application spawns [TK-THR-006].

| TK | Relies on |
|---|---|
| TK-THR-001 | SC-WAIT-003, WP-ACK-012 |
| TK-THR-003 | WP-ACK-011, WP-ACK-012 |
| TK-THR-005 | WP-MODAL-001, WP-MODAL-002, WP-MODAL-003, WP-DND-006 |
| TK-THR-006 | SC-WAIT-001, SC-WAIT-002, AU-LIB-003, AU-EVENT-005 |

#### 4.0.5 T2 marking

The T2 paths are event decode, layout, drawing and present (charter §4), and audio render. The functions on them are marked **[T2]** in `ndtk.h`, in this text and, in Swift, by the attributes below:

| Path | C (`ndtk.h`) | Swift (`NDToolkit.swift`) |
|---|---|---|
| event decode | `ndtk_wait`, `ndtk_poll` (after the system call) | `Events.subscript`; `UI.handle` |
| frames | `ndtk_request_frame`, `ndtk_cpu_surface_acquire`, `ndtk_cpu_surface_fill`, `ndtk_present`, `ndtk_gpu_begin_frame`, `ndtk_gpu_end_frame` | `CPUSurface.fill`; `Window.requestFrame`, `Window.present`, `GPUContext.beginFrame/endFrame` by the outer-wrapper rule |
| layout and paint | `ndtk_ui_handle`, `ndtk_ui_draw`, `ndtk_node_set_samples` | `UI.handle`, `UI.set(_:samples:)`; `UI.draw` by the outer-wrapper rule (it ends in the present) |
| drawing | every `ndtk_canvas_*`, `ndtk_text_measure` | every `Canvas` method |
| input state | `ndtk_gamepad_state_get` | `Loop.gamepadState` |
| audio (**[RT]**) | `ndtk_now`, `ndtk_wake`, `ndtk_post`, `ndtk_audio_contract`, `ndtk_audio_play`, `ndtk_audio_voice_stop`, the render callback | `AudioRenderer.render`; `LoopWaker.wake/post`, `AudioStream.write/read` and `Mixer.play/stop/set` are **[RT]** through the C guarantees they wrap (SC-USER-004, AU-LIB-006) |

- A **[T2]** function MUST NOT take a lock and MUST NOT allocate from the heap other than through an arena spill (TK-MEM-004); it MAY make only the system calls its section names (the wait, the present write, the surface read) [TK-T2-001].
- An **[RT]** function MUST NOT take a lock, allocate or block; `ndtk_wake` and `ndtk_post` MAY make the one non-blocking `kevent64` trigger of SC-USER-004, and every other **[RT]** function MUST NOT make a system call [TK-T2-002].
- In Swift, a T2 function whose whole body is Swift MUST carry `@_noLocks` and `@_noAllocation` (the pinned toolchain's spellings, Swift 6.4), and the modules implementing them MUST build with the performance diagnostics enabled; where a T2 function ends in a system call, the system call MUST sit in an unannotated outer function that calls an annotated inner one, as S7 W8 did [TK-T2-003].
- Adding a heap allocation to any annotated T2 function MUST fail the build of the implementation (negative test) [TK-T2-004].

| TK | Relies on |
|---|---|
| TK-T2-001 | SC-LIB-007 (`nd_wait` itself allocation-free), WP-PAINT-015 |
| TK-T2-002 | SC-USER-004, AU-LIB-006, AU-VOICE-003, AU-T2-001, AU-T2-002 |
| TK-T2-003, TK-T2-004 | AU-T2-003 (the same rule for the audio library), language-policy.md §2 |

#### 4.0.6 The Swift interface and the pinned toolchain

- `NDToolkit.swift` MUST type-check as Swift 6 with warnings as errors in an `nd_swift_library`, and when P4-12's implementation starts, its public interface MUST match the file, checked by a test that compares the implementation's emitted `.swiftinterface` with it [TK-SW-001].

The interface follows language-policy.md §4: `~Copyable` owners with `borrowing` and `consuming` parameters, `Span` views, typed throws, `Sendable` values across threads. Checked against Xcode's Swift 6.4 (`swiftlang-6.4.0.34.1`, the compiler rules_swift uses), three constructs need a fallback:

| Wanted | Status on the pinned toolchain | Normative form in version 1 |
|---|---|---|
| `CPUSurface.pixels: MutableRawSpan` (a property returning a span tied to `self`, as the S7 shim had) | rejected: "a mutating method cannot return a ~Escapable result"; accepted only with `-enable-experimental-feature Lifetimes` and `@_lifetime(&self)` | `withPixels { (inout MutableRawSpan) in … }` (same for `IOBuffer`) |
| `Span<UInt8>` as an enum payload (event text) | not storable in a stored enum (S7 API.md changelog item 3, SHIM-NOTES W14) | `Span8`: pointer and count into the arena, valid until the next wait; `Event` and `Payload` are therefore not `Sendable` |
| `Events` and `Canvas` as `~Escapable` | same lifetime-feature dependency | `Events` is an escapable `RandomAccessCollection` whose validity is TK-MEM-003; `Canvas` is `~Copyable` and handed out only inside a closure |

The experimental feature is not used because it would put an experimental flag into every client's build. When lifetime dependencies are stable in the pinned toolchain, a later version adds the property forms beside these (P13).

### 4.1 Loop

**Types.** `ndtk_loop_desc` (40 bytes), `ndtk_message` (16 bytes); Swift `Loop` (`~Copyable, Sendable`), `LoopOptions`, `LoopWaker` (`Sendable`), `Message`.

**Calls.** `ndtk_loop_create`, `ndtk_loop_destroy`, `ndtk_wait` **[T2]**, `ndtk_poll` **[T2]**, `ndtk_wake` **[RT]**, `ndtk_post` **[RT]**, `ndtk_timer_at`, `ndtk_timer_cancel`, `ndtk_watch_fd`, `ndtk_watch_port`, `ndtk_unwatch`, `ndtk_loop_fd`.

**Semantics.**
- A loop MUST own exactly one kqueue, and every source of the loop MUST be registered in it: the `events` fid of each attached window, the desktop-level `events` fid while the loop has a window, the wake knote on `SC_WAKE_IDENT`, the application's descriptors and Mach ports, the audio session descriptor while the loop owns audio objects, the agent server's descriptor, and completion sources [TK-LOOP-001].
- `ndtk_wait` MUST block in exactly one `nd_wait` call, with the earlier of the caller's deadline and the loop's earliest timer as the deadline and the corresponding leeway, and in no other blocking call [TK-LOOP-002].
- `ndtk_wait` MUST return when at least one event is ready (the count), when the deadline passes with none (0), or after a wake (at least the wake event); with `NDTK_DEADLINE_NONE` it MUST wait for events only, and with `NDTK_DEADLINE_POLL` or `ndtk_poll` it MUST NOT block [TK-LOOP-003].
- A signal that interrupts the wait MUST make `ndtk_wait` return −1 with `NDTK_E_INTR`, without retrying [TK-LOOP-004].
- When more events are ready than the loop's event capacity, the rest MUST stay queued for the next wait, and the toolkit MUST NOT lose any [TK-LOOP-005].
- `ndtk_post` MUST copy the message into the loop's multi-producer ring and trigger the wake; the message MUST be delivered as `NDTK_EV_MESSAGE`, in order per posting thread, and `ndtk_post` MUST NOT block: with the ring full it MUST return false with `NDTK_E_LIMIT` and the message is not delivered [TK-LOOP-006].
- Any number of `ndtk_wake` calls between two waits MUST produce at most one `NDTK_EV_WAKE`; a post MUST wake the loop without producing `NDTK_EV_WAKE` [TK-LOOP-007].
- A timer MUST be delivered as `NDTK_EV_TIMER` no earlier than its deadline, and all of a loop's timers MUST share the one deadline knote of `nd_wait`: the toolkit MUST NOT register other timers, and MUST NOT use calendar-epoch timers [TK-LOOP-008].
- A repeating timer's next deadline MUST be its previous deadline plus the period; when several periods have passed, one event MUST be delivered with `expirations` counting them [TK-LOOP-009].
- An fd or port watch MUST be level-triggered: its event is delivered at every wait while the condition holds, with `NDTK_FD_EOF` at end of file; it MUST NOT be delivered twice in one wait [TK-LOOP-010].
- `ndtk_loop_fd` MUST return the loop's kqueue descriptor, which is readable exactly when a wait would not block, so that a loop can be nested in another loop; polling that descriptor MUST NOT consume events [TK-LOOP-011].
- The toolkit MUST NOT call any timer-resolution control and MUST NOT spin: while waiting, the loop thread is blocked in the kernel [TK-LOOP-012].
- When no source is ready and no timer is due, the loop thread MUST NOT wake: the toolkit arms no periodic timer of its own, for caret blinking, housekeeping or anything else [TK-LOOP-013].
- Within one window, events MUST be delivered in the order of that window's stream; input events of different windows SHOULD be ordered by `time_ns` [TK-LOOP-014].
- Before an event is returned, the toolkit MUST have applied its effect to its own state: after an `NDTK_EV_CONFIGURE` is returned, `ndtk_cpu_surface_acquire` MUST return the buffer of that configuration [TK-LOOP-015].
- With `NDTK_LOOP_AUTO_INTENT`, the toolkit MUST set the loop thread's intent to `interactive` while any of the loop's windows has visibility SHOWN, and to `background` while none has, at the first wait after a change; with `intent` set, it MUST apply that intent at the first wait [TK-LOOP-016].
- Destroying a loop MUST close its windows and cancel its timers, sources and I/O requests; their handles become stale (TK-HDL-003) [TK-LOOP-017].

**Errors.** `NDTK_E_BUSY` (TK-THR-002), `NDTK_E_INTR`, `NDTK_E_LIMIT` (post), `NDTK_E_HANDLE`; `ndtk_loop_create` fails with `NDTK_E_NOMEM` or `NDTK_E_UNSUPPORTED` (no `SC_CAP_WAIT`).

**Threading.** `ndtk_wake` and `ndtk_post` (Swift: `LoopWaker`) from any thread, including a thread joined to a real-time ticket. Everything else from the loop thread or, for timers and watches, from any thread.

**Lifetime.** Swift's `Loop` is `~Copyable` and `Sendable`: moving it to another thread moves the ownership of TK-THR-002, and its `deinit` destroys the loop.

| TK | Relies on |
|---|---|
| TK-LOOP-001 | SC-WAIT-001, SC-WAIT-002, SC-WAIT-003, SC-9P-001, SC-9P-008, SC-9P-009, WP-EV-013, WP-EV-014, WP-EV-017, AU-LIB-003, AU-EVENT-005, SC-USER-001 |
| TK-LOOP-002 | SC-LIB-001, SC-LIB-002, SC-TIMER-005, SC-TIME-001 |
| TK-LOOP-003 | SC-LIB-003, SC-LIB-004, SC-LIB-005, SC-LIB-006 |
| TK-LOOP-004 | SC-LIB-008 |
| TK-LOOP-005 | WP-EV-002, WP-EV-018, SC-9P-010 |
| TK-LOOP-006, TK-LOOP-007 | SC-USER-001, SC-USER-002, SC-USER-003, SC-USER-004 |
| TK-LOOP-008, TK-LOOP-009 | SC-TIMER-001, SC-TIMER-002, SC-TIMER-003, SC-TIMER-004, SC-TIMER-006, SC-TIMER-007 |
| TK-LOOP-010, TK-LOOP-011 | SC-WAIT-001, SC-9P-012 |
| TK-LOOP-012, TK-LOOP-013 | SC-RES-001, WP-FRAME-006 |
| TK-LOOP-014 | WP-EV-025, WP-EV-027, WP-EV-028 |
| TK-LOOP-015 | WP-CONF-009, WP-PAINT-012 |
| TK-LOOP-016 | SC-INT-001, SC-INT-002, WP-CONF-004 (SC §4.6 "Window state") |

### 4.2 Callback driver

The SDL3-shaped thin layer (R3): `ndtk_run(app, argc, argv)` with `init`, `iterate`, `event` and `quit` (`ndtk_app`, 48 bytes); Swift `run(options:init:iterate:event:quit:)`.

- `ndtk_run` MUST create one loop from `app->loop` and call `init` once; `CONTINUE` starts the iterations, and any other result ends the run [TK-DRV-001].
- Each iteration MUST be one `ndtk_wait` with no deadline of the driver's own, then `event` once per returned event in order, then `iterate` once [TK-DRV-002].
- When any callback returns `SUCCESS` or `FAILURE`, the driver MUST stop calling `event` and `iterate`, call `quit` exactly once with that result, destroy the loop, and return 0 for `SUCCESS` and 1 for `FAILURE`; in C, `quit` MUST also be called when `init` fails; in Swift, where no state exists when `init` throws, `run` MUST return 1 without calling `quit` [TK-DRV-003].
- The driver MUST NOT pace iterations itself: an application paces with `ndtk_request_frame` or timers, and with nothing requested the loop sleeps (TK-LOOP-013) [TK-DRV-004]. SDL3's NeoDarwin backend maps SDL's iterate rate onto frame requests.
- The driver MUST NOT end the run on `NDTK_EV_QUIT` or `NDTK_EV_CLOSE`: they are delivered to `event`, and the application decides (P2) [TK-DRV-005].
- The driver MUST be implemented only with public `ndtk.h` calls (Swift: public `NDToolkit` API), so that any program using it can be rewritten on `ndtk_wait` with the same behaviour [TK-DRV-006].

| TK | Relies on |
|---|---|
| TK-DRV-001 … TK-DRV-006 | (the loop, §4.1); WP-MODAL-001 for TK-DRV-005 |

### 4.3 Events

**The record.** `ndtk_event`, 96 bytes: `kind` (u16), `flags` (u16), `seq` (u32), `window` (u64 handle), `time_ns` (u64, SC clock), `data` and `data_len` (a span into the arena), then a 56-byte payload union. Offsets are in `ndtk.h` and asserted by `ndtk_layout.c`. The Swift `Event` is its projection, with `Payload` an enum.

- Every event MUST be delivered as one `ndtk_event` with the layout of `ndtk.h` [TK-EV-001].
- `time_ns` MUST be, for input events, the device sample time carried in the extended record unchanged; for other window events, the record's `nsec`; for loop-level events, the SC clock when the toolkit dequeued the source [TK-EV-002].
- `seq` MUST increase by one per event delivered by the loop, wrapping, with no gaps [TK-EV-003].
- `window` MUST be the handle of the window whose stream produced the event, and 0 for loop-level events (timer, fd, message, wake, audio, io, dialog, agent, path, gpu, and desktop-level output, theme, keymap and quit) [TK-EV-004].
- Variable payloads (text, preedit text and style runs, drop paths, the error text of an ack, keymap and output names, dialog paths, agent lines, watched paths, GPU messages) MUST be delivered through `data`/`data_len` in the arena, as whole UTF-8 characters where they are text [TK-EV-005].
- Every flag, bit or enumerated value that names a window-protocol value (modifiers, key flags, buttons, pointer flags, tools, scroll flags, the text flag, frame flags, state bits, visibility, buffer modes, pad buttons and axes) MUST equal its `desktop.h` value, so that a payload field is the wire field unchanged [TK-EV-006].
- The toolkit MUST open every window stream in the extended format, selecting by class every type the table below delivers, and MUST NOT depend on the plain format [TK-EV-007].
- A record of a type the toolkit does not know MUST be skipped by its size and not delivered [TK-EV-008]. An application skips event kinds it does not know (Swift: `.unknown`).
- A text split across several `DE_TEXT` records MUST be delivered as one `NDTK_EV_TEXT` when all its parts arrived in the same wait, and otherwise as consecutive events with `NDTK_EVF_CONTINUES` on all but the last [TK-EV-009].
- When a window's stream reports `DS_LAGGING`, the next event of that window MUST carry `NDTK_EVF_LAGGING`; after the server discarded the queue, the toolkit MUST re-read `desc` and `config` and deliver an `NDTK_EV_CONFIGURE` with the current configuration before any other event of that window [TK-EV-010].
- The toolkit MUST NOT drop or coalesce events beyond the server's own coalescing, except wakes (TK-LOOP-007) and repeating-timer expirations (TK-LOOP-009) [TK-EV-011].
- Decoding records into events is **[T2]**: it MUST use only the arena [TK-EV-012].
- `NDTK_EV_QUIT` MUST be delivered to every loop that has a window when the server sends `DE_QUIT`, and the toolkit MUST NOT exit the process itself [TK-EV-013].
- `NDTK_EV_ACK` MUST be delivered only for requests the application tagged (§4.4); tags the toolkit uses internally MUST NOT surface [TK-EV-014].

**Kinds and their sources** (a kind is never renumbered; new kinds are appended):

| Kind | Source | Payload | Lower requirements |
|---|---|---|---|
| `KEY_DOWN` 1, `KEY_UP` 2 | `DE_KEY` (`Deskkey`) | `key` | WP-KEY-001, WP-KEY-003 … WP-KEY-006, WP-KEY-009, WP-EXT-008 |
| `TEXT` 3 | `DE_TEXT` | `text`, data | WP-IME-004, WP-IME-007, WP-EXT-010, WP-EXT-011 |
| `PREEDIT` 4 | `DE_PREEDIT` | `text`, data (text, then style runs) | WP-IME-004, WP-IME-008, WP-EV-033 |
| `POINTER_MOTION` 5, `_DOWN` 6, `_UP` 7 | `DE_MOUSE` (`Deskpointer`) | `pointer` | WP-PTR-001, WP-PTR-003 … WP-PTR-006, WP-EV-003 … WP-EV-006 |
| `SCROLL` 8 | `DE_SCROLL` | `scroll` | WP-PTR-008, WP-EV-032 |
| `RELATIVE` 9 | `DE_MOTION` | `relative` | WP-PTR-010 … WP-PTR-012, WP-EV-031 |
| `CONFIGURE` 10 | `DE_CONFIGURE` (`Deskconfig`) | `configure` | WP-CONF-001 … WP-CONF-006, WP-EV-028, WP-EV-029 |
| `FRAME` 11 | `DE_FRAME` (`Deskframe`) | `frame` | WP-FRAME-003 … WP-FRAME-013, WP-EV-030 |
| `GAMEPAD` 12 | `DE_GAMEPAD`, `DE_PADBUTTON`, `DE_PADAXIS`, `DE_PADSENSOR` | `gamepad` | WP-PAD-001 … WP-PAD-007 |
| `TIMER` 13, `FD` 14, `MESSAGE` 15, `WAKE` 16 | the loop | `timer`, `fd`, `message`, none | SC-TIMER-003, SC-WAIT-001, SC-USER-002 |
| `AUDIO` 17 | `AUevent` from the session descriptor | `audio` (the `AUevent`, verbatim) | AU-EVENT-001 … AU-EVENT-004, AU-LIB-004, AU-LIB-005 |
| `CLOSE` 18 | `DE_CLOSE`; the stream ending | `close` | WP-EV-019, WP-DECOR-001 |
| `QUIT` 19 | `DE_QUIT` | none | WP-EV-012 |
| `ACK` 20 | `DE_ACK` (`Deskack`) | `ack`, data | WP-ACK-007, WP-ACK-008, WP-EV-026 |
| `DELETE_SURROUNDING` 21 | `DE_DELSURROUND` | `delete_surrounding` | WP-IME-009, WP-IME-013 |
| `FOCUS` 22 | `DE_FOCUS` | `focus` | WP-KEY-009, WP-IME-010 |
| `POINTER_ENTER` 23, `_LEAVE` 24 | `DE_ENTER`, `DE_LEAVE` | `pointer` | WP-EV-003 |
| `STATE` 25 | `DE_STATE` | `state` | WP-EV-010, WP-DECOR-017, WP-CONF-005 |
| `MENU` 26 | `DE_MENU` | `menu` | WP-MENU-003 |
| `DROP` 27 | `DE_DROP` | `drop`, data | WP-DND-001, WP-EXT-012 |
| `POPUP_DONE` 28 | `DE_POPUPDONE` | `popup_done` | WP-POP-010, WP-POP-011 |
| `POINTER_CONSTRAINT` 29 | `DE_POINTER` | `constraint` | WP-PTR-015, WP-PTR-016 |
| `PROXIMITY` 30 | `DE_PROXIMITY` | `proximity` | WP-PTR-007 |
| `KEYMAP` 31 | `DE_KEYMAP` | `keymap`, data | WP-KEY-012 |
| `OUTPUT` 32 | `DE_OUTPUT` | `output`, data | WP-OUT-004 |
| `THEME` 33 | `DE_THEME` | `theme` | WP-DECOR-002, WP-EV-012 |
| `IO` 34, `DIALOG` 35, `AGENT` 36, `PATH` 39, `GPU` 40 | the toolkit | `io`, `dialog`, `agent`, `path`, `gpu` | §4.12, §4.4, §4.13, §4.8 |
| `EXPOSE` 37 | `DE_EXPOSE` | `expose` | WP-PAINT-008 |
| `MOVE` 38 | `DE_MOVE` | `move` | WP-CTL-001 |

`DE_RESIZE` is not delivered: its information is in the configuration the toolkit selects (WP-CONF-008). `DE_WINNEW`, `DE_WINGONE` and `DE_WORKSPACE` are launcher-level and not delivered.

| TK | Relies on |
|---|---|
| TK-EV-001, TK-EV-003, TK-EV-004 | WP-EXT-005, WP-EV-001 |
| TK-EV-002 | WP-EXT-006, WP-EV-027, SC-TIME-002 |
| TK-EV-005, TK-EV-009 | WP-EXT-010, WP-EXT-011, WP-EXT-012 |
| TK-EV-006 | WP-REC-001, WP-REC-008, WP-REC-009 |
| TK-EV-007 | WP-EV-020, WP-EV-021, WP-MASK-006, WP-MASK-010, WP-MASK-012 |
| TK-EV-008 | WP-EXT-002, WP-EXT-009, WP-CAP-007 |
| TK-EV-010 | WP-EV-010, WP-EV-011, WP-EV-035 … WP-EV-038, WP-NS-008 |
| TK-EV-011 | WP-EV-040 |
| TK-EV-012 | SC-LIB-007 |
| TK-EV-013 | WP-EV-012, WP-EV-017 |
| TK-EV-014 | WP-ACK-006, WP-ACK-009 |

### 4.4 Windows and outputs

**Types.** `ndtk_window_desc` (64 bytes), `ndtk_request` (64 bytes), `ndtk_output_info` (136 bytes), `ndtk_dialog_desc` (56 bytes); Swift `Window`, `WindowDescriptor`, `WindowRequest`, `OutputInfo`, `DialogDescriptor`.

**Opening and closing.**
- `ndtk_window_open` MUST create a wsys window of the requested kind, attach it to the loop, open its stream in the extended format (TK-EV-007), request `buffer device` unless opened with `NDTK_WIN_LOGICAL_BUFFER`, set the title, size limits and flags, and show it unless `NDTK_WIN_HIDDEN` is set; it MUST return once the window exists, and its first `NDTK_EV_CONFIGURE` MUST be delivered by the loop's next wait [TK-WIN-001].
- Opening MUST fail with `NDTK_E_PERMISSION` when the namespace has no `/n/desktop`, `NDTK_E_SERVICE` when wsys does not answer, and `NDTK_E_UNSUPPORTED` when the server is a version 1 server [TK-WIN-002]. In Swift, `openWindow` throws.
- Sizes and positions given to window calls MUST be logical points; device pixels appear only in configurations and surfaces [TK-WIN-003].
- `ndtk_window_close` MUST delete the window and end every object bound to it (surfaces, UI and nodes, GPU context), whose handles become stale [TK-WIN-004].
- `NDTK_EV_CLOSE` with `forced` 0 is a request: the toolkit MUST NOT close the window on its own; when the window's stream ends without `ndtk_window_close` (Kill, server exit), the toolkit MUST deliver one `NDTK_EV_CLOSE` with `forced` 1, after which the handle is stale [TK-WIN-005].
- `ndtk_window_attach` MUST move the window's stream registration to another loop; events returned before the call stay valid under TK-MEM-003, and later events go to the new loop [TK-WIN-006].

**Requests.** `ndtk_window_request` takes one `ndtk_request`; `ndtk_window_set_title`, `_set_size` and `_show` are shorthands for it; Swift has `Window.set(_:)` and `Window.submit(_:)`.

| Op | wsys message | Relies on |
|---|---|---|
| `TITLE`, `SIZE`, `MOVE`, `SHOW`, `HIDE`, `RAISE`, `LOWER`, `FOCUS`, `ZOOM`, `MIN_SIZE`, `MAX_SIZE`, `WORKSPACE`, `PARENT` | `title`, `resize`, `move`, `show`, `hide`, `raise`, `lower`, `focus`, `zoom`, `minsize`, `maxsize`, `workspace`, `parent` | WP-CTL-001 … WP-CTL-003, WP-CTL-008 |
| `FULLSCREEN` | `snap full`; off: `resize -r` to the geometry before | WP-CTL-001, WP-CTL-003 |
| `DECOR`, `HIT`, `HIT_CLEAR` | `decor none|server`, `hit …`, `hit clear` | WP-DECOR-003 … WP-DECOR-013 |
| `INTERACTIVE_MOVE`, `INTERACTIVE_RESIZE` | `move`, `resize [edge E]` | WP-DECOR-014, WP-DECOR-015, WP-DECOR-017 |
| `POINTER` | `pointer lock|confine|free|warp` | WP-PTR-013 … WP-PTR-017 |
| `KEY_REPEAT` | `repeat on|off` | WP-KEY-008 |
| `MENU`, `MENU_ITEM` | the `menu` file; `on`/`off` | WP-MENU-001 … WP-MENU-004 |
| `CURSOR`, `CURSOR_VISIBLE` | the `cursor` file | WP-NS-003 |
| `LATENCY`, `BUFFER`, `VIEWPORT` | `latency`, `buffer`, `viewport` | §4.6 |
| `POPUP` | `popup PARENT x0 y0 x1 y1 anchor gravity [flip] [slide] [resize] [grab]` | WP-POP-001 … WP-POP-007 |

- Each request MUST be sent as the wsys message of the table, and the call MUST return only after the write has returned, with `NDTK_OK` or the mapped error of TK-ERR-003 [TK-WIN-007]. The request is therefore applied or refused when the call returns.
- With a non-null `out_tag`, or on a window opened with `NDTK_WIN_WANT_ACKS`, the request MUST be tagged and exactly one `NDTK_EV_ACK` with that tag MUST follow the events the request caused [TK-WIN-008].
- `ndtk_window_ctl` MUST pass a raw window ctl line unchanged, tagged when `out_tag` is non-null, except that it MUST refuse lines that change the mask or kind (`mask`, `flags ±popup`, `flags ±tooltip`) with `NDTK_E_ARG`, since the toolkit owns them [TK-WIN-009].
- A popup or tooltip window MUST be opened unmapped and shown only by `NDTK_REQ_POPUP`; a dismissal MUST be delivered as `NDTK_EV_POPUP_DONE`, and the popup MUST stay valid for placing again [TK-WIN-010].

**Outputs, clipboard, drag, dialogs.**
- `ndtk_outputs` MUST list the outputs of `/n/desktop/outputs/` with their stable names, scale, refresh, rectangle and `hwtime` flag, and output changes MUST be delivered as `NDTK_EV_OUTPUT` to every loop that has a window [TK-WIN-011].
- The toolkit MUST NOT compute a window's scale from outputs; the configuration's scale is the only scale [TK-WIN-012].
- `ndtk_clipboard_set` and `ndtk_clipboard_get` MUST write and read `snarf` as UTF-8 text; `get` returns the length needed [TK-WIN-013].
- `ndtk_drag_paths` MUST write the desktop `drag` message and return at once; a count of 0 cancels [TK-WIN-014].
- For `DE_DROP`, the toolkit MUST deliver the paths as `data`, reading the window's `drop` file itself when the record carries none; it MUST NOT open the paths [TK-WIN-015].
- A dialog MUST answer with exactly one `NDTK_EV_DIALOG` (accepted with paths, or cancelled), and the toolkit MUST NOT wait for the user in any call [TK-WIN-016]. In version 1 a file dialog is a toolkit-drawn transient window in the application's own process, showing only the application's namespace; `NDTK_CAP_DIALOG_FILE` reports it.

| TK | Relies on |
|---|---|
| TK-WIN-001 | WP-NS-002, WP-NS-004, WP-DCTL-001, WP-DCTL-005, WP-DCTL-006, WP-EV-020, WP-BUF-001, WP-CONF-001, WP-CONF-002 |
| TK-WIN-002 | WP-CAP-002, WP-CAP-005, WP-SEC-001 |
| TK-WIN-003 | WP-CONF-003 (units: WP §2, "point") |
| TK-WIN-004 | WP-CTL-005, WP-PAINT-012 |
| TK-WIN-005 | WP-DECOR-001, WP-EV-019, WP-CTL-001 (`close`) |
| TK-WIN-006 | WP-EV-015, WP-EV-016 |
| TK-WIN-007 | WP-ACK-001, WP-ACK-002, WP-ACK-004, WP-ACK-005, WP-CTL-004, WP-PERF-001 |
| TK-WIN-008 | WP-ACK-006, WP-ACK-007, WP-ACK-008, WP-ACK-009, WP-EV-026 |
| TK-WIN-009 | WP-CTL-009, WP-MASK-011 |
| TK-WIN-010 | WP-DCTL-006, WP-POP-001 … WP-POP-012 |
| TK-WIN-011 | WP-OUT-001, WP-OUT-002, WP-OUT-003, WP-OUT-004, WP-MASK-013 |
| TK-WIN-012 | WP-CONF-003, WP-CONF-006 |
| TK-WIN-013 | WP-DND-004 |
| TK-WIN-014 | WP-DND-001, WP-DND-003, WP-DND-006 |
| TK-WIN-015 | WP-EXT-012, WP-DND-001, WP-DND-005, WP-SEC-005 |
| TK-WIN-016 | WP-MODAL-001, WP-DCTL-005 (transient) |

### 4.5 Input

**Keys.**
- `NDTK_EV_KEY_DOWN` and `_UP` MUST carry the record's HID usage as `scancode`, its rune, its modifiers after the event, its flags, and the keysym, base and layout of the extended body, unchanged [TK-INP-001].
- The toolkit MUST NOT generate key repeat; repeated keys carry `NDTK_KEY_REPEAT` from the server, and `NDTK_REQ_KEY_REPEAT` turns server repeat on or off for the window [TK-INP-002].
- A release flagged `NDTK_KEY_CANCEL` MUST be delivered as `NDTK_EV_KEY_UP`, and the toolkit's own key state (UI, shortcuts) MUST treat it as a release [TK-INP-003].
- `NDTK_EV_TEXT` MUST come only from `DE_TEXT`: the toolkit MUST NOT synthesise text from key events [TK-INP-004]. An application that wants text enables text input; one that wants shortcuts matches key events by `base` or `scancode`.

**Text input and IME.**
- `ndtk_text_input_set` MUST express the state as the window's `ime` messages (`enable`/`disable`, `purpose`, `rect` in content coordinates, `surrounding`), writing only the messages whose values changed since the last call; `ndtk_text_input_reset` MUST write `reset` [TK-INP-005].
- Preedit, commit and delete-surrounding events MUST be delivered in stream order with their ime serial; the toolkit's own text adapter (TK-TEXT-004) MUST apply a commit whose serial is older than its latest `surrounding` and MUST ignore such a stale deletion [TK-INP-006].

**Pointer and pen.**
- Pointer positions MUST be the record's 24.8 window points as floats; pen fields MUST be converted into the units of `ndtk.h` from the fields of `Deskpointer`, and a record that changes several buttons MUST be delivered as one down or up event per changed button, in bit order, with the same time [TK-INP-007].
- The toolkit MUST NOT synthesise a second, emulated pointer stream for a pen; proximity MUST be delivered as `NDTK_EV_PROXIMITY` [TK-INP-008].
- The toolkit MUST select `DE_SCROLL` and deliver scrolling only as `NDTK_EV_SCROLL`, never as wheel buttons [TK-INP-009].
- The toolkit MUST select `DE_MOTION` while a pointer lock or confinement is requested on the window, and deliver it as `NDTK_EV_RELATIVE` [TK-INP-010].
- Pointer constraints MUST be requested through `NDTK_REQ_POINTER`, failing with `NDTK_E_NOFOCUS` when the window lacks focus, and every change MUST be delivered as `NDTK_EV_POINTER_CONSTRAINT` with its reason [TK-INP-011].
- The toolkit MUST deliver pointer coordinates in window points whatever the buffer and viewport, and MUST NOT rescale them to buffer pixels [TK-INP-012].

**Gamepads and keymap.**
- Gamepads MUST be read from `/n/desktop/gamepads/`: connection and disconnection as `NDTK_EV_GAMEPAD` changes, buttons unchanged, sticks as value / 32767 clamped to −1…1 with +y down, triggers as value / 32767; a pad's handle MUST become stale at disconnection, and a reconnected pad gets a new handle [TK-INP-013].
- Pad events MUST be delivered only to the loop that watches gamepads, by default the first loop that opened a window; `ndtk_gamepad_state_get` MUST return the state folded from the events that loop has read, without a system call [TK-INP-014].
- Rumble, LEDs and sensors MUST be the pad's ctl messages, failing with `NDTK_E_UNSUPPORTED` where the pad lacks them; the toolkit MUST NOT request background pad input [TK-INP-015].
- Keymap changes MUST be delivered as `NDTK_EV_KEYMAP`, and `ndtk_keymap_layout` MUST read `/n/desktop/keymap` [TK-INP-016].

Touch and gestures are not in window protocol version 2; `NDTK_CAP_TOUCH` is reserved (§10).

| TK | Relies on |
|---|---|
| TK-INP-001 | WP-KEY-001, WP-KEY-003, WP-KEY-004, WP-KEY-005, WP-KEY-006, WP-EXT-008 |
| TK-INP-002 | WP-KEY-007, WP-KEY-008 |
| TK-INP-003 | WP-KEY-009 |
| TK-INP-004 | WP-IME-005, WP-IME-006, WP-IME-007, WP-IME-011, WP-KEY-013 |
| TK-INP-005 | WP-IME-001, WP-IME-002, WP-IME-003, WP-SEC-004 |
| TK-INP-006 | WP-IME-004, WP-IME-008, WP-IME-009, WP-IME-010, WP-IME-012, WP-IME-013 |
| TK-INP-007, TK-INP-008 | WP-PTR-001, WP-PTR-003, WP-PTR-004, WP-PTR-005, WP-PTR-006, WP-PTR-007, WP-REC-001 |
| TK-INP-009 | WP-PTR-008, WP-PTR-009 |
| TK-INP-010 | WP-PTR-010, WP-PTR-011, WP-PTR-012 |
| TK-INP-011 | WP-PTR-013 … WP-PTR-017, WP-SEC-002 |
| TK-INP-012 | WP-BUF-006 |
| TK-INP-013, TK-INP-014 | WP-PAD-001 … WP-PAD-006, WP-PAD-008, WP-PAD-009 |
| TK-INP-015 | WP-PAD-007, WP-PAD-010, WP-PAD-011, WP-SEC-003 |
| TK-INP-016 | WP-KEY-010, WP-KEY-011, WP-KEY-012 |

### 4.6 Frames and surfaces

**Types.** `ndtk_cpu_surface` (48 bytes), `ndtk_irect`; Swift `CPUSurface` (`~Copyable`), `Frame`, `FrameFlags`.

**Calls.** `ndtk_request_frame` **[T2]**, `ndtk_cpu_surface_acquire` **[T2]**, `ndtk_cpu_surface_fill` **[T2]**, `ndtk_present` **[T2]**, `ndtk_cpu_surface_release`; the `LATENCY`, `BUFFER` and `VIEWPORT` requests.

**Frame requests and feedback.**
- `ndtk_request_frame` MUST only mark the window, waking its loop when called from another thread; the loop MUST write `wantframe` for its marked windows before its next `nd_wait`, at most once between two frame events delivered for a window, so that further calls coalesce in the toolkit [TK-FRM-001]. On the loop thread a frame request is therefore **[T2]** with no system call.
- A window that neither presents nor requests MUST cause no frame event and no toolkit wakeup [TK-FRM-002].
- `NDTK_EV_FRAME` MUST carry the fields of `Deskframe` unchanged: `target_ns` (usable directly as a wait deadline), `presented_ns` (0 when nothing was shown), `refresh_ns`, `present`, `config_seq`, `flags` and `missed` [TK-FRM-003].
- A throttled window's frames MUST be delivered, with `NDTK_FRAME_THROTTLED`; the toolkit never withholds them [TK-FRM-004].
- `NDTK_REQ_LATENCY` MUST set the window's `latency`; a present beyond the bound MUST fail at once with `NDTK_E_BUSY` [TK-FRM-005].
- On NeoDarwin the toolkit MUST NOT set `NDTK_FRAME_ESTIMATED` and MUST NOT put an estimate in `presented_ns`; the bit exists for host shims [TK-FRM-006].

**The CPU surface.**
- `ndtk_cpu_surface_acquire` MUST return the shared-memory buffer of the latest configuration delivered to the application, re-reading the window's `surface` after a buffer change before it returns; `config_seq` MUST be that configuration's seq; it MUST fail only for an invalid handle or a window with no buffer [TK-FRM-007].
- `ndtk_present` MUST send `present` with the damage rectangle (the whole buffer when `damage` is NULL), tagged with `config_seq` (or the surface's own when 0), with `at` when `at_ns` is non-zero, and MUST return as soon as the write returns; it MUST consume the surface [TK-FRM-008]. A surface drawn for an older configuration stays presentable: a resize is never an error.
- The toolkit MUST use copy mode, and `age` MUST be 1 for a surface acquired after this window presented a buffer of the same configuration, and 0 otherwise [TK-FRM-009].
- A surface released or dropped without a present MUST show nothing [TK-FRM-010].
- The toolkit MUST NOT substitute its own timing for CPU presents: they get the server's feedback exactly as GPU presents do [TK-FRM-011].
- `NDTK_REQ_BUFFER` and `NDTK_REQ_VIEWPORT` MUST set `buffer` and `viewport`; a fixed buffer MUST survive window resizes without a new surface [TK-FRM-012].
- A window MUST use `chan x8r8g8b8` unless opened with `NDTK_WIN_ALPHA`, which MUST set `r8g8b8a8` and `DF_ALPHA`; `chan` MUST be reported in the surface [TK-FRM-013].

| TK | Relies on |
|---|---|
| TK-FRM-001, TK-FRM-002 | WP-FRAME-004, WP-FRAME-005, WP-FRAME-006, WP-PERF-006 |
| TK-FRM-003 | WP-FRAME-003, WP-FRAME-007 … WP-FRAME-013, WP-EV-030, SC-TIME-002 |
| TK-FRM-004 | WP-FRAME-018 … WP-FRAME-021 |
| TK-FRM-005 | WP-FRAME-014 … WP-FRAME-017 |
| TK-FRM-006, TK-FRM-011 | WP-FRAME-008, WP-FRAME-009, WP-FRAME-010 |
| TK-FRM-007 | WP-PAINT-011, WP-PAINT-012, WP-PAINT-013, WP-CONF-009, WP-SEC-001 |
| TK-FRM-008 | WP-PAINT-004, WP-PAINT-005, WP-PAINT-015, WP-PAINT-016, WP-PAINT-017, WP-CONF-010 … WP-CONF-015 |
| TK-FRM-009 | WP-PAINT-004, WP-PAINT-006 (not used), WP-CONF-008 |
| TK-FRM-010 | WP-PAINT-003 |
| TK-FRM-012 | WP-BUF-001 … WP-BUF-005 |
| TK-FRM-013 | WP-CTL-001 (`chan`, `flags`), WP-REC-006 |

### 4.7 Drawing

2D drawing is a canvas over a CPU surface (`ndtk_canvas_begin` … `_end`; Swift `CPUSurface.draw(scale:_:)`) or inside a UI canvas node. Every canvas call is **[T2]**. The rasteriser is Vesper and the materials are libstyle's (theme-engine.md §2); both are internal to the toolkit.

- A canvas MUST take coordinates in logical points, scaled by the scale passed at `begin`, and MUST have finished all its drawing into the surface when `ndtk_canvas_end` returns [TK-DRAW-001].
- Canvas calls MUST record into and rasterise from the frame arena and retained caches only, and MUST make no system call [TK-DRAW-002].
- `ndtk_canvas_end` MUST report as `damage_out` the union of the buffer pixels the canvas touched [TK-DRAW-003].
- `ndtk_canvas_material` MUST draw the current theme's material for a class and part [TK-DRAW-004]. Colours given to `fill`, `stroke` and `polyline` are the application's own content; the toolkit's widgets paint only through materials and styles (TK-STYLE-003).
- `ndtk_canvas_text` and `ndtk_text_measure` MUST shape their text (TK-TEXT-001) in the font the theme gives the class, and MUST rasterise glyphs from a retained cache; a frame that uses only cached glyphs MUST make no heap allocation [TK-DRAW-005].
- Anti-aliased coverage MUST be used for curves and diagonal edges, and an axis-aligned fill whose edges fall on device-pixel boundaries MUST cover exactly those pixels [TK-DRAW-006].
- `ndtk_canvas_clip_push` and `_pop` MUST form a stack that intersects clips; drawing outside the current clip MUST NOT touch pixels [TK-DRAW-007].
- `ndtk_canvas_image` MUST draw the image scaled into the destination rectangle with a linear filter, converting its `chan` to the surface's [TK-DRAW-008].

| TK | Relies on |
|---|---|
| TK-DRAW-001 … TK-DRAW-003 | WP-PAINT-011 (the surface), WP-PAINT-004 (damage) |
| TK-DRAW-004 | theme-engine.md §2, §3 (no ids; §10 item 10) |
| TK-DRAW-005 … TK-DRAW-008 | (toolkit; Vesper) |

### 4.8 GPU

Vulkan is the interop seam and `webgpu.h` (Dawn) is the drawing API (charter §6). The toolkit offers the window-handle bag for engines that take the raw path, and one helper that returns a ready device, queue and surface.

**Types.** `ndtk_gpu_handle_bag` (56 bytes), `ndtk_gpu_desc` (48), `ndtk_gpu_context` (96), `ndtk_gpu_frame` (32), `ndtk_gpu_budget` (40); WebGPU objects are `webgpu.h`'s opaque `struct WGPU*Impl *`, forward-declared so `ndtk.h` includes nothing. Swift: `GPUHandleBag`, `GPUDescriptor`, `GPUContext` (`~Copyable`), `GPUFrame` (`~Copyable`, ended exactly once).

- `ndtk_gpu_handle_bag_get` MUST return the window's wsys id, the port of its current `surface`, its desktop mount and its config seq [TK-GPU-001]. This is what a NeoDarwin Vulkan WSI surface and a NeoDarwin `webgpu.h` surface source take (their names are P7's, §10).
- `ndtk_gpu_open` MUST, in one call, create or reuse the process's Dawn instance, request an adapter (low-power when asked), request a device with the listed features, get its queue, create a surface from the window's handle bag, and configure it for the window's buffer size with a format the server accepts, usage `RENDER_ATTACHMENT` (plus `STORAGE_BINDING` with `NDTK_GPU_STORAGE`) and the requested latency; it MUST either succeed completely or release everything it created [TK-GPU-002].
- With a fixed `buffer_width` and `buffer_height`, the helper MUST request a fixed buffer, so that window resizes do not reconfigure the surface and the compositor scales it [TK-GPU-003].
- `ndtk_gpu_begin_frame` MUST reconfigure the surface when the window's delivered configuration changed the buffer since the last configure, then acquire the texture; when acquisition reports the surface outdated or suboptimal, it MUST reconfigure and retry once, so that a resize is never an error returned to the application [TK-GPU-004].
- `ndtk_gpu_end_frame` MUST present the frame's texture tagged with the configuration it was drawn for, with the damage; a frame MUST be ended exactly once, and a second end MUST fail with `NDTK_E_STATE` [TK-GPU-005].
- `ndtk_gpu_presented` MUST tag the window's next GPU present with the given configuration, for applications that present through raw `webgpu.h` or Vulkan [TK-GPU-006].
- Device loss and uncaptured errors MUST be delivered as `NDTK_EV_GPU` to the window's loop (TK-ERR-007); a lost context's handle stays valid for `ndtk_gpu_close` only [TK-GPU-007].
- A compute pass that writes the surface texture (`NDTK_GPU_STORAGE`) MUST reach composition without a copy made by the toolkit [TK-GPU-008].
- `ndtk_gpu_budget_get` MUST fail with `NDTK_E_UNSUPPORTED`, and `NDTK_CAP_GPU_BUDGET` MUST be clear, until P7 provides a per-task budget; the toolkit MUST NOT report an estimate as a budget [TK-GPU-009].
- The toolkit MUST NOT offer a 3D API of its own; buffer import and export (the F-107 buffer object) are reserved behind `NDTK_CAP_GPU_BUFFER` [TK-GPU-010].

| TK | Relies on |
|---|---|
| TK-GPU-001 | WP-PAINT-011, WP-PAINT-012, WP-PAINT-013, WP-SEC-001 |
| TK-GPU-002 | WP-BUF-007, WP-BUF-008, WP-CAP-006, WP-FRAME-014 … WP-FRAME-017 |
| TK-GPU-003 | WP-BUF-002, WP-BUF-003 |
| TK-GPU-004, TK-GPU-005, TK-GPU-006 | WP-BUF-007, WP-CONF-010, WP-CONF-011, WP-CONF-013, WP-CONF-014, WP-FRAME-010 |
| TK-GPU-007 | (webgpu.h) |
| TK-GPU-008 | WP-FRAME-013 |
| TK-GPU-009, TK-GPU-010 | (P7; §10 item 6) |

### 4.9 UI

The retained UI (charter P6–P8, P14): a node table per window, flex layout in logical points, style owned by the theme, shaped text, and a text view and text field that carry the IME adapter.

**Types.** `ndtk_node_kind`, `ndtk_ui_event` (24 bytes), `ndtk_ui_stats` (80 bytes), `ndtk_canvas_fn`; Swift `UI` (a `final class`, not `Sendable`), `NodeID`, `NodeChange`, `UIEvent`, `UIStats`, `Theme`, `Style`.

`UI` is a class rather than a `~Copyable` struct because a one-expression tree (`ui.column(ui.label(…), ui.row(…))`) nests calls on the same table, which exclusivity forbids for a mutating struct (the S7 shape). It is bound to one thread (TK-THR-004) and holds no lock.

#### 4.9.1 Node table

- A UI MUST belong to one window, and its nodes MUST be handles into a slot table with free-list reuse and generation checks [TK-UI-001].
- Construction MUST work leaves first: a container call adopts the nodes passed to it, so a tree is one expression; adding a node that already has a parent MUST move it [TK-UI-002].
- Destroying a node MUST destroy its subtree and make the handles of every node in it stale [TK-UI-003].
- A node MUST hold its kind, tree links, interned class, flex, hidden flag, frame, measured size and widget state, and nothing that sets a colour, font or size; the API MUST NOT offer a call that sets one [TK-UI-004].
- The v1 kinds MUST behave as follows: `column`, `row` and `stack` lay out children (§4.9.2); `spacer` takes flex space; `label` shows text; `button` reports a click on a release inside it after a press inside it; `checkbox` toggles its value on a click; `slider` changes its value by pointer drag and arrow keys, clamped to its range; `meter` shows a value; `scope` draws its samples; `text_field` edits one line and `text_view` many (§4.9.4); `image` shows copied pixels; `canvas` calls the application's draw function (TK-UI-011); `scroll` clips and scrolls one child [TK-UI-005].
- `ndtk_ui_handle` MUST route an event of its window through the table (pointer events by hit test in reverse paint order, keys and text to the focused node), update widget state, and return the result as a value (`clicked`, `changed`, `edited`, `focused`, `consumed` or `ignored`); it MUST return `ignored` for another window's events and MUST NOT call back into the application [TK-UI-006].
- Keyboard focus MUST move among focusable nodes (button, checkbox, slider, text field, text view) with Tab and Shift-Tab in tree order [TK-UI-007].
- The UI MUST call `ndtk_request_frame` itself when a change it owns needs painting, and MUST NOT otherwise wake the loop: the caret does not blink [TK-UI-008].
- `ndtk_ui_draw`, called on its window's `NDTK_EV_FRAME`, MUST lay out if needed, paint the damage and present once; with nothing changed it MUST present nothing and return false [TK-UI-009].
- Damage MUST be the union of the frames of nodes whose appearance changed since the last present, widened to a full repaint when the surface's `age` is 0 [TK-UI-010].
- A canvas node's draw function MUST be called during paint with a canvas clipped to the node's frame [TK-UI-011].
- On a configuration with a new seq, the UI MUST lay out for the new size and its next present MUST be tagged with that seq [TK-UI-012].
- Steady-state layout and paint MUST make no heap allocation and take no lock (T2), and `ndtk_ui_stats` MUST report presents, skipped draws, full repaints, invalid handles, and the arena's capacity, high-water mark and spills [TK-UI-013].
- `ndtk_ui_export` MUST write the node table as text, one node per line: handle, parent handle, kind, class, flags, frame in points, value, and the length of its text [TK-UI-014].

#### 4.9.2 Layout

- Layout MUST be two passes in logical points: measure bottom-up (the theme's minimum size and padding plus content), then arrange top-down, giving a row's or column's extra main-axis space to children in proportion to flex and stretching them on the cross axis; `stack` overlays its children [TK-LAYOUT-001].
- Text nodes MUST measure height for a given width (wrapping), so that a column of wrapping labels gets the heights its width implies [TK-LAYOUT-002].
- Arranged frames MUST have their edges on whole device pixels at the window's scale [TK-LAYOUT-003].
- Layout MUST run only when the window size, the theme, or a node's size-affecting state changed [TK-LAYOUT-004].

#### 4.9.3 Style

- A node's style MUST come from the theme by class name, falling back along dots (`label.param`, then `label`, then `default`) [TK-STYLE-001].
- The theme MUST be the window system's (`/n/theme`) unless a path is given; fonts MUST resolve per class at the window's scale; `NDTK_EV_THEME` MUST re-resolve every style and repaint fully without rebuilding the node table [TK-STYLE-002].
- The toolkit's widgets MUST paint only through styles and materials, so that changing the theme's palette changes every widget pixel it paints [TK-STYLE-003].

#### 4.9.4 Text, shaping and the IME adapter

- All text the toolkit draws or measures MUST be shaped: split into runs by script and bidi level (UAX #9), shaped with OpenType substitution and positioning (HarfBuzz-class), with font fallback per cluster, so that combining marks, ligatures and complex scripts render correctly [TK-TEXT-001]. The S7 UI drew one glyph per scalar; this is the gap S7 named (s7-prototypes.md §3 item 6).
- Caret movement, deletion and hit testing MUST work in grapheme clusters (UAX #29), and line breaking MUST follow UAX #14 [TK-TEXT-002].
- Text models MUST store UTF-8 and use byte offsets, as the window protocol and the agent protocol do [TK-TEXT-003].
- The text field and text view MUST carry the IME adapter, with no application code: while focused in a focused window, enable text input with the node's purpose, the surrounding text and the caret rectangle; show preedit inline at the caret, underlined, with the caret at the composition cursor; insert commits and apply deletions (TK-INP-006); update the caret rectangle and surrounding text after any caret, scroll, text or preedit change; disable text input on focus loss [TK-TEXT-004].
- A text field with purpose `password` MUST set that purpose and MUST NOT export its text (TK-AGENT-007) [TK-TEXT-005].
- A text view MUST lay out only the lines it shows, so that editing or scrolling costs work proportional to the visible lines, not to the document [TK-TEXT-006].
- Text views and fields MUST take inserted text only from `NDTK_EV_TEXT` and match editing keys on key events [TK-TEXT-007].

| TK | Relies on |
|---|---|
| TK-UI-001 … TK-UI-003 | (toolkit tables, §4.0.1) |
| TK-UI-004, TK-STYLE-001, TK-STYLE-003 | theme-engine.md §1, §2 (no ids) |
| TK-UI-006, TK-UI-007 | WP-EV-003, WP-KEY-002, WP-KEY-009, WP-DECOR-012, WP-DECOR-013 |
| TK-UI-008, TK-UI-009 | WP-FRAME-004, WP-FRAME-005, WP-FRAME-006, WP-PERF-006 |
| TK-UI-010 | WP-PAINT-004 |
| TK-UI-012 | WP-CONF-009, WP-CONF-010, WP-CONF-015, WP-PERF-008 |
| TK-STYLE-002 | WP-DECOR-002, WP-EV-012, WP-CONF-003 |
| TK-TEXT-003 | WP-IME-002 (byte offsets), WP-EXT-010 |
| TK-TEXT-004, TK-TEXT-007 | WP-IME-001 … WP-IME-013, WP-KEY-013 |
| TK-TEXT-005 | WP-SEC-004 |
| TK-UI-005, TK-UI-011, TK-UI-013, TK-UI-014, TK-LAYOUT-*, TK-TEXT-001, TK-TEXT-002, TK-TEXT-006 | (toolkit) |

### 4.10 Audio

Streams and voices are the audio service's (AU §4.5, §4.9); the toolkit adds loop integration, a Swift surface and the minimal-call conveniences. `ndtk.h` forward-declares `struct AUcontract`, `struct AUrender_info` and `struct AUstream_params`; C clients include `nd_audio.h` to read them.

**Calls.** `ndtk_audio_open` (F32 output, callback mode, default device), `ndtk_audio_open_params` (any `AUstream_params`), `_start`, `_stop`, `_close`, `ndtk_audio_contract` **[RT]**, `ndtk_audio_join`, `ndtk_audio_buffer`, `ndtk_audio_play` **[RT]**, `ndtk_audio_voice_stop` **[RT]**, `ndtk_audio_session`. Swift: `AudioStream` (`~Copyable`), `AudioBuffer` (`~Copyable`), `Mixer`, `AudioRenderer`, `AudioContract`, `RenderInfo`.

- The toolkit MUST open one audio session per process, at the first audio call, and `ndtk_audio_session` MUST return it for direct `au_*` calls; a namespace without `/n/sys/audio` MUST make audio calls fail with `NDTK_E_PERMISSION` [TK-AUD-001].
- The session descriptor MUST be a source of every loop that opened an audio object; on readiness the toolkit MUST drain the session's events without blocking and deliver each as `NDTK_EV_AUDIO` to the loop that opened its object, and device and default events to every such loop, in the service's order and with no thread of its own [TK-AUD-002].
- When `NDTK_EV_AUDIO` for a contract change is returned, `ndtk_audio_contract` MUST already return that generation [TK-AUD-003].
- Opening a stream MUST be one `au_stream_open` whose stream thread and admission are the service's; a refused admission MUST fail the open with `NDTK_E_ADMISSION`, the scheduling contract's reason verbatim in the detail and, in Swift, the refusal record in `ToolkitError.refusal`; the toolkit MUST NOT fall back to an unadmitted thread [TK-AUD-004].
- A C render function MUST be installed as the stream's render with nothing in between; a Swift `AudioRenderer` MUST be called from one `@convention(c)` thunk, the only unchecked frame, with the buffer as a `MutableSpan<Float>` [TK-AUD-005]. The closure form is also offered and is not checkable at the call site (S7 W8).
- The toolkit MUST NOT add a thread, a copy, a lock or a system call between the stream thread and the render function [TK-AUD-006].
- `ndtk_watch_port` MUST accept a queue stream's doorbell port, so that queue-mode readiness can join the one wait [TK-AUD-007].
- `ndtk_audio_buffer` MUST create an immutable mixer buffer, `ndtk_audio_play` MUST start a voice without a system call, and a voice's end MUST be delivered as `NDTK_EV_AUDIO`; releasing a buffer (Swift `deinit`) MUST let its voices finish [TK-AUD-008].
- The toolkit MUST report the contract as the service states it and MUST NOT add latency terms or re-derive the period [TK-AUD-009].
- A stream following the default device MUST keep its handle across device changes, and the toolkit MUST NOT turn a contract change into an error of a later call [TK-AUD-010].
- `ndtk_audio_join` MUST make the calling thread join the stream's admitted deadline, and fail with `NDTK_E_UNSUPPORTED` when the service offers no group [TK-AUD-011].
- Capture streams MUST be opened through `ndtk_audio_open_params`, with permission failures mapped by TK-ERR-003 [TK-AUD-012].
- Playing a sound from nothing MUST take no more toolkit calls than the service's bar: `ndtk_audio_buffer` and `ndtk_audio_play`, with the session implicit [TK-AUD-013].

The rules for the render callback itself are the service's (AU §4.13): no lock, allocation, blocking or system call (AU-T2-001), communication through atomics and SPSC rings set up before `start` (AU-T2-002), the Swift subset (no libm, no first use of a class, no `&&`/`||`), and the warm-up call (AU-STREAM-006). The toolkit restates none of them.

| TK | Relies on |
|---|---|
| TK-AUD-001 | AU-NS-001, AU-NS-002, AU-NS-003, AU-LIB-008 |
| TK-AUD-002 | AU-LIB-003, AU-LIB-004, AU-EVENT-001, AU-EVENT-002, AU-EVENT-004, AU-EVENT-005, SC-WAIT-001 |
| TK-AUD-003 | AU-LIB-005, AU-CONTRACT-004, AU-CONTRACT-005 |
| TK-AUD-004 | AU-STREAM-001 … AU-STREAM-005, AU-ABI-002, SC-RT-001, SC-RT-007, SC-RT-020 |
| TK-AUD-005, TK-AUD-006 | AU-T2-003, AU-T2-004, AU-STREAM-007, AU-STREAM-008 |
| TK-AUD-007 | AU-RING-008, AU-RING-009, SC-WAIT-001 |
| TK-AUD-008 | AU-VOICE-001 … AU-VOICE-005, AU-VOICE-009 |
| TK-AUD-009 | AU-CONTRACT-001, AU-CONTRACT-002, AU-CONTRACT-003, AU-PERF-001 |
| TK-AUD-010 | AU-CONTRACT-007, AU-ROUTE-002, AU-STREAM-016 |
| TK-AUD-011 | AU-STREAM-010, SC-RT-009, SC-RT-010 |
| TK-AUD-012 | AU-SEC-002, AU-SEC-003, AU-CAPTURE-001 |
| TK-AUD-013 | AU-PERF-011 |

### 4.11 Time and threads

- `ndtk_now` MUST return the SC clock, **[RT]** [TK-TIME-001].
- Every time and deadline in this API MUST be on the SC clock, so that a frame's `target_ns`, an event's `time_ns` and an audio host time converted with the contract's timebase can be passed to a wait unchanged [TK-TIME-002].
- `enum ndtk_intent` MUST equal `enum sc_intent`, and `ndtk_thread_set_intent` MUST set the calling thread's intent and no other thread's [TK-TIME-003].
- `ndtk_thread_spawn` MUST start a thread, name it, and set its intent in the new thread before `fn` runs [TK-TIME-004].
- The toolkit MUST NOT make any thread real-time: `NDTK_INTENT_AUDIO` without an admitted ticket runs as interactive, and only the service's stream threads are admitted [TK-TIME-005].
- `ndtk_sleep_until` MUST sleep until the deadline, never earlier, with the given leeway [TK-TIME-006].
- The toolkit MUST NOT offer affinity, priority or timer-resolution calls [TK-TIME-007].

Thread pools are libdispatch's; a queue's QoS corresponds to an intent as SC-INT-002's table gives it. The toolkit wraps neither.

| TK | Relies on |
|---|---|
| TK-TIME-001, TK-TIME-002 | SC-TIME-001, SC-TIME-002, SC-TIME-003, WP-EXT-006, AU-CONTRACT-002 |
| TK-TIME-003, TK-TIME-004 | SC-INT-001, SC-INT-002, SC-INT-006, SC-INT-007 |
| TK-TIME-005 | SC-INT-003, SC-RT-015, SC-RT-016 |
| TK-TIME-006 | SC-LIB-001, SC-LIB-002, SC-TIMER-003, SC-TIMER-005 |
| TK-TIME-007 | SC-PLACE-001, SC-RES-001 |

### 4.12 Files and I/O

- `ndtk_io_read` and `ndtk_io_write` MUST start the transfer off the loop thread and return a request handle; the completion MUST be delivered as exactly one `NDTK_EV_IO` to the submitting loop, through the loop's post-and-wake path [TK-IO-001]. When a kernel I/O queue exists (F-217), its completion source replaces the wake (SC-WAIT-001).
- The buffer of a request is lent to the toolkit from submission until its completion event is returned; the caller MUST NOT touch it meanwhile, and the toolkit MUST NOT touch it afterwards [TK-IO-002]. In Swift the `IOBuffer` is consumed by the request and returned once by `take(_:)`.
- `ndtk_io_cancel` MUST be best effort, and a cancelled request MUST still complete exactly once, with `NDTK_E_CANCELLED` when the transfer did not happen [TK-IO-003].
- `ndtk_known_path_get` MUST resolve a known folder from the process's namespace and environment at the time of the call, and MUST fail with `NDTK_E_UNSUPPORTED` when the namespace has no such folder [TK-IO-004].
- `ndtk_watch_path` MUST deliver changes of a local path as `NDTK_EV_PATH`, and MUST fail with `NDTK_E_UNSUPPORTED` for a path served by nd9p until the scheduling contract defines vnode notifications for it [TK-IO-005].
- The toolkit MUST NOT perform a file transfer on the loop thread, other than the bounded ctl and surface operations of §4.4 and §4.6 [TK-IO-006].

| TK | Relies on |
|---|---|
| TK-IO-001 | SC-USER-001 … SC-USER-004, SC-WAIT-001, SC-INT-002 |
| TK-IO-005 | SC-WAIT-001, SC-9P-002 (sized files), SC-SEC-005 |
| TK-IO-006 | WP-PERF-001 |
| TK-IO-002 … TK-IO-004 | (toolkit) |

### 4.13 Agent export and accessibility

One truth, many views (P14): the node table is what `/n/app` exports, what `/n/agent/APPID` serves, and what accessibility reads.

- The toolkit MUST serve the process's export itself, from the wait of the loop that created the first UI: the 9P server's descriptor is a source of that loop (TK-LOOP-001), requests are answered on the loop thread, and the node tables need no lock [TK-AGENT-001].
- The export MUST exist for every process that has a UI, without an application call, under an APPID taken from the program's manifest (or executable name) and registered as `agent.APPID` (agent-protocol.md §1); `ndtk_agent_export` MUST only change the APPID and version [TK-AGENT-002]. Accessibility therefore never depends on the application opting in; who can see the tree is decided by namespaces.
- `/n/app/APPID/ui/WINDOW` MUST serve each window's node table in the `ndtk_ui_export` format, and `/n/app/APPID/state` the same JSON as the agent `state` [TK-AGENT-003].
- The agent `state` MUST contain the application's own state (`ndtk_agent_state_set`) and a `ui` member listing, per window, each node's handle, parent, kind, class, accessible name, text, value, range, flags (focused, hidden, disabled) and frame in points, with a `seq` [TK-AGENT-004].
- The schema MUST list the UI verbs `press node=`, `set_value node= value=`, `set_text node= text=`, `focus node=` and `scroll node= dy=`, and the application's verbs from `ndtk_agent_verb_add` [TK-AGENT-005].
- A UI verb MUST be applied on the loop thread by synthesising the events a user would cause, delivered with `NDTK_EVF_AGENT`, so that `ndtk_ui_handle` reports it as it reports a user; a verb on a hidden or disabled node MUST fail; `if_seq` MUST be honoured [TK-AGENT-006].
- An application verb MUST be delivered as `NDTK_EV_AGENT` with the command line in `data`, and MUST be answered once with `ndtk_agent_reply`; a request still unanswered when its loop ends MUST be answered with an error by the toolkit [TK-AGENT-007].
- Node changes (created, destroyed, changed, focus) MUST be logged as deltas on the agent `log` while a reader has it open, and `ndtk_agent_log` MUST append application events [TK-AGENT-008].
- A node MUST be exported without its text when it is a password field or marked private, and its text MUST NOT appear in the log [TK-AGENT-009].
- The same export MUST be the accessibility tree: roles from node kinds, the name from `ndtk_node_set_label` or else the node's text, value and range, focus; there is no second tree [TK-AGENT-010].
- The export MUST add no per-frame work: state is produced when `state` is opened, and log lines only while `log` is open [TK-AGENT-011].

| TK | Relies on |
|---|---|
| TK-AGENT-001 | SC-WAIT-001, SC-9P-006 (the server side of readiness), agent-protocol.md §4 |
| TK-AGENT-002, TK-AGENT-004, TK-AGENT-005, TK-AGENT-007, TK-AGENT-008 | agent-protocol.md §1 (schema, state, actions, log; optimistic concurrency), namespaces-agents.md §3, §5 (no ids; §10 item 11) |
| TK-AGENT-003 | namespaces-agents.md §5 (`/n/app/*/state`), §7 (P5-06) |
| TK-AGENT-006 | agent-protocol.md §1 (`if_seq`) |
| TK-AGENT-009 | WP-SEC-004 |
| TK-AGENT-010, TK-AGENT-011 | (toolkit; P14) |

## 5. Versioning and capabilities

- Evolution MUST be additive: new calls, event kinds (appended), record fields (in reserved space or behind a larger `size`) and capability bits; a version 1 meaning is never changed, and removal is by deprecation only [TK-CAP-001].
- `ndtk_capabilities(loop)` MUST report exactly the features usable through that loop, derived from the window system's `caps`, `nd_sched_capabilities()` and the audio service's `au_query`; the reserved bits MUST read false in version 1 [TK-CAP-002].
- Records with a `size` field MUST accept any size at least the version 1 size and reject a smaller one with `NDTK_E_ARG`; the toolkit MUST write no byte beyond the caller's size; reserved fields MUST be zero on input (otherwise `NDTK_E_ARG`) and written as zero [TK-CAP-003].
- A call that needs an absent capability MUST fail with `NDTK_E_UNSUPPORTED` [TK-CAP-004].
- `ndtk_api_version()` MUST return the running library's `NDTK_API_VERSION` [TK-CAP-005].
- When P4-12's implementation copies `ndtk.h` and `NDToolkit.swift`, a test MUST compare each copy with this reference (spec-conventions.md §1) [TK-CAP-006].

| TK | Relies on |
|---|---|
| TK-CAP-001 | WP-CAP-001, SC-CAP-001, AU-VER-003 |
| TK-CAP-002 | WP-CAP-002, WP-CAP-003, WP-CAP-004, WP-CAP-007, SC-CAP-001, AU-VER-001, AU-VER-002 |
| TK-CAP-003 | SC-CAP-002, SC-CAP-003, AU-ABI-004, AU-ABI-005 |
| TK-CAP-004 | WP-CAP-006 |

## 6. Security and capabilities

The toolkit has no authority of its own: it reaches every resource through the process's namespace and the tokens its attaches carry (namespaces-agents.md §4).

| Path or token | Grants | Used by |
|---|---|---|
| `/n/desktop` (a view attached with the window, or the whole tree) | windows, outputs, keymap, gamepads, snarf, drag (WP §6) | §4.4–§4.6 |
| `/n/theme` | the theme | §4.9.3 |
| `/n/sys/audio` with `cap:audio:play`, `cap:audio:capture`, `cap:audio:monitor` | streams, voices, capture (AU §6) | §4.10 |
| `/n/sys/sched` | nothing is needed: intent and admission within budget are unprivileged (SC §6) | §4.11 |
| `/n/agent`, `/n/app` | registration of the export; who reads it is decided by the reader's namespace | §4.13 |
| `cap:wsys:input:background` | background gamepad input: never requested by the toolkit | §4.5 |

- The toolkit MUST NOT open a path outside the process's namespace, and MUST NOT ask a broker or daemon for authority the process lacks [TK-SEC-001].
- A missing namespace path or capability MUST fail the creating call with `NDTK_E_PERMISSION` and clear the matching capability bit, never crash [TK-SEC-002].
- The text of password fields MUST NOT leave the process except to the input method under purpose `password` [TK-SEC-003].
- Dropped paths and dialog results MUST be delivered as paths only; the toolkit MUST NOT open them on the application's behalf or widen its access [TK-SEC-004].
- Agent verbs MUST act only through the routes a user's input takes: they MUST NOT reach a node the user could not operate (hidden, disabled, or in a window the verb's caller cannot see) [TK-SEC-005].

| TK | Relies on |
|---|---|
| TK-SEC-001, TK-SEC-002 | WP-SEC-001, WP-CAP-006, AU-NS-002, AU-SEC-002, SC-SEC-001 |
| TK-SEC-003 | WP-SEC-004 |
| TK-SEC-004 | WP-DND-005, WP-SEC-005 |
| TK-SEC-005 | WP-SEC-002 (the same principle for pointer lock); agent-protocol.md §1 |

## 7. Performance contract

These are requirements, each tied to a test in §8. "Reference machine" is P4-03's reference configuration running NeoDarwin with wsys and audiod; host numbers from S7 are quoted for comparison only.

- **Calls to first sound.** The S7 minimal program, which reaches a visible window, a presented frame, input read and a sound started, MUST take no more than 13 toolkit call sites, counted with the S7 rule (`prototypes/README.md`), in Swift and in C [TK-PERF-001]. S7 measured 12 (Swift) and 10 (C); `NDToolkitMinimal.swift` is the Swift program against this interface, at 12, and the C program below is at 10.
- **Idle.** A program that neither presents, requests frames nor arms timers, with no input arriving, MUST cause 0 wakeups of its loop thread and 0 compositions over 60 s [TK-PERF-002]. S7 measured 0 wakeups/s on the host.
- **Pacing.** The game-loop program on the reference machine MUST reach a frame-pacing error p99 ≤ 1 ms over 10,000 frames with no timer-resolution call, its loop thread using < 2 % CPU while waiting [TK-PERF-003].
- **Wake overhead.** From `nd_wait` returning with one ready event to `ndtk_wait` returning it, the toolkit MUST add at most 50 µs at p99 [TK-PERF-004].
- **Editor.** The text-editor program MUST drop 0 frames at 120 Hz during a 10 s scroll of a 100,000-line document on the reference machine, with draw time p99 ≤ 4 ms at its window size [TK-PERF-005].
- **Live resize.** During a 2 s interactive resize, the text-editor program MUST present at each new configuration within 4 ms (p99) of the configuration's event, and its loop MUST never go longer than two refresh intervals without returning from a wait [TK-PERF-006].
- **Allocations.** In steady state, event decode, layout, paint and present MUST make 0 heap allocations per frame in the text-editor and synth-ui programs [TK-PERF-007]. S7 measured 0 in NDTKUI's own layout and paint.
- **Audio.** The synth-ui program on the reference machine MUST meet the audio service's targets unchanged with the UI animating at display rate: period and latency reported exactly, 0 underruns over the soak, no lock or allocation on the render path [TK-PERF-008].
- **First GPU frame.** From an open window, a GPU program MUST reach its first presented GPU frame with 3 toolkit calls (`ndtk_gpu_open`, `ndtk_gpu_begin_frame`, `ndtk_gpu_end_frame`) plus its own encoding [TK-PERF-009]. S7 needed 26 `webgpu.h` calls.
- **Zero copy.** The compute-display program MUST present every frame with `NDTK_FRAME_ZEROCOPY` set once the server supports GPU presents, with no copy between the compute pass and composition [TK-PERF-010].
- **One wait, no helpers.** The minimal program MUST run with one thread plus the audio service's stream threads, and every event source of the S7 self-test MUST arrive through one wait [TK-PERF-011].

**The C minimal program** (10 call sites to first sound):

```c
ndtk_loop l = ndtk_loop_create(NULL);                                        /* 1 */
ndtk_window w = ndtk_window_open(l, "minimal", 640, 360, 0);                 /* 2 */
ndtk_au_handle tone = ndtk_audio_buffer(l, samples, 24000, 48000, 1);        /* 3 */
ndtk_request_frame(w);                                                       /* 4 */
for (bool playing = false;;) {
    const ndtk_event *ev; int32_t n = ndtk_wait(l, NDTK_DEADLINE_NONE, 0, &ev); /* 5 */
    for (int32_t i = 0; i < n; i++) switch (ev[i].kind) {
    case NDTK_EV_CONFIGURE:
        if (ev[i].u.configure.visibility == NDTK_VIS_SHOWN && !playing)
            playing = ndtk_audio_play(l, tone, 1.0f, 0) != 0;                /* 6 */
        break;
    case NDTK_EV_FRAME: {
        ndtk_cpu_surface s;
        if (!ndtk_cpu_surface_acquire(w, &s)) break;                         /* 7 */
        ndtk_cpu_surface_fill(&s, 0xff000000u | (uint32_t)(ev[i].u.frame.target_ns >> 24 & 0xff), NULL); /* 8 */
        ndtk_present(w, &s, NULL, 0, 0);                                     /* 9 */
        ndtk_request_frame(w);                                               /* 10 */
        break; }
    case NDTK_EV_KEY_DOWN: printf("key %u\n", ev[i].u.key.keysym); break;
    case NDTK_EV_CLOSE: case NDTK_EV_QUIT: return 0;
    }
}
```

## 8. Conformance

Methods:
- **unit**: `bazel test //docs/desktop/...` on the host, or a host test of `libndtk` against fake wsys and audiod servers;
- **tkconform**: a toolkit conformance program run on NeoDarwin against a wsys started with `-t` (its `input` hook, WP-TEST-001), a scripted audiod, and the scheduling contract's kernel;
- **meas**: a measurement on the reference machine;
- **S7**: an S7 program ported from the host shim to this API (minimal in Swift and C, game-loop, text-editor, synth-ui, compute-display, `ndtk-selftest`), with its S7 metrics;
- **build**: a compile-time check in the implementation's build.

| Test | Requirements | Method | Pass criterion |
|---|---|---|---|
| TK-T-001 | TK-HDL-001, TK-HDL-002, TK-EV-001, TK-EV-006, TK-TIME-003, TK-CAP-005 | unit (`ndtk_layout_test`) | every record size and field offset of `ndtk.h` asserted in C23 `-Wpedantic`; every constant equal to its `desktop.h` or `nd_sched.h` counterpart; handle accessors round-trip; toolkit kinds in 0x40–0x7F |
| TK-T-002 | TK-EV-001 | unit (`ndtk_cxx`) | `ndtk.h` compiles as C++20 with `-Wall -Wextra -Werror -pedantic`, with the same layouts and the forward-declared types usable |
| TK-T-003 | TK-SW-001, TK-ERR-004, TK-T2-003 | unit (`ndtoolkit_interface`) | `NDToolkit.swift` and `NDToolkitMinimal.swift` type-check as Swift 6 with warnings as errors; creating calls are `throws(ToolkitError)`; T2 functions carry the annotations |
| TK-T-004 | (the text) | unit (`toolkit_conformance_test`) | every MUST has an id and a test; ids unique; every cited WP and AU id exists in its specification |
| TK-T-005 | TK-CAP-006, TK-SW-001 | build | the implementation's copies of `ndtk.h` and its `.swiftinterface` match this reference |
| TK-T-010 | TK-LOOP-001, TK-LOOP-002, TK-LOOP-003, TK-LOOP-006, TK-LOOP-007, TK-LOOP-010, TK-THR-006, TK-PERF-011 | S7 (`ndtk-selftest`) | on a loop on the first thread and one on a second thread, one wait returns a one-shot timer, a repeating timer, pipe readiness, a wsys record, a post, one wake for 100 triggers, an audio contract change and an I/O completion; one `nd_wait` per wait (DTrace); no toolkit thread exists beyond the audio stream thread |
| TK-T-011 | TK-LOOP-003, TK-LOOP-004, TK-LOOP-005, TK-THR-002 | unit | `DEADLINE_POLL` never blocks; a past deadline returns 0 at once; `EINTR` returns −1 `NDTK_E_INTR`; 1,000 ready records with capacity 256 arrive over four waits with none lost; a concurrent second wait fails `NDTK_E_BUSY` |
| TK-T-012 | TK-LOOP-006, TK-LOOP-007, TK-T2-002 | unit | 4 threads × 10,000 posts arrive in per-thread order; a full ring returns false `NDTK_E_LIMIT`; posts produce no `WAKE`; wake and post from a thread joined to a ticket never block (scheduler trace) |
| TK-T-013 | TK-LOOP-008, TK-LOOP-009, TK-LOOP-012 | tkconform | 10,000 timers fire at or after their deadlines (0 early); only `SC_DEADLINE_IDENT` is registered (kqueue dump); a 1 ms repeating timer stalled 10 ms delivers one event with `expirations` 10 and no drift over 60 s; no resolution call in a syscall audit |
| TK-T-014 | TK-LOOP-010, TK-LOOP-011 | unit | an fd left unread is reported at every wait, once per wait, with EOF at end; the loop's kqueue fd in another kqueue becomes readable with a ready source, and polling it consumes nothing |
| TK-T-015 | TK-LOOP-013, TK-FRM-002, TK-UI-008, TK-PERF-002 | meas | minimal and text-editor idle 60 s after activity: 0 wakeups of the loop thread (DTrace), 0 compositions (`stats`), caret visible and still |
| TK-T-016 | TK-LOOP-014, TK-LOOP-015, TK-EV-002 | tkconform | a window's events arrive in stream order; injected input to two windows is ordered by `time_ns`, which equals the injected `nsec`; after `CONFIGURE` is returned the acquired surface has its seq |
| TK-T-017 | TK-LOOP-016, TK-LOOP-017 | tkconform | with `AUTO_INTENT`, iconifying the only window makes the loop thread `background` at the next wait and showing it `interactive` (`nd_thread_intent_info`); destroying a loop closes its windows and stales every handle it owned |
| TK-T-018 | TK-DRV-001, TK-DRV-002, TK-DRV-003, TK-DRV-004, TK-DRV-005, TK-DRV-006 | S7 (minimal-callback, C and Swift) | init, then per wait all events then one iterate; `quit` exactly once with the result, also after a failing C `init`; Swift `init` throwing returns 1 without `quit`; QUIT and CLOSE reach `event` and do not end the run; with nothing requested the program sleeps; the driver's source uses only `ndtk.h` |
| TK-T-020 | TK-EV-003, TK-EV-004, TK-EV-005, TK-EV-009, TK-EV-013 | tkconform | `seq` gap-free per loop; loop-level events have window 0; a 6,000-byte commit arrives as one `TEXT` of whole characters; `DE_QUIT` reaches every loop with a window and the process keeps running |
| TK-T-021 | TK-EV-007, TK-EV-008, TK-EV-011, TK-EV-014 | tkconform | every stream is in the extended format (server trace); an injected record of an unknown type 47+ is skipped; 100 unread motions reach the application as the server coalesced them and nothing else is merged; internal tags never surface as `ACK` |
| TK-T-022 | TK-EV-010 | tkconform | 5,000 unread records: the next event carries `LAGGING`; after a server discard the first event of the window is a `CONFIGURE` equal to `config` |
| TK-T-023 | TK-EV-012, TK-T2-001, TK-PERF-007 | meas | allocation hook over 10,000 waits with mixed events and over the editor and synth UIs at display rate: 0 heap allocations in decode, layout, paint and present |
| TK-T-024 | TK-T2-003, TK-T2-004 | build | T2 modules build with the performance diagnostics; a variant with an `Array.append` in `UI.handle` and one in `Events.subscript` fail to compile |
| TK-T-025 | TK-HDL-003, TK-HDL-004, TK-HDL-005 | unit | every call with 0, a stale, a foreign-kind and an AU handle returns its failure value with `NDTK_E_HANDLE`, changes nothing and counts; a slot reused after destroy has a new generation; handles work from any thread |
| TK-T-026 | TK-ERR-001, TK-ERR-002, TK-ERR-003 | unit | each failure sets code and a non-empty detail; success clears them; a refused ctl write's detail contains the server's error string; an admission refusal's detail contains `nd_sched_error_detail()`'s text |
| TK-T-027 | TK-ERR-005, TK-ERR-006, TK-ERR-007, TK-GPU-007 | tkconform | killing wsys, killing audiod, removing the audio device and losing the GPU device each produce events and failing calls (`NDTK_E_SERVICE`, `_DEVICE`, `_GPU`) while the process keeps running |
| TK-T-028 | TK-MEM-001, TK-MEM-002, TK-MEM-003, TK-MEM-004 | unit | the arena is reset per wait and never shrinks; event data stays intact until the next wait (canary); an oversized frame spills, completes, counts one spill and the arena has grown at the next reset |
| TK-T-029 | TK-MEM-005, TK-MEM-006 | unit | caller buffers overwritten after each call leave titles, texts, menus and JSON unchanged; with a hook set first every table and arena allocation goes through it; set later it fails `NDTK_E_STATE` |
| TK-T-030 | TK-THR-001, TK-THR-003, TK-THR-004, TK-THR-005 | tkconform | windows opened and driven from a background loop; 4 threads issuing requests to one window make progress with no toolkit lock (lock profiler); a UI call from another thread fails `NDTK_E_STATE`; a window resize drag, bar menu, drag and open dialog leave every call's latency unchanged |
| TK-T-031 | TK-WIN-001, TK-WIN-002, TK-WIN-003 | tkconform | open gives a shown window with `buffer device` (`buffer logical` with `NDTK_WIN_LOGICAL_BUFFER`), extended stream and the first `CONFIGURE` in the next wait; without `/n/desktop` → `NDTK_E_PERMISSION`; wsys absent → `_SERVICE`; version 1 server → `_UNSUPPORTED`; sizes are points at scale 2 |
| TK-T-032 | TK-WIN-004, TK-WIN-005, TK-WIN-006 | tkconform | close stales the window's surface, UI, nodes and GPU handles; a close gadget click yields `CLOSE` 0 and the window stays; Kill yields `CLOSE` 1 and a stale handle; attach moves later events to the other loop |
| TK-T-033 | TK-WIN-007, TK-WIN-008, TK-WIN-009 | tkconform | each op of the table produces its message (server trace) and its result is visible in `desc` when the call returns; `resize a b`-class errors map to `NDTK_E_ARG`; with `out_tag`, one `ACK` after the request's events; raw `mask …` refused |
| TK-T-034 | TK-WIN-010 | tkconform | a popup opens unmapped, maps on `POPUP` at the anchor, an outside click yields `POPUP_DONE` and the popup can be placed again |
| TK-T-035 | TK-WIN-011, TK-WIN-012 | tkconform | `input output add` appears in `ndtk_outputs` with its name and an `OUTPUT` event in every loop with a window; moving a window to a 180/120 output changes only the configuration's scale |
| TK-T-036 | TK-WIN-013, TK-WIN-014, TK-WIN-015, TK-SEC-004 | tkconform | clipboard round-trips; `drag` returns at once and 0 paths cancels; a drop delivers paths from the record, and from `drop` when too long; the toolkit opens none of them (fs trace) |
| TK-T-037 | TK-WIN-016, TK-SEC-004 | tkconform | an open dialog answers once with accepted paths or cancellation; the loop runs meanwhile; the dialog lists only the namespace |
| TK-T-040 | TK-INP-001, TK-INP-002, TK-INP-003, TK-INP-016 | tkconform | injected keys carry HID usage, rune, keysym, base, mods after the event; repeat comes only from the server; `KEY_REPEAT` off stops it; a focus change mid-press gives a `CANCEL` release; a layout switch gives `KEYMAP` |
| TK-T-041 | TK-INP-004, TK-INP-005, TK-INP-006 | tkconform | with text input off no `TEXT` exists; `ndtk_text_input_set` twice with the same state writes nothing the second time; preedit, commit and deletion arrive in order; the adapter applies a stale-serial commit and drops the stale deletion |
| TK-T-042 | TK-INP-007, TK-INP-008, TK-INP-012 | tkconform | a pen sample gives one pointer event with pressure and tilt in `ndtk.h` units; a record changing two buttons gives two events; no emulated stream; proximity events; with a 256×256 viewport the pointer stays in window points |
| TK-T-043 | TK-INP-009, TK-INP-010, TK-INP-011 | tkconform | trackpad and wheel give `SCROLL` only; `RELATIVE` arrives while locked and not otherwise; lock without focus fails `NDTK_E_NOFOCUS`; focus loss gives `POINTER_CONSTRAINT` with its reason |
| TK-T-044 | TK-INP-013, TK-INP-014, TK-INP-015 | tkconform | `input pad` add, buttons and axes give `GAMEPAD` events with normalised values to the watching loop only; the state matches without a system call; removal stales the handle; `rumble` on a pad without it fails `NDTK_E_UNSUPPORTED`; `background` is never written |
| TK-T-050 | TK-FRM-001, TK-FRM-003, TK-FRM-004, TK-FRM-005, TK-FRM-006 | tkconform | three `request_frame`s before a tick write one `wantframe`, before the next `nd_wait`, and none on the calling thread (syscall trace); `FRAME` fields equal `Deskframe`; a hidden window gets throttled frames; at latency 1 a second present fails `NDTK_E_BUSY` at once; `ESTIMATED` is never set |
| TK-T-051 | TK-FRM-007, TK-FRM-008, TK-FRM-009, TK-FRM-010, TK-FRM-011 | tkconform | after a resize, acquire returns the new buffer with its seq; a surface of the old seq is accepted and shown padded; present returns in < 1 ms with the output stalled; `age` is 0 then 1; a released surface shows nothing; CPU frames carry `HWTIME` or `COMPOSITED` |
| TK-T-052 | TK-FRM-012, TK-FRM-013 | tkconform | a fixed 256×256 buffer survives resizes; `viewport` with `nearest` scales it; `ALPHA` windows get `r8g8b8a8` and `DF_ALPHA` |
| TK-T-053 | TK-DRAW-001, TK-DRAW-002, TK-DRAW-003, TK-DRAW-006, TK-DRAW-007, TK-DRAW-008 | unit | golden images at scales 1, 1.5 and 2 for fills, strokes, polylines, clips and images; pixel-aligned fills have no partial pixels; `damage_out` equals the touched bounds; no system call during canvas calls (syscall trace) |
| TK-T-054 | TK-DRAW-004, TK-DRAW-005, TK-STYLE-003 | unit | swapping the theme palette changes every widget pixel and no application-drawn pixel; a frame of cached glyphs makes 0 allocations |
| TK-T-055 | TK-GPU-001, TK-GPU-002, TK-GPU-003, TK-GPU-004, TK-GPU-005, TK-GPU-006, TK-PERF-009 | S7 (game-loop, compute-display) | the handle bag carries the window's id and surface port; `ndtk_gpu_open` returns a configured device, queue and surface or nothing (failure injected at each step leaves no object); a resize reconfigures in `begin_frame` with no error; presents carry their config seq; a second end fails `NDTK_E_STATE`; first GPU frame after 3 calls |
| TK-T-056 | TK-GPU-008, TK-GPU-009, TK-GPU-010, TK-PERF-010 | S7 (compute-display) + unit | with `surface.gpu`, every frame has `ZEROCOPY` and no toolkit copy (GPU trace); `ndtk_gpu_budget_get` fails `NDTK_E_UNSUPPORTED` with the bit clear; no toolkit 3D call exists |
| TK-T-060 | TK-PERF-001, TK-AUD-013 | S7 (minimal, Swift and C) | call sites counted by the S7 rule: Swift ≤ 13 (12 in `NDToolkitMinimal.swift`), C ≤ 13 (10 in §7); the tone plays |
| TK-T-061 | TK-PERF-003 | S7 (game-loop on wsys) | pacing error p99 ≤ 1 ms over 10,000 frames; no resolution call; loop CPU < 2 % while waiting |
| TK-T-062 | TK-PERF-004 | meas | `nd_wait` return to `ndtk_wait` return p99 ≤ 50 µs over 100,000 single-event wakes |
| TK-T-063 | TK-PERF-005, TK-TEXT-006 | S7 (text-editor) | 10 s autoscroll of 100,000 lines at 120 Hz: 0 dropped frames; draw p99 ≤ 4 ms; per-frame work independent of document length |
| TK-T-064 | TK-PERF-006, TK-UI-012 | S7 (text-editor, drag test) | 2 s resize: each new configuration presented within 4 ms p99, tagged with its seq; no wait gap over two refresh intervals |
| TK-T-065 | TK-PERF-008, TK-AUD-005, TK-AUD-006 | S7 (synth-ui) | the AU performance soak (AU-T-061) passes with the UI animating; the C render runs with no Swift frame and the Swift renderer with one thunk (stack samples); no toolkit frame between doorbell and render |
| TK-T-070 | TK-UI-001, TK-UI-002, TK-UI-003, TK-UI-004 | unit | a one-expression tree builds; `bind` returns handles; moving a parented node reparents it; destroying a subtree stales every handle in it; no colour, font or size setter exists (API audit) |
| TK-T-071 | TK-UI-005, TK-UI-006, TK-UI-007 | unit | scripted events give `clicked` for a press and release inside a button and none for a release outside; checkbox toggles; slider drags and arrow keys clamp; Tab order is tree order; another window's events are `ignored`; no callback into the app |
| TK-T-072 | TK-UI-008, TK-UI-009, TK-UI-010, TK-UI-011, TK-UI-013 | unit | a state change requests one frame; `draw` with nothing changed returns false and presents nothing; damage is the changed nodes' union and full at age 0; canvas draws are clipped to their node; stats report presents, skips, arena high-water and spills |
| TK-T-073 | TK-UI-014, TK-AGENT-003 | unit | `ndtk_ui_export` lists every node with its fields; `/n/app/APPID/ui/WINDOW` serves the same text |
| TK-T-074 | TK-LAYOUT-001, TK-LAYOUT-002, TK-LAYOUT-003, TK-LAYOUT-004 | unit | golden layouts for rows, columns, stacks and flex distribution at scales 1, 1.25, 2; wrapping labels get height for width; every edge on a device pixel; layout runs only on the listed changes (counter) |
| TK-T-075 | TK-STYLE-001, TK-STYLE-002 | tkconform | `label.param` falls back to `label` then `default`; a `/n/theme` write gives `THEME` and a full repaint with the node table unchanged; fonts follow a scale change |
| TK-T-076 | TK-TEXT-001, TK-TEXT-002, TK-TEXT-003 | unit | shaping goldens for Latin with ligatures, combining marks, Arabic, Devanagari, Hebrew mixed with Latin, and emoji ZWJ sequences; caret steps and deletes whole clusters; UAX #14 breaks; offsets are UTF-8 bytes |
| TK-T-077 | TK-TEXT-004, TK-TEXT-005, TK-TEXT-007 | S7 (text-editor) + tkconform | with the test input method, Japanese and Pinyin compositions show inline and commit with no app IME code; the candidate rect follows the caret; focus loss disables text input; a password field sets purpose `password` and exports no text; text is inserted only from `TEXT` |
| TK-T-080 | TK-AUD-001, TK-AUD-002, TK-AUD-003 | unit (fake audiod) | one session per process; no `/n/sys/audio` gives `NDTK_E_PERMISSION`; contract, underrun, device and voice-end events reach the right loops in service order with no toolkit thread; the contract read after the event has its generation |
| TK-T-081 | TK-AUD-004, TK-AUD-012 | unit (fake SC refusing) | open fails `NDTK_E_ADMISSION` with SC's reason verbatim and the refusal in Swift; no callback ever runs; capture without the capability fails `NDTK_E_PERMISSION` |
| TK-T-082 | TK-AUD-007, TK-AUD-008, TK-AUD-009, TK-AUD-010, TK-AUD-011 | unit + S7 (game-loop) | a doorbell port watched in the loop wakes it; play makes 0 system calls; releasing a playing buffer lets it finish; the contract equals `au_stream_contract`; a default-device switch keeps the handle and yields only an event; join fails `NDTK_E_UNSUPPORTED` without `AU_CAP_RT_GROUP` |
| TK-T-090 | TK-TIME-001, TK-TIME-002, TK-TIME-006 | unit | `ndtk_now` equals `nd_sched_now_ns` within one call's time; a frame `target_ns` passed to `ndtk_wait` needs no conversion; `sleep_until` never returns early in 10,000 trials |
| TK-T-091 | TK-TIME-004, TK-TIME-005, TK-TIME-007 | tkconform | a spawned thread reports its intent and name before `fn` runs; an `AUDIO` thread without a ticket is not real-time; no affinity, priority or resolution symbol is exported (symbol audit) |
| TK-T-092 | TK-IO-001, TK-IO-002, TK-IO-003, TK-IO-006 | unit | 1,000 reads complete once each in the submitting loop; the loop thread performs no file transfer (syscall trace); a cancelled request completes once with `NDTK_E_CANCELLED` or its bytes; the Swift buffer returns once through `take` |
| TK-T-093 | TK-IO-004, TK-IO-005 | tkconform | known folders resolve from the namespace, absent ones fail `NDTK_E_UNSUPPORTED`; a local file change gives `PATH`; an nd9p path fails `NDTK_E_UNSUPPORTED` |
| TK-T-100 | TK-AGENT-001, TK-AGENT-002, TK-AGENT-011 | tkconform | a UI program with no agent call appears in `/n/agent/index` under its manifest id; `state` is served from the loop thread with no toolkit thread; a frame with no reader open does no export work (profile) |
| TK-T-101 | TK-AGENT-004, TK-AGENT-005, TK-AGENT-006, TK-AGENT-007 | tkconform | `state` lists every node with the fields; `press` on a button gives `clicked` through `ndtk_ui_handle` with `NDTK_EVF_AGENT`; a hidden node's `press` fails; a stale `if_seq` is refused; an app verb arrives as `AGENT` and its reply is the one the agent reads; an unanswered verb is answered with an error when the loop ends |
| TK-T-102 | TK-AGENT-008, TK-AGENT-009, TK-AGENT-010, TK-SEC-003, TK-SEC-005 | tkconform | the log shows node deltas only while open; a password field's text is absent from `state`, `ui` and the log; the accessibility client reads roles, names, values and focus from the same export; a verb cannot reach a window outside the caller's namespace |
| TK-T-103 | TK-CAP-001, TK-CAP-002, TK-CAP-003, TK-CAP-004 | unit + tkconform | the bits match `caps`, `nd_sched_capabilities` and `au_query` of a fake server set; reserved bits are false; a record of size 8 fails `NDTK_E_ARG`; a larger size is accepted and bytes past it untouched; non-zero reserved fields fail; a call needing an absent capability fails `NDTK_E_UNSUPPORTED` |
| TK-T-104 | TK-SEC-001, TK-SEC-002 | tkconform | in a namespace without `/n/desktop`, `/n/sys/audio` or `/n/agent`, each creating call fails `NDTK_E_PERMISSION` and the bit is clear; an fs trace shows no access outside the namespace |
| TK-T-105 | TK-HDL-006 | tkconform | the handles in `/n/app` and in agent replies equal the handles the program holds |

Every MUST in §4–§7 is covered by at least one test above; `toolkit_conformance_test.sh` checks this mechanically.

## 9. Rationale and evidence

**Why this shape at all.** Q2 found that every layer programmers use converges on one fixed event record with a window id, a loop with wait and poll and a deadline, a coalesced redraw request paced by the display, resize and scale as events, a handle bag for the GPU, index-and-generation handles, a per-frame arena, two-tier audio and theme-owned style (q2-shapes.md, short answer; R1–R11). S7 then wrote five programs three ways each and found the candidate API at or under the heritage bar with nothing but a 1,100-line host shim to hide macOS (s7-prototypes.md §1, §2). This document turns that validated candidate into requirements, replacing each shim workaround with the lower-layer requirement that removes it.

**One wait, one kqueue** (P1, R2; F-201, F-203). The shim built the wait from CFRunLoop parts in about 200 lines (W11) and a stack-switching fiber to escape AppKit's live-resize loop (W1, 107 lines, high risk). On NeoDarwin the wait is `nd_wait` over nd9p readiness, `EVFILT_USER` and one deadline knote (SC §4.1–4.5), so the toolkit keeps only the table. Registering every toolkit timer on the one deadline knote (TK-LOOP-008) is what makes the S7 self-test's "one wait" literal: one system call per wait, with the scheduling contract's precision (SC-PERF-001) and no resolution setting anywhere (SC-RES-001).

**Callback driver as a thin layer** (R3). SDL3's callbacks exist to survive platform-owned loops; NeoDarwin has none, so the application-owned loop is the core and the driver is a convenience whose only job is to make SDL3's NeoDarwin backend map 1:1. TK-DRV-006 keeps it honest: it may use nothing a program cannot.

**Handles and the two memories** (P5, P6, R8, R9). sokol, Godot, bevy and gpui converge on index + generation; the same layout as the audio service's handles means one decoder and one rule (TK-HDL-002 cites AU-ABI-001). S7's NDTKUI showed the two-memory split works: 1.5 KB arena high-water for the editor, 0 allocations in layout and paint (SHIM-NOTES, UI wave).

**Errors** (P12, R12). Typed throws for creation and diagnosed no-ops for calls on handles is the S7 split (API.md changelog item 10, `openWindow` did not throw because the host had no recoverable failure); on NeoDarwin a missing `/n/desktop` or an absent wsys is recoverable, so opening throws. `AudioError` of the prototype is folded into one `ToolkitError` that carries the admission refusal, because the refusal record is SC's and belongs wherever admission can fail.

**Frames** (P3, P4, P9; F-101, F-102, F-205, F-208, F-209). Every frame field is `Deskframe`'s, unchanged (TK-FRM-003), and the prototype's `presentedEstimated` survives only as a host-shim bit that NeoDarwin never sets (TK-FRM-006): the window protocol forbids passing an estimate off as actual (WP-FRAME-009), which is API.md changelog item 1 made a server guarantee. Copy mode by default gives `age` 1 without the buffer-count question WP leaves open (WP §10 item 7). Surfaces are tagged with their configuration, so a resize is never an error (R6): S7 measured 88 configures and 88 presents at the new size with the loop never blocked.

**GPU: one helper, raw path kept** (charter §6; s7-prototypes.md §3 item 1). Twenty-six `webgpu.h` calls to the first GPU frame cost more than the whole SDL3 program; three toolkit calls after the window (TK-PERF-009) put GPU programs back at the heritage bar, while engines keep the handle bag. Reconfiguring inside `begin_frame` removes wgpu's `Outdated` path (R6). `STORAGE_BINDING` on the surface is the compute-to-display zero-copy case S7 proved once a 7-line wgpu-native bug is fixed; the helper uses Dawn, which the charter names.

**UI** (P6–P8, P14; R9, R11). The S7 editor was 44 lines against 365 for SDL3 and 367 native, with the IME handled entirely by the adapter; that is the evidence for retained, class-styled widgets and for putting the adapter in the text view (TK-TEXT-004). Shaping is required (TK-TEXT-001) because S7's one-glyph-per-scalar drawing is acceptable for a monospace editor and wrong for a general text field (s7-prototypes.md §3 item 6). The caret does not blink because a blink timer is exactly the idle wakeup P9 and TK-PERF-002 forbid.

**Audio over AU, not beside it** (R10; F-215, F-216). The toolkit's value is loop integration and a Swift surface; the stream thread, its admission, the ring, the contract and the render rules are the service's, so the toolkit passes AU handles and records through and cites AU. The C trampoline (AU-T2-004) and the checked `AudioRenderer` protocol are the two ways S7 W8 found to keep a render compiler-checked or honest.

**Agent export in the loop** (P14; agent-protocol.md). Serving the export from the loop thread is what lets the node table be read without a lock and without a helper thread; producing state only when `state` is opened keeps it off the frame path. Exporting without an opt-in makes accessibility unconditional; namespaces, not the application, decide who can read it.

**The Swift fallbacks** (§4.0.6). The pinned compiler rejects a `MutableRawSpan` property without an experimental feature, and cannot store a `Span` in an enum; S7 hit the second (W14) and used the first only behind `Lifetimes`. The closure forms cost nothing at run time and keep every client's build free of experimental flags.

## 10. Open issues

1. **Immediate builder** (charter P8, §11 question 2). Deferred until after the retained core; S7's editor reached 44 lines without it. `NDTK_CAP_IMMEDIATE` is reserved; the builder will rebuild a subtree into the frame arena keyed by stable ids and diff it into the node table.
2. **Wayland extensions** (charter §11 question 3). Out of scope here: the front door (P4-04) translates Wayland into wsys and never into this API.
3. **SC text not exported to Bazel.** `docs/kernel/BUILD.bazel` does not `exports_files(["scheduling-contract.md"])`, so `toolkit_conformance_test` checks WP and AU citations but not SC ones. The SC ids cited here were checked by running the same script by hand. Proposal against P4-16: export the file; the test then adds `SC=`.
4. **Named cursors** (WP). The window protocol has only the `cursor` file (cursor(6) images) and no requirement id for it; toolkits need named system cursors (text, resize edges, pointer) drawn by the theme. Proposal against window-protocol.md.
5. **Window attention** (WP). WIN.window.attention has no message; `NDTK_CAP_ATTENTION` is reserved. Proposal against window-protocol.md.
6. **GPU interop** (P7). The Vulkan WSI extension and `webgpu.h` surface-source names for the handle bag, the GPU buffer object (F-107, `NDTK_CAP_GPU_BUFFER`) and the per-task budget with a pressure signal (F-109, `ndtk_gpu_budget_get`) are P7's. Until then GPU presents depend on `surface.gpu` (WP-BUF-008).
7. **Alpha in `r8g8b8a8`** (WP). The window protocol does not say whether `DF_ALPHA` content is premultiplied; the toolkit assumes premultiplied. Proposal against window-protocol.md §4.9.
8. **Flip mode and buffer age** (WP §10 item 7). The toolkit uses copy mode only (TK-FRM-009) until WP specifies the buffer count behind flip mode and `latency N`.
9. **Touch and gestures, clipboard types** (WP §10 item 10). Not in window protocol version 2; `NDTK_CAP_TOUCH` and `NDTK_CAP_CLIPBOARD_TYPES` are reserved. The clipboard is UTF-8 text only (`snarf`).
10. **Widget classes in the theme** (theme-engine.md). The theme format has chrome sections but no widget classes (`label`, `button`, `slider`, …) and no requirement ids; TK-STYLE needs a widget vocabulary with padding, gap, radius and minimum sizes. Proposal against theme-engine.md (P4-06).
11. **Agent protocol and `/n/app`.** agent-protocol.md is version 1 without requirement ids, and nothing but namespaces-agents.md §5 names `/n/app`. TK-AGENT-003 fixes `/n/app/APPID/ui/WINDOW` and `/n/app/APPID/state` provisionally; the layout belongs in agent-protocol.md or namespaces-agents.md (P5-06), and this document will then cite it.
12. **Watching nd9p paths** (SC). The scheduling contract defines readiness of nd9p files but not vnode notifications (`EVFILT_VNODE`) for them, so `ndtk_watch_path` refuses `/n` paths (TK-IO-005). Proposal against scheduling-contract.md §4.2.
13. **Asynchronous I/O queue** (SC §1, F-217). Completions go through the post-and-wake path until a kernel I/O queue exists; SC-WAIT-001 already requires its completion source to join the one wait.
14. **Audio helper groups** (AU §10 item 1). AU-STREAM-010 still returns `AU_ERR_UNSUPPORTED` pending reconciliation with SC's tickets (SC-RT-009); `ndtk_audio_join` follows it.
15. **Known folders.** No specification says where home, configuration, cache and data live in a NeoDarwin namespace; TK-IO-004 resolves them at run time and fails when absent. Proposal to namespaces-agents.md or the packaging specification.
16. **File chooser beyond the namespace.** The v1 dialog is toolkit-drawn and sees only the application's namespace (TK-WIN-016). A system chooser that grants access to a file the application cannot yet see (a powerbox) needs a protocol with P4-09 and `keyd`.
17. **Shaping engine.** TK-TEXT-001 requires HarfBuzz-class shaping; whether it is vendored HarfBuzz (C++, allowed as upstream code, language-policy.md §1) or part of the Typeface stack (P4-14) is P4-12's decision.
18. **Swift lifetimes.** When lifetime dependencies are stable in the pinned toolchain, add `CPUSurface.pixels`, a `~Escapable` `Events` and `Span` payloads beside the §4.0.6 forms (additive, P13).

## 11. Changelog

| Version | Date | Change |
|---|---|---|
| 1 (draft) | 2026-09-28 | First normative draft for P4-12, from the charter and the S7 candidate API. Changes from `prototypes/ndtk/API.md`: handles take the audio service's layout (kind, 24-bit generation, index); the event record grows from 72 to 96 bytes with a `data` span, `flags` and 21 more kinds (ack, delete-surrounding, focus, enter, leave, state, menu, drop, popup-done, constraint, proximity, keymap, output, theme, io, dialog, agent, expose, move, path, gpu); `WHEEL` becomes `SCROLL` with the window protocol's fields; key events carry `base`, `rune` and `layout` and no text (text only from `TEXT`); frame flags take the window protocol's values, and `presentedEstimated` moves to a host-only bit; `openWindow` throws; one `ToolkitError` replaces `AudioError`; `Loop` becomes a `~Copyable` owner with a `Sendable` `LoopWaker`; `CPUSurface.pixels` becomes `withPixels`; audio streams, buffers and voices are AU handles and the C render has `AUrender_fn`'s type; added the one-call GPU helper, the handle bag, requests with tags, popups, hit regions, outputs, dialogs, the canvas, text shaping, text field, checkbox, stack, scroll, image and canvas nodes, asynchronous I/O, and the agent and accessibility export. |
