<!-- SPDX-License-Identifier: BSD-2-Clause -->
# base

Darwin base: dyld, Libc, libdispatch, libplatform, libpthread, libmalloc, launchd, Foundation. Each component: `upstream.lock` + `patches/` + BUILD overlay. Epic P2-07.

**Phase 1 (P1-08b):** libSystem from Apple source, per `docs/base/libsystem.md`. `//base:sysroot` stages the headers; `base/<project>/build.sh` replays each project's Xcode target under a `base_library` rule (`rules/base.bzl`). Built so far: `libsystem_kernel` (xnu `libsyscall/`), `libsystem_platform`, `libsystem_pthread`, `libsystem_malloc`, each with an export test. `base/sdk` holds NeoDarwin's stand-ins for internal-SDK headers.
