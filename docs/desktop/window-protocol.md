<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- Provenance: distilled from the plan-neo pilot (/Users/jkane/Development/c/plan-neo, commit b0a35a1, 2026-09-22), where this design was first written and its reference implementation built and tested. This is NeoDarwin's normative text. -->

# The NeoDarwin window protocol: `wsys` over 9P

Version 1. An Intuition- and classic Mac-style desktop in the lineage of Plan 9's `rio`: overlapping windows with title bars and close, depth, zoom and sizing gadgets, a menu bar at the top of the screen, iconified windows on a Workbench-style desktop, and workspaces modelled on Intuition screens. Every interaction remains a read or write on a synthetic 9P file tree, so a client on another machine works exactly like a local one. The binary records and control strings defined here are declared in `docs/desktop/desktop.h`.

> Lineage from rio: the server is an ordinary user process serving 9P, posted in the `nsd` registry (`/n/sys/srv`) and mounted per window with an attach specifier; the local fast path is a shared-memory `surface` (below); `mouse(3)` button encoding and `keyboard.h` runes are reused; the snarf buffer is global. What changes: window decorations, menus, depth control and a proper event stream are the server's job, not the client's, and remote clients can paint through 9P without `/dev/draw`.

## Design rules

- **One tree, three vocabularies.** Control is text lines on `ctl` files, like `wctl`. State and events are fixed-size little-endian binary records, so C clients read straight into structs. Pixels are raw rows in a `image` file addressed by offset, so `pwrite` is the whole drawing API.
- **The server composes, the client paints.** A window's content is a buffer the client owns; the server retains a copy, draws the frame around it, composites the stack, and never asks the client to repaint because of occlusion. Expose events exist only for clients that opt out of retention to save memory.
- **Everything the user can do with a gadget, a program can do with a ctl message**, with the same name: `raise`, `lower`, `zoom`, `hide`, `snap`, `close`.
- **Menus are data, not drawing.** A client describes its menus in a text file; the server renders them in the menu bar in the current theme and reports selections as events. This is how the Mac and Amiga got consistent menus across every program, and it means menus work over a slow link.
- **Windows are files, so windows are scoped by namespace.** A process sees the window it was started in as `/n/desktop/self`. It only sees other windows if its namespace was built to show them.

## 1. Namespace

The server is called `wsys`. It posts `the `nsd` registry entry `wsys.$user.$pid`` and sets `$desktop` to that path in every process it starts. The tree is mounted on `/n/desktop` by `nsd`. The attach specifier selects a view:

| Attach spec | View |
|---|---|
| empty | the whole tree below; used by the launcher, a task bar, screenshot tools |
| `N` | the same tree, plus `self` bound to `windows/N`; this is what a program running in window N sees |
| `new` *args* | creates a window as `new` in the desktop ctl would, then attaches as above |
| `none` | tree without `windows/`; for programs that only need `snarf` or `theme` |

```
/n/desktop/
    ctl            rw   desktop control; read returns one line of state
    events         r    desktop-level Deskevent stream (window create/destroy, workspace, theme, quit)
    screen         r    composited display as an image(6) file
    snarf          rw   clipboard, UTF-8 text
    theme          rw   key/value text: fonts, colours, metrics, menu bar mode
    menubar        r    text rendering of the current menu bar, for accessibility and tests
    outputs/                                                  (NeoDarwin addition)
        NAME/
            ctl    rw   mode, logical scale, refresh, colour profile; read returns state line
    workspaces/
        N/
            ctl    rw   name, background; read returns state line
            windows r   ids of windows on this workspace, one per line
    windows/
        new        r    reading allocates a window and returns its id, like /dev/draw/new
        N/
            ctl    rw   window control; read returns the descriptor as one text line
            desc   r    the same descriptor as a 96-byte binary Deskwin record
            events r    32-byte Deskevent records, blocking; filtered by the mask
            image  rw   the back buffer: rows of pixels in the window's chan format
            screen r    the front buffer, what is currently shown, as image(6)
            title  rw   UTF-8 title, also the label of the icon when hidden
            menu   rw   menu definition, text; write replaces, read returns it
            cursor w    cursor for the pointer while over this window, cursor(6) format
            icon   w    image shown on the desktop when hidden, image(6) format
            drop   r    paths from the last DE_DROP, one per line
            winname r   name of the kernel draw image for this window, for libdraw clients
            mouse  r    rio-compatible text mouse stream for this window
            kbd    r    rio-compatible kbd stream (k, K, c messages)
            cons   rw   the window's characters, for a program that reads /dev/cons
    self  ->  windows/N       only in a view attached with a window id
```

Files a rio program expects (`/dev/mouse`, `/dev/kbd`, `/dev/winname`, `/dev/wctl`, `/dev/label`, `/dev/snarf`) are provided by binding `self/mouse`, `self/kbd`, `self/winname`, `self/ctl`, `self/title`, `snarf` into `/dev` in the window's namespace; `wsys` does this itself for programs it starts. `self/cons` is bound onto `/dev/cons` with it: it returns the characters typed at the window and nothing else, so that a program which reads the console for its keyboard (libdraw's `einit` does) gets its own window's input rather than whatever someone is typing at the kernel console. It is not a terminal: nothing is echoed, there is no line editing, and a write to it goes to the console `wsys` was started from. A terminal is an ordinary client that serves a real `cons` to its children; the server does not know about text.

### Desktop ctl

| Message | Effect |
|---|---|
| `new [-r x0 y0 x1 y1] [-dx n -dy n] [-flags +a -b] [-title t] [-ws n] [cmd args...]` | Create a window. Without `cmd` the window is empty and belongs to the writer; with `cmd` the server runs it with `self` bound and `$winid` set. Reply on read: the new id. |
| `wsnew [name]`, `wsswitch n`, `wsdelete n` | Workspaces. Deleting moves its windows to workspace 0. |
| `lowerall` | Show the desktop: every window to the back. |
| `cleanup` | Tile the icons of hidden windows, Workbench style. |
| `drag` *path*... | The pointer carries these paths until the buttons come up; the window they come up over gets DE_DROP. With no paths, cancels. |
| `quit` | Deliver DE_QUIT to every window, wait one second, exit. |

Reading the desktop ctl returns one line: `wsys 1 screen 0 0 1920 1080 chan x8r8g8b8 workspace 0 of 2 windows 5 focus 3 theme default`.

### Window ctl

| Message | Effect |
|---|---|
| `resize dx dy` or `resize -r x0 y0 x1 y1` | Content size or content rectangle in screen coordinates. Clamped to `minsize`/`maxsize`. Delivers DE_RESIZE, and DE_MOVE if the origin changed. |
| `move x y` | Content origin in screen coordinates. Delivers DE_MOVE. |
| `raise`, `lower` | To the front or back of the window's layer (backdrop, normal, on-top). Does not change focus, as in rio. |
| `snap left|right|top|bottom|full|center` or `snap grid cols rows i` | Place the frame on the workspace. Sets DS_SNAPPED; the next move or resize clears it. `full` hides the frame and sets DS_FULL. |
| `zoom` or `zoom x0 y0 x1 y1` | Toggle between the normal and zoom geometry, or set the zoom geometry. |
| `hide`, `show` | Iconify to the desktop, or restore and raise. A hidden window keeps a place on the desktop for its icon; a click on the icon shows the window again and gives it the keyboard, and a drag moves the icon. |
| `focus` | Make current: keyboard input and the menu bar. Also raises unless the theme says otherwise. |
| `title text...` | Same as writing the title file. |
| `flags +close -depth ...` | Change gadgets and behaviour by DF_ name in lowercase. The frame is redrawn. |
| `minsize dx dy`, `maxsize dx dy` | Limits on user resizing. |
| `mask 0x...` or `mask mouse key frame ...` | Which event types this window receives. Default is DM_DEFAULT. |
| `present [x0 y0 x1 y1]` | Make the back buffer visible. Section 2. |
| `mode copy|flip` | Buffer mode. Section 2. |
| `chan x8r8g8b8|r8g8b8a8|x8b8g8r8|r8g8b8|k8` | Pixel format of the image file. Reallocates; delivers DE_RESIZE. |
| `grab on|off` | Route all pointer events here until off, or until every button is released. |
| `parent id` | Become a transient of `id`: raised, hidden and moved between workspaces with it. |
| `workspace n` | Move to another workspace. |
| `pid n` | Lead process, shown in the label and used by Kill. |
| `close` | Ask the client to close: DE_CLOSE with a=0. The gadget does the same. If the window is still there after the theme's `closegrace` seconds, the Desktop menu offers Kill, which delivers DE_CLOSE with a=1 and then deletes. |
| `delete` | Destroy the window now and post a `kill` note to the pid's note group. |

Reading a window ctl returns the descriptor as text, one line, the same fields as the Deskwin record: `id 3 parent 0 ws 0 depth 0 rect 100 100 740 580 frame 96 76 744 584 min 64 64 max 0 0 flags titlebar,close,depth,zoom,size,drag,retain state visible,focused chan x8r8g8b8 stride 2560 seq 41 pid 812 mode copy`. Reads never block; use the events file to wait for changes.

## 2. Event loop and painting

### Events

A window's `events` file delivers 32-byte Deskevent records in one total order with a wrapping sequence number. A read blocks until at least one event is queued and returns as many whole records as fit. The client keeps one process reading events into a channel, as libdraw's `initmouse` does today.

Delivery rules:

- **Mouse** goes to the window under the pointer, in content coordinates, plus the grabbing window if any. Motion with no button change is coalesced: if the client has not read the previous motion, it is replaced. Button changes are never coalesced. A drag that leaves the window keeps delivering until the buttons are up, as in rio.
- **Keys** go to the focused window. Every key produces DE_KEY with down and up, autorepeat flagged, with the rune in `a` (0 for modifiers and dead keys) and a physical keycode in `b`.
- **Frame** events pace painting. After a `present`, the server sends DE_FRAME when the frame containing the new content has been composited. It does not yet mean the frame was scanned out: there is no vertical blank on this system until a display driver or virtio-gpu provides one, and docs/desktop-performance.md section 2.4 says what the server can honestly promise in the meantime. A client that wants tear-free animation waits for it before presenting again.
- **Resize** means the server has reallocated the image file. Its contents are undefined until the client presents. The window keeps showing the retained copy of the old content, scaled by nothing and clipped, until then.
- **Close** is a request. The client saves, then writes `delete`, or ignores it.
- **Overflow**: the queue holds 1024 events. When full, the server sets DS_LAGGING, sends DE_STATE, and drops motion and enter/leave only. Keys, buttons, resize, close and menu events are never dropped.

A desktop-level `events` file carries DE_WINNEW, DE_WINGONE, DE_WORKSPACE, DE_THEME and DE_QUIT for launchers and task bars.

### Painting

The `image` file is the client's back buffer: `dy` rows of `stride` bytes in `chan` format, both read from the descriptor. A client writes any rectangle with `pwrite` at `desk_pixeloffset(w, x, y)`. Writes larger than the connection's iounit are split by the kernel; the server assembles them. Nothing is visible until the client writes `present`.

`present [rect]` copies the rectangle (default: everything) from the back buffer into the server's front buffer for the window, redraws the frame if needed, composites the stack, and schedules DE_FRAME. The front buffer is never torn: composition reads it only between presents. This is **copy mode**, the default, and it is what makes the pixel path safe over a network: the client may keep writing the back buffer while the previous frame is on screen.

`mode flip` trades a copy for a constraint. The server holds two buffers; `present` swaps them, so the `image` file now refers to the buffer that was on screen one frame ago. The client must repaint everything it presents, or track two frames of damage. Use it for full-frame animation on a local machine.

A retained window (DF_RETAIN, the default) never receives DE_EXPOSE. Turning retention off with `flags -retain` frees the server's copy; the client then receives DE_EXPOSE with the rectangle it must repaint whenever the window is revealed, and must answer with a present.

### The draw path

> **On NeoDarwin** the fast path is `windows/N/surface`: a read returns a Mach port name (checked against the 9P attach's audit token) for a shared-memory back buffer with the same `present` semantics as `image`. The paragraph below describes the Plan 9 pilot's `draw(3)` path, kept for rio-compatible clients running under a 9P bridge; XNU has no `devdraw`.

Local libdraw clients need not move pixels through 9P. `winname` returns the name of a kernel `draw(3)` image that *is* the window's front buffer, the same way rio's `/dev/winname` does, so `initdraw` and `getwindow` work unchanged and drawing goes straight to the kernel with hardware blits. `wsys` allocates each window as a layer on the kernel screen and draws decorations in a separate layer above it. The `image` file and `winname` refer to the same pixels: a client may use either, and a screenshot tool reads `screen` regardless. On a DE_RESIZE a draw-path client calls `getwindow`, exactly as after rio's `r` mouse message.

### Reference loop

```c
/* one proc reads events into a channel; the main proc paints */
Deskevent e;
for(;;){
    recv(evc, &e);
    switch(e.type){
    case DE_RESIZE:
        w.stride = e.c; w.chan = e.d;               /* or re-read desc */
        w.r.x1 = w.r.x0 + e.a; w.r.y1 = w.r.y0 + e.b;
        realloc back buffer; damage = whole window;
        break;
    case DE_MOUSE:  handle(e.a, e.b, e.c); break;
    case DE_KEY:    if(e.d & DKF_DOWN) key(e.a, e.c); break;
    case DE_MENU:   menu(e.a, e.b); break;
    case DE_CLOSE:  save(); fprint(ctl, "delete\n"); return;
    case DE_FRAME:  inflight = 0; break;
    }
    if(damage is not empty && !inflight){
        paint(damage);                                  /* into local pixel buffer */
        for(y = damage.y0; y < damage.y1; y++)
            pwrite(imgfd, row(y, damage.x0), rowbytes(damage), desk_pixeloffset(&w, damage.x0, y));
        fprint(ctl, "present %d %d %d %d\n", damage.x0, damage.y0, damage.x1, damage.y1);
        inflight = 1; damage = empty;
    }
}
```

## 3. Frame, gadgets and menus

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

Menus are written to `windows/N/menu`, one directive per line, tab separated. The server shows the Desktop menu first, then the focused window's menus. Shortcuts are single runes with the meta key implied.

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

Selecting an item delivers DE_MENU with the menu id (0 for the Desktop menu) and item id; for `check` items the server toggles the mark and reports the new state. `off id` and `on id` grey an item without rewriting the file.

Dragging a file icon from a Workbench-style file browser onto a window delivers DE_DROP with the pointer position and the number of paths; the client reads `drop` to get them.

The drag itself is a desktop ctl message: `drag` *path*... says that the pointer is now carrying those paths. The server draws them at the pointer, and when the buttons come up it gives them to the window underneath - its `drop` file holds the paths, one per line, and DE_DROP says where the pointer was in that window's coordinates and how many there are. A release over the desktop, or over the window that started the drag, drops nothing. `drag` with no paths cancels. Only the server needs to know where the pointer went, so the program that started the drag never learns where its file landed, and a program that receives one needs nothing but the mask and two reads.

## 4. Record layouts

Both records are little-endian, 4-byte aligned, and identical to the C structs on x86_64 and aarch64. `desktop.h` provides portable pack and unpack functions for any other host.

**Deskwin, 96 bytes.** id, parent, workspace, depth; content rect x0 y0 x1 y1; frame rect x0 y0 x1 y1; mindx, mindy, maxdx, maxdy; flags, state, chan, stride; seq, pid, mode, reserved. All 32-bit.

**Deskevent, 32 bytes.** type, win, msec, seq, then four signed 32-bit payload words a..d whose meaning per type is given in `desktop.h` next to the enumerator.

Pixel format values are the channel descriptors of `draw(3)`, so `chan` in the descriptor is directly usable with `allocimage` and `chantostr`.

## 5. Compatibility with rio

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

## 6. Open points

- Per-output scale and colour profile live in `outputs/` (version 1.1); per-window overrides are still open.
- Compressed pixel writes for slow links (image(6) compressed rows on `image`) are reserved for version 2 behind `mode`.
- Sub-windows (Intuition gadgets inside a client's own window) are the client's business; the server has no widget toolkit.

## Pilot implementation status (plan-neo, roadmap D2)

`wsys` serves this tree. It is one process: lib9p delivers 9P requests in order, requests that must wait are parked on the stream they are waiting for, and composition runs when something has changed. What is implemented:

- The namespace of section 1, with `ctl`, `events`, `screen`, `snarf`, `theme`, `menubar`, `workspaces/N/{ctl,windows}` and `windows/{new,N/...}`. Reading `windows/new` allocates a window at open time, as `/dev/draw/new` does.
- Every window ctl message of section 1 and every desktop ctl message, the rio spellings included, with the descriptor available as text on `ctl` and as the 96-byte record on `desc`.
- Events: the 32-byte records, per-stream sequence numbers, the mask, motion coalescing, and the overflow rule that drops only motion and enter/leave.
- Painting: `image` as the back buffer addressed by `desk_pixeloffset`, `present` with copy and flip modes, retention, and `DE_FRAME` after each present.
- Frames and gadgets drawn by libstyle, with clicks on the close, depth, zoom, size and title gadgets turning into the ctl messages of the same name.
- Menus as data: the definition file, the Desktop menu, `on`/`off`, check items, the rendered bar on `menubar`, and `DE_MENU` on selection.
- The theme file with live reload: a write or a `set` line bumps the generation, reloads fonts, redraws every frame and posts `DE_THEME`.

The draw path of section 2 is in (roadmap D3): with a kernel screen, each window is a layer, `winname` names it, a libdraw client paints into it through `/dev/draw`, and a pixel-path client's `present` is loaded into the same layer. `wsys -n` keeps the in-memory framebuffer for a headless server. `docs/img/desk-screen.png` is a QEMU capture of the result.

Section 5 is in (roadmap D4): real input reaches the server from `/dev/mouse` and `/dev/scancode` (`wsys/input.c`), the `self/mouse`, `self/kbd` and `self/cons` streams carry it in rio's words, and `wsys` binds those and `winname`, `ctl`, `title` and `snarf` into `/dev` for a program it starts, so an unmodified rio program runs in a window. `cmd/term` is the terminal: it serves `cons` and `consctl` for its child, which is `rc`.

Roadmap D5 is in: `cmd/wb` is the Workbench-style browser, hidden windows have icons on the desktop that open them again, and `drag` delivers DE_DROP. A terminal that receives one types the paths, which is how a file dragged onto `cmd/term` becomes an argument.

`wsys -t` still accepts the test hook it needed before there were devices, since it is also how the conformance test presses gadgets: one extra desktop ctl message, `input mouse X Y BUTTONS [MODS]`, `input key RUNE [CODE] [MODS] [FLAGS]`, `input text STRING` (each rune as a key event) and `input probe X Y` (what the kernel holds at a point, for the draw path). `wsysconform` makes 53 checks over the protocol and runs in the boot test.
