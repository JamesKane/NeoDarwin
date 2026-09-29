<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Console and graphics foundation

## 0. Scope

NeoDarwin's graphics stop at a **console**: kernel text on the firmware framebuffer, a framebuffer device for userland, virtual terminals, and GPU drivers that userland can use. It does not include a window system, compositor, toolkit or desktop. Those belong to downstreams (Magi's `wsys` and desktop, `downstream.md`) or to ports. This matches FreeBSD, where `vt(4)` is in the base and graphical environments come from ports.

## 1. Stages

| Stage | What appears on screen | Mechanism | Roadmap |
|---|---|---|---|
| G0 kernel text console | boot log and panic text on the framebuffer | `neoboot` passes the UEFI GOP framebuffer in `Boot_Video`; XNU's own video console (`osfmk/console/video_console.c`) draws text with its built-in ISO font | P1 |
| G1 framebuffer device | `/dev/fb`-like access from userland | `ndfb.kext`: an `IOFramebuffer` subclass over GOP or a simple-framebuffer ACPI description, on Apple's open `IOGraphics` family | P4-01 |
| G1v virtual terminals | several logins on the framebuffer, switched from the keyboard, with scrollback and UTF-8 | a console server that owns `ndfb` and the HID keyboard, draws text with a bundled OFL console font, and serves a tty per terminal | P4-26 |
| G4 GPU | GPU drivers usable from userland, Vulkan through Mesa | a per-GPU kernel driver (kext or dext) plus Mesa userland where licences allow, first target chosen by licence and documentation | P7-01 |

## 2. Handing the screen to a display server

A downstream display server (Magi's `wsys`, or a port) takes over the framebuffer and input from the console the way a display server takes over from `vt(4)` on FreeBSD:

- the console server gives up `ndfb` and the keyboard when a session job with the display entitlement asks for them, and takes them back when that job exits or crashes, so a failed display server leaves a working console;
- a key chord (Ctrl+Alt+F*n*) is reserved for switching back to a text terminal;
- output modes, scale and colour profile are published by `ndfb` as IOKit properties, readable with `ioreg` and `--libxo json` tools.

## 3. Open question: graphical environments from ports

Most open-source display servers (wlroots compositors, Weston, Xorg's modern drivers) expect Linux DRM/KMS, GBM and libinput. NeoDarwin has `IOFramebuffer`, IOKit HID and, later, its own GPU drivers. The options:

1. a small DRM/KMS-shaped userland library over `ndfb` and IOKit HID, enough for wlroots' software path (the approach FreeBSD's `drm-kmod` avoids needing, because FreeBSD runs Linux's DRM in the kernel);
2. an Xorg or Weston backend written against `ndfb` directly;
3. no graphical environments in ports until the GPU work (P7-01) sets the interface.

This is decided with P7-01. It does not block 1.0, whose bar is the command line (`freebsd-parity.md`).
