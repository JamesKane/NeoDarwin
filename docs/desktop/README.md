<!-- SPDX-License-Identifier: BSD-2-Clause -->
# NeoDarwin desktop specifications

These are the normative texts for the window system, the theme engine, interface configuration and the application–agent protocol. Each was designed and first implemented in the **plan-neo pilot** (`/Users/jkane/Development/c/plan-neo`, commit `b0a35a1`, 2026-09-22), whose reference implementations, a window server with a 53-check conformance test, `libstyle` with pixel-identical renders, `libagent` with an end-to-end demo, and `lib9p`, are carried into the NeoDarwin build (epic P4-14). The texts here are NeoDarwin's; each carries a provenance line and uses NeoDarwin's mount points.

| Specification | Companion header | What it fixes |
|---|---|---|
| [`window-protocol.md`](window-protocol.md) | [`desktop.h`](desktop.h) | `wsys` over 9P at `/n/desktop`: ctl vocabularies, `Deskwin` and `Deskevent` records, `image` + `present`, the `surface` fast path, `outputs/`, menus as data, drag and drop, workspaces |
| [`theme-engine.md`](theme-engine.md) | [`style.h`](style.h) | `libstyle` version 1: the theme file, raster primitives, chrome drawing, live reload at `/n/theme`; appendix A maps the chrome study's tokens onto the file |
| [`ui-configuration.md`](ui-configuration.md) | — | theme version 2 (classes, selectors, the user-wins cascade, named frames and backgrounds), binds, modes, window rules, the generated preferences window |
| [`agent-protocol.md`](agent-protocol.md) | [`agent.h`](agent.h) | `/n/agent/APPID/{schema,state,actions,log}`, `seq` and `if_seq`, the `agentd` registry, the rules for verbs and state |
| [`toolkit-charter.md`](toolkit-charter.md) (draft) | — | the toolkit API charter from the P4-05 study: principles, surface area, threading and ownership, GPU decision, bindings, compatibility layers, what it needs from the protocol, S7 validation |

The visual reference for the chrome is the study at https://claude.ai/artifact/M4LEDkHLwxppcSSk6gsR7E (NeXT, Amiga, BeOS, OPEN LOOK and IRIX lineage; procedural materials; `neon`, `neon-hc` and `daylight` themes).

## Names and paths

| Thing | NeoDarwin name |
|---|---|
| window system server | `wsys`; registered with `nsd` as `wsys.$user.$pid`; `$desktop` names it |
| window tree | `/n/desktop/…` (`ctl`, `events`, `screen`, `snarf`, `theme`, `menubar`, `binds`, `status`, `schema`, `outputs/`, `workspaces/`, `windows/`, `self`) |
| theme tree | `/n/theme/…` (`ctl`, `theme`, `gen`, `list`, `fonts`, per-key files) |
| agent trees | `/n/agent/{index,log,APPID/…}` |
| theme cascade | `/System/Library/Style/default`, `/System/Library/Style/app/NAME`, `~/lib/style`; themes under `/System/Library/Style/themes/` and `~/lib/style/themes/` |
| local pixel fast path | `windows/N/surface` (Mach shared memory); `image` + `present` is the portable and remote path |
| window-manager modifier | `Super`, delivered as `DK_META` |
| service registry | `nsd`, visible at `/n/sys/srv` |

## Pilot designs that feed later epics

The plan-neo pilot also holds worked designs that NeoDarwin schedules rather than specifies here:

| Pilot design | NeoDarwin epic |
|---|---|
| `libintuition` widget model (widgets hold no colour, font or size; declarative tree; arena allocation; measure/arrange; typed 32-byte events; two loops) | primary candidate in the toolkit API study, P4-05 |
| frame-budget rules (64×64 damage tiles, occlusion, compositor clock, `stats`, p99 under 4 ms, zero composites when idle) | `wsys`, P4-03 |
| package key chain and peer exchange | `ndpkg`, P2-04 |
| `edit` (rope with summaries, renderer seam, glyph atlas, tree-sitter, agent endpoint) | P7-04 |
| `dbg` (`.ndi` offsets-not-pointers index, immediate-mode UI, agent endpoint) | P7-05 |
| SoC interfaces (rings, doorbells, fences as blocking reads) | GPU design, P7-01 |

From the **NuAqua pilot** (`../NuAqua`) NeoDarwin takes the Vesper renderer, the Typeface TrueType stack and the scanline path filler, moved into NeoDarwin modules with refactored namespaces (P4-06, P4-14). From the **VectraOS pilot** (`../designs`) it takes the compositor / window-manager / shell role split and the capability discipline the namespaces design applies.
