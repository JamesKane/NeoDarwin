<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- Provenance: distilled from the plan-neo pilot (/Users/jkane/Development/c/plan-neo, commit b0a35a1, 2026-09-22), where this design was first written and its reference implementation built and tested. This is NeoDarwin's normative text. -->

# libstyle: the themeable style engine

Version 1, and what `wsys` implements today. Version 2 — classes, selectors with specificity, named frame and background resources, the three-file cascade in which the user's file always wins, and a client API so a program's own buttons are themed — is designed in `docs/ui-configuration.md`, which also adds the keybindings this version has none of. Everything below stays valid: a version 1 theme file loads unchanged under version 2.

The window server `wsys` draws every frame, gadget, shadow and menu bar through `libstyle`, a freestanding C23 library with no allocation and no dependency beyond `desktop.h`. Its input is a human-readable theme file; its runtime state is a `Theme` record that `wsys` exposes as the `/n/theme` tree so a theme can be edited live with `echo`. The default theme is Retro-Future Cyberpunk: near-black grounds, cyan neon edges, magenta-to-violet dithered title gradients, phosphor-green menu text, amber for the pressed state, VGA and Terminus bitmap fonts.

Files:

| Path | Role |
|---|---|
| `docs/desktop/style.h` | public API: `Theme`, raster, fonts, chrome |
| `libstyle/theme.c` | parser, formatter, `style_ctl` |
| `libstyle/raster.c` | software rasterizer over XRGB32 |
| `libstyle/font.c` | Plan 9 subfont loader, including the compressed image(6) decoder |
| `libstyle/chrome.c` | frames, gadgets, shadows, menu bar, hit testing |
| `themes/cyberpunk.theme`, `themes/amber.theme` | the default and a second theme that exercises every switch |
| `tools/styletest.c` | host renderer and round-trip test; produced the figures below |

> Everything in the library was built and run: it compiles freestanding for `x86_64-none-elf` and `aarch64-none-elf` with `-Wall -Wextra -Wpedantic -Werror`, and the renderer runs clean under AddressSanitizer and UBSan on the host, loading 9front's own `vga` and `terminus` subfonts.

## 1. Theme file format

One directive per line: `SECTION.KEY VALUE`. Blank lines and `#` comments are ignored. Keys are fixed; unknown keys are errors, so a typo cannot silently fall back. Parsing is atomic: a file with one bad line is rejected whole, with the line number, and the previous theme stays in force. `style_format` writes the same format back, and format, parse, format is byte-identical, which is what makes `/n/theme/theme` a file you can read, edit and write back.

### Palette

`palette.NAME COLOUR` where colour is `#rrggbb`, `#rrggbbaa` (alpha, used by shadows and scanlines), or `@NAME` to copy another entry already defined. The thirty names, grouped by what they paint:

| Group | Names |
|---|---|
| desktop | `desktop`, `grid` |
| frame | `frame`, `frame.inactive`, `edge`, `edge.inactive`, `edge.dark` |
| title | `title.text`, `title.text.inactive`, `title.shadow`, `scanline` |
| gadgets, four states | `gadget`, `gadget.ink`, `gadget.hover`, `gadget.hover.ink`, `gadget.active`, `gadget.active.ink`, `gadget.inactive`, `gadget.inactive.ink` |
| shadow | `shadow` |
| menu bar | `menubar`, `menu.edge`, `menu.text`, `menu.hilite`, `menu.hilite.text` |
| for clients | `accent`, `accent2`, `text`, `text.dim`, `back` |

Every colour the engine paints comes from this table, so a theme cannot half-apply.

### Chrome dimensions

```
frame.border   4      # thickness left, right, bottom
frame.title    20     # title bar height; gadgets are this tall
frame.gadget   20     # gadget width
frame.corner   16     # sizing gadget square, overlapping the content corner
frame.edge     1      # outline thickness, 0..2
frame.gap      1      # between gadgets
```

### Title gradients

`title.active` and `title.inactive` take `KIND #from [#to] [dither] [bands N]`. `flat` uses one colour. `vertical` and `horizontal` interpolate; `bands N` quantizes to N colour steps for the low-res look and `dither` breaks the boundaries with an ordered 4x4 Bayer pattern. Without `bands`, `dither` still snaps to 16 levels so the bar reads as bitmap art rather than a smooth ramp. `title.align left|center`, `title.pad N`, `title.scanlines on|off` (overlay the `scanline` colour on odd rows) and `title.textshadow on|off` complete the bar.

### Menu bar geometry

```
menubar.height   22
menubar.pad      8      # left inset of the first title
menubar.itempad  8      # horizontal padding inside a title
menubar.gap      4      # between titles
menubar.edge     1      # bottom rule thickness, colour menu.edge
menubar.mode     always # always: Mac; button: Amiga, shown while the right button is held
```

### Shadows and desktop

`shadow.style none|solid|checker|hatch` and `shadow.offset DX DY`. The shadow colour is `palette.shadow`, and its alpha is honoured, so `#000000e0` gives a near-solid shadow that still lets the desktop grid ghost through. `desktop.grid N` draws a dot every N pixels in `palette.grid`; 0 disables it.

### Font descriptors

`font.SLOT PATH [tracking N] [bold]` for the slots `text`, `title`, `menu`, `mono`. The path names a Plan 9 subfont file; `tracking` adds pixels between glyphs and `bold` is synthetic, drawn twice one pixel apart, which is how bitmap terminals did it. Version 1 loads one subfont per slot covering 0x20 to 0x7E; `.font` composites that span Unicode are resolved by `wsys` through the kernel's `string()` on the draw path and are a version 2 item for the software path.

## 2. Rendering pipeline

### Layers

1. **Theme** is parsed text; nothing in it is pixels.
2. **Raster** is nine primitives over an `Simage` (XRGB32 rows, any stride): fill with alpha, horizontal and vertical lines, outline, bevel, gradient, scanlines, checker and hatch patterns, dots, and a 1-bit mask blit. All clip to the image. No anti-aliasing anywhere: every edge is a whole pixel, which is the aesthetic.
3. **Fonts** load a subfont into caller-supplied scratch memory, decoding the compressed image(6) block format with the same 1024-byte window LZ decoder the kernel uses, and blit glyphs through the mask blit.
4. **Chrome** composes the primitives into frames, gadgets, shadows, the menu bar and the desktop, and answers geometry questions so `wsys` can hit-test without drawing.

### Drawing a frame

`style_drawframe` paints only the decoration ring and the title bar; the client's content rectangle is never touched, except by the sizing gadget corner that overlaps it as Intuition's did. Order:

1. fill the ring with `frame` or `frame.inactive`
2. outer outline, `frame.edge` pixels of `edge` or `edge.inactive`: the neon line
3. one-pixel `edge.dark` outline hugging the content, so content and frame never bleed
4. title bar: gradient, scanline overlay, one-pixel rule under it, then the title text with its one-pixel shadow, clipped to the space between gadgets
5. gadgets: close at the left; zoom and depth at the right, each a square `frame.gadget` wide and `frame.title` tall, with a one-pixel `edge.dark` separator on the side facing the title
6. sizing gadget in the bottom-right corner

Gadget colour comes from a four-way state chosen in `gadgetcolors`: window not focused, pressed (`active`), pointer over it (`hover`), or normal. The window server passes that state in `Sframestate` from its own pointer tracking; the engine has no idea what a mouse is. Gadget glyphs are one-pixel vector shapes (Intuition's filled-centre close box, two overlapping frames for depth, a frame with a filled corner for zoom, a staircase for size) drawn inside an inset square, so they scale with `frame.gadget` and never need bitmaps.

### Shadows

`style_drawshadow` paints only the part of the offset rectangle that peeks out from under the frame, so drawing order is shadow, content, frame with no overdraw of the window itself. High contrast comes from the default `#000000e0` solid style; `checker` and `hatch` reproduce the Workbench 1.3 and Mac wsys accessory looks with no alpha at all, which matters on the 1-bit and 8-bit paths.

### The hardware path

`wsys` on real hardware does not push every pixel through the software rasterizer. Each primitive has a direct equivalent in `draw(3)`, which the kernel executes with the display's blitter:

| raster primitive | draw(3) |
|---|---|
| fill, lines, outline, bevel | `draw(dst, r, colour, nil, ZP)` with a replicated 1x1 colour image; alpha fills pass the colour's alpha |
| gradient | a 1xH (or Wx1) strip rendered once per theme generation and title height, then `draw` with the strip as a replicated source |
| scanlines, checker, hatch, dots | a 4x4 or 2x2 tile with alpha, replicated |
| glyph blit | `string()` with the font, or `draw` with the subfont image as mask |
| shadow | one `draw` of a replicated tile clipped to the two exposed rectangles |

The cache is small and keyed by theme generation: gradient strips per distinct height, the four gadget states per gadget type as tiny images, the pattern tiles. When the generation changes the cache is dropped and rebuilt lazily. Because the software path and the draw path use the same rectangles from the same geometry functions, a frame looks identical whichever path drew it, and the software path is what runs on the pixel-only 9P clients and in tests.

### Damage

`wsys` redraws a frame only when its `Sframestate` changes: focus, hover gadget, pressed gadget, title, flags, or theme generation. Hover changes redraw one gadget rectangle, not the frame. Content presents never touch the frame. This is what keeps a theme with an alpha shadow and a dithered gradient cheap: those are drawn on state changes, which are rare, not on every frame.

## 3. Live reloading through the namespace

The theme engine's runtime configuration is a file tree served by `wsys` and bound at `/n/theme`:

```
/n/theme/
    ctl      w   set KEY VALUE | get KEY | reset | name NAME | load PATH | save PATH
    theme    rw  the whole theme in file format; write replaces it atomically
    gen      r   generation number; a read blocks until it changes
    list     r   every key, one per line
    fonts    r   loaded fonts with status, one per line
    KEY      rw  one file per key, in section directories: palette/edge, frame/border, ...
```

Every write goes through `style_ctl` or `style_parse` into a scratch `Theme`. On success `wsys` loads any font whose path changed, swaps the scratch in under its lock, bumps `gen`, drops the render cache, marks every frame and the menu bar dirty, wakes readers blocked on `gen`, and delivers `DE_THEME` to windows that asked for it. On failure the write returns the parser's error string and nothing changes. There is no restart and no partially applied theme.

Examples:

```
echo 'set palette.edge #ff3cac' > /n/theme/ctl     # one key, takes effect now
echo '#39ff14' > /n/theme/palette/menu.text          # same thing as a file
cat /n/theme/theme > mine.theme; ...; cp mine.theme /n/theme/theme
echo 'load /System/Library/Style/themes/amber.theme' > /n/theme/ctl
cat /n/theme/gen                                     # blocks until the next change
```

Clients that draw their own widgets link `libstyle`, read `/n/theme/theme` once, and keep one process blocked on `gen`; when it returns they re-read and repaint. A client on another machine mounts the same tree over 9P and follows the same theme.

`wsys -t PATH` picks the theme at startup, defaulting to `/System/Library/Style/themes/cyberpunk.theme`, and the compiled-in default in `style_default` is used if that file is missing or bad, so the desktop always comes up.

## 4. Figures

The two images below were rendered by `tools/styletest.c` through the software path. Left: the default cyberpunk theme with a focused window whose zoom gadget is hovered, an inactive window behind it, and a dialog whose close gadget is pressed. Right: the amber theme, showing flat title bars, centred titles, a checker shadow, thick edges and the Amiga-style menu bar mode.

## 5. Open points

- Multi-subfont fonts on the software path, needed for titles outside ASCII.
- Per-window theme overrides (`windows/N/theme` in wsys) for terminals that want their own palette.
- An 8-bit colour-mapped output path for the raster layer; today it is XRGB32 only, and the draw path already handles depth conversion in the kernel.

## Pilot implementation status (plan-neo, roadmap D1)

In the pilot, libstyle ran on the target itself: `styletest` is one source (`tools/styletest.c`) built twice, for the host over its libc and for the target over lib9, and it renders the same scene to a PPM either way. Fonts come from real Plan 9 files; a font(6) *font file* now loads as well as a single subfont file, so a font can be assembled from many subfonts (`sfont_parsefontfile` reads the ranges, `sfont_addsub` loads each one, and the renderer picks the subfont per rune and draws all of them on the font's baseline). The scene includes a line of Latin, Greek and Cyrillic drawn from three subfonts of `themes/font/neo.16.font`, and a star that no subfont covers, to show the fallback.

The check is the hash of the finished image. `themes/render.sums` holds one FNV-1a hash per theme; the host render (`tools/render.sh`, run by `meson test`) and the target render under QEMU (`tools/qemu-test.sh`, after `fontsetup.rc` copies the fonts from the flat boot file system into a ramfs tree) must both produce it. They do, for both themes, which is the pixel-identical result D1 asks for.


## Appendix A. The chrome study's tokens in this format

The visual reference for NeoDarwin's chrome (https://claude.ai/artifact/M4LEDkHLwxppcSSk6gsR7E) is written as CSS tokens. They are a rendering of this theme file; the mapping fixes which keys and version-2 resources (`docs/desktop/ui-configuration.md`) carry each token.

| Study token / effect | Theme file |
|---|---|
| `--void --panel --raised --well` | `palette.desktop`, `palette.frame`, `palette.back`, `palette.well` (new) |
| `--text --dim` | `palette.text`, `palette.text.dim` |
| `--mag --cyan --amber --green --red --violet` | `palette.accent`, `palette.accent2`, `palette.accent.mag` … (new named accents) |
| `--metal-hi --metal-lo --edge --bevel-hi --bevel-lo` | `background brushed { vertical @metal.hi @metal.lo hairlines 3 }`, `palette.edge`, `frame bevel { light @bevel.hi; dark @bevel.lo }` |
| `--lcd-bg --lcd-fg --lcd-ghost` | `palette.lcd`, `palette.lcd.ink`, `lcd.ghost 13` |
| glow `--gk`, `--bloom`, `--bevel` width, scanlines | `frame neon { glow N }`, `readout.bloom on|off` (new), `frame.bevel N`, `title.scanlines` |
| anodized panel, glass | `background anodized { noise 7 over vertical … }`, `background glass { blur 9 sheen @edge }` (new) |
| schemes `neon`, `neon-hc`, `daylight` | `.theme` files shipped beside `cyberpunk.theme` and `amber.theme` |

Two renderer backends sit behind one file: the whole-pixel bitmap backend described above (Plan 9 subfonts, no anti-aliasing) and the Vesper vector backend (anti-aliased, TrueType, glow and blur). A theme selects with `render bitmap|vector`; `font.SLOT` accepts a subfont path or a TrueType face.
