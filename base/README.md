<!-- SPDX-License-Identifier: BSD-2-Clause -->
# base

Darwin base: dyld, Libc, libdispatch, libplatform, libpthread, libmalloc, launchd, Foundation. Each component: `upstream.lock` + `patches/` + BUILD overlay. Epic P2-07.

**Phase 1 (P1-08b, done):** libSystem from Apple source, per `docs/base/libsystem.md`. `//base:sysroot` stages the headers. `base/<project>/build.sh` replays each project's Xcode target under a `base_library` rule (`rules/base.bzl`), and each library has an export test. Built: dyld and libdyld, libSystem.B, and the libraries it reexports: libsyscall, libplatform, libpthread, libmalloc, Libc (with libsystem_darwin and libsystem_collections), libclosure, libdispatch, Libinfo, syslog, Libnotify, libmacho, copyfile, removefile, mDNSResponder's client and msun. It also builds objc4 and LLVM's runtimes (libc++, libc++abi, libunwind, libcompiler_rt). `base/standins` holds small libraries standing in for closed ones (libxpc, libsystem_trace, corecrypto and others), and `base/sdk` holds headers standing in for internal-SDK ones. `//base:root` merges it all into the runtime root, which `//images:hello_root` boots.
