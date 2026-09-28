<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- Provenance: distilled from the plan-neo pilot (/Users/jkane/Development/c/plan-neo, commit b0a35a1, 2026-09-22), where this design was first written and its reference implementation built and tested. This is NeoDarwin's normative text. Revised to version 2 by epic P4-17. -->

# The NeoDarwin window protocol: `wsys` over 9P

| | |
|---|---|
| **Status** | draft |
| **Version** | 2 |
| **Epic** | P4-17 (window protocol revision 2); consumed by P4-03 `wsys`, P4-02 `inputd`, P4-04 Wayland front door, P4-12 toolkit |
| **Evidence** | study repository `../NeoDarwin-api-study` at commit `77e9b08`: `friction/F-101-*.md`, `F-102-*`, `F-202-*`, `F-205-*` … `F-214-*`; `reports/q2-shapes.md` §7 (R1, R5–R7); `reports/s7-prototypes.md`; `prototypes/ndtk/API.md`, `prototypes/ndtk/SHIM-NOTES.md`. Pilot: `plan-neo` commit `b0a35a1`, `cmd/desk/`, `cmd/deskconform.c` |
| **Supersedes** | version 1 of this document (the pilot text, 2026-09-22). Version 1 behaviour remains valid unchanged: every version 1 requirement below keeps its meaning, and a version 1 client sees exactly version 1 traffic |
| **Header** | [`desktop.h`](desktop.h), `DESK_API_VERSION 2`, C23 and C++20; layouts checked by [`desktop_layout.c`](desktop_layout.c) and [`desktop_cxx.cpp`](desktop_cxx.cpp) through [`BUILD.bazel`](BUILD.bazel) |

This document fixes the wire protocol between `wsys` and its clients: the file tree, the ctl vocabularies, the binary records and their layouts, the semantics of the event stream (when it is readable, ordering, coalescing, overflow), presentation and its timing feedback, window configuration and scale, decorations, popups, and the input channels (keys and keymap, text input and IME, pointer with pen fields, relative pointer and lock, gamepads). It deliberately leaves open: how a client waits for an events file to become readable, and the clock, which are the kernel scheduling contract's (`docs/kernel/scheduling-contract.md`, "SC"; this document cites its requirement ids); the transport of GPU buffer objects into a window, which is P7-06's; the toolkit API over this protocol, which is P4-12's; the compositor's internal design (graphics-desktop.md §2); and window-manager policy such as placement, binds and rules (`ui-configuration.md`).

## Contents

0. Status (above)
1. [Scope and non-goals](#1-scope-and-non-goals)
2. [Terms](#2-terms)
3. [Model](#3-model)
4. [Interfaces](#4-interfaces)
   - 4.1 [Namespace and attach](#41-namespace-and-attach) · 4.2 [Desktop ctl](#42-desktop-ctl) · 4.3 [Window ctl](#43-window-ctl) · 4.4 [Completion, tags and errors](#44-completion-tags-and-errors)
   - 4.5 [The event stream](#45-the-event-stream) · 4.6 [The mask](#46-the-mask) · 4.7 [Extended records](#47-extended-records)
   - 4.8 [Configuration](#48-configuration) · 4.9 [Painting and presenting](#49-painting-and-presenting) · 4.10 [The frame clock and presentation feedback](#410-the-frame-clock-and-presentation-feedback) · 4.11 [Buffer size and viewport](#411-buffer-size-and-viewport)
   - 4.12 [Frame, gadgets and decorations](#412-frame-gadgets-and-decorations) · 4.13 [Menus](#413-menus) · 4.14 [Drag and drop, snarf](#414-drag-and-drop-snarf) · 4.15 [Popups](#415-popups)
   - 4.16 [Keyboard and keymap](#416-keyboard-and-keymap) · 4.17 [Text input and IME](#417-text-input-and-ime) · 4.18 [Pointer, pen, scrolling, relative motion and lock](#418-pointer-pen-scrolling-relative-motion-and-lock) · 4.19 [Gamepads](#419-gamepads) · 4.20 [Outputs](#420-outputs)
   - 4.21 [Record layouts](#421-record-layouts) · 4.22 [No global lock and no modal loop](#422-no-global-lock-and-no-modal-loop) · 4.23 [Compatibility with rio](#423-compatibility-with-rio) · 4.24 [The test hook](#424-the-test-hook)
5. [Versioning and capabilities](#5-versioning-and-capabilities)
6. [Security and capabilities](#6-security-and-capabilities)
7. [Performance contract](#7-performance-contract)
8. [Conformance](#8-conformance)
9. [Rationale and evidence](#9-rationale-and-evidence)
10. [Open issues](#10-open-issues)
11. [Changelog](#11-changelog)

**Reading the requirements.** MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as in RFC 2119 and RFC 8174, and are normative only in capitals. Every normative sentence ends with its requirement id, `[WP-AREA-NNN]`. Requirements introduced by version 2 are in blocks headed **Version 2**; everything outside those blocks is version 1 and has the meaning version 1 gave it. Plain sentences without an id are explanation.

## 1. Scope and non-goals

An Intuition- and classic Mac-style desktop in the lineage of Plan 9's `rio`: overlapping windows with title bars and close, depth, zoom and sizing gadgets, a menu bar at the top of the screen, iconified windows on a Workbench-style desktop, and workspaces modelled on Intuition screens. Every interaction remains a read or write on a synthetic 9P file tree, so a client on another machine works exactly like a local one.

> Lineage from rio: the server is an ordinary user process serving 9P, posted in the `nsd` registry (`/n/sys/srv`) and mounted per window with an attach specifier; the local fast path is a shared-memory `surface` (§4.9); `mouse(3)` button encoding and `keyboard.h` runes are reused; the snarf buffer is global. What changes: window decorations, menus, depth control and a proper event stream are the server's job, not the client's, and remote clients can paint through 9P without `/dev/draw`.

**Design rules.** These carry over from version 1 unchanged:

- **One tree, three vocabularies.** Control is text lines on `ctl` files, like `wctl`. State and events are fixed-layout little-endian binary records, so C clients read straight into structs. Pixels are raw rows in an `image` file addressed by offset, so `pwrite` is the whole drawing API.
- **The server composes, the client paints.** A window's content is a buffer the client owns; the server retains a copy, draws the frame around it, composites the stack, and never asks the client to repaint because of occlusion. Expose events exist only for clients that opt out of retention to save memory.
- **Everything the user can do with a gadget, a program can do with a ctl message**, with the same name: `raise`, `lower`, `zoom`, `hide`, `snap`, `close`. This is also what makes every ctl message bindable to a key (`ui-configuration.md` §3.1).
- **Menus are data, not drawing.** A client describes its menus in a text file; the server renders them in the menu bar in the current theme and reports selections as events.
- **Windows are files, so windows are scoped by namespace.** A process sees the window it was started in as `/n/desktop/self`. It only sees other windows if its namespace was built to show them.

Version 2 adds three rules, which every section below applies:

- **Nothing in the protocol waits for the user, and nothing in the protocol stops the application.** There is no modal loop and no global lock anywhere (§4.22). Gestures, menus, popups and drags are run by the server while the client's event stream keeps flowing.
- **The frame clock belongs to the server.** Clients request frames and are told when their frames were actually shown (§4.10).
- **A resize is never an error.** Size, scale and buffer arrive together as one sequenced configuration, and presents say which configuration they were drawn for (§4.8).

**Non-goals.**
- A widget toolkit: sub-windows and widgets inside a client's window are the client's (P4-12 builds the toolkit on this protocol).
- Automatic tiling and layout policy: `ui-configuration.md` §3.4 draws that line, and this protocol only offers requests the user or a bind makes.
- How a client waits for readiness, how timers are armed and what a thread's scheduling class is: the scheduling contract owns these. The one wait is SC-WAIT-001; readiness of an `events` fid is SC-9P-001 to SC-9P-015 (the `Tready`/`Rready` request); the cross-thread wake is `EVFILT_USER` (SC-USER-001 to SC-USER-004); deadline timers are the contract timer and `nd_wait` (SC-TIMER-001, SC-TIMER-003, SC-LIB-001); thread intent is SC-INT-002.
- GPU buffer allocation, fences and the Vulkan WSI: P7-06 and P7-07. This document fixes only what a GPU present means to the window (§4.11).
- Touch, gestures (pinch, swipe), tablet pad buttons and rings, HDR and colour management: not in version 2 (§10).

## 2. Terms

| Term | Meaning |
|---|---|
| **server**, `wsys` | the window system process that serves the tree below |
| **client** | any process that reads or writes the tree; normally the program that owns a window |
| **view** | the tree as one attach sees it (§4.1) |
| **point** | the logical unit of every coordinate in this protocol. At scale 1 a point is a device pixel, which is all version 1 knew |
| **device pixel** | a pixel of an output |
| **scale** | device pixels per point for a window, a rational `scalenum/scaleden` chosen by the server (§4.8) |
| **buffer** | the pixels a client presents: the `image` file, or the shared memory behind `surface`. Its size is in buffer pixels |
| **configuration** | the set of values in a `Deskconfig` record: logical size, device-pixel size, scale, buffer, state, visibility, clock. Each change produces a new configuration with the next **config seq** |
| **present** | the request that makes buffer content visible (§4.9). The *n*-th accepted present of a window has **present number** *n* |
| **frame clock** | the server's per-output tick at which composition happens; a window follows the clock of the output given in its configuration |
| **tick** | one period of a frame clock |
| **presentation time** | the time at which a composited frame became visible on the output |
| **stream** | the queue of event records behind an `events` file (§4.5) |
| **plain format** | a stream whose records are 32-byte `Deskevent`s: version 1 |
| **extended format** | a stream whose records are extended records (§4.7): version 2 |
| **deadline clock** | the scheduling contract's SC clock: `mach_absolute_time()` in nanoseconds, which does not advance while the system sleeps (SC-TIME-001). Every nanosecond time in this protocol (`nsec`, DE_FRAME `target` and `presented`, the NS of `present at NS`) is on it, as SC-TIME-002 requires of every interface, so it can be passed to `nd_wait` or `nd_rt_set_deadline` without conversion. A client that needs time counting sleep derives it at the edge (SC-TIME-003) |
| **gesture** | a pointer interaction the server runs itself: a move or resize by the frame, by a hit region, by a `bindm` bind or by an interactive `move`/`resize` message; a menu tracked in the bar; a drag |
| **kind** | toplevel, transient, popup or tooltip (§4.15) |

## 3. Model

**Objects and lifetimes.**

| Object | Created by | Destroyed by | Identity |
|---|---|---|---|
| desktop | the server at start | `quit` (DE_QUIT first) | the `nsd` registry name `wsys.$user.$pid` |
| workspace | `wsnew` | `wsdelete` (its windows move to 0) | index |
| output | hotplug | unplug (DE_OUTPUT) | stable name; `id` while it exists |
| window | `new`, reading `windows/new`, an attach `new` | `delete`, or the owning client's last fid gone after `close` | id > 0, never reused while the server runs |
| event stream | per window: the window; desktop level: each open of `/n/desktop/events` | window deletion; the last clunk of that open | — |
| configuration | every change to a `Deskconfig` field | superseded by the next | config seq |
| present | each accepted `present` | shown, or superseded (mailbox) | present number |
| popup chain | `popup ... grab` | an outside press, parent loss, or the client | the popups' ids |
| gamepad | hotplug | unplug (DE_GAMEPAD) | slot `N` plus generation |

**Window state.** The `Deskstate` bits are independent flags, with these transitions:

```
             new ──────────────► VISIBLE (and FOCUSED unless NOFOCUS)
  kind popup/tooltip: new ─────► unmapped ──popup──► VISIBLE ──outside press / parent lost──► unmapped
                    hide ◄──────► show                     (HIDDEN: icon on the desktop)
          zoom toggles ZOOMED; snap sets SNAPPED, the next move or resize clears it; snap full sets FULL
      gesture start ──► INTERACTIVE ──► gesture end            (v2)
           no pixel on any output ──► OCCLUDED ──► a pixel shown  (v2)
                              close ──► CLOSING ──► delete
```

**The frame cycle** (version 2), for one window on one output:

```
 client                                   server                                   output
   │  (DE_CONFIGURE seq=7 read)              │                                        │
   │  draw for config 7 into buffer          │                                        │
   │── present 0 0 w h config 7 ────────────►│ accepted, number n; never blocks        │
   │                                         │── compose at the tick deadline ───────►│
   │                                         │◄──────────── scanout (time T) ─────────│
   │◄── DE_FRAME presented=T present=n  ─────│                                        │
   │     target=T+refresh, frame=k+1         │                                        │
   │  (wantframe with no present ──► DE_FRAME with presented=0, DFF_REQUESTED)         │
   │  hidden or occluded ──► the same events at the throttled rate, DFF_THROTTLED     │
```

**Resize** (version 2): the user drags the sizing gadget. The server sets DS_INTERACTIVE, and at most once per tick issues a new configuration (DE_CONFIGURE, and DE_RESIZE when the buffer was reallocated). It keeps composing the window's last present, clipped or padded and never stretched, until a present tagged with the new configuration arrives or a short bound passes. Nothing waits for the client, and the client's own loop keeps running because nothing runs inside it.

## 4. Interfaces

### 4.1 Namespace and attach

The server is called `wsys`. It posts the `nsd` registry entry `wsys.$user.$pid` and sets `$desktop` to that path in every process it starts [WP-NS-001]. The tree is mounted on `/n/desktop` by `nsd`. The attach specifier selects a view:

| Attach spec | View |
|---|---|
| empty | the whole tree below; used by the launcher, a task bar, screenshot tools |
| `N` | the same tree, plus `self` bound to `windows/N`; this is what a program running in window N sees |
| `new` *args* | creates a window as `new` in the desktop ctl would, then attaches as above |
| `none` | tree without `windows/`; for programs that only need `snarf` or `theme` |

The server MUST serve the four attach specifiers with the views in this table [WP-NS-002].

```
/n/desktop/
    ctl            rw   desktop control; read returns one line of state
    events         r    desktop-level Deskevent stream (window create/destroy, workspace, theme, quit)
                        v2: rw; a write sets the format of this open (§4.5)
    screen         r    composited display as an image(6) file
    snarf          rw   clipboard, UTF-8 text
    theme          rw   key/value text: fonts, colours, metrics, menu bar mode
    menubar        r    text rendering of the current menu bar, for accessibility and tests
    caps           r    v2: capability list, one "NAME VERSION" per line (§5)
    keymap         r    v2: the active keyboard layout and its table (§4.16)
    outputs/                                                  (NeoDarwin addition)
        NAME/
            ctl    rw   mode, logical scale, refresh, colour profile; read returns state line
    gamepads/                                                 v2 (§4.19)
        N/
            ctl    rw   rumble, LEDs, player number, sensors; read returns the descriptor line
            desc   r    the same descriptor line
            events rw   extended records for this pad
            state  r    32-byte Deskpadstate snapshot
    workspaces/
        N/
            ctl    rw   name, background; read returns state line
            windows r   ids of windows on this workspace, one per line
    windows/
        new        r    reading allocates a window and returns its id, like /dev/draw/new
        N/
            ctl    rw   window control; read returns the descriptor as one text line
            desc   r    the same descriptor as a 96-byte binary Deskwin record
            config r    v2: the current configuration as an 80-byte Deskconfig record (§4.8)
            events rw   Deskevent records, blocking; filtered by the mask
                        v2: a write sets the format of the stream (§4.5)
            image  rw   the back buffer: rows of pixels in the window's chan format
            surface r   NeoDarwin: the shared-memory back buffer (§4.9)
            screen r    the front buffer, what is currently shown, as image(6)
            title  rw   UTF-8 title, also the label of the icon when hidden
            menu   rw   menu definition, text; write replaces, read returns it
            cursor w    cursor for the pointer while over this window, cursor(6) format
            icon   w    image shown on the desktop when hidden, image(6) format
            drop   r    paths from the last DE_DROP, one per line
            ime    rw   v2: text input state: enable, caret rectangle, surrounding text (§4.17)
            winname r   name of the kernel draw image for this window, for libdraw clients
            mouse  r    rio-compatible text mouse stream for this window
            kbd    r    rio-compatible kbd stream (k, K, c messages)
            cons   rw   the window's characters, for a program that reads /dev/cons
    self  ->  windows/N       only in a view attached with a window id
```

The server MUST serve every file in this tree with the modes shown, except that `winname` is served only where a kernel draw device exists (§4.9) [WP-NS-003]. Reading `windows/new` MUST allocate a window at open time and return its id, as `/dev/draw/new` does [WP-NS-004].

Files a rio program expects (`/dev/mouse`, `/dev/kbd`, `/dev/winname`, `/dev/wctl`, `/dev/label`, `/dev/snarf`) are provided by binding `self/mouse`, `self/kbd`, `self/winname`, `self/ctl`, `self/title`, `snarf` into `/dev` in the window's namespace; `wsys` MUST do this itself for programs it starts [WP-NS-005]. `self/cons` is bound onto `/dev/cons` with it: it MUST return the characters typed at the window and nothing else, so that a program which reads the console for its keyboard (libdraw's `einit` does) gets its own window's input [WP-NS-006]. It is not a terminal: nothing is echoed, there is no line editing, and a write to it MUST go to the console `wsys` was started from [WP-NS-007]. A terminal is an ordinary client that serves a real `cons` to its children; the server does not know about text.

The binary files `desc`, `config` and `gamepads/N/state` MUST return one whole record to a read at offset 0 whose count is at least the record size, and MUST NOT block [WP-NS-008].

### 4.2 Desktop ctl

| Message | Effect |
|---|---|
| `new [-r x0 y0 x1 y1] [-dx n -dy n] [-flags +a -b] [-title t] [-ws n] [cmd args...]` | Create a window. Without `cmd` the window is empty and belongs to the writer; with `cmd` the server runs it with `self` bound and `$winid` set. Reply on read: the new id. |
| `wsnew [name]`, `wsswitch n`, `wsdelete n` | Workspaces. Deleting moves its windows to workspace 0. |
| `lowerall` | Show the desktop: every window to the back. |
| `cleanup` | Tile the icons of hidden windows, Workbench style. |
| `drag` *path*... | The pointer carries these paths until the buttons come up; the window they come up over gets DE_DROP. With no paths, cancels. |
| `quit` | Deliver DE_QUIT to every window, wait one second, exit. |

The server MUST implement each desktop ctl message with the effect in this table [WP-DCTL-001]. Reading the desktop ctl MUST return one line that begins `wsys 1 screen X0 Y0 X1 Y1` and carries at least the keys `chan`, `workspace I of N`, `windows N`, `focus ID`, `theme NAME` and `gen N`, where `gen` is the theme generation [WP-DCTL-002]. For example: `wsys 1 screen 0 0 1920 1080 chan x8r8g8b8 workspace 0 of 2 windows 5 focus 3 theme default gen 7`.

**Version 2.**
- The `1` after `wsys` is the version of this line's format and MUST stay `1`; a version 2 server MUST append `api 2` to the line [WP-DCTL-003].
- A server MAY append further `KEY VALUE` pairs to the lines read from any ctl file, and only at their end; clients MUST ignore keys they do not know [WP-DCTL-004].
- `new` accepts `-kind toplevel|transient|popup|tooltip`, default `toplevel` [WP-DCTL-005]. A window of kind popup or tooltip MUST be created unmapped, with DF_POPUP, DF_TRANSIENT and DF_BORDERLESS set (and DF_TOOLTIP and DF_NOFOCUS for a tooltip), and MUST NOT become visible until a `popup` message places it (§4.15) [WP-DCTL-006].
- `mode NAME` on the desktop ctl switches the bind mode of `ui-configuration.md` §3.3; it is that document's message and this protocol does not change it.

### 4.3 Window ctl

| Message | Effect |
|---|---|
| `resize dx dy` or `resize -r x0 y0 x1 y1` | Content size or content rectangle in screen coordinates. Clamped to `minsize`/`maxsize`. Delivers DE_RESIZE, and DE_MOVE if the origin changed. |
| `move x y` | Content origin in screen coordinates. Delivers DE_MOVE. |
| `raise`, `lower` | To the front or back of the window's layer (backdrop, normal, on-top). Does not change focus, as in rio. |
| `snap left\|right\|top\|bottom\|full\|center` or `snap grid cols rows i` | Place the frame on the workspace. Sets DS_SNAPPED; the next move or resize clears it. `full` hides the frame and sets DS_FULL. |
| `zoom` or `zoom x0 y0 x1 y1` | Toggle between the normal and zoom geometry, or set the zoom geometry. |
| `hide`, `show` | Iconify to the desktop, or restore and raise. A hidden window keeps a place on the desktop for its icon; a click on the icon shows the window again and gives it the keyboard, and a drag moves the icon. |
| `focus` | Make current: keyboard input and the menu bar. Also raises unless the theme says otherwise. |
| `title text...` | Same as writing the title file. |
| `flags +close -depth ...` | Change gadgets and behaviour by DF_ name in lowercase. The frame is redrawn. |
| `minsize dx dy`, `maxsize dx dy` | Limits on user resizing. |
| `mask 0x...` or `mask mouse key frame ...` | Which event types this window receives. Default is DM_DEFAULT (§4.6). |
| `present [x0 y0 x1 y1]` | Make the back buffer visible (§4.9). |
| `mode copy\|flip` | Buffer mode (§4.9). |
| `chan x8r8g8b8\|r8g8b8a8\|x8b8g8r8\|r8g8b8\|k8` | Pixel format of the image file. Reallocates; delivers DE_RESIZE. |
| `grab on\|off` | Route all pointer events here until off, or until every button is released. |
| `parent id` | Become a transient of `id`: raised, hidden and moved between workspaces with it. |
| `workspace n` | Move to another workspace. |
| `pid n` | Lead process, shown in the label and used by Kill. |
| `close` | Ask the client to close: DE_CLOSE with a=0. The gadget does the same. If the window is still there after the theme's `closegrace` seconds, the Desktop menu offers Kill, which delivers DE_CLOSE with a=1 and then deletes. |
| `delete` | Destroy the window now and post a `kill` note to the pid's note group. |

The server MUST implement each window ctl message with the effect in this table, and MUST reflect the result in the descriptor before the write returns [WP-CTL-001]. `resize` MUST clamp to `minsize` and `maxsize` [WP-CTL-002]. `move` and `resize` MUST clear DS_SNAPPED [WP-CTL-003]. A message the server does not know, or one with malformed arguments, MUST be refused with an error and MUST NOT change anything [WP-CTL-004]. After `delete`, the window's files MUST no longer open [WP-CTL-005].

Reading a window ctl MUST return the descriptor as text, one line, the same fields as the Deskwin record, with flags and state as comma-separated lowercase names [WP-CTL-006]: `id 3 parent 0 ws 0 depth 0 rect 100 100 740 580 frame 96 76 744 584 min 64 64 max 0 0 flags titlebar,close,depth,zoom,size,drag,retain state visible,focused chan x8r8g8b8 stride 2560 seq 41 pid 812 mode copy`. Reads of ctl MUST NOT block; clients wait on the events file for changes [WP-CTL-007].

**Version 2** window ctl messages. Each is specified in the section named.

| Message | Effect | § |
|---|---|---|
| `@TAG message...` | Any window ctl message, acknowledged by DE_ACK carrying TAG | 4.4 |
| `resize +-dx +-dy`, `move +-dx +-dy` | Relative size or position: an argument with an explicit sign is relative | below |
| `move`, `resize`, `resize edge E` (no coordinates) | Start an interactive, server-run move or resize from the current button press | 4.12 |
| `present [x0 y0 x1 y1] [config SEQ] [at NS]` | Present, tagged with the configuration drawn for, optionally not before a time | 4.9, 4.10 |
| `wantframe` | Ask for one DE_FRAME at the next tick | 4.10 |
| `latency mailbox\|N` | Bound on presents not yet shown | 4.10 |
| `buffer logical\|device\|DX DY` | How the buffer is sized | 4.11 |
| `viewport x0 y0 x1 y1 [linear\|nearest]`, `viewport off` | Which part of the buffer is shown, and the scaling filter | 4.11 |
| `decor server\|none` | Server-drawn frame, or none with hit regions | 4.12 |
| `hit REGION x0 y0 x1 y1`, `hit clear` | Hit-test regions in content coordinates | 4.12 |
| `popup PARENT x0 y0 x1 y1 [options]` | Place an anchored popup | 4.15 |
| `pointer lock\|confine x0 y0 x1 y1\|free [x y]\|warp x y` | Pointer constraints | 4.18 |
| `repeat on\|off` | Server key repeat for this window | 4.16 |
| `mask +NAME -NAME ...` | Change the mask by type or class name | 4.6 |

- A signed argument (`+N` or `-N`) to `resize` or `move` MUST be applied relative to the current size or origin, and the result clamped as for an absolute request [WP-CTL-008]. This is the form the `binde ... resize +32 +0` binds of `ui-configuration.md` §3.2 write.
- `flags` MUST accept `nodecor` (the same as `decor none`/`decor server`) and MUST refuse `+popup`, `-popup`, `+tooltip` and `-tooltip` with `badarg`, since kind is fixed at creation [WP-CTL-009].
- A version 2 window ctl line read back MAY carry, after `mode`, the keys `kind K config SEQ latency L buffer MODE` [WP-CTL-010].

### 4.4 Completion, tags and errors

**Completion.** A write to a window ctl file MUST return only after the request has been applied or refused, and after every event it causes has been queued [WP-ACK-001]. The descriptor and configuration read after the write returns MUST show the request's result [WP-ACK-002]. A request whose visible effect animates (`zoom`, `snap full`, `show`) MUST complete its state and geometry change before the write returns and animate afterwards [WP-ACK-003]. Completion never depends on user input or on another client (§4.22).

**Errors.** A refused write fails with an error string (version 1). **Version 2:** the error string MUST begin with one of the code names of `Deskerr` (`badmsg`, `badarg`, `busy`, `noconfig`, `nofocus`, `nopress`, `gone`, `perm`, `unsupported`, `short`) followed by `: ` and a human-readable detail [WP-ACK-004]. A refused request MUST change nothing [WP-ACK-005].

**Version 2: tagged requests.** A window ctl line MAY begin with `@TAG ` where TAG is a decimal number from 1 to 4294967295 [WP-ACK-006]. For a tagged request the server MUST queue exactly one DE_ACK on the window's stream, after every event the request caused and before the write returns, carrying the tag, the result code (DESK_EOK or the error), and the descriptor seq and config seq after the request; on error its data holds the error string [WP-ACK-007]. DE_ACK MUST be delivered even when the write fails [WP-ACK-008]. Tags are the client's: the server MUST NOT interpret them beyond echoing them [WP-ACK-009]. A tag on the desktop ctl MUST be refused with `badmsg` in version 2 [WP-ACK-010].

A toolkit that issues ctl writes from any thread uses tags to learn completion inside its one wait, and a remote client may pipeline tagged writes without waiting for each reply.

**Threading.** Window ctl requests are serialised per window by the server in the order the server receives them [WP-ACK-011]. There is no ordering between requests to different windows. A client MAY write any window's ctl from any thread [WP-ACK-012].

### 4.5 The event stream

A window's `events` file delivers event records in one total order with a wrapping sequence number [WP-EV-001]. A read MUST block until at least one whole record is queued and MUST return as many whole records as fit in its count, never a partial record [WP-EV-002]. The client keeps one reader of the stream; with the pilot's parked reads that was a reader proc, and in version 2 it is a readiness wait (below).

**Delivery rules** (version 1):

- **Mouse** MUST go to the window under the pointer, in content coordinates, plus the grabbing window if any [WP-EV-003]. Motion with no button change MUST be coalesced: if the client has not read the previous motion, it is replaced in place, keeping its position in the queue and its seq [WP-EV-004]. Button changes MUST NOT be coalesced [WP-EV-005]. A drag that leaves the window MUST keep delivering until the buttons are up, as in rio [WP-EV-006].
- **Keys** MUST go to the focused window [WP-EV-007]; §4.16 gives the full routing. Every key produces DE_KEY with down and up, autorepeat flagged, with the rune in `a` (0 for modifiers and dead keys) and a physical keycode in `b` [WP-EV-008].
- **Frame** events pace painting (§4.10).
- **Resize** means the server has reallocated the buffer (§4.8, §4.9). Its contents are undefined until the client presents. The window MUST keep showing the retained copy of the old content, not scaled, clipped, until then [WP-EV-009].
- **Close** is a request. The client saves, then writes `delete`, or ignores it.
- **Overflow**: the queue holds DESK_MAXQUEUE (1024) records. When full, the server MUST set DS_LAGGING, send DE_STATE, and drop motion and enter/leave only [WP-EV-010]. Keys, buttons, resize, close and menu events MUST NOT be dropped [WP-EV-011].

A desktop-level `events` file MUST carry DE_WINNEW, DE_WINGONE, DE_WORKSPACE, DE_THEME and DE_QUIT for launchers and task bars [WP-EV-012].

**Version 2: readability.** An open `events` fid MUST be readable, for readiness purposes, exactly when a read on it would return without blocking: when at least one whole record deliverable to it is queued, or when the stream has ended [WP-EV-013]. How a client waits for that — kqueue readiness on a 9P fid served through nd9p's readiness request (SC-9P-006, SC-9P-008, SC-9P-009), alongside the `EVFILT_USER` wake (SC-USER-001, SC-USER-002) and the contract timer (SC-TIMER-001, SC-LIB-001), all in one wait (SC-WAIT-001) — is the scheduling contract's. A readiness request is not a read: it consumes nothing, does not count against WP-EV-016 and does not change the format or coalescing (SC-9P-006). A read issued after readiness was reported MUST NOT block unless another read on the same stream consumed the records in between [WP-EV-014]. Every `events` file (window, desktop-level and gamepad) MUST report length 0 to a stat or `Tgetattr`, so that nd9p classifies it as a stream file and reports its readiness by SC-9P-006 rather than by file size [WP-EV-041].

**Version 2: streams and readers.**
- Each window has one stream; every open of its `events` file reads from it [WP-EV-015]. At most one read may be outstanding on a stream; a second concurrent read MUST fail with `busy` and leave the queue alone [WP-EV-016].
- Each open of the desktop-level `events` file MUST be a stream of its own that receives every desktop-level event from the time of the open, so a launcher and a task bar do not take each other's events [WP-EV-017].
- A read whose count is smaller than the next whole record MUST fail with `short` and leave the record queued [WP-EV-018]. A reader that uses a count of at least DESK_EXTMAX never sees this.
- A stream ends when its window is deleted or the server quits. After the end, queued records are discarded and every read MUST fail with `gone`; the fid stays readable so a waiting client wakes [WP-EV-019].

**Version 2: format.** A stream starts in the plain format. Writing `format ext` to an open `events` fid MUST switch that stream to the extended format, and `format plain` MUST switch it back [WP-EV-020]. The format applies to every read that begins after the write returns [WP-EV-021]. When the last fid of a window stream is clunked, the stream MUST revert to plain, so a version 1 client opening it afterwards sees version 1 traffic [WP-EV-022]. In the plain format, records of types 32 and above MUST NOT be queued or delivered [WP-EV-023], and changes that touch only DS_INTERACTIVE or DS_OCCLUDED MUST NOT produce DE_STATE [WP-EV-024]. `gamepads/N/events` is always in the extended format (§4.19).

**Version 2: ordering.**
- Within one stream, records MUST be delivered in the order they were queued, and `seq` MUST increase by one per queued record, so that a record replaced in place by coalescing keeps its seq and a dropped record frees its number [WP-EV-025]. A client therefore sees no gaps; loss is signalled by DS_LAGGING.
- Events caused by one ctl request MUST be queued in the order the request's effects happen, before the request's DE_ACK (§4.4) [WP-EV-026].
- Between different streams there is no order. Input records carry `nsec`, the sample time on the deadline clock, and a client that merges streams orders input by it [WP-EV-027].
- DE_CONFIGURE for a configuration MUST be queued before any DE_MOUSE, DE_SCROLL or DE_FRAME whose values depend on that configuration [WP-EV-028].

**Version 2: coalescing.** Coalescing replaces an unread record in place, keeping its queue position and its seq. The server MUST coalesce exactly these, and nothing else [WP-EV-040]:

| Record | Rule |
|---|---|
| DE_MOUSE motion | as version 1: an unread motion with the same buttons is replaced (WP-EV-004) |
| DE_CONFIGURE | an unread one is replaced by the newer configuration [WP-EV-029] |
| DE_FRAME | an unread one is replaced; the new one's `missed` counts presents superseded since the last DE_FRAME the client read [WP-EV-030] |
| DE_MOTION | consecutive unread records are merged by summing deltas, keeping the latest `nsec` [WP-EV-031] |
| DE_SCROLL | consecutive unread records with the same flags are merged by summing [WP-EV-032] |
| DE_PREEDIT | an unread one is replaced by the newer composition [WP-EV-033] |
| DE_PADAXIS | an unread record for the same axis is replaced [WP-EV-034] |

**Version 2: overflow.** Beyond the version 1 rule:
- DE_MOTION and DE_SCROLL MUST be merged, not dropped, when the queue is full [WP-EV-035].
- DE_CONFIGURE, DE_ACK, DE_TEXT, DE_PREEDIT, DE_DELSURROUND, DE_POPUPDONE, DE_POINTER, DE_PROXIMITY, DE_KEYMAP, DE_PADBUTTON, DE_GAMEPAD and DE_OUTPUT MUST NOT be dropped [WP-EV-036].
- Records that must not be dropped are queued past DESK_MAXQUEUE. If a stream reaches DESK_HARDQUEUE records, the server MAY discard its whole queue; if it does, it MUST then queue one DE_STATE with DS_LAGGING set as the first record, and, in the extended format, one DE_CONFIGURE with the current configuration, so the client resynchronises from `desc` and `config` [WP-EV-037].
- DS_LAGGING MUST be cleared, with a DE_STATE, when a read empties the queue [WP-EV-038].
- A client that stops reading MUST NOT delay composition, other windows or other clients (§4.22) [WP-EV-039].

**Lifetime and ownership.** Records returned by a read belong to the reader. Variable data inside an extended record is valid for as long as the reader keeps the read buffer; the toolkit's rule that event payload spans are valid until the next wait (charter §5) is built on this.

### 4.6 The mask

A window's mask selects the event types queued on its stream [WP-MASK-001]. The default is DM_DEFAULT (mouse, key, resize, expose, frame, focus, close, menu, state, quit) [WP-MASK-002]. `mask 0xHEX` MUST set the mask to the number HEX, bit *t* selecting type *t* [WP-MASK-003]. `mask NAME...` MUST set the mask to exactly the named types, where each name is the lowercase type name without `DE_` (`mouse`, `key`, `resize`, …) and `all` means every version 1 type, DM_ALL [WP-MASK-004]. A type not in the mask MUST NOT be queued [WP-MASK-005].

**Version 2: a mask wider than 32 types.**
- The mask is a set of DESK_NTYPES (256) bits, `Deskmaskset`; its word 0 is the version 1 mask [WP-MASK-006].
- Type numbers 19 to 31 MUST NOT be assigned, in this or any later version, so a version 1 mask with every bit set selects exactly the version 1 types [WP-MASK-007].
- `mask 0xHEX` accepts up to 64 hex digits; a number of 8 or fewer digits therefore leaves every type of 32 and above unselected, which is the version 1 meaning [WP-MASK-008].
- `all` keeps its version 1 meaning, types 1 to 18, in every version [WP-MASK-009].
- `mask NAME...` also accepts version 2 type names (`configure`, `ack`, `text`, `preedit`, `delsurround`, `keymap`, `scroll`, `motion`, `pointer`, `proximity`, `popupdone`) and the class names of `Deskclass` (`pointer`, `keyboard`, `text`, `window`, `frame`, `menu`, `dnd`, `ack`, `popup`, `desktop`, `gamepad`), where a class name selects every type of the class [WP-MASK-010]. Where a type name and a class name are spelled alike (`pointer`, `ack`, `frame`, `menu`, `text`), the name means the class, which includes the type.
- `mask +NAME -NAME ...` MUST add and remove types or classes from the current mask, in order; a line that mixes signed and unsigned names MUST be refused with `badarg` [WP-MASK-011].
- Selecting a type of 32 or above is allowed on any stream; such records are queued only while the stream is in the extended format (§4.5) [WP-MASK-012].
- The desktop-level streams and gamepad streams have no mask: they deliver every type their format allows [WP-MASK-013].

The static assertion `DE_MAX <= 32` in `desktop.h` keeps its version 1 meaning: the version 1 types fit the version 1 mask word. Version 2 adds `DE_CONFIGURE >= 32` and `DE_V2MAX <= DESK_NTYPES`.

### 4.7 Extended records

**Version 2.** `Deskevent` stays 32 bytes, and the plain format is unchanged. The extended format carries every record as an extended record:

```
offset 0   Deskexthead (32 bytes)
             0  type     Desketype | DESK_EXT (bit 31)
             4  win      window id; 0 for desktop-level and gamepad records
             8  msec     as Deskevent.msec
            12  seq      as Deskevent.seq: the same stream counter
            16  size     bytes of the whole record
            20  bodylen  u16: bytes of typed body
            22  datalen  u16: bytes of variable data
            24  nsec     u64: event time, ns, on the deadline clock
offset 32  body         bodylen bytes: the type's fixed record (§4.21)
offset 32+bodylen  data datalen bytes: text, preedit, paths, names
           padding      zero bytes to a multiple of 8
```

- Every record of an extended-format stream MUST be an extended record with DESK_EXT set in `type` [WP-EXT-001].
- `size` MUST equal 32 + `bodylen` + `datalen` rounded up to a multiple of 8, and MUST NOT exceed DESK_EXTMAX (4096) [WP-EXT-002]; padding bytes MUST be zero [WP-EXT-003].
- `bodylen` MUST be a multiple of 4 [WP-EXT-004].
- The first four words MUST have the meaning they have in `Deskevent`, so a reader dispatches on the first word in either format [WP-EXT-005].
- `nsec` MUST be the time of the event on the deadline clock: for input, the time the device sample was taken as reported by the HID path; for other events, the time the server queued them [WP-EXT-006]. `msec` MUST be the same instant on the version 1 millisecond clock [WP-EXT-007].
- A version 1 type in the extended format MUST carry its four version 1 words `a..d` as the first 16 bytes of its body, followed by the version 2 fields its type defines, if any (DE_MOUSE: `Deskpointer`; DE_KEY: `Deskkey`; DE_FRAME: `Deskframe`) [WP-EXT-008].
- A body grows only by appending fields in a later version; a reader MUST use the fields it knows and skip the rest by `bodylen`, and MUST skip a record of an unknown type by `size` [WP-EXT-009].
- Variable data MUST be whole UTF-8 characters where it is text [WP-EXT-010]. Text longer than one record allows is split across consecutive records of the same type, each flagged to continue (DTF_CONT for DE_TEXT) [WP-EXT-011].
- In the extended format DE_DROP's data MUST hold the dropped paths, newline-terminated, if they fit in one record; otherwise the data is empty and the client reads `drop` as in version 1 [WP-EXT-012].

The layout is the toolkit's event record (charter §3, Events) in wire form: a type, a window, a 64-bit nanosecond timestamp, a sequence, a typed payload and a payload span.

### 4.8 Configuration

**Version 2.** A window's configuration is everything a client needs to render one frame at the right size and density: the `Deskconfig` record (§4.21), served as `windows/N/config` and delivered as the body of DE_CONFIGURE.

| Field | Meaning |
|---|---|
| `seq` | the config seq: 1 at creation, incremented by one for each change to any other field |
| `state` | `Deskstate`, the same bits as `Deskwin.state` |
| `dx`, `dy` | logical content size, points |
| `pdx`, `pdy` | device pixels the content covers: `dx × scale` and `dy × scale`, rounded to nearest |
| `scalenum`, `scaleden` | the window's scale, a rational; `scaleden` is 120 in version 2 |
| `bdx`, `bdy`, `stride`, `chan` | the buffer: size in buffer pixels, bytes per row, pixel format |
| `visibility` | `Deskvis`: unmapped, shown, occluded, offscreen, hidden |
| `bufmode` | `Deskbufmode` (§4.11) |
| `refresh` | the interval of this window's frame clock, ns; the throttled interval when throttled |
| `output` | the id of the output whose scale and clock the window follows (§4.20) |
| `latency` | 0 for mailbox, else N (§4.10) |

- The server MUST issue a new configuration, with the next config seq, whenever any field other than `seq` changes, and MUST queue DE_CONFIGURE for it on the window's stream if the stream selects it [WP-CONF-001].
- `config` MUST return the current configuration [WP-CONF-002].
- **Scale is the window's, chosen by the server.** The server MUST choose one scale per window, the scale of the output the window mostly covers, and MUST change it, with a new configuration, when that output changes [WP-CONF-003]. Clients never compute scale from outputs.
- **Visibility is computed by the server.** `visibility` MUST be SHOWN when at least one pixel of the window is on an output, OCCLUDED when it is on the shown workspace but fully covered, OFFSCREEN when it is on a workspace not shown or its outputs are off or locked, HIDDEN when iconified, and UNMAPPED for a popup not placed or dismissed [WP-CONF-004]. DS_OCCLUDED MUST be set exactly when the window is mapped and `visibility` is OCCLUDED or OFFSCREEN [WP-CONF-005].
- **One event for the whole change.** A change that alters several fields at once (a move to an output of another scale changes `scale`, `pdx`, `pdy`, possibly the buffer, `refresh` and `output`) MUST be one configuration, not several [WP-CONF-006].
- **Pacing during a gesture.** While DS_INTERACTIVE is set, the server MUST issue at most one configuration per tick of the window's clock [WP-CONF-007].

**Delivery switches the buffer.** When a new configuration changes the buffer (size, stride or chan):
- The server MUST reallocate the buffer and queue DE_RESIZE, with `a`,`b` the new buffer size, `c` the stride and `d` the chan [WP-CONF-008]. In the version 1 buffer mode (logical, §4.11) the buffer size is the logical content size, which is what version 1's `a=dx b=dy` meant.
- `image` writes MUST go to the buffer of the latest configuration *delivered* to the client, where delivered means returned by a read of the window's stream (as DE_RESIZE or DE_CONFIGURE) or of `config`; a window whose mask selects neither DE_RESIZE nor DE_CONFIGURE is delivered each configuration when it takes effect [WP-CONF-009]. A client therefore never writes rows of one stride into a buffer of another: the switch happens at the moment it learns of it.

**Presents are tagged; a resize is never an error.**
- `present ... config SEQ` states the configuration the content was drawn for [WP-CONF-010]. An untagged present MUST be treated as tagged with the latest configuration delivered to the client [WP-CONF-011].
- A present tagged with a seq the server never issued MUST be refused with `noconfig` [WP-CONF-012].
- A present tagged with an older configuration MUST be accepted [WP-CONF-013]. If that configuration's buffer size equals the current one, it is composited normally; otherwise the server MUST composite the buffer at its own size, anchored at the content's top left, clipped where it is larger and padded with the theme's window background where it is smaller, and MUST NOT stretch it [WP-CONF-014].
- **Synchronised resize.** When a configuration change that alters the content size was caused by the user or the server (a gesture, `snap`, `zoom`, an output change), and the window has made at least one tagged present, the server MUST keep showing the window at its previous geometry, frame included, until a present tagged with the new configuration arrives or two refresh intervals pass, whichever is first, and then show the new geometry [WP-CONF-015]. Waiting MUST NOT delay the composition of any other window [WP-CONF-016]. A window that never tagged a present gets the version 1 behaviour: the new geometry at once, with the retained old content clipped.

### 4.9 Painting and presenting

**The `image` file** is the client's back buffer: `dy` rows of `stride` bytes in `chan` format, both read from the descriptor. A client writes any rectangle with `pwrite` at `desk_pixeloffset(w, x, y)`. Writes larger than the connection's iounit are split by the kernel; the server MUST assemble them [WP-PAINT-001]. A read of `image` MUST return what was written [WP-PAINT-002]. Nothing is visible until the client writes `present` [WP-PAINT-003]. (Version 2: `image` holds the buffer of §4.8, whose size and stride are in the configuration and, in the logical buffer mode, equal the descriptor's.)

`present [rect]` copies the rectangle (default: everything) from the back buffer into the server's front buffer for the window, redraws the frame if needed, composites the stack, and schedules DE_FRAME [WP-PAINT-004]. The front buffer MUST never be torn: composition reads it only between presents [WP-PAINT-005]. This is **copy mode**, the default, and it is what makes the pixel path safe over a network: the client may keep writing the back buffer while the previous frame is on screen.

`mode flip` trades a copy for a constraint. The server holds two buffers; `present` swaps them, so the `image` file now refers to the buffer that was on screen one frame ago [WP-PAINT-006]. The client must repaint everything it presents, or track two frames of damage. Use it for full-frame animation on a local machine.

A retained window (DF_RETAIN, the default) MUST NOT receive DE_EXPOSE [WP-PAINT-007]. Turning retention off with `flags -retain` frees the server's copy; the client then MUST receive DE_EXPOSE with the rectangle it must repaint whenever the window is revealed, and must answer with a present [WP-PAINT-008].

The window's `screen` file MUST be an image(6) file of the front buffer [WP-PAINT-009]. The desktop `screen` file MUST be an image(6) file of the composited display, showing each window's presented pixels where the window is and the desktop around it [WP-PAINT-010].

**The surface fast path.** `windows/N/surface`: a read returns a Mach port name, checked against the 9P attach's audit token, for a shared-memory back buffer with the same `present` semantics as `image` [WP-PAINT-011]. **Version 2:** the memory behind a surface port holds one buffer of one configuration; after a configuration that changes the buffer is delivered, the client MUST read `surface` again to obtain the new buffer, and the server MUST keep the previous buffer's memory valid for the client until the client releases the port [WP-PAINT-012]. A surface present MAY come from any thread or process holding the port [WP-PAINT-013].

**The draw path.** The Plan 9 pilot's `draw(3)` path is kept for rio-compatible clients running under a 9P bridge; XNU has no `devdraw`. Where a kernel draw device exists, `winname` MUST return the name of a kernel `draw(3)` image that is the window's front buffer, the way rio's `/dev/winname` does, so `initdraw` and `getwindow` work unchanged [WP-PAINT-014]. The `image` file and `winname` refer to the same pixels: a client may use either, and a screenshot tool reads `screen` regardless. On a DE_RESIZE a draw-path client calls `getwindow`, exactly as after rio's `r` mouse message.

**Version 2: presenting never blocks.**
- A `present` write MUST return as soon as the present is accepted or refused, and MUST NOT wait for composition, for a tick or for scanout [WP-PAINT-015].
- A present to a window that is hidden, occluded or offscreen MUST be accepted and MUST NOT change the window's visibility or state [WP-PAINT-016].
- `present ... at NS`: the present MUST NOT be shown in a frame whose expected presentation time is earlier than NS minus half a refresh interval [WP-PAINT-017]. It is shown in the first frame at or after that; if it is shown more than one interval after NS its DE_FRAME carries DFF_LATE.

**Reference loop** (version 2; a version 1 client drops the `config` word and reads DE_RESIZE instead of DE_CONFIGURE):

```c
/* one wait: the events fd is readable (SC-9P-006, SC-9P-009), then read */
uint8_t buf[DESK_EXTMAX*4];
for(;;){
    wait_readable(evfd);                                   /* nd_wait on a kqueue holding evfd (SC-LIB-001) */
    ssize_t n = read(evfd, buf, sizeof buf);
    for(size_t o = 0, len; o < (size_t)n && (len = desk_reclen(buf+o, n-o)) != 0; o += len){
        Deskexthead h; desk_unpackhead(buf+o, &h);
        const uint8_t *body = buf + o + DESK_EXTHEADSZ;
        switch(h.type & ~DESK_EXT){
        case DE_CONFIGURE: desk_unpackconfig(body, &cfg); damage = all; break;   /* buffer switched */
        case DE_FRAME:     inflight = 0; target = desk_get64(body+24); break;
        case DE_MOUSE:     pointer(desk_get32(body+16), desk_get32(body+20)); break; /* 24.8 */
        case DE_TEXT:      insert(body+8, h.datalen); break;
        case DE_CLOSE:     save(); fprint(ctl, "delete\n"); return;
        }
    }
    if(damage && !inflight){
        paint(damage, cfg.bdx, cfg.bdy, scale(cfg));        /* into the buffer of cfg */
        fprint(ctl, "present %d %d %d %d config %u\n", R(damage), cfg.seq);
        inflight = 1; damage = empty;
    } else if(animating && !inflight)
        fprint(ctl, "wantframe\n");
}
```

### 4.10 The frame clock and presentation feedback

**Version 1.** After a present, the server sends DE_FRAME when the frame containing the new content has been composited [WP-FRAME-001]. DE_FRAME carries `a` = the frame counter and `b` = microseconds since the previous frame [WP-FRAME-002]. A client that wants tear-free animation waits for it before presenting again.

**Version 2: one clock per output, owned by the server.**
- The server MUST run one frame clock per output and MUST pace every window by the clock of the output named in its configuration [WP-FRAME-003]. Changes of visibility or output change the window's clock through its configuration; they never destroy a client-side timing object, because there is none.
- DE_FRAME MUST be queued for a window at a tick if, since the previous DE_FRAME, a present of the window was shown or the window asked with `wantframe` [WP-FRAME-004]. Several `wantframe`s before one tick MUST produce one DE_FRAME [WP-FRAME-005]. A window that neither presents nor asks MUST receive no DE_FRAME, and the server MUST NOT compose on its account [WP-FRAME-006].
- DE_FRAME for a present MUST be queued no earlier than the presentation time of the frame that contains it [WP-FRAME-007]. This strengthens version 1's "composited", which it implies.

**Version 2: the frame record.** In the extended format DE_FRAME's body is `Deskframe`:

| Field | Meaning |
|---|---|
| `a`, `b` | the version 1 words |
| `frame` | this window's clock frame number, 64-bit; `a` is its low 32 bits |
| `target` | ns: the presentation time a present written now can reach, if it arrives before the server's composition deadline for that frame |
| `presented` | ns: the presentation time of the latest present shown since the previous DE_FRAME; 0 if none was |
| `refresh` | ns: the clock's interval, the throttled interval when throttled |
| `present` | the present number that `presented` refers to |
| `config` | the config seq that present was tagged with |
| `flags` | `Deskframeflags` |
| `missed` | presents accepted but superseded without being shown, since the previous DE_FRAME |

- `presented` MUST be the actual presentation time: when the output reports scanout (a page-flip or vblank completion time), it MUST be that time, with DFF_HWTIME set [WP-FRAME-008]. When the output reports no scanout time, `presented` MUST be the time composition of that frame completed, with DFF_COMPOSITED set; the server MUST NOT report an estimate or a nominal vblank as either [WP-FRAME-009].
- This holds for every kind of buffer: `image` presents, `surface` presents and GPU presents MUST receive the same feedback [WP-FRAME-010]. S7 showed the CPU path does not get this by default (§9).
- `target` MUST be the next presentation time the window can still reach, computed from the same clock and flip history as `presented` [WP-FRAME-011].
- DE_FRAME sent for a `wantframe` with no present shown MUST have `presented` 0 and DFF_REQUESTED set [WP-FRAME-012].
- If the buffer was scanned out without a composition copy, the server SHOULD set DFF_ZEROCOPY [WP-FRAME-013].

**Version 2: queue depth, `latency`.**
- `latency mailbox` is the default and is version 1's behaviour: at most one present waits to be shown; a later present merges into it (copy mode) or replaces it (flip mode), and is never refused [WP-FRAME-014]. Each superseded present counts in the next DE_FRAME's `missed`.
- `latency N`, N from 1 to DESK_MAXLATENCY (3): presents are shown in order, each for at least one tick, and at most N may be accepted and not yet shown [WP-FRAME-015]. A present beyond the bound MUST be refused at once with `busy` and change nothing; it MUST NOT block [WP-FRAME-016].
- `latency N` with N greater than 1 requires copy mode: `latency 2` or `3` in flip mode, and `mode flip` at latency 2 or 3, MUST be refused with `badarg` [WP-FRAME-017].

**Version 2: throttled, never withheld.**
- When a window's visibility is OCCLUDED, OFFSCREEN or HIDDEN, its clock MUST keep ticking at a throttled rate between DESK_THROTTLE_MIN_HZ (1) and DESK_THROTTLE_MAX_HZ (10), and SHOULD tick at 10 Hz [WP-FRAME-018]. The throttled interval is in the configuration's `refresh`, and DE_FRAME carries DFF_THROTTLED [WP-FRAME-019].
- The server MUST keep delivering DE_FRAME to a throttled window under the rules above, so a client that paces on DE_FRAME slows down and never stalls [WP-FRAME-020].
- Presents to a throttled window MUST be accepted and MUST NOT cause composition of an output [WP-FRAME-021].

### 4.11 Buffer size and viewport

**Version 2.** The buffer's size is chosen by `buffer`, and the part of it shown by `viewport`. The compositor scales; the client never runs a rescale pass of its own.

| Message | Buffer size | Showing it |
|---|---|---|
| `buffer logical` (default; version 1) | the logical content size: one buffer pixel per point | scaled by the window's scale |
| `buffer device` | `pdx` × `pdy`: one buffer pixel per device pixel | 1:1 |
| `buffer DX DY` | DX × DY, independent of the window | scaled to the content rectangle |

- The server MUST size the buffer as the table says and MUST issue a configuration when the buffer changes [WP-BUF-001].
- With `buffer DX DY`, resizing the window MUST NOT reallocate the buffer or queue DE_RESIZE; the configuration changes and DE_CONFIGURE reports it [WP-BUF-002].
- `viewport X0 Y0 X1 Y1` selects a source rectangle in buffer pixels; the server MUST scale that rectangle to the whole content rectangle, with a linear filter unless `nearest` is given [WP-BUF-003]. `viewport off` shows the whole buffer [WP-BUF-004]. A viewport outside the buffer MUST be refused with `badarg` [WP-BUF-005].
- Pointer coordinates stay in points of the window, whatever the buffer and viewport; mapping them into buffer pixels is the client's [WP-BUF-006].

**GPU surfaces.** A GPU present is a buffer object (P7-06: a Mach memory entry with a format and a fence) presented into the window. Whatever its transport, a GPU present MUST follow the same rules as an `image` or `surface` present: it is tagged with a config seq (§4.8), obeys `latency` (§4.10), is sized by `buffer` and shown through `viewport` (this section), and receives the same DE_FRAME feedback [WP-BUF-007]. The server MUST list the `surface.gpu` capability only when it accepts GPU presents [WP-BUF-008]. The transport is P7-06's (§10).

This is the `wp_viewporter` shape the S7 compute-to-display prototype asked for: a fixed-size simulation buffer presented into a window of any size without an extra dispatch.

### 4.12 Frame, gadgets and decorations

The frame follows Intuition: a title bar with the close gadget at the left, the depth gadget (front/back toggle) and zoom gadget at the right, and a sizing gadget in the bottom right corner. The Mac contributes the always-visible menu bar and the drag-anywhere-on-title-bar behaviour. The theme file chooses between them where they disagree:

| Theme key | Values | Meaning |
|---|---|---|
| `menubar` | `always`, `button` | Mac style at the top of every workspace, or Amiga style shown while the right button is held |
| `focus` | `click`, `pointer` | Click to focus, or focus follows the pointer (rio's behaviour) |
| `raiseonfocus` | `on`, `off` | |
| `closegrace` | seconds | How long after DE_CLOSE before Kill is offered |
| `font`, `titlefont` | font path | |
| `frame`, `active`, `inactive`, `desktop`, `text`, `menu`, `hilite` | `#rrggbb` | Colours |
| `border`, `titleheight` | pixels | Metrics |

A click on a gadget MUST have the effect of the ctl message of the same name: close delivers DE_CLOSE with a=0, depth toggles `raise`/`lower`, zoom toggles `zoom`, and the sizing gadget and title bar run a resize or move gesture [WP-DECOR-001]. A write to `theme` MUST bump the theme generation, redraw every frame (so each window's descriptor `seq` moves), post DE_THEME on the desktop streams, and be visible when `theme` is read back [WP-DECOR-002].

**Version 2: `decor none` with hit regions.**
- `decor none` sets DF_NODECOR: the server MUST draw no title bar, gadgets or border, and MUST keep the window's shadow, snap, zoom and full-screen behaviour and, unless DF_FIXEDSIZE, an invisible resize margin of the theme's `resizemargin` outside the content [WP-DECOR-003]. `decor server` clears it [WP-DECOR-004]. With DF_NODECOR, the frame rectangle equals the content rectangle [WP-DECOR-005]. DF_BORDERLESS keeps its version 1 meaning, which also removes the resize margin.
- `hit REGION X0 Y0 X1 Y1` declares a region in content coordinates; REGION is `drag`, `close`, `depth`, `zoom`, `hide`, `menu`, `edge E` (E one of `n s e w ne nw se sw`) or `client` [WP-DECOR-006]. Regions stack in the order written, the last on top; `hit clear` removes them all [WP-DECOR-007]. Hit regions apply to any window, decorated or not.
- The server MUST hit-test each primary-button press (button 1 or a pen tip) against the regions in force at the moment of the press, itself, without asking the client [WP-DECOR-008]. A press in `drag` MUST start a move gesture, and a double press there MUST toggle zoom [WP-DECOR-009]. A press and release inside the same gadget region MUST act as that gadget, `menu` opening the window's menu at the press [WP-DECOR-010]. A press in `edge E` MUST start a resize gesture from that edge [WP-DECOR-011]. A press in `client`, or outside every region, MUST be delivered to the client as DE_MOUSE [WP-DECOR-012]. Presses the server acts on MUST NOT be delivered as DE_MOUSE; pointer motion without buttons over any region MUST be, so the client can draw hover [WP-DECOR-013].

**Version 2: interactive move and resize from the client.**
- `move` and `resize` with no coordinates, and `resize edge E`, MUST start a move or resize gesture run by the server from the button press in progress, exactly as a press on the title bar or sizing gadget would [WP-DECOR-014]. Without a button held that was pressed over the window, they MUST fail with `nopress` [WP-DECOR-015]. `resize` with no edge resizes from the corner nearest the pointer.
- The `bindm ... window move` and `bindm ... window resize` binds of `ui-configuration.md` §3.2 write exactly these messages while their button is held, and MUST behave identically [WP-DECOR-016].

**Version 2: DS_INTERACTIVE.**
- The server MUST set DS_INTERACTIVE, with DE_STATE, when a move or resize gesture of the window starts, and clear it, with DE_STATE, when the gesture ends by release or cancellation [WP-DECOR-017].
- During the gesture the server MUST keep delivering the window's events, frame clock included, and MUST pace configurations as §4.8 says [WP-DECOR-018]. The client is never inside the gesture.
- The server MAY cancel a gesture on Escape, restoring the geometry it had at the start [WP-DECOR-019].

### 4.13 Menus

Menus are written to `windows/N/menu`, one directive per line, tab separated. The server shows the Desktop menu first, then the focused window's menus [WP-MENU-001]. Shortcuts are single runes with the meta key implied.

```
menu	File
item	1	Open...	o
item	2	Save	s
sep
item	3	Quit	q
menu	View
check	10	Wrap lines	w	on
sub	Zoom
item	11	50%
item	12	100%
end
```

`menubar` MUST return the text rendering of the current bar, the Desktop menu as `menu	0	Desktop` first [WP-MENU-002]. Selecting an item MUST deliver DE_MENU with the menu id (0 for the Desktop menu) and item id; for `check` items the server toggles the mark and reports the new state in `c` [WP-MENU-003]. `off id` and `on id` MUST grey and ungrey an item without rewriting the file, and `menubar` shows a greyed item with the word `disabled` [WP-MENU-004].

**Version 2.** Menus tracked in the bar are server gestures: while one is open, every client's stream, frame clock included, MUST keep flowing [WP-MENU-005]. A client that draws its own menus uses popups (§4.15).

### 4.14 Drag and drop, snarf

Dragging a file icon from a Workbench-style file browser onto a window delivers DE_DROP with the pointer position and the number of paths; the client reads `drop` to get them.

The drag itself is a desktop ctl message: `drag` *path*... says that the pointer is now carrying those paths. The server draws them at the pointer, and when the buttons come up MUST give them to the window underneath: its `drop` file holds the paths, one per line, and DE_DROP says where the pointer was in that window's coordinates and how many there are [WP-DND-001]. A release over the desktop, or over the window that started the drag, MUST drop nothing [WP-DND-002]. `drag` with no paths cancels [WP-DND-003]. Only the server needs to know where the pointer went, so the program that started the drag never learns where its file landed, and a program that receives one needs nothing but the mask and two reads.

A write to `snarf` MUST replace the clipboard, and a read MUST return it [WP-DND-004].

**Version 2.** The receiving window's `drop` and the DE_DROP data (§4.7) carry path strings only; they MUST NOT grant access, and the receiver opens them through its own namespace [WP-DND-005]. A drag MUST NOT block the program that started it: the `drag` write returns at once and the program's stream keeps flowing [WP-DND-006].

### 4.15 Popups

**Version 2.** Popups are menus, completion lists, tooltips and candidate windows a client draws itself, placed relative to a parent by the server. Global coordinates stay for toplevels; popups add `xdg_positioner` placement without giving them up.

```
popup PARENT X0 Y0 X1 Y1 [anchor A] [gravity G] [offset DX DY] [flip] [slide] [resize] [grab]
```

- `popup` is accepted only by a window of kind popup or tooltip; on any other window it MUST fail with `badarg` [WP-POP-001]. PARENT is a window in the caller's view: a toplevel, a transient or another popup, which makes a chain [WP-POP-002].
- X0..Y1 is the anchor rectangle in PARENT's content coordinates. A is the point on it the popup attaches to (`n s e w ne nw se sw center`, default `center`), and G the direction the popup extends from that point (the same names, default `se`) [WP-POP-003]. The popup's size is its current content size.
- The server MUST place the popup at A in direction G, add the offset, and then, if the popup does not fit the work area of the output containing A (the output less the menu bar and dock), adjust it per axis, x then y, in this order: `flip` mirrors A and G on that axis and is kept if it then fits; `slide` moves it along the axis until it fits or its start meets the work area's; `resize` shrinks it to fit; an adjustment not requested is not made [WP-POP-004].
- The first `popup` MUST map the popup above its parent and set DS_VISIBLE; a later `popup` MUST re-place it [WP-POP-005]. The placement is reported as for any window: DE_MOVE, and DE_RESIZE and DE_CONFIGURE if `resize` shrank it [WP-POP-006].
- When the parent moves or resizes, the server MUST re-place the popup with the same arguments [WP-POP-007].
- With `grab`, the popup MUST take the pointer grab and the keyboard for as long as it is mapped: keys go to its stream, while its parent keeps DS_FOCUSED [WP-POP-008]. A tooltip MUST NOT take `grab` and MUST NOT receive pointer input; presses pass to the window beneath [WP-POP-009].
- A press outside every window of a grabbing popup chain MUST dismiss the chain: each popup is unmapped, top first, and receives DE_POPUPDONE with DPD_OUTSIDE; that press MUST NOT be delivered to any window [WP-POP-010]. When the parent is hidden, moves to another workspace or is deleted, its popups MUST be unmapped with DE_POPUPDONE and DPD_PARENT [WP-POP-011]. A dismissed popup is unmapped, not deleted: the client places it again or deletes it.
- A popup MUST NOT be iconified and MUST NOT appear on the desktop, in a task bar list of toplevels, or in the menu bar [WP-POP-012].

### 4.16 Keyboard and keymap

**Version 1.** DE_KEY: `a` = rune (0 for modifiers and dead keys, DKF_NORUNE), `b` = physical keycode, `c` = modifiers, `d` = `Deskkeyflags` (down, repeat) [WP-KEY-001].

**Version 2: routing.** A key event MUST be routed in this order [WP-KEY-002]:
1. the binds of the current bind mode (`ui-configuration.md` §3.2–3.3). A key a bind consumes is not delivered, except for a `bindp` bind, whose key is delivered with DKF_BOUND;
2. the input method, if the destination window has text input enabled (§4.17);
3. the popup holding a grab, if any, else the focused window.

Binds and modes keep working unchanged: this protocol adds nothing between the key and the bind table.

**Version 2: the key record.**
- `b` MUST be the key's USB HID usage: a Keyboard/Keypad page (0x07) usage as its usage id, and a usage on another page as `page << 16 | id` [WP-KEY-003]. Version 1 left the encoding of `b` open; this fixes it. The pilot's PC scancodes were a pre-HID stand-in.
- `c` MUST be the modifier state after the event, including DK_R* bits for the right-hand keys and DK_LEVEL3 for the third shift level; DK_SHIFT, DK_CTRL, DK_ALT and DK_META are set when either side is held [WP-KEY-004].
- Modifier keys MUST produce their own DE_KEY down and up, with `a` 0 and DKF_NORUNE [WP-KEY-005].
- In the extended format, DE_KEY's body appends `keysym` (X11-compatible for non-printing keys, the rune otherwise), `base` (the rune the key gives with no modifiers in the active layout) and `layout` (the active layout's index) [WP-KEY-006]. A toolkit matches a shortcut by position (`b`) or by base character (`base`), which is its policy.
- **Repeat is the server's.** The server MUST generate key repeat from the keymap's delay and interval, flag it DKF_REPEAT, and stop it when focus changes [WP-KEY-007]. `repeat off` MUST stop repeat for the window, and `repeat on` restore it [WP-KEY-008]. Clients never generate repeat.
- **No lost key-ups.** When focus leaves a window, the server MUST first deliver, to that window, a release for every key it delivered down and not up, flagged DKF_CANCEL, then DE_FOCUS with a=0 [WP-KEY-009].

**Version 2: the keymap.** `/n/desktop/keymap` is text [WP-KEY-010]:

```
gen 4
layout 0 us
layout 1 de nodeadkeys
active 1
repeat 500 33333                       delay ms, interval us; "repeat 0 0" if the server does not repeat
key 0x04 a  0061 0041 00e6 00c6       usage, name, rune per level: none, shift, level3, level3+shift (hex, - for none)
...
```

- The file MUST name every layout, the active one and the repeat settings, and MUST give a `key` line for every usage the active layout maps [WP-KEY-011].
- A layout, active-layout or repeat change MUST increment `gen` and queue DE_KEYMAP on the desktop streams and on every window stream that selects it, with its body giving the generation, active layout, delay and interval and its data the layout name [WP-KEY-012].
- Dead keys and compose MUST be resolved in the server when text input is enabled; the client never sees a dead key's intermediate state as text [WP-KEY-013].

### 4.17 Text input and IME

**Version 2.** The input method runs in or beside `wsys`, as a filter in the ktrans/kbdtap lineage, never inside the client, and it never calls into the client. The client supplies a text model through `windows/N/ime`; the toolkit's text-field adapter does this for widgets.

**The `ime` file.** One message per write:

| Message | Meaning |
|---|---|
| `enable [purpose P]` | Text input on for this window |
| `disable` | Text input off; any composition is committed or cancelled by the input method |
| `rect X0 Y0 X1 Y1` | The caret rectangle, content coordinates |
| `purpose normal\|password\|number\|phone\|email\|url\|terminal` | What is being typed |
| `surrounding CURSOR ANCHOR TEXT...` | Text around the caret, with the cursor and selection anchor as byte offsets into it |
| `reset` | Abandon the composition: the client's text changed under it |

- Each accepted write MUST increment the window's ime serial [WP-IME-001]. A read of `ime` MUST return one line: `serial N enabled|disabled purpose P rect X0 Y0 X1 Y1 composing yes|no` [WP-IME-002].
- The server MUST place the input method's candidate window from `rect`, mapped to screen coordinates and scale by the server [WP-IME-003]; the client never sees screen coordinates.

**Events.** DE_PREEDIT, DE_TEXT and DE_DELSURROUND are extended records; each carries the ime serial the input method had applied when it produced the event [WP-IME-004].

- **Text comes only from DE_TEXT while text input is on.** Keys go to the input method first. Keys it consumes MUST NOT be delivered as DE_KEY [WP-IME-005]. Keys it passes on MUST be delivered as DE_KEY with DKF_IMEPASS [WP-IME-006]; when such a key types text (its rune is printable and no modifier other than shift, caps lock or level 3 is held), the server MUST queue a DE_TEXT with DTF_KEY carrying that text immediately after the DE_KEY down [WP-IME-007]. A client inserts text from DE_TEXT only, and matches shortcuts on DE_KEY.
- DE_PREEDIT carries the whole current composition: text, the cursor range (−1 hides the cursor) and style runs; an empty text ends the composition [WP-IME-008].
- When the input method commits and deletes around the cursor at once, DE_DELSURROUND MUST come before the DE_TEXT it prepares [WP-IME-009]. A DE_PREEDIT that ends or replaces the composition follows the commit.
- Before DE_FOCUS with a=0 is delivered, the input method MUST have committed or cancelled any composition, and its resulting records MUST be queued first [WP-IME-010].
- While text input is off, DE_TEXT, DE_PREEDIT and DE_DELSURROUND MUST NOT be queued, and keys follow the version 1 rules [WP-IME-011].
- The server MUST NOT wait for any answer from the client for input method processing [WP-IME-012]. A client that receives a DE_DELSURROUND whose serial is older than its latest `surrounding` SHOULD apply commits and MAY ignore the deletion [WP-IME-013].

### 4.18 Pointer, pen, scrolling, relative motion and lock

**Version 1.** DE_MOUSE: `a`,`b` = position in window coordinates, `c` = buttons (`mouse(3)` encoding), `d` = modifiers [WP-PTR-001]. `grab on` routes all pointer events to the window until `grab off` or every button is released [WP-PTR-002].

**Version 2: the pointer record, with pen fields.** In the extended format DE_MOUSE's body is `Deskpointer`:
- `fx`, `fy` MUST be the position in window points in 24.8 fixed point, consistent with `a`, `b` [WP-PTR-003].
- **One stream for the pen.** A pen sample MUST be exactly one DE_MOUSE with DMF_PEN, the tip as DB_LEFT and the barrel buttons as DB_MIDDLE and DB_RIGHT, carrying tool type, pressure, tilt, rotation and distance, scaled into window points by the server [WP-PTR-004]. A plain-format stream MUST receive the same DE_MOUSE records without the pen fields; the server MUST NOT emit a second, emulated stream [WP-PTR-005]. A pen in proximity and not touching produces DE_MOUSE with DMF_HOVER and no buttons.
- Fields a device does not report MUST be zero, and a mouse sample MUST have DESK_TOOL_MOUSE and zero pen fields [WP-PTR-006].
- DE_PROXIMITY MUST be queued when a pen tool enters or leaves proximity over the window, with a tool serial stable while it is in proximity, the tool type taken from the HID Digitizer usage (never from a device name), the tool's capabilities and its hardware serial [WP-PTR-007].

**Version 2: scrolling.**
- A window whose extended-format stream selects DE_SCROLL MUST receive wheel and trackpad scrolling as DE_SCROLL only: deltas in points (24.8), detents in 1/120 units for a wheel, DSF_PRECISE for a continuous source, DSF_INVERTED when natural scrolling is on, and DSF_STOP when a continuous sequence ends [WP-PTR-008].
- Otherwise scrolling MUST be delivered as version 1 wheel buttons: a DB_WHEELUP or DB_WHEELDOWN press and release per detent, continuous scrolling being accumulated into detents of 120 units; horizontal scrolling is not delivered [WP-PTR-009].

**Version 2: relative motion.**
- DE_MOTION carries unaccelerated device deltas and accelerated deltas in points, both 24.8 [WP-PTR-010]. It MUST be queued, when selected, for motion while the pointer is over the window, grabbed by it or locked to it [WP-PTR-011]. It MUST NOT be derived from absolute positions; it comes from the HID path [WP-PTR-012].

**Version 2: pointer constraints.** `pointer ...` on a window ctl:

| Message | Effect |
|---|---|
| `pointer lock` | Hide the cursor and hold it where it is; motion arrives only as DE_MOTION |
| `pointer confine X0 Y0 X1 Y1` | Keep the visible cursor inside the rectangle, content coordinates |
| `pointer free [X Y]` | Release; with X Y, show the cursor there, content coordinates |
| `pointer warp X Y` | Move the cursor to X Y, content coordinates |

- `lock`, `confine` and `warp` MUST fail with `nofocus` unless the window has keyboard focus [WP-PTR-013].
- While locked, DE_MOUSE MUST NOT be queued for motion; button changes MUST still be, at the locked position [WP-PTR-014].
- The server MUST suspend a lock or confinement when the window loses focus and restore it when focus returns, and MUST suspend it for the duration of a server gesture, queuing DE_POINTER with the new state and the reason (DPR_FOCUS, DPR_GESTURE) at each change [WP-PTR-015]. Every change by request MUST also queue DE_POINTER, with DPR_REQUEST [WP-PTR-016].
- `warp` MUST take effect in event order: every DE_MOUSE queued after the write returns reports positions from the warped position, and the first carries DMF_WARPED [WP-PTR-017].

### 4.19 Gamepads

**Version 2.** Gamepads are desktop-level devices. The kernel HID gamepad class (P3-07, `inputd` P4-02) normalises each controller; `wsys` delivers it; the toolkit wraps it.

```
/n/desktop/gamepads/N/
    ctl     rw   rumble LOW HIGH MS | triggers L R MS | led R G B | player N | sensors on|off | background on|off
    desc    r    pad N gen G name "..." vendor 0xVVVV product 0xPPPP caps rumble,led,... mapping standard|generic
    events  rw   extended records: DE_PADBUTTON, DE_PADAXIS, DE_PADSENSOR
    state   r    Deskpadstate, 32 bytes
```

- Every pad MUST report the mapped layout of `Deskpadbuttons` and `Deskpadaxes`, whatever its hardware: face buttons by position, sticks and triggers normalised to the ranges in `desktop.h` [WP-PAD-001]. `mapping` is `standard` for a pad with a class driver and `generic` for one mapped from the controller database [WP-PAD-002].
- Each pad MUST appear exactly once, in one slot; there are no parallel legacy paths [WP-PAD-003].
- On connection and disconnection the server MUST queue DE_GAMEPAD on every extended-format desktop stream [WP-PAD-004]. After disconnection the pad's files MUST fail with `gone`, and the slot's generation MUST increase before the slot is reused [WP-PAD-005].
- `events` is always in the extended format; button records MUST NOT be coalesced or dropped, and axis records coalesce per axis (§4.5) [WP-PAD-006]. Sensor records MUST be queued only after `sensors on` [WP-PAD-007].
- `state` MUST return the pad's current buttons and axes and the seq and time of the last event folded in, without blocking [WP-PAD-008].
- **Input goes to the focused application.** Through a view attached to a window, pad events and `state` changes MUST be delivered only while that window, or a popup or transient of it, has keyboard focus [WP-PAD-009]. `background on` lifts this, and MUST require the grant of §6; through a whole-tree view, pad input MUST be delivered only with `background on` [WP-PAD-010].
- `rumble`, `triggers`, `led` and `player` MUST fail with `unsupported` on a pad whose `caps` lack the feature [WP-PAD-011].

### 4.20 Outputs

`outputs/NAME/ctl` (NeoDarwin addition) sets and reports an output's mode, logical scale, refresh and colour profile [WP-OUT-001].

**Version 2.**
- NAME MUST be stable for a given display across hotplug and restarts, derived from its EDID identity and connector, never an index [WP-OUT-002].
- The ctl line MUST carry `id N scale NUM/DEN refresh NS hwtime yes|no` and, for the primary output, `primary` [WP-OUT-003]. `hwtime yes` means frames on it get DFF_HWTIME.
- The server MUST queue DE_OUTPUT on every extended-format desktop stream when an output is added, removed or changed, with its id, scale, refresh, flags, logical rectangle and mode, and its name as data [WP-OUT-004].

### 4.21 Record layouts

All records are little-endian and identical to the C structs of `desktop.h` on x86_64 and aarch64 [WP-REC-001]. `desktop.h` provides portable pack and unpack functions for any other host. Every size and every field offset below is asserted by `desktop_layout.c` (WP-T-058).

**Version 1, unchanged.**

**Deskwin, 96 bytes**, 4-byte aligned [WP-REC-002]: id, parent, workspace, depth; content rect x0 y0 x1 y1; frame rect x0 y0 x1 y1; mindx, mindy, maxdx, maxdy; flags, state, chan, stride; seq, pid, mode, reserved. All 32-bit. `seq` MUST be incremented on every change to the record [WP-REC-003]. `reserved` MUST be zero [WP-REC-004].

**Deskevent, 32 bytes** [WP-REC-005]: type, win, msec, seq, then four signed 32-bit payload words a..d whose meaning per type is given in `desktop.h` next to the enumerator.

Pixel format values are the channel descriptors of `draw(3)`, so `chan` in the descriptor is directly usable with `allocimage` and `chantostr` [WP-REC-006].

**Version 2.** New records, with byte offsets. Reserved fields MUST be zero when written by the server [WP-REC-007].

| Record | Size | Fields (offset) |
|---|---:|---|
| `Deskconfig` | 80 | seq 0, state 4, dx 8, dy 12, pdx 16, pdy 20, scalenum 24, scaleden 28, bdx 32, bdy 36, stride 40, chan 44, visibility 48, bufmode 52, refresh 56, output 60, latency 64, reserved[3] 68 |
| `Deskexthead` | 32 | type 0, win 4, msec 8, seq 12, size 16, bodylen (u16) 20, datalen (u16) 22, nsec (u64) 24 |
| `Deskpointer` (DE_MOUSE body) | 48 | a b c d 0–15, fx 16, fy 20, flags 24, tool 28, tooltype (u16) 32, pressure (u16) 34, tiltx (i16) 36, tilty (i16) 38, rotation (u16) 40, distance (u16) 42, reserved 44 |
| `Deskkey` (DE_KEY body) | 32 | a b c d 0–15, keysym 16, base 20, layout 24, reserved 28 |
| `Deskframe` (DE_FRAME body) | 64 | a b c d 0–15, frame (u64) 16, target (u64) 24, presented (u64) 32, refresh (u64) 40, present 48, config 52, flags 56, missed 60 |
| `Deskack` | 16 | tag 0, error 4, descseq 8, configseq 12; data: error text |
| `Desktext` | 8 | imeseq 0, flags 4; data: UTF-8 |
| `Deskpreedit` | 24 | imeseq 0, textlen 4, cursor0 8, cursor1 12, nstyle 16, reserved 20; data: text, zero padding to 4, then `nstyle` × `Deskpestyle` (start 0, end 4, style 8; 12 bytes) |
| `Deskdelsurround` | 16 | imeseq 0, before 4, after 8, reserved 12 |
| `Deskkeymap` | 16 | gen 0, layout 4, delay 8, interval 12; data: layout name |
| `Deskscroll` | 24 | dx 0, dy 4, v120x 8, v120y 12, flags 16, reserved 20 |
| `Deskmotion` | 16 | dx 0, dy 4, adx 8, ady 12 |
| `Deskpointerstate` | 8 | state 0, reason 4 |
| `Deskproximity` | 24 | in 0, tool 4, tooltype 8, caps 12, hwserial (u64) 16 |
| `Deskpopupdone` | 8 | reason 0, reserved 4 |
| `Deskoutput` | 48 | id 0, change 4, scalenum 8, scaleden 12, refresh 16, flags 20, rect 24, pdx 40, pdy 44; data: name |
| `Deskgamepad` | 16 | pad 0, connected 4, caps 8, gen 12; data: name |
| `Deskpadbutton` / `Deskpadaxis` / `Deskpadsensor` | 16 | button 0, down 4, buttons 8 / axis 0, value 4 / sensor 0, x 4, y 8, z 12 |
| `Deskpadstate` | 32 | seq 0, buttons 4, axes[6] (i16) 8, reserved 20, nsec (u64) 24 |

The bodies of the other version 1 types in the extended format are their four words, 16 bytes (§4.7).

`desktop.h` MUST compile without warnings as freestanding C23 and as C++20, giving the same layouts in both; minimum-length array parameters are spelled through `DESK_STATIC`, and the declarations sit inside `extern "C"` for C++ [WP-REC-010]. C++ clients include it directly: SDL backends, Qt, Dawn and engines.

New values in existing fields, all in bits or numbers version 1 left unused: DF_POPUP, DF_TOOLTIP, DF_NODECOR (flags bits 15–17); DS_INTERACTIVE, DS_OCCLUDED (state bits 9–10); DK_RSHIFT … DK_LEVEL3 (modifier bits 8–12); DKF_IMEPASS, DKF_BOUND, DKF_CANCEL (key flag bits 3–5); event types 32–47 [WP-REC-008]. A version 2 server MUST NOT give any other meaning to a bit or number version 1 defined [WP-REC-009].

### 4.22 No global lock and no modal loop

**Version 2.** This section is the protocol's answer to F-202; every other section is written to satisfy it.

- **No modal loop.** No request in this protocol MUST wait for a user action or for another client to complete; every ctl write, every read of a file other than `events`, and every `present` MUST complete in time independent of user input [WP-MODAL-001].
- Gestures (move, resize, menus in the bar, drags, popup chains) MUST be run by the server while the client's stream keeps receiving events, its frame clock keeps ticking, and its ctl writes keep completing [WP-MODAL-002]. No event marks "the loop is now inside a gesture" except DS_INTERACTIVE, which is state, not control.
- **No global lock.** No request MUST give one client exclusive use of the display, of input or of the server [WP-MODAL-003]. The pointer grab (`grab on`, a popup's `grab`) and pointer lock are scoped to one window, end on button release, dismissal or focus loss, and MUST NOT stop other windows' composition, frame clocks or event streams [WP-MODAL-004]. Keyboard focus is a routing decision, not a lock.
- **Nothing on the frame path waits for a client.** Composition MUST NOT wait for any client to read events, to present, or to answer (graphics-desktop.md §2) [WP-MODAL-005].
- The only blocking operation in the protocol is a read of an `events` file, and it blocks only its own read [WP-MODAL-006].
- There is no modal dialog in the protocol. A dialog is a transient window; whether its parent ignores input meanwhile is the client's decision.

### 4.23 Compatibility with rio

| rio | wsys |
|---|---|
| `wctl` `top`, `bottom`, `hide`, `unhide`, `current`, `delete`, `resize`, `move`, `new` | `raise`, `lower`, `hide`, `show`, `focus`, `delete`, `resize`, `move`, `new`; the old spellings are accepted on `ctl` for one release |
| `wctl` read blocks until change | `ctl` read never blocks; wait on `events` |
| `mouse` with `r` message on resize | `self/mouse` still sends `r`; native clients get DE_RESIZE |
| `winname`, `getwindow` | unchanged |
| `label` | `title` |
| `cons` for keyboard input | `self/cons`, bound onto `/dev/cons`: the window's characters, uncooked |
| `consctl`, `text`, `kbdtap` | not served; a terminal program (`cmd/term`) provides a cooked `cons` and `consctl` for its children |
| `screen`, `snarf` | unchanged |
| `window` (window image as image(6)) | `windows/N/screen` |

The server MUST accept the rio `wctl` spellings in this table on window ctl files [WP-RIO-001], and `self/mouse` MUST send an `r` message on resize [WP-RIO-002].

### 4.24 The test hook

A server started with `-t` MUST accept one extra desktop ctl message, `input`, which injects input as if it came from devices, so the conformance test can press gadgets [WP-TEST-001]:

| Form | Since |
|---|---|
| `input mouse X Y BUTTONS [MODS]` | version 1 |
| `input key RUNE [CODE] [MODS] [FLAGS]` | version 1 |
| `input text STRING` (each rune as a key event) | version 1 |
| `input probe X Y` (what the kernel draw device holds at a point) | version 1 |
| `input pen X Y BUTTONS PRESSURE TILTX TILTY [TOOLTYPE]`, `input proximity in\|out TOOL TOOLTYPE` | version 2 |
| `input scroll DX DY [precise\|V120]`, `input motion DX DY` | version 2 |
| `input ime preedit TEXT`, `input ime commit TEXT`, `input ime delete BEFORE AFTER` (drive the test input method) | version 2 |
| `input pad N add NAME\|remove\|button B 0\|1\|axis A V` | version 2 |
| `input output add NAME X0 Y0 X1 Y1 SCALENUM REFRESHNS\|remove NAME` | version 2 |
| `input clock NAME hw\|nohw` (whether an output reports scanout times) | version 2 |

A server started without `-t` MUST refuse `input` with `badmsg` [WP-TEST-002].

## 5. Versioning and capabilities

**Versions.** This is version 2. Evolution is additive (spec-conventions.md §2; charter P13): a later version adds messages, files, event types, record fields in reserved space or appended to extended bodies, and capabilities, and never changes what an earlier version defined [WP-CAP-001]. Removal is by deprecation only. `DESK_API_VERSION` in `desktop.h` is 2; `WP_API_VERSION` is the same value under the spec-conventions.md §5 name.

**Capability query.** `/n/desktop/caps` is text, one capability per line, `NAME VERSION`, where VERSION is the protocol version that defined the capability [WP-CAP-002]. A client tests for a feature by its name, never by version arithmetic.

| Name | Meaning when listed |
|---|---|
| `protocol 2` | the server implements version 2 |
| `ext 2` | extended format, the wide mask, class names (§4.5–4.7) |
| `ack 2` | tagged requests and DE_ACK (§4.4) |
| `configure 2` | `config`, DE_CONFIGURE, tagged presents, synchronised resize (§4.8) |
| `frame 2` | `Deskframe` feedback, `wantframe`, `latency`, `at`, the throttled clock (§4.10) |
| `viewport 2` | `buffer` and `viewport` (§4.11) |
| `decor 2` | `decor none`, `hit`, interactive `move`/`resize`, DS_INTERACTIVE (§4.12) |
| `popup 2` | window kinds and `popup` (§4.15) |
| `keymap 2` | the key record fields, `keymap`, DE_KEYMAP, server repeat, `repeat` (§4.16) |
| `ime 2` | the `ime` file and its events (§4.17) |
| `pen 2` | pen fields and DE_PROXIMITY (§4.18) |
| `pointer 2` | DE_MOTION, `pointer` constraints, DE_POINTER (§4.18) |
| `scroll 2` | DE_SCROLL (§4.18) |
| `output 2` | stable output names and DE_OUTPUT (§4.20) |
| `gamepad 2` | `gamepads/` (§4.19) |
| `surface.gpu 2` | GPU presents (§4.11); optional |

- A version 2 server MUST list every name in this table except `surface.gpu`, which it lists only if it accepts GPU presents [WP-CAP-003]. A capability MUST be listed only when the whole of its section is implemented [WP-CAP-004].
- A server that does not serve `caps` is a version 1 server [WP-CAP-005].
- A request that needs a capability the server does not offer MUST fail with `unsupported` [WP-CAP-006].
- Clients MUST ignore lines whose names they do not know [WP-CAP-007].
- The desktop ctl line carries `api 2` (§4.2) as a second, coarse signal.

## 6. Security and capabilities

Authority comes from the namespace and from capability tokens carried in the 9P attach (`docs/architecture/namespaces-agents.md` §4). This protocol adds no ambient authority.

| Grant | What it allows |
|---|---|
| a view attached with `N` | `self` = window N and the files the whole tree offers to every client; the other windows' files only if the namespace was built to show them |
| a whole-tree view | every window's files, `screen`, `workspaces/`; for launchers, task bars and the session |
| `cap:wsys:capture:<win>` | reading another window's `screen` from a view that does not otherwise include it (namespaces-agents.md §2) |
| `cap:wsys:input:background` (name pending, §10) | `background on` on a gamepad |
| a server started with `-t` | the `input` test hook; never a production session |

- A client MUST only be able to act on windows its view contains; the server MUST check the attach's audit token for `surface` ports (§4.9) [WP-SEC-001].
- `pointer lock`, `confine` and `warp` are honoured only for the focused window (§4.18), so a background window cannot take or move the pointer [WP-SEC-002].
- Pad input goes only to the focused application unless the background grant is held (§4.19) [WP-SEC-003].
- Surrounding text written to `ime` MUST be given only to the input method; with purpose `password`, the input method MUST NOT store, learn from or show candidates for the text [WP-SEC-004].
- `drop` and DE_DROP carry paths, never access (§4.14) [WP-SEC-005].
- The `input` test hook is refused outside `-t` (§4.24) [WP-SEC-006].
- `keymap`, `caps`, `outputs/` and `theme` hold no secrets and are readable in any view that contains them.

## 7. Performance contract

These are requirements, each tied to a test in §8. "Reference machine" is the P4-03 reference configuration; measurements run on NeoDarwin with `wsys` and 16 mapped windows.

| Id | Target | Test |
|---|---|---|
| ctl completion | a local window ctl write (`move`, `resize`, `present`) completes in p99 ≤ 1 ms, measured at the client, with 16 windows and a gesture in progress [WP-PERF-001] | WP-T-131 |
| frame feedback latency | DE_FRAME is queued within 1 ms (p99) of the presentation time it reports [WP-PERF-002] | WP-T-132 |
| feedback exactness | with DFF_HWTIME, `presented` equals the display driver's reported flip time exactly [WP-PERF-003] | WP-T-133 |
| pacing | a client that presents once per DE_FRAME before the composition deadline is shown at `target` with an error p99 ≤ 1 ms (the S7 game-loop criterion) [WP-PERF-004] | WP-T-134 |
| input latency | DE_KEY and DE_MOUSE are readable within 1 ms (p99) of their `nsec` sample time, where readable means the client's `kevent` wait on the `events` fid has returned; of this, SC-PERF-004 bounds the segment from queueing at the server to that return at 500 µs (p99), which leaves 500 µs from sample to queue [WP-PERF-005] | WP-T-135 |
| idle | a desktop where no client presents or asks for frames makes zero compositions and queues zero DE_FRAME per second [WP-PERF-006] | WP-T-136 |
| throttled | a hidden window that presents on every DE_FRAME causes zero compositions of any output and receives between 1 and 10 DE_FRAME per second [WP-PERF-007] | WP-T-137 |
| resize | during a 2 s interactive resize, the window receives at most one DE_CONFIGURE per tick, and a client that presents within 4 ms of each is shown at the new size within one tick of its present [WP-PERF-008] | WP-T-138 |
| no stall | a client that stops reading its events for 10 s while others animate does not change their p99 frame time by more than 0.5 ms [WP-PERF-009] | WP-T-139 |

## 8. Conformance

Tests `WP-T-001` to `WP-T-057` are the pilot's `deskconform` checks at plan-neo commit `b0a35a1`, in the order the program makes them. The first 53 are the D2–D4 checks the version 1 text and P4-03's exit criterion count; `WP-T-039` to `WP-T-042` were added by roadmap D5 (drag and drop, icons). They describe version 1 and MUST pass unchanged against a version 2 server, which proves additivity [WP-CAP-008]. Each needs only the version 1 header; NeoDarwin's `deskconform` port replaces `/dev/desktop` by `/n/desktop` and the line prefix `desk 1` by `wsys 1` (§10).

Method: **dc** = protocol conformance through `deskconform`; **unit** = `bazel test //docs/desktop/...`; **meas** = measurement on NeoDarwin; **S7** = an S7 prototype ported to wsys.

### 8.1 Version 1 checks (the pilot's deskconform)

| Test | Requirements | Method | Pass criterion |
|---|---|---|---|
| WP-T-001 | WP-DCTL-002, WP-NS-001, WP-NS-008 | dc | the desktop ctl line begins `wsys 1 screen` |
| WP-T-002 | WP-NS-003, WP-NS-004 | dc | reading `windows/new` gives an id > 0 |
| WP-T-003 | WP-REC-002, WP-NS-008 | dc | `desc` is 96 bytes with id, stride and chan set |
| WP-T-004 | WP-DCTL-001, WP-CTL-006 | dc | a new window's ctl line has `state visible,focused` |
| WP-T-005 | WP-CTL-001, WP-ACK-002 | dc | `resize 300 200` gives a 300×200 content rect |
| WP-T-006 | WP-EV-001, WP-EV-002, WP-MASK-001, WP-CONF-008 | dc | DE_RESIZE arrives after `resize` |
| WP-T-007 | WP-CTL-001 | dc | `resize -r 100 100 500 400` gives exactly that rect |
| WP-T-008 | WP-CTL-001 | dc | `move 60 80` puts the content origin there |
| WP-T-009 | WP-EV-001, WP-ACK-001 | dc | DE_MOVE arrives after `move` |
| WP-T-010 | WP-CTL-002 | dc | `resize 10000 10000` after `maxsize 640 480` gives 640×480 |
| WP-T-011 | WP-CTL-001 | dc | `title` sets the title file |
| WP-T-012 | WP-CTL-001, WP-CTL-006 | dc | `flags +ontop` shows `ontop` |
| WP-T-013 | WP-CTL-001 | dc | `flags -zoom` clears DF_ZOOM |
| WP-T-014 | WP-CTL-001 | dc | `snap left` sets DS_SNAPPED |
| WP-T-015 | WP-CTL-001 | dc | `snap grid 2 2 3` is accepted |
| WP-T-016 | WP-CTL-003 | dc | `move` clears DS_SNAPPED |
| WP-T-017 | WP-CTL-001 | dc | `zoom` sets DS_ZOOMED |
| WP-T-018 | WP-CTL-001 | dc | a second `zoom` clears it |
| WP-T-019 | WP-CTL-001 | dc | `hide` sets DS_HIDDEN |
| WP-T-020 | WP-CTL-001 | dc | `show` sets DS_VISIBLE |
| WP-T-021 | WP-CTL-001, WP-PAINT-006 | dc | `mode flip` shows `mode flip` |
| WP-T-022 | WP-CTL-001, WP-REC-006 | dc | `chan k8` shows `chan k8` |
| WP-T-023 | WP-PTR-002 | dc | `grab on` sets DS_GRABBED |
| WP-T-024 | WP-CTL-001 | dc | `pid 4242` shows `pid 4242` |
| WP-T-025 | WP-CTL-004, WP-ACK-005 | dc | `nonsense` is refused |
| WP-T-026 | WP-CTL-001 | dc | `raise` gives depth 0 |
| WP-T-027 | WP-CTL-001 | dc | `lower` gives a depth > 0 |
| WP-T-028 | WP-CTL-001 | dc | `parent ID` makes the window a transient |
| WP-T-029 | WP-PAINT-001 | dc | 32 rows written to `image` with `desk_pixeloffset` |
| WP-T-030 | WP-PAINT-002 | dc | a row reads back as written |
| WP-T-031 | WP-PAINT-004, WP-FRAME-001 | dc | `present 0 0 64 32` is answered by DE_FRAME |
| WP-T-032 | WP-PAINT-006, WP-FRAME-001 | dc | a flip-mode `present` is answered by DE_FRAME |
| WP-T-033 | WP-PAINT-009 | dc | `windows/N/screen` has an image(6) header |
| WP-T-034 | WP-MENU-001, WP-MENU-002 | dc | the written menus appear in `menubar` after the Desktop menu |
| WP-T-035 | WP-MENU-004 | dc | `off 2` shows item 2 `disabled` |
| WP-T-036 | WP-MASK-003, WP-MASK-004, WP-MASK-005 | dc | with `mask state`, DE_MOVE is not delivered and DE_STATE is |
| WP-T-037 | WP-DECOR-001, WP-TEST-001 | dc | a click on the close gadget delivers DE_CLOSE |
| WP-T-038 | WP-EV-003, WP-PTR-001 | dc | a click in the content arrives as DE_MOUSE 10 20 1 |
| WP-T-039 | WP-DND-001 | dc (D5) | a drag released over the window gives DE_DROP 30 40 2 |
| WP-T-040 | WP-DND-001 | dc (D5) | `drop` holds the two paths |
| WP-T-041 | WP-CTL-001 | dc (D5) | `hide` before `cleanup` sets DS_HIDDEN |
| WP-T-042 | WP-CTL-001 | dc (D5) | a click on the icon shows the window again |
| WP-T-043 | WP-EV-007, WP-EV-008, WP-KEY-001 | dc | `input key 97 30 0 1` gives DE_KEY with rune 97 and DKF_DOWN |
| WP-T-044 | WP-MENU-003 | dc | choosing File's first item delivers DE_MENU |
| WP-T-045 | WP-DCTL-001 | dc | `wsnew second` creates workspace 1 named `second` |
| WP-T-046 | WP-CTL-001 | dc | `workspace 1` moves a window there |
| WP-T-047 | WP-DCTL-001, WP-DCTL-002 | dc | `wsswitch 1` shows `workspace 1 of 2` |
| WP-T-048 | WP-DND-004 | dc | `snarf` reads back what was written |
| WP-T-049 | WP-DECOR-002, WP-DCTL-002 | dc | a theme write increases `gen` |
| WP-T-050 | WP-DECOR-002, WP-REC-003 | dc | the theme write increases the window's `seq` |
| WP-T-051 | WP-DECOR-002, WP-EV-012 | dc | DE_THEME arrives on the desktop stream |
| WP-T-052 | WP-DECOR-002 | dc | `theme` shows the change |
| WP-T-053 | WP-PAINT-010 | dc | `screen` has an image(6) header |
| WP-T-054 | WP-PAINT-010 | dc | the window's presented pixel is on `screen` where the window is |
| WP-T-055 | WP-PAINT-010 | dc | the desktop around the window is another colour |
| WP-T-056 | WP-CTL-001 | dc | `close` sets DS_CLOSING |
| WP-T-057 | WP-CTL-005 | dc | after `delete`, the window's files do not open |

### 8.2 Version 2 tests

Every new `dc` test below is a `deskconform` check added by P4-17's implementation in P4-03; each needs `caps` to list the capability it tests, and is skipped with a diagnostic, never passed, when it does not.

| Test | Requirements | Method | Pass criterion |
|---|---|---|---|
| WP-T-058 | WP-REC-001, WP-REC-002, WP-REC-005, WP-EXT-002 (layout), WP-MASK-006, WP-MASK-007 | unit | `desktop_layout.c` compiles: every record size and field offset of §4.21 asserted, `DE_MAX <= 32`, types 32+ outside word 0 |
| WP-T-059 | WP-REC-001, WP-EXT-002, WP-EXT-009 | unit | `desktop_layout_test` passes: pack/unpack round trips, little-endian order, `desk_reclen` walks and refuses bad records, classes cover every assigned type |
| WP-T-153 | WP-PAINT-003, WP-PAINT-005 | dc | pixels written to `image` without `present` never appear on `screen`; while one thread writes rows continuously and another presents, `screen` only ever shows whole presents (each row block of one present has one colour) |
| WP-T-152 | WP-REC-010 | unit | `desktop_cxx.cpp` includes `desktop.h` and compiles with `-std=c++20 -Wall -Wextra -Werror -pedantic`, asserting the same sizes and offsets and instantiating every inline helper |
| WP-T-060 | WP-CAP-002, WP-CAP-003, WP-CAP-004, WP-CAP-005, WP-DCTL-003 | dc | `caps` lists every mandatory name with version 2; the ctl line begins `wsys 1 ` and has `api 2` |
| WP-T-061 | WP-CAP-007, WP-DCTL-004, WP-CTL-010 | dc | the client library ignores an injected unknown caps line and an unknown trailing ctl key (fixture server) |
| WP-T-062 | WP-CAP-006 | dc | with `-t` and a capability disabled by a test flag, its message fails `unsupported: ` |
| WP-T-063 | WP-ACK-004, WP-ACK-005 | dc | `nonsense` fails with an error beginning `badmsg: `; `resize a b` with `badarg: `; nothing changed (`seq` unmoved) |
| WP-T-064 | WP-ACK-001, WP-ACK-002, WP-EV-026 | dc | after `resize` returns, `desc` and `config` show it and DE_RESIZE, DE_CONFIGURE are already readable without blocking |
| WP-T-065 | WP-ACK-003 | dc | `zoom` and `snap full` show their final geometry in `desc` immediately after the write |
| WP-T-066 | WP-ACK-006, WP-ACK-007, WP-ACK-009 | dc | `@77 move 10 10` queues DE_MOVE then DE_ACK tag 77, error 0, with the new descseq and configseq |
| WP-T-067 | WP-ACK-008 | dc | `@78 nonsense` fails and still queues DE_ACK tag 78 with DESK_EBADMSG and the error text |
| WP-T-068 | WP-ACK-010 | dc | `@1 lowerall` on the desktop ctl fails `badmsg` |
| WP-T-069 | WP-ACK-011, WP-ACK-012 | dc | 1,000 tagged `move`s from four threads: every DE_ACK arrives, in the server's order, with strictly increasing descseq |
| WP-T-070 | WP-EV-013, WP-EV-014, WP-EV-041 | dc | every `events` file reports length 0; readiness through kqueue (`Tready`, SC-9P-006) is reported only with a whole record queued, or at the stream's end; a read after it returns without blocking |
| WP-T-071 | WP-EV-015, WP-EV-016 | dc | two fids on one window read one stream; a second concurrent read fails `busy` |
| WP-T-072 | WP-EV-017, WP-EV-012 | dc | two opens of the desktop `events` each receive DE_WINNEW for a new window |
| WP-T-073 | WP-EV-018 | dc | a 16-byte read fails `short`; a following 32-byte read returns the record |
| WP-T-074 | WP-EV-019 | dc | after `delete`, a parked or later read fails `gone` and the fid is readable |
| WP-T-075 | WP-EV-020, WP-EV-021, WP-EXT-001, WP-EXT-005 | dc | after `format ext`, every record has DESK_EXT, a consistent size and the version 1 words; after `format plain`, 32-byte records again |
| WP-T-076 | WP-EV-022, WP-EV-023, WP-EV-024, WP-MASK-012 | dc | with DE_CONFIGURE selected and the stream plain, no type ≥ 32 arrives and a gesture's DS_INTERACTIVE produces no DE_STATE; after the last clunk a new open is plain |
| WP-T-077 | WP-EV-025 | dc | across coalescing and drops, seq read is gap-free and increasing |
| WP-T-078 | WP-EV-004, WP-EV-005, WP-EV-040 | dc | 100 unread motions leave one motion record in place; button changes are all present |
| WP-T-079 | WP-EV-006 | dc | a button-1 drag leaving the window keeps delivering DE_MOUSE until release |
| WP-T-080 | WP-EV-028, WP-EV-029 | dc | three unread resizes leave one DE_CONFIGURE, ahead of the DE_MOUSE that follows the last |
| WP-T-081 | WP-EV-030, WP-FRAME-014 | dc | three unread frames leave one DE_FRAME; mailbox presents superseded count in `missed` |
| WP-T-082 | WP-EV-031, WP-EV-032, WP-EV-035 | dc | 5,000 unread `input motion 1 0` and scrolls with a full queue sum exactly into the queued records |
| WP-T-083 | WP-EV-033, WP-EV-034 | dc | unread preedits and pad axis records are replaced by the newest |
| WP-T-084 | WP-EV-010, WP-EV-011, WP-EV-036, WP-EV-038 | dc | 2,000 unread records: DS_LAGGING set with DE_STATE; every key, button, configure, ack, text, preedit, popupdone and pad button present; DS_LAGGING cleared by the draining read |
| WP-T-085 | WP-EV-037 | dc | past DESK_HARDQUEUE, if the server discards, the next records are DE_STATE with DS_LAGGING then DE_CONFIGURE |
| WP-T-086 | WP-EV-039, WP-MODAL-005 | dc | a window that never reads while another animates: the other's DE_FRAME intervals stay at the refresh interval |
| WP-T-087 | WP-EV-027, WP-EXT-006, WP-EXT-007 | dc | injected input's `nsec` matches the injection time on the SC clock (`nd_sched_now_ns`, SC-TIME-001) to 1 ms; `msec` is the same instant; `nsec` orders records across two windows |
| WP-T-088 | WP-EXT-003, WP-EXT-004, WP-EXT-008, WP-EXT-010, WP-EXT-011 | dc | padding zero, bodylen multiple of 4, version 1 bodies start with a..d; a 6,000-byte commit arrives as whole-character DE_TEXT records with DTF_CONT on all but the last |
| WP-T-089 | WP-EXT-012 | dc | an ext-format DE_DROP carries the paths as data |
| WP-T-090 | WP-MASK-002, WP-MASK-008, WP-MASK-009 | dc | a new window's mask is DM_DEFAULT; `mask 0xffffffff` and `mask all` select no type ≥ 32 |
| WP-T-091 | WP-MASK-010, WP-MASK-011 | dc | `mask window frame` selects the class; `mask +configure -move` edits; `mask +a b` fails `badarg` |
| WP-T-092 | WP-MASK-013 | dc | a desktop stream receives DE_OUTPUT without any mask message |
| WP-T-093 | WP-CONF-001, WP-CONF-002, WP-CONF-006 | dc | moving a window onto an output with scale 180/120 gives one DE_CONFIGURE with scale, pdx, pdy, output and refresh changed, equal to `config` |
| WP-T-094 | WP-CONF-003 | dc | a window straddling two outputs takes the scale of the one it mostly covers |
| WP-T-095 | WP-CONF-004, WP-CONF-005 | dc | visibility reads shown, occluded (covered by another window), offscreen (other workspace) and hidden (iconified); DS_OCCLUDED matches |
| WP-T-096 | WP-CONF-007, WP-DECOR-017, WP-DECOR-018 | dc | a 2 s injected resize drag: DS_INTERACTIVE set then cleared with DE_STATE; ≤ 1 DE_CONFIGURE per tick; DE_FRAME keeps arriving |
| WP-T-097 | WP-CONF-008, WP-CONF-009 | dc | after a resize, writes made before reading DE_CONFIGURE land in the old buffer and writes after it in the new |
| WP-T-098 | WP-CONF-010, WP-CONF-011, WP-CONF-012, WP-CONF-013 | dc | `present config 999` fails `noconfig`; an older seq is accepted; untagged is accepted |
| WP-T-099 | WP-CONF-014 | dc | a present tagged with an older, smaller configuration: `screen` shows it at the top left, padded, not stretched |
| WP-T-100 | WP-CONF-015, WP-CONF-016 | dc | during a resize of a tagging window, `screen` keeps the old frame until the tagged present or two intervals; another window's frames are not delayed |
| WP-T-101 | WP-PAINT-011, WP-PAINT-012, WP-PAINT-013, WP-SEC-001 | dc | a surface port from another audit token is refused; after a buffer change the old port stays mapped and a new read returns the new buffer; a present from a second thread works |
| WP-T-102 | WP-PAINT-007, WP-PAINT-008 | dc | a retained window gets no DE_EXPOSE; after `flags -retain`, uncovering it gives DE_EXPOSE |
| WP-T-103 | WP-PAINT-015, WP-FRAME-016 | dc | `present` returns in < 1 ms while the output is stalled by the test clock; at `latency 1` a second present fails `busy` at once |
| WP-T-104 | WP-PAINT-016, WP-FRAME-021 | dc | presents to a hidden window are accepted, leave it hidden and cause no composition (`stats`) |
| WP-T-105 | WP-PAINT-017 | dc | `present at T` is not shown before T − refresh/2; a late one carries DFF_LATE |
| WP-T-106 | WP-FRAME-003, WP-FRAME-004, WP-FRAME-005, WP-FRAME-006 | dc | three `wantframe`s give one DE_FRAME; a window that neither presents nor asks gets none and causes no composition |
| WP-T-107 | WP-FRAME-002, WP-FRAME-007, WP-FRAME-008 | dc | with `input clock hw`, DE_FRAME for a present has DFF_HWTIME, `presented` equal to the test clock's flip time, `a` = low bits of `frame` |
| WP-T-108 | WP-FRAME-009 | dc | with `input clock nohw`, DE_FRAME has DFF_COMPOSITED and never DFF_HWTIME |
| WP-T-109 | WP-FRAME-010 | dc | image, surface and (if `surface.gpu`) GPU presents all get the same feedback fields |
| WP-T-110 | WP-FRAME-011, WP-FRAME-012, WP-FRAME-013 | dc | `target` is the next reachable flip; a `wantframe` DE_FRAME has presented 0 and DFF_REQUESTED |
| WP-T-111 | WP-FRAME-014, WP-FRAME-015, WP-FRAME-017 | dc | mailbox never refuses; `latency 2` shows two presents in order on consecutive ticks; `latency 2` in flip mode fails `badarg` |
| WP-T-112 | WP-FRAME-018, WP-FRAME-019, WP-FRAME-020 | dc | an iconified window presenting on each DE_FRAME gets 1–10 DE_FRAME/s with DFF_THROTTLED and `refresh` the throttled interval |
| WP-T-113 | WP-BUF-001, WP-BUF-002 | dc | `buffer device` at scale 2 gives a 2× buffer; `buffer 256 256` survives window resizes without DE_RESIZE |
| WP-T-114 | WP-BUF-003, WP-BUF-004, WP-BUF-005, WP-BUF-006 | dc | a 256×256 buffer with `viewport 0 0 128 128 nearest` fills a 512×512 window with 4×4 pixel blocks on `screen`; a viewport outside fails `badarg`; DE_MOUSE stays in window points |
| WP-T-115 | WP-BUF-007, WP-BUF-008 | dc | if `surface.gpu` is listed, a GPU present obeys config tags, latency and viewport; if not, a GPU present fails `unsupported` |
| WP-T-116 | WP-DECOR-003, WP-DECOR-004, WP-DECOR-005, WP-CTL-009 | dc | `decor none` removes the title bar from `screen`, frame == content, resize margin works; `decor server` restores it; `flags +popup` fails `badarg` |
| WP-T-117 | WP-DECOR-006, WP-DECOR-007, WP-DECOR-008, WP-DECOR-009 | dc | a press-drag in a `drag` region moves the window; a double press zooms; a later `client` region on top of it receives the press |
| WP-T-118 | WP-DECOR-010, WP-DECOR-011, WP-DECOR-012, WP-DECOR-013 | dc | `close` region click gives DE_CLOSE and no DE_MOUSE; `edge se` resizes; hover motion over regions arrives as DE_MOUSE |
| WP-T-119 | WP-DECOR-014, WP-DECOR-015, WP-DECOR-016, WP-DECOR-019, WP-CTL-008 | dc | `move` while button 1 is held drags the window; without a press it fails `nopress`; a `bindm` bind does the same; `resize +32 +0` grows by 32; Escape (if the server cancels) restores geometry |
| WP-T-120 | WP-POP-001, WP-POP-002, WP-POP-003, WP-POP-004, WP-DCTL-005, WP-DCTL-006 | dc | a `-kind popup` window is unmapped until placed; anchored at `s`/`se` it sits under the anchor; near the bottom edge `flip` puts it above, `slide` shifts it, `resize` shrinks it |
| WP-T-121 | WP-POP-005, WP-POP-006, WP-POP-007 | dc | first `popup` maps it; parent `move` moves it by the same amount; DE_MOVE reports placements |
| WP-T-122 | WP-POP-008, WP-POP-009, WP-POP-010, WP-POP-011, WP-POP-012 | dc | with `grab`, keys reach the popup while the parent stays focused; an outside click gives DE_POPUPDONE outside to a two-level chain, top first, and reaches no window; hiding the parent gives DPD_PARENT; a tooltip lets a click through; popups are absent from `menubar` and the icon list |
| WP-T-123 | WP-KEY-002, WP-KEY-003, WP-KEY-004, WP-KEY-005, WP-KEY-006 | dc | a bound key is not delivered, a `bindp` key arrives with DKF_BOUND; `b` is HID usage 0x04 for `a`; right shift gives DK_SHIFT and DK_RSHIFT after the event; modifiers have their own DE_KEY; ext body has keysym and base |
| WP-T-124 | WP-KEY-007, WP-KEY-008, WP-KEY-009 | dc | a held key repeats with DKF_REPEAT at the keymap rate; `repeat off` stops it; focusing another window while it is held gives a DKF_CANCEL release before DE_FOCUS 0 |
| WP-T-125 | WP-KEY-010, WP-KEY-011, WP-KEY-012, WP-KEY-013 | dc | `keymap` has layouts, active, repeat and a line per key; switching layout bumps `gen` and delivers DE_KEYMAP; a dead key plus `e` with text input on gives one DE_TEXT `é` |
| WP-T-126 | WP-IME-001, WP-IME-002, WP-IME-003, WP-SEC-004 | dc | each `ime` write bumps `serial`; the read line matches; the test input method sees `rect` in screen coordinates at scale 2; with purpose password the test IM records nothing |
| WP-T-127 | WP-IME-004, WP-IME-005, WP-IME-006, WP-IME-007, WP-IME-011 | dc | with text input on, a consumed key gives no DE_KEY; a passed `a` gives DE_KEY with DKF_IMEPASS then DE_TEXT `a` with DTF_KEY; with it off, no DE_TEXT |
| WP-T-128 | WP-IME-008, WP-IME-009, WP-IME-010, WP-IME-012, WP-IME-013 | dc | preedit carries text, cursor and styles; delete precedes commit; a focus change mid-composition queues the commit before DE_FOCUS 0; a client that never reads its events does not stall the IM; the fixture client applies a stale-serial commit and drops the stale deletion |
| WP-T-129 | WP-PTR-003, WP-PTR-004, WP-PTR-005, WP-PTR-006, WP-PTR-007 | dc | `input pen` gives one DE_MOUSE with DMF_PEN, pressure and tilt, and fx/fy consistent with a/b; the plain stream gets the same records without pen fields and no second copy; a mouse sample has zero pen fields; proximity in/out arrive with a stable tool serial |
| WP-T-130 | WP-PTR-008, WP-PTR-009, WP-PTR-010, WP-PTR-011, WP-PTR-012, WP-PTR-013, WP-PTR-014, WP-PTR-015, WP-PTR-016, WP-PTR-017, WP-SEC-002 | dc | DE_SCROLL replaces wheel buttons only when selected; `pointer lock` without focus fails `nofocus`; locked, motion arrives only as DE_MOTION; focus loss and return give DE_POINTER with DPR_FOCUS; `warp` gives DMF_WARPED on the next DE_MOUSE |
| WP-T-131 | WP-PERF-001, WP-MODAL-001 | meas | ctl completion p99 ≤ 1 ms with 16 windows and a gesture in progress |
| WP-T-132 | WP-PERF-002 | meas | DE_FRAME readable within 1 ms p99 of its `presented` |
| WP-T-133 | WP-PERF-003 | meas | `presented` equals the driver's flip timestamps over 10,000 frames |
| WP-T-134 | WP-PERF-004 | S7 | the game-loop prototype on wsys: frame pacing error p99 ≤ 1 ms, no timer-resolution call |
| WP-T-135 | WP-PERF-005 | meas | input readable (client `kevent` returned) within 1 ms p99 of `nsec`; the queued-to-`kevent` segment is SC-T-023's |
| WP-T-136 | WP-PERF-006 | meas | idle desktop: `stats` shows zero composites and zero DE_FRAME over 60 s |
| WP-T-137 | WP-PERF-007 | meas | iconified animating window: zero composites, 1–10 DE_FRAME/s |
| WP-T-138 | WP-PERF-008 | S7 | the text-editor prototype resized for 2 s: ≤ 1 DE_CONFIGURE per tick, shown at the new size within a tick of each present |
| WP-T-139 | WP-PERF-009 | meas | a stalled client changes others' p99 frame time by ≤ 0.5 ms |
| WP-T-140 | WP-MODAL-002, WP-MENU-005, WP-DND-006 | dc | during an injected title-bar drag, a bar menu held open, and a `drag` in flight, a second thread's `present`/`wantframe` loop keeps receiving DE_FRAME every tick and its ctl writes complete |
| WP-T-141 | WP-MODAL-003, WP-MODAL-004 | dc | while window A holds `grab on` and a popup chain, window B's frames, key focus changes by `focus` and composition continue |
| WP-T-142 | WP-MODAL-006, WP-CTL-007 | dc | reads of `desc`, `config`, `caps`, `keymap`, `ime`, `menubar`, `screen` and pad `state` return without blocking under a stalled client |
| WP-T-143 | WP-PAD-001, WP-PAD-002, WP-PAD-003, WP-PAD-004, WP-PAD-005 | dc | `input pad 0 add` gives DE_GAMEPAD and `gamepads/0` with the mapped layout; remove gives DE_GAMEPAD 0 and `gone`; re-adding raises `gen` |
| WP-T-144 | WP-PAD-006, WP-PAD-007, WP-PAD-008 | dc | buttons all arrive; sensor records only after `sensors on`; `state` matches the last event |
| WP-T-145 | WP-PAD-009, WP-PAD-010, WP-PAD-011, WP-SEC-003 | dc | through window A's view, pad input stops when B takes focus; `background on` without the grant fails `perm`; `rumble` on a pad without it fails `unsupported` |
| WP-T-146 | WP-OUT-001, WP-OUT-002, WP-OUT-003, WP-OUT-004 | dc | `input output add` gives DE_OUTPUT with name data; the ctl line has id, scale, refresh, hwtime; the same NAME after remove and add |
| WP-T-147 | WP-NS-002, WP-NS-005, WP-NS-006, WP-NS-007 | dc | the four attach views; a program started by wsys has `/dev/mouse`, `/dev/cons` etc. bound; `cons` gives only this window's characters |
| WP-T-148 | WP-RIO-001, WP-RIO-002, WP-PAINT-014, WP-EV-009 | dc | rio spellings `top`, `current`, `unhide` work; `self/mouse` sends `r` on resize; where a draw device exists, `winname` names the front buffer; after resize the retained content shows clipped |
| WP-T-149 | WP-TEST-001, WP-TEST-002, WP-SEC-006 | dc | `input` works under `-t` and fails `badmsg` without it |
| WP-T-150 | WP-SEC-005, WP-DND-005, WP-DND-002, WP-DND-003 | dc | a drop of a path the receiver cannot open does not let it open the path; a release over the source window or the desktop drops nothing; `drag` with no paths cancels |
| WP-T-151 | WP-CAP-001, WP-CAP-008, WP-REC-007, WP-REC-008, WP-REC-009, WP-REC-004 | dc | WP-T-001 … WP-T-057 pass unchanged against the version 2 server; reserved fields read zero; no version 1 bit changes meaning |

Every MUST in §4–§7 is covered by at least one test above; `desktop_layout.c` and `deskconform` carry the test ids in their messages.

## 9. Rationale and evidence

**Why revise rather than add a second protocol.** The window protocol already had the shape the study's friction entries want: the server runs gestures, menus are data, drag is a ctl message, retained content keeps showing during a resize (F-202's "the protocol already has the right shape"). Version 2 fills the gaps the entries name, each as an addition (charter P13; spec-conventions.md §2), so version 1 clients, the rio compatibility layer and the pilot's conformance test keep working unchanged (WP-T-151).

**Event mask.** `desktop.h:398` asserted `DE_MAX <= 32`, and version 2 needs 16 more types with more to come (charter §9; R1). Three options were weighed: widen the hex mask to 64 bits; mask by class only; or a bit set wide enough for good, addressed by number and by class name. The protocol takes the third: a 256-bit `Deskmaskset` whose word 0 *is* the version 1 mask, so `mask 0x…` with up to 8 digits means exactly what it meant, and `mask all` stays version 1's DM_ALL. Numbers 19–31 are never assigned, because a version 1 client that set every bit of its word must not start receiving types it cannot parse. Class names (`pointer`, `text`, `window`, …) give the coarse subscription that a toolkit wants, without giving up per-type control, which Wayland and SDL both keep. The static assertion is kept with its version 1 meaning and two new assertions guard the new space.

**Extended records.** R1 asks to keep `Deskevent` at 32 bytes on the wire and to add a 64-bit ns timestamp and a payload span. The extended record keeps the first 16 bytes of `Deskevent` (type, window, msec, seq), so a reader dispatches on one word in either format; it adds an explicit size (records can be skipped without knowing the type), a body length (bodies grow by appending, P13) and a data length (text, preedit, paths, names), and pads to 8 so the 64-bit fields align. The format is chosen per stream by a write to the events fid, rather than by the mask, so a version 1 client never sees a record longer than 32 bytes, and it reverts at the last close so a later version 1 opener is safe (WP-EV-022). The alternative, a side file per payload (`drop` in version 1, `pen` and `ime` reads in F-211/F-212's first sketches), costs a second read and a race per event; the S7 toolkit's `Span8` payloads (API.md changelog item 3) map directly onto the data area instead.

**Frame feedback** (F-101, F-102, F-209; P3; R5). Every platform's present feedback is partial or estimated (F-101 cites mpv, zed, gtk, chromium, dxvk and MoltenVK sanitising or guessing). The shape chosen is `wp_presentation_feedback`'s, the only exact, push-based design in F-101's table, delivered through the event stream rather than a callback. Two S7 findings shaped it: Metal reports no presentation time during live resize and the CPU surface path has none at all (SHIM-NOTES W3, W4; s7-prototypes.md §3.5), so WP-FRAME-010 requires the same feedback for every buffer kind, and WP-FRAME-009 forbids passing an estimate off as actual, which is the `presentedEstimated` lesson of API.md changelog item 1. Where an output has no scanout time (a framebuffer without vblank), DFF_COMPOSITED says so honestly, which is version 1's "composited, not scanned out" made machine-readable. `latency mailbox` as the default is version 1's copy-mode behaviour named; `latency N` fails fast instead of blocking, because a blocking present is exactly the Wayland FIFO stall SDL, Blender and Zed worked around (F-102). The throttled clock is the 10 Hz timer the S7 shim needed (W6) turned into a server guarantee.

**Configuration** (F-205, F-208; P4; R6). Resize and scale are one record with one sequence number, and presents carry it, so the client never has to treat a size mismatch as an error (wgpu's `Outdated`, R6). S7 measured this model on the host: 88 configures, 88 presents at the new size, configure-to-present p50 9 ms, and the loop never blocked, against 2,092 ms for a conventional AppKit pump (s7-prototypes.md §1). Buffer switching on *delivery* (WP-CONF-009) removes the race version 1 had between DE_RESIZE being queued and the client's writes, without adding an acknowledgement message: reading the event is the acknowledgement. Synchronised resize waits for the client with a bound of two intervals and never blocks other windows: the frame rule "nothing on the frame path waits for a client" (graphics-desktop.md §2) with F-208's "short bound". The buffer is in logical units by default so a version 1 client on a 2× output is correct though soft; `buffer device` is the one opt-in, which avoids Windows' four-mode DPI matrix (F-205 item 4).

**Sequenced acknowledgements and popups** (F-206). Win32 and rio's synchronous model is the one the corpus spends least code on; the protocol keeps it (a ctl write returns applied, WP-ACK-001) and adds tags so asynchronous issuers and remote pipelines can learn completion inside the one wait (charter §3, Windows). Popups take `xdg_positioner` semantics, because SDL, winit and Wine each re-implement them (F-206), while toplevels keep global coordinates.

**`decor none` and hit regions; DS_INTERACTIVE** (F-207, F-202). Applications will put tabs in the title bar; if the only choices are "server frame" and "no frame", they turn the frame off and rebuild drag, snap and shadow, as on Windows. Hit-testing stays in the server, so there is no `WM_NCHITTEST` round trip and no click delay. Interactive `move`/`resize` without coordinates is the `xdg_toplevel.move` model and is also what the `bindm` binds of `ui-configuration.md` already write, so the binds become specified behaviour rather than a special case. DS_INTERACTIVE lets engines pick a cheap resize path and lets Wine synthesise `WM_ENTERSIZEMOVE` exactly (F-202 item 2).

**Input channels** (F-210 to F-214). Keys: HID usages in `b`, modifiers after the event with sides, server repeat, and a keymap file remove the 1–6 kLOC per-platform translators every toolkit carries (F-210); the S7 shim needed about 165 lines for this on macOS (W5). The DKF_CANCEL release addresses the lost key-up that SDL and Godot reset around (F-202). IME: the input method is in the server and never calls into the client, which is the lesson of TSF and Cocoa's synchronous queries (F-211); the "text only from DE_TEXT while text input is on" rule is SDL3's `TEXT_INPUT` plus `KEY_DOWN` split, which removes the double-insert ambiguity. S7 showed that with this model the editor needs no IME code beyond the adapter (44 lines of app code). Pen: one stream with optional fields, Haiku's model with Wayland's tool identity (F-212); the fields sit on the pointer record, as charter §3 asks. Relative motion and lock are server policy the client requests, never warping emulation (F-213); S7's host pointer lock took four calls (W13). Gamepads: one mapped layout from the kernel class, delivered as files, so SDL's NeoDarwin backend is small (F-214; Q5).

**No global lock, no modal loop** (F-202; P2). The most expensive shim workaround in S7 was running AppKit on a second stack so the app's wait could return inside the live-resize loop: 107 lines, 57 of them assembly, rated high risk (W1). On NeoDarwin that cost is zero only if the protocol never has a modal operation; §4.22 states it as requirements so it cannot erode.

**Readiness semantics here, waiting there.** The scheduling contract (P4-16) owns *how* a client waits: kqueue readiness on 9P fids through nd9p's non-consuming `Tready`/`Rready` request (SC-9P-004 to SC-9P-015), the `EVFILT_USER` wake (SC-USER-001), absolute-deadline timers (SC-TIMER-001, SC-LIB-001), and the one clock (SC-TIME-001) (charter §9; R2). This document owns *when* the events file is readable (WP-EV-013), what a read returns, ordering, coalescing and overflow, because those are properties of the records and the stream.

**Evidence from the pilot** (version 1). The plan-neo pilot's `wsys` (then `desk`) served this tree as one lib9p process with parked reads; it implemented every version 1 ctl message, the event records, per-stream sequence numbers, the mask, motion coalescing and the overflow rule, `image` with copy and flip modes and retention, gadgets drawn by libstyle, menus as data, the live theme, the draw path (roadmap D3), rio compatibility including `self/cons` (D4), and the Workbench browser, desktop icons and drag and drop (D5). `deskconform` ran in the boot test. Two pilot deviations from the version 1 text are recorded in §10.

## 10. Open issues

1. **Check count.** The version 1 text and P4-03's exit criterion say 53 `deskconform` checks; the pilot at `b0a35a1` makes 57, the four drag-and-drop and icon checks of D5 having been added after the count was written. §8.1 maps all 57. P4-03's exit criterion should say 57 (roadmap owner).
2. **Pilot spellings.** The pilot's ctl line begins `desk 1` and its mount point was `/dev/desktop`; NeoDarwin's are `wsys 1` and `/n/desktop`. The `deskconform` port must change both (P4-03).
3. **Pilot overflow.** With a full queue and nothing droppable, the pilot drops the oldest record, which version 1's text forbids; WP-EV-037 defines the case. The pilot also serves one queue for the desktop-level `events`, which WP-EV-017 replaces by one stream per open.
4. **Deadline clock. Resolved** (consistency pass, `docs/spec-consistency.md`). SC fixes one clock, `mach_absolute_time` in nanoseconds (SC-TIME-001 to SC-TIME-003), and §2 defines the deadline clock as it, so a DE_FRAME `target` is passed directly to `nd_wait` as a deadline. The pilot-derived `wsys` used `mach_continuous_time`; P4-03 must switch it to the SC clock. Until it does, its times exceed SC time by the sleep offset, and only after the machine has slept (SC §4.0).
5. **Readiness of 9P fids. Resolved.** SC reports readiness through the `Tready`/`Rready` extension, which consumes no data and is not a read for WP-EV-016 (SC-9P-006); `wsys`, a `libns` server, implements it, and `nsd` emulates it for legacy servers (SC-9P-016). Only the fallback read-ahead of a direct mount parks a read (SC-9P-018), which no `wsys` client meets. WP-EV-041 makes every `events` file a stream file for SC-9P-001. One edge remains: two opens of one window stream (WP-EV-015) each get readiness, and a record the other open consumed makes a read on this one park, so an `O_NONBLOCK` read can then block (SC open issue 9).
6. **GPU present transport.** WP-BUF-007 fixes what a GPU present means; how a buffer object and its fence reach `wsys` is P7-06's, and `surface.gpu` stays unlisted until then.
7. **Surface buffers.** The surface's buffer age (EGL semantics; API.md changelog item 4) and the number of buffers behind a surface in flip mode and `latency N` are not specified; P4-03 should propose them from its implementation.
8. **Token names.** `cap:wsys:input:background` is a placeholder until `keyd` (P5-03) fixes the token vocabulary; `docs/architecture/namespaces-agents.md` does not list it (its §2 lists only `cap:wsys:capture:<win>` among `wsys` tokens). _Partly resolved 2026-09-28: namespaces-agents.md §2 now lists `cap:wsys:input:background`; the final token name is still keyd's (P5-03)._
9. **Keymap table.** The `key` line gives four levels; layouts with a fifth level or with more complex xkb features need an extension, to be decided with `inputd` (P4-02).
10. **Not in version 2.** Touch and gestures (INP.touch, INP.gesture), tablet pad buttons and rings, HDR and colour management, compressed pixel writes for slow links (reserved behind `mode` since version 1), per-window scale overrides, and tagged requests on the desktop ctl.
11. **Wayland front door.** The mapping of `xdg_surface.configure`/`ack_configure`, `wp_presentation`, `wp_viewporter`, `xdg_positioner`, `zwp_text_input_v3`, `zwp_relative_pointer_v1` and `zwp_pointer_constraints_v1` onto this protocol belongs in P4-04; each has a direct counterpart here.
12. **Throttle rate.** 10 Hz is the S7 shim's figure; P4-03 should measure power at 1, 4 and 10 Hz before WP-FRAME-018's SHOULD is fixed.
13. **Namespace spelling.** `namespaces-agents.md` §3 lists `/n/desktop`, as this protocol uses, but its §7 roadmap hook P5-06 speaks of `windows/` "mirrored from `/n/wsys/wins`", a path this protocol does not define. The namespaces document should say `/n/desktop/windows` (its owner's change, not made here). _Resolved 2026-09-28: namespaces-agents.md §7 now says `/n/desktop/windows`._

## 11. Changelog

| Version | Date | Change |
|---|---|---|
| 1 | 2026-09-22 | The pilot's text (plan-neo `b0a35a1`), adapted to NeoDarwin: `/n/desktop`, the `nsd` registry, `outputs/`, the `surface` fast path. |
| 2 (draft) | 2026-09-28 | Epic P4-17. Restructured to spec-conventions.md with requirement ids on the version 1 text, whose meaning is unchanged, and two clarifications of what version 1 left open: the desktop ctl line's `gen` key (which the pilot's test already read) and the encoding of DE_KEY `b` (USB HID usage). Added: capability query (`caps`); extended records and per-stream format; a 256-bit mask with class names; stream readability, ordering, coalescing and overflow rules; tagged ctl requests with DE_ACK and error codes; configuration (`config`, DE_CONFIGURE, rational scale, logical and pixel sizes, visibility, tagged presents, synchronised resize); presentation feedback (`Deskframe`, actual times for every buffer kind, `wantframe`, `latency`, `present at`, throttled clock); `buffer` and `viewport`; `decor none` with hit regions, interactive `move`/`resize`, DS_INTERACTIVE and DS_OCCLUDED; anchored popups and window kinds; key record, keymap and server repeat; text input and IME; pen fields, scrolling, relative motion and pointer constraints; gamepads; output identity and DE_OUTPUT; the no-global-lock, no-modal-loop requirements; conformance table. `desktop.h` is `DESK_API_VERSION 2`, first-party, C23 and C++20, and compile-checked by `docs/desktop/BUILD.bazel`. |
| 2 (draft) | 2026-09-28 | Consistency pass with SC and AU (`docs/spec-consistency.md`); no id renumbered or removed. The deadline clock is SC's `mach_absolute_time` clock (§2, citing SC-TIME-001 to SC-TIME-003), replacing the assumed `mach_continuous_time`. Placeholder references to the scheduling contract replaced by SC ids (§1, WP-EV-013, §4.9, §9). Added WP-EV-041 (every `events` file reports length 0, a stream file for SC-9P-001). WP-PERF-005 states its split with SC-PERF-004. Open issues 4 and 5 resolved; 13 added. `desktop.h`: clock comments, `WP_API_VERSION`. |
