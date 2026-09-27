<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Graphics console, window system and desktop

## 0. Direction

- **Visual direction: 1990s workstation lineage, procedurally drawn, owned by the user.** NeXT menus and dock, Amiga window gadgets and an MUI-style preferences panel, BeOS/Haiku vector icons, OPEN LOOK tear-offs, IRIX-style workspaces, and retro-cyberpunk readouts (LCD, LEDs), rendered from gradients, bevels, glow, blur and procedural noise with no bitmaps, and re-skinnable through theme files. The visual reference is the chrome study at https://claude.ai/artifact/M4LEDkHLwxppcSSk6gsR7E.
- **The protocols are NeoDarwin's own and are specified in `docs/desktop/`:** the window protocol over 9P, the `libstyle` theme engine and its configuration model (classes, cascade, binds, modes, rules), and the application–agent protocol. They were designed and first implemented in the plan-neo pilot and are carried forward with NeoDarwin's mount points and fast path.
- **The renderer core comes from the NuAqua pilot:** the framework-free Vesper renderer, the Typeface TrueType rasteriser and the scanline path filler move into NeoDarwin modules with refactored namespaces. NuAqua's Aqua theme and widget scene do not.
- **Later modernisation** takes what is becoming standard in alternative Linux desktops (tiling and scrollable layouts, keyboard-first workspaces, overview modes, layer-shell panels, portals, fractional scaling, colour management). Binds, modes and rules are already in the configuration model; the rest is a scheduled study (§7).
- **The application API is not settled.** Before the toolkit's public surface is designed, a study of the existing platform APIs (Windows, macOS, Linux, Haiku, Plan 9) and of open-source applications built on them decides what the API should look like: where the platforms agree and developers are productive, and where getting performance out of them is high-friction (§6). Until then the principles in §6.2 are inputs, not decisions.

## 1. Staged path to pixels

| Stage | What appears on screen | Mechanism | Roadmap |
|---|---|---|---|
| G0 kernel text console | boot log and panic text on the framebuffer | `neoboot` passes the UEFI GOP framebuffer in `Boot_Video`; XNU's own video console (`osfmk/console/video_console.c`) draws text with the built-in ISO font | P1-M1 |
| G1 framebuffer device | `/dev/fb`-like access from userland | `ndfb.kext`: an `IOFramebuffer` subclass over GOP or a simple-framebuffer ACPI description, on Apple's open `IOGraphics` family | P4-01 |
| G2 window system | windows, cursor, damage, vsync, workspaces | `wsys`, the window system server, compositing in software with the Vesper renderer; input from `inputd` | P4-02..04 |
| G3 desktop chrome | screen bar, docked and pop-up menus, dock with live tiles, column file viewer, prefs, terminal | the chrome on the native protocol; Wayland clients side by side | P4-06.. |
| G4 acceleration | GPU-composited scenes, Vulkan for clients | per-GPU kernel driver plus Mesa userland where licences allow; same command lists | P7 |

## 2. Window system (`wsys`)

- **Protocols.** Two front doors, one window table: (a) the **native protocol is the NeoDarwin window protocol over 9P** (`docs/desktop/window-protocol.md`): text `ctl` lines, 96-byte `Deskwin` and 32-byte `Deskevent` records, `image` + `present` painting with copy and flip modes, menus as data, drag and drop, workspaces, served at `/n/desktop`; NeoDarwin adds a `surface` file that hands out a Mach shared-memory buffer as the local fast path and an `outputs/` tree for per-output scale and colour profile; (b) **Wayland core** (`wl_compositor`, `wl_shm`, `xdg_shell`, `wl_seat`, `wl_output`) over a Unix socket so existing toolkits, terminals and browsers run; a Wayland surface appears in `windows/N` like any other. Xwayland is an optional package.
- **Namespace.** `wsys` serves `/n/desktop/{ctl,events,screen,snarf,theme,menubar,binds,status,schema}`, `/n/desktop/workspaces/N/`, `/n/desktop/windows/{new,N/{ctl,desc,events,image,surface,screen,title,menu,cursor,icon,drop,mouse,kbd,cons}}`, `/n/desktop/outputs/NAME/`, and `self` in a window-scoped attach. The theme tree is `/n/theme`. The column file viewer browses these trees directly; the dock's LEDs read `/n/sys/srv/*/state`.
- **Roles.** Compositor (pixels), window-manager policy (placement, workspaces, focus, key-chords) and shell chrome (screen bar, dock, menus) are separate modules with separate capabilities; the shell is a leaf that the session can run without.
- **Scene data model and frame rules.** Surfaces, transforms, damage and buffer slots live in struct-of-arrays tables indexed by generation-checked handles; a frame is a pure function from tables to a command list consumed by the software renderer today and a GPU backend later. The frame rules (from the plan-neo pilot's frame-budget design) are: damage as a 64×64 tile bitmap, front-to-back occlusion, the compositor on its own clock, nothing on the frame path waits for a client, `DE_FRAME` means composited and nothing more, `stats` exposes frames, composites, dropped, p50/p99 ms and damage tiles; targets are p99 under 4 ms and zero composites when idle.
- **Window management.** Every gadget action is a ctl message (`raise`, `lower`, `zoom`, `hide`, `snap`, `close`, `workspace n`) and every key binding writes one, so agents and keys share a path (`docs/desktop/ui-configuration.md` §3.1). Binds, modes (Hyprland-style submaps and a leader key) and window rules come from `/n/desktop/binds`; there is no automatic tiling: `snap grid cols rows i` tiles on request and nothing moves a window the user did not ask to move. Workspaces switch by `Super+1…9`, `Super+Tab`, `Super+O` overview; menus are docked at top-left, popped under the pointer on right-click, or torn off.
- **Outputs.** Per-output mode, logical scale (fractional), refresh and colour profile are first-class from the start because the chrome is procedural and must stay sharp at every scale.

## 3. Input (`inputd`)

Reads HID reports from USB HID and virtio-input dexts via IOKit queues; normalises to a compact event array with monotonic timestamps; forwards batches to `wsys` and to `/n/sys/input/events`. Keymaps, pointer acceleration, gestures and the key-chord table are data files under `/n/sys/input/config`, hot-reloaded.

## 4. Chrome vocabulary and theming

### 4.1 Lineage per element (from the study)

| Element | Lineage | Decision |
|---|---|---|
| Window frame | Amiga, NeXT | close gadget left; zoom and depth right; brushed-metal title bar; active window marked by an accent underline and glow, inactive frames stay dim |
| Main menu | NeXT | vertical, docked top-left, draggable; no global menu bar; each item a bevelled key with right-aligned shortcut |
| Pop-up menu | NeXT | right button pops the main menu under the pointer; submenus cascade beside it |
| Torn-off submenu | NeXT, OPEN LOOK | any menu or popup tears off into a panel with its own close box |
| Preferences | Amiga MUI | page list left, framed groups with titles set into the border, grooved-metal slider knobs, cycle gadgets, Save / Use / Cancel; the user owns the look |
| Readouts | retro-cyberpunk | backlit LCD with ghost segments, switchable bloom; LEDs for server health (green running, amber degraded, off stopped) |
| File viewer | NeXT, Plan 9 | shelf, icon path, horizontal scroller, Miller columns over namespace paths; union and served directories marked by emblem; status line names the file server |
| Dock | NeXT, BeOS replicants | right edge; app tiles with running LED; live clock and load tiles |
| Screen bar | IRIX desks | current workspace, output mode/scale/profile, status messages |
| Icons | BeOS/Haiku | three-quarter perspective, heavy dark outline, lit top-left, cast shadow; type shown by a corner emblem; vector format modelled on Haiku's HVIF so one icon serves 16–128 pt |

### 4.2 Materials the renderer must produce

Anodized panel (fractal noise over a gradient, bevelled), brushed metal (interleaved hairlines over a gradient), glass (backdrop blur with a specular edge), backlit LCD (inner shadow, sheen, ghost segments, bloom), LEDs (radial highlight plus glow), knobs (knurled edge from a repeating conic gradient, masked tick ring, glowing indicator). Every material is procedural so it is sharp at any output scale and cheap to re-colour.

This fixes the Vesper roadmap: to what it has today (SDF rounded rects, linear gradients, pinstripe, scanline path fill, glyph rasterising) it must add inner and outer shadows, glow/bloom, backdrop blur, procedural noise, repeating and conic gradients, masks, and an HVIF-style vector icon format. GPU backends later consume the same command list.

### 4.3 Scheme engine: the user owns the look

The engine is `libstyle` (`docs/desktop/theme-engine.md`; version 2 in `docs/desktop/ui-configuration.md`), extended with the study's materials. The chrome study's tokens are a rendering of the same theme file (theme-engine.md, appendix A). In the study's vocabulary:

- A **scheme** is a data file of tokens: surfaces (`void`, `panel`, `raised`, `well`), text (`text`, `dim`), accents (`mag`, `cyan`, `amber`, `green`, `red`, `violet`), metals (`metal-hi`, `metal-lo`, `edge`, `bevel-hi`, `bevel-lo`), readouts (`lcd-bg`, `lcd-fg`, `lcd-ghost`), and effects (`glow`, `bloom`, `bevel` width, `scanlines`, `reduce-motion`), plus per-output overrides.
- Shipped schemes: `neon` (default), `neon-hc` (high contrast), `daylight`. Users add schemes as files under `~/lib/prefs/schemes/` and pick them in the MUI-style prefs panel or by writing `/n/wsys/ctl`.
- **Contrast is a test.** Every scheme is checked for WCAG AA body-text contrast against `panel` and `raised` at build time; the study already flags the two neon tokens that fail on raised surfaces, so the engine must be able to auto-derive compliant variants for text on those surfaces.
- Type roles are fixed, faces are themable: **chrome** (title bars, legends, buttons; uppercase, wide tracking), **interface** (menus, labels, body), **readout** (LCD only, never prose), **namespace** (paths, file contents, key equivalents). Default faces are OFL-licensed (Chakra Petch, IBM Plex Sans Condensed, VT323, IBM Plex Mono) and are bundled, which removes NuAqua's proprietary-font stand-in problem.
- Theming is scheme-level, not widget-level: apps never hard-code colours; they name classes (`button`, `field`, `list`, `scroll`, `tip`, …) and the cascade (system, application, user; the user always wins) decides.
- Two rendering backends behind one file: `libstyle/raster.c` for whole-pixel bitmap art with Plan 9 subfonts, Vesper for the anti-aliased procedural look with TrueType faces; a theme picks `render bitmap|vector`.

## 5. Terminal, viewer, prefs: the first three clients

The graphic terminal (serves `cons` to its child; scrollback exported through the agent protocol), the column file viewer over `/n` (Miller columns, shelf, icon path), and the preferences panel, which is *generated* from the theme class table as ui-configuration §2.6 describes, are the first native clients and the Phase 4 exit tests. All three are shell furniture: the session runs without them.

## 6. Application APIs: study first, then design

### 6.1 The study (epic P4-05, before any public toolkit API is frozen)

The study is research on APIs and source code, not a survey of developers. It reads what the major platforms offer and what real applications actually call, and it looks for two things: the common ground where the platforms have converged and developers are productive, and the places where applications fight the platform to get performance.

- **Platform APIs.** For each platform, the surface an application reaches for windows, input, drawing and GPU, audio, timing, files and I/O, threads and memory:
  - *Windows:* Win32 and the message loop, DXGI and Direct3D 12, WinUI/WinRT, GameInput and Raw Input, WASAPI, IOCP, DirectStorage, multimedia timers and MMCSS.
  - *macOS:* AppKit, Core Animation and `CAMetalLayer`, Metal and Metal compute, GCD and QoS classes, Core Audio, IOSurface, the `GameController` framework, `kqueue`.
  - *Linux:* Wayland core and `xdg_shell` plus X11, DRM/KMS and GBM, Vulkan and `VK_KHR_present_wait`, EGL, libinput and evdev, PipeWire and ALSA, `io_uring` and `epoll`, `memfd` and dma-buf.
  - *Haiku and BeOS:* `BApplication`/`BWindow`/`BView`, `BLooper` messaging, `app_server`, the Media Kit.
  - *Plan 9:* `draw`, `libcontrol`, `/dev/mouse` and `/dev/cons` as files.
- **Cross-platform layers.** SDL3, GLFW, sokol, raylib, bgfx, wgpu and Dawn, Dear ImGui and egui, Qt and GTK. Each is evidence of the lowest common denominator its authors settled on and of the per-platform backend code it takes to reach it.
- **Applications with source.** Chosen because they push performance and carry per-platform backends that can be read and measured:
  - *Game engines and games:* Godot, Bevy, O3DE, the open id Tech releases (Quake III, Doom 3 BFG), Ogre, LÖVE.
  - *Translation and emulation layers:* Wine and Proton, DXVK and VKD3D-Proton, MoltenVK, Dolphin, RPCS3. These are a direct record of where one platform's model does not map onto another's.
  - *High-performance compute:* llama.cpp and ggml, PyTorch's device backends, Blender and Cycles (CUDA, HIP, Metal, oneAPI), OpenMM, and the CUDA, ROCm/HIP, SYCL, OpenCL, Metal compute and Vulkan compute models they target.
  - *Interactive tools:* Blender's UI, Krita, Chromium's and Firefox's compositors, terminals such as Alacritty and Ghostty, editors such as Zed and Lapce, audio tools that use JUCE.
- **Method.**
  - *Call-surface analysis:* which platform calls each project actually makes, and how often. The intersection across platforms is the candidate core API. Calls every project wraps the same way are candidates for promotion into the platform.
  - *Backend weight:* the size and churn of each project's per-platform directory (Godot `platform/`, SDL `src/video/*`, Blender `intern/ghost`, ggml's backends). A large or often-patched backend points to friction.
  - *Workaround mining:* platform `#ifdef`s, comments marking hacks and workarounds, driver-bug tables, and issue-tracker and commit history on performance regressions.
  - *Prototypes:* four throwaway prototypes built against candidate API shapes on NeoDarwin's protocols: a game loop, a text editor, a synth UI, and a GPU compute job whose result is displayed without a copy.
- **Friction to look for.** The study asks whether each of these is a real problem for the corpus and, if so, what NeoDarwin should provide instead:
  - frame pacing and present timing, and variable refresh;
  - shader and pipeline-state compilation stutter, and pipeline caching;
  - main-thread-only window and event rules;
  - input latency and high-rate or raw input;
  - audio latency and real-time thread scheduling;
  - timer resolution and sleep accuracy;
  - thread placement on heterogeneous cores (QoS, affinity);
  - GPU memory residency, and zero-copy sharing among CPU, GPU compute and the compositor, including on unified-memory machines;
  - asset streaming and asynchronous file I/O;
  - large pages and pinned memory;
  - the effort of keeping a separate backend for each GPU API;
  - build and packaging friction.
- **Output.**
  - *Toolkit API charter:* the surface area; the language bindings that matter (Swift native, C ABI for everyone else, Zig and Rust bindings generated from the C ABI); the ownership and threading model; and the compatibility layers worth shipping (an SDL3 backend is likely). The toolkit itself is Swift (language policy T1, with T2 on the draw and event paths).
  - *Friction register:* each observed high-friction point, the evidence behind it (projects, code sites, issues), and a proposed NeoDarwin answer, tagged with the layer that should own it (toolkit, `wsys` fast path, GPU and compute stack in P7, or kernel scheduler and VM).
- **Already fixed, below the toolkit.** The window protocol (§2), the theme classes a widget must draw through, and the agent protocol every app serves are not reopened by the study. If a friction finding needs something from them (a present-timing event, a zero-copy surface kind), it is filed against that protocol as an extension proposal.

### 6.2 Candidate principles (inputs to the study, not decisions)

**Primary candidate:** the `libintuition` widget model from the plan-neo pilot (see `docs/desktop/README.md`): widgets hold no colour, no font and no self-chosen size; a declarative tree in one expression; arena-only allocation; two-pass measure/arrange; 32-byte typed events; reactive and frame-synchronised loops; theme reload without a tree rebuild; only dirty widgets draw, one present per frame, nothing presented if nothing changed. The study tests it against what the application corpus actually calls.

Other candidate principles: Retained tables rather than object graphs; diffs as the update protocol; generation-checked handles across boundaries; protocols for behaviour and structs for data; batch everything that crosses a boundary; explicit ownership and lifetimes; every window and state tree exported under `/n/app`; accessibility as a view over the same node table; versioned data ABIs; sympathy for the hardware (tile-based rendering, frame callbacks that stop when nothing is damaged).

## 7. Modernisation study (epic P4-11, after the first chrome ships)

Binds, modes and window rules (`docs/desktop/ui-configuration.md` §3) are the first modernisation and are already in the configuration model. Further candidates to evaluate against the Plan Neo direction, taken from where alternative Linux desktops are converging: scrollable and tiling layouts (niri, Hyprland, sway, COSMIC's tiling toggle) as an optional workspace mode; dynamic workspaces and overview (GNOME, COSMIC); `wlr-layer-shell` for panels and docks; `foreign-toplevel`, `screencopy`, `idle-inhibit`, `tearing-control`; XDG desktop portals for sandboxed apps (which map naturally onto `/n` capabilities); fractional scaling and `wp_color_management`; VRR; notification daemons and launchers of the rofi/wofi kind. Each is adopted only if it holds up against the §6.1 study's findings and can be drawn in the chrome vocabulary.
