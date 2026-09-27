<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Build system design

## 1. Decision

**Bazel (Bzlmod) with a hermetic, pinned LLVM + Swift toolchain, cross-compiling every target from any macOS or Linux host, with remote caching from day one.**

The build replaces four incompatible upstream systems: XNU's GNU make with Apple-internal helpers (`EmbeddedDeviceMap`, `kextsymboltool`, `setsegname`, `config`), Xcode projects (dyld, Libc, libdispatch, IOKit families), autotools/CMake in third-party code, and SwiftPM in NuAqua.

### Why Bazel

| Requirement | Bazel | CMake + Ninja | Buck2 | GN + Ninja (Fuchsia style) |
|---|---|---|---|---|
| Hermetic toolchain, no host leakage | `toolchains_llvm`, sandboxed actions | no | yes | partial |
| Multi-arch cross builds in one invocation | `--platforms`, transitions | painful | yes | yes |
| Swift + C/C++ + Starlark rules maintained by the vendor | `rules_swift`, `rules_apple` (Apple-maintained) | no Swift story | no Swift rules | no |
| Remote cache / remote execution API | yes | no | yes (RE API) | no |
| Package/image generation as first-class rules | custom rules + `rules_pkg` | scripts | custom rules | custom |
| Ecosystem size, hiring, docs | largest | largest but wrong shape | small | small |

Buck2 is the credible alternative (faster, Rust, same RE API). It loses on Swift rules and community size. CMake is the LLVM project's own build but has no hermeticity or multi-project graph and is rejected for the OS build; it remains acceptable *inside* vendored third-party packages that Bazel wraps.

## 2. Toolchain

- One tarball per host OS: `neodarwin-toolchain-<ver>` = clang, lld (`ld64.lld` for Mach-O, `lld-link` for the UEFI PE/COFF loader), llvm-objcopy/objdump/nm/dsymutil, `swiftc`, `swift-driver`, `sourcekit-lsp`, `compiler-rt`, built from `swiftlang/llvm-project` so Swift and C share one LLVM. Pinned by SHA-256 in `MODULE.bazel`.
- Target triples: `arm64-apple-darwin` for Darwin userland/kernel today (keeps upstream ABI assumptions), with a **`*-neodarwin`** vendor triple introduced when the ABI diverges (roadmap P2). AMD64 and RISCV64 add `x86_64-…` and `riscv64-…` platforms; RISC-V additionally needs a Mach-O `CPU_TYPE_RISCV64` and lld/llvm support (multi-arch design §4).
- Linker policy: **`ld64.lld` for everything, including the kernel.** XNU is linked upstream with Apple `ld64` using `-fixup_chains`, `-segprot`, `-kext` style flags and `LC_DYLD_CHAINED_FIXUPS`. Making the kernel link with `ld64.lld` is a tracked risk (`P0-06`): the fallback is to vendor Apple's open-source `ld64` (cctools-port) as a host tool while lld catches up.
- Embedded Swift is the same `swiftc` with `-enable-experimental-feature Embedded` (or its stable spelling) and `-no-allocations`; `swift_embedded_binary`/`swift_embedded_library` rules produce freestanding Mach-O/ELF and, for `neoboot`, the PE/COFF image via `lld-link`. The language policy's CI gates (strict concurrency, no-allocation diagnostics for T2 modules, `-no-allocations` for T3, `lang-audit` for C/C++ justifications) are Bazel aspects.
- Sanitizers, coverage (`-fprofile-instr-generate`), and static analysis run through the same toolchain via Bazel configs (`--config=asan`, `--config=cov`, `--config=analyze`).

## 3. Repository build layout

```
MODULE.bazel                     # bzlmod root; toolchain and rules deps pinned
.bazelrc                          # --config= for arch, kernel variant, sanitizers, remote cache
platforms/                        # constraint_values: os:neodarwin, cpu:{aarch64,x86_64,riscv64}, board:{sbsa,qemu-virt,cd8180}
toolchains/                       # LLVM/Swift toolchain definitions, cc_toolchain_config, swift toolchain
rules/                            # NeoDarwin Starlark rules (below)
kernel/                           # xnu vendored + patches + BUILD (see repository.md)
kexts/<family>/BUILD
boot/neoboot/BUILD
base/<component>/BUILD            # dyld, Libc, libdispatch, launchd, …
services/<daemon>/BUILD
desktop/nuaqua/BUILD
tools/<tool>/BUILD
third_party/<pkg>/BUILD           # wrapped upstream (CMake/autotools via rules_foreign_cc, or native BUILD)
images/BUILD                      # system_image, esp_image, ramdisk targets
```

## 4. Custom rules (the cohesive part)

| Rule | Produces | Replaces |
|---|---|---|
| `xnu_kernel(name, board_config, kernel_config = "RELEASE", options = [...])` | `kernel.<config>.<board>` Mach-O + symbol files | `make TARGET_CONFIGS=…`; internally drives `config` and `newvers` as hermetic host tools in phase 1, native `cc_*` graph in phase 2 |
| `kernel_config(name, master_files, options)` | generated `config.h`/Makefile fragments | `SETUP/config` |
| `mig_library(name, defs, user_side, server_side)` | MIG C stubs as `cc_library` | Xcode MIG build phases |
| `exports_list(name, srcs)` | linker export/alias files | `config/*.exports` + `generate_linker_exports.sh` |
| `kext(name, srcs, info_plist, deps, bundle_id)` | `.kext` bundle with `Info.plist`, symbol set validation against the kernel's exports | Xcode kext templates + `kextsymboltool` |
| `kext_collection(name, kernel, kexts, kind = "boot" \| "system" \| "aux")` | `MH_FILESET` kernel collection via `kcgen`, fixup chains emitted | `kmutil create` |
| `dext(name, …)` | DriverKit-style userland driver bundle | Xcode |
| `nd_package(name, manifest, files, deps, hooks)` | `.ndpkg` with signed manifest | none |
| `system_image(name, packages, kernel_collection, rootfs = "zfs" \| "hfs" \| "ramdisk")` | bootable image + ESP tree | none |
| `qemu_test(name, image, machine, cpus, expect)` | test rule that boots the image and greps serial | ad-hoc scripts |
| `swift_library/ swift_binary` (from `rules_swift`) | Swift 6 modules with strict concurrency | SwiftPM |
| `swift_embedded_binary`, `swift_embedded_library` | freestanding Embedded Swift (no runtime, `-no-allocations`), PE/COFF for UEFI or Mach-O/ELF | none |
| `kext_swift` (experimental) | Embedded Swift objects linked into a `kext` behind C entry points | none |

Phase 1 is implemented (`rules/xnu.bzl`, `tools/xnu/`): `nd_build_sdk` produces NeoDarwin's additions to the host SDK as real files, each action assembles its own overlay SDK, `xnu_headers` and `libfirehose_kernel` build from pinned Apple archives, and `xnu_kernel` runs the upstream makefiles, optionally in link-gap-report mode. See `kernel/README.md` for the targets and the settings that differ from Apple's.

The `xnu_kernel` rule is delivered in two phases so the kernel port is never blocked on build-system work: **phase 1** (P0) wraps the upstream makefiles in a sandboxed `genrule` with the hermetic toolchain injected via `CC=`, `LD=`, `HOST_*`, and `EXTRA_TARGET_CONFIGS`; **phase 2** (P2) replaces the makefiles with a native graph generated from XNU's `conf/files*` lists by a converter kept under `tools/xnu2bazel` so future Apple drops re-convert mechanically.

## 5. Host tools

Written in Swift 6 (language policy T1); all built by the same graph and used as Bazel `tool` inputs, so a host tool change invalidates exactly the outputs that depend on it.

| Tool | Role |
|---|---|
| `kcgen` | links kernel + kexts into an `MH_FILESET` with `LC_FILESET_ENTRY` and fixup chains; validates symbol resolution against export lists |
| `kcheck` | verifies every rebased pointer in a collection lands inside the image (loader-fixup correctness) |
| `dtdump` | prints/validates an Apple-format device tree against `dt-abi.md` |
| `ndimage` | assembles ESP + system image from packages |
| `ndsign` | Ed25519 signing of manifests, kernel collections and images; keys in `keyd` |
| `xnu2bazel` | converts XNU `conf/files*`, `Makefile` fragments and `MASTER*` configs into BUILD files |
| `lang-audit` | lists every first-party C/C++ file with its justification line; fails CI on a missing one |

## 6. Reproducibility and CI

- `SOURCE_DATE_EPOCH`, deterministic UUIDs (`-no_uuid` + content hash), no absolute paths (`-ffile-prefix-map`), sorted inputs. A nightly job builds twice on different hosts and diffs.
- Remote cache (bazel-remote or Buildbarn) from P0; remote execution optional later.
- Every PR: `bazel test //...` for host tools and unit tests, `qemu_test` smoke for `qemu-virt` on 1 and 4 CPUs, 16K page kernel. Nightly: full matrix (page sizes, CPU counts, TrustZone firmware, sbsa-ref, x86_64 once available) and hardware-in-the-loop.
- CI definitions are GitHub-Actions-compatible YAML executed by self-hosted runners so they run unchanged under Forgejo Actions (repository design §5).

## 7. Migration of existing components

| Component | Upstream build | Plan |
|---|---|---|
| xnu | GNU make | phase 1 wrap, phase 2 native (§4) |
| dyld, Libc, libplatform, libpthread, libmalloc, libdispatch, Libinfo, Libnotify, libsystem | Xcode projects | `xcodeproj2bazel` one-shot conversion per drop; hand-maintained BUILD overlays under `base/<component>/` |
| IOKit families (IOPCIFamily, IOStorageFamily, IOGraphics, hfs) | Xcode | `kext` rule with BUILD overlays |
| swift-corelibs-foundation, libdispatch (swift branch) | CMake | `rules_foreign_cc` initially, native later |
| ACPICA, libwayland, zstd, libarchive, sqlite | CMake/autotools | `rules_foreign_cc` or native BUILD; vendored with SHA pins |
| NuAqua | SwiftPM | `rules_swift` BUILD files; SwiftPM manifest kept for macOS-host development |
| pkgsrc bootstrap | bmake | runs *outside* Bazel in a builder container; its outputs are imported as packages (packaging design §6) |
