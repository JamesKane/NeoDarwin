<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Self-hosting

## 1. Goal

NeoDarwin builds NeoDarwin: on a NeoDarwin machine, one command builds the kernel collection, the system set and the ports tree, and the result is **byte-identical** to what CI built from the same commit on a macOS or Linux host. This is part of the 1.0 bar (`freebsd-parity.md` §1). FreeBSD's `make buildworld buildkernel` is the model for the experience; Bazel and the pinned toolchain are the mechanism (`build-system.md`).

## 2. Ladder

| Step | What runs on NeoDarwin | Epic | Main risk |
|---|---|---|---|
| 1 | clang, lld and the LLVM tools from the pinned toolchain, built for NeoDarwin by the same Bazel graph and shipped in the base with the SDK | P5-10 | none beyond the base libraries (P2-07) |
| 2 | `swiftc` and the Swift runtime, and `lldb` with `debugserver` over Mach exception ports | P5-10 | Swift's host dependencies (Foundation for the driver and SwiftPM). swift-corelibs-foundation is already planned as a package |
| 3 | Bazel, bootstrapped from source through the ports tree; it needs a JDK | P5-11 | an OpenJDK port. OpenJDK's BSD and macOS ports are the starting point, but HotSpot on a new OS is real work. The fallback is a Bazel built with GraalVM native-image on a host and shipped as a port binary, recorded as a temporary exception to "builds itself" |
| 4 | the whole graph: kernel, base, system set, then the ports tree with `ndports bulk` | P5-12 | build time and memory on the Q8B; the remote cache (P2-08) may be used, provided the check in §3 still passes with it off |

## 3. The check

The P5-12 exit test builds the system set on the board with the remote cache off, compares it with CI's artifact for the same commit, boots it as a new boot environment, and runs the parity workflow suite (`freebsd-parity.md` §4) in it.
