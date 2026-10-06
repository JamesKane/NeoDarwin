<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Build system design

## 1. Decision

**Bazel (Bzlmod) with a hermetic, pinned LLVM + Swift toolchain, cross-compiling every target from any macOS or Linux host, with remote caching from day one.**

The build replaces three incompatible upstream systems: XNU's GNU make with Apple-internal helpers (`EmbeddedDeviceMap`, `kextsymboltool`, `setsegname`, `config`), Xcode projects (dyld, Libc, libdispatch, IOKit families), and autotools/CMake in third-party code.

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

- **Build hosts (decided 2026-10-06):** builds run on macOS only. Linux hosts run QEMU tests on artifacts built on macOS. Apple's macOS SDK is licensed for Apple hardware, and the base builds against it (`-isysroot`), so Linux builds wait for a NeoDarwin SDK assembled from Apple's open-source (APSL) headers and the sysroot `base/` stages. That belongs with self-hosting (P5-10).
- One tarball per host OS: `neodarwin-toolchain-<ver>` = clang, lld (`ld64.lld` for Mach-O, `lld-link` for the UEFI PE/COFF loader), llvm-objcopy/objdump/nm/dsymutil, `swiftc`, `swift-driver`, `sourcekit-lsp`, `compiler-rt`, built from `swiftlang/llvm-project` so Swift and C share one LLVM. Pinned by SHA-256 in `MODULE.bazel`.
- Target triples: `arm64-apple-darwin` for Darwin userland/kernel today (keeps upstream ABI assumptions), with a **`*-neodarwin`** vendor triple introduced when the ABI diverges (roadmap P2). AMD64 and RISCV64 add `x86_64-…` and `riscv64-…` platforms; RISC-V additionally needs a Mach-O `CPU_TYPE_RISCV64` and lld/llvm support (multi-arch design §4).
- Linker policy (revised 2026-10-06, user decision): **`ld64.lld` for UEFI and userland where it works; Apple's open-source `ld64` (ld64-957.1, APSL), built from source, for the kernel and kexts** (P0-06, done 2026-10-06: §2.1, `toolchains/ld64/README.md`). Today's `ld64.lld` can't link a static Mach-O kernel or a kext (§2.1.2). `--//rules:kernel_linker=xcode` still selects the host Xcode's `ld` as a fallback. The base userland links with Xcode's `ld` until P0-02 decides between ld64 and lld for it. The earlier policy, "`ld64.lld` for everything", remains a goal if lld gains static kernels, `-segment_order`, split-seg info and `-kext`.
- Embedded Swift is the same `swiftc` with `-enable-experimental-feature Embedded` (or its stable spelling) and `-no-allocations`; `swift_embedded_binary`/`swift_embedded_library` rules produce freestanding Mach-O/ELF and, for `neoboot`, the PE/COFF image via `lld-link`. The language policy's CI gates (strict concurrency, no-allocation diagnostics for T2 modules, `-no-allocations` for T3, `lang-audit` for C/C++ justifications) are Bazel aspects.
- Sanitizers, coverage (`-fprofile-instr-generate`), and static analysis run through the same toolchain via Bazel configs (`--config=asan`, `--config=cov`, `--config=analyze`).

### 2.1 Linking the kernel and kexts (P0-06, done)

**Status (2026-10-06):** Apple's ld64-957.1, built from source (`//toolchains/ld64`, `toolchains/ld64/README.md`), links the kernel and the kexts by default. `--//rules:kernel_linker` (`rules/BUILD.bazel`) selects the linker: `ld64` (default), `xcode` (the host Xcode's `ld`, ld-prime `ld-27037.1`) or `lld` (§2.1.2, kernel only). The kernel and kexts it links pass the same QEMU tests as the Xcode-linked ones, so P0-06's exit holds (§2.1.1). ld64 is built without libtapi, libLTO and bitcode support, which the kernel and kexts don't use.

#### 2.1.1 The from-source ld64 against Xcode's ld

**The build.** `toolchains/ld64/build.sh` replays `ld64.xcodeproj`'s `ld` target with Xcode's clang. `compat/` stands in for the internal-SDK headers (corecrypto, CommonDigestSPI, CrashReporterClient, `os/lock_private.h`, `dyld_priv.h`). `nd_stubs.cpp` replaces the LTO, `.tbd` and bitcode-bundle sources, so those inputs fail with an error that names the cause. The binary is reproducible: `SOURCE_DATE_EPOCH=0`, and two builds produce the same SHA-256. `//toolchains/ld64:ld64_smoke_test` checks `-v` (`PROJECT:ld64-957.1`), a static `LC_UNIXTHREAD` link and the `.tbd` refusal.

**How it's wired.** For `xnu_kernel`, the `ld` binary is copied into the XNU tree as an overlay (`tools/nd/ld64/ld`), and XNU's `LD` becomes `$(KC++) --ld-path=$(SRCROOT)/tools/nd/ld64/ld -nostdlib`. The rest of XNU's link line is unchanged. For `nd_kext`, the kext scripts link with `$ND_KEXT_LD` (`kexts/zfs/kext.sh`, `kexts/swift_trial/kext.sh`). clang passes `-lto_library` unconditionally, and ld64 accepts and ignores it.

**The kernel's structure** (`kernel.release.sbsa{,.unstripped}`, the same objects linked by each linker):

| | Xcode `ld` | ld64-957.1 |
|---|---|---|
| file type, flags, load commands | `MH_EXECUTE`, `0x200001`, 26 commands (5872 bytes), `LC_UNIXTHREAD` | identical |
| base and segment order | `__TEXT` at `0xfffffe0007004000`, `__PRELINK_TEXT` at `0xfffffe0004004000`, XNU's `-segment_order` | identical |
| segment sizes | `__DATA_CONST` 0x18c000 | `__DATA_CONST` 0x190000. Every other segment has the same size |
| section order | `__DATA_CONST,__mod_init_func` last in its segment | first (ld64's built-in ordering), so `__const`'s 16 KiB alignment adds one page. Every later segment sits 0x4000 higher. `__DATA,__llvm_prf_*` (empty) also moves |
| `__TEXT,__eh_frame` flags | `0x6800000b` | `0x0` (the section isn't used in the kernel) |
| split-seg info, function starts | 0x106070 and 0xd620 bytes | identical sizes |
| local relocations (`nlocrel`), `nextrel` | 84723, 0 | identical |
| exports | 7195 | the identical set |
| symbol table (unstripped) | 247239 symbols | 253995: ld64 keeps 6756 more assembler-local `l…` symbols (`l___const.*`). The stripped kernel's `__LINKEDIT` is the same size |

kcgen accepts both kernels, and kcheck passes on both collections. The ISA audit (`//kernel:sbsa_isa_audit`) passes on the ld64 kernel. The VMAPPLE link-gap report (`//kernel:vmapple_release_gaps`) finds the same 395 undefined symbols, 157 of them required only by the export list or `-e`. ld64 words that last case differently from Xcode's `ld`: it prints `-exported_symbol[s_list] command line option` or `-u command line option` instead of `<initial-undefines>`. `tools/xnu/kernel.sh` reads both forms. ld64 also lists every object that references a symbol, where Xcode's `ld` lists only some of them.

**The kexts.** `zfs` and `NDSwiftTrial` match Xcode's: `MH_KEXT_BUNDLE`, the same load commands and segment layout, external and local relocations (zfs 2804 and 4747), split-seg info, the same exported and undefined symbol sets, and identical `__TEXT_EXEC` disassembly. The differences are that `__DATA_CONST,__got` comes first instead of last, and that the string table is about 9 KiB smaller.

**The boot A/B.** The same tests ran on each linker's kernel and kexts, all passing on both: `sbsa_boot_test`, `//tests/qemu:smoke`, `sbsa_session_boot_test`, `sbsa_smp_boot_test`, `sbsa_ref_boot_test`, `sbsa_zfs_pool_test` and `sbsa_swift_trial_test`. With ld64 as the default, the full QEMU set and `bazel test //...` pass as well (`roadmap/backlog.yaml` P0-06).

**Follow-ups.** The base userland still links with Xcode's `ld`: P0-02 decides between ld64 (which would need libtapi, from `apple-oss-distributions/tapi` in the LLVM build) and lld. P0-02's pinned clang replaces Xcode's for building ld64 itself.

**Link-only iteration.** As for lld (§2.1.2, Repro), keep a work tree with `ND_XNU_KEEP_WORK`. Then, in `DIR/obj/RELEASE_ARM64_SBSA`, run the kernel's link line (`VERBOSE=YES`) with `--ld-path=…/ld` added after `clang++`.

#### 2.1.2 Linking the kernel with ld64.lld (the investigation)

**Status (2026-10-06):** the SBSA kernel's objects link with upstream `ld64.lld` 22.1.8, but the result isn't a kernel. kcgen rejects it, so it can't boot.

**Which lld.** The swift.org 6.3.2 toolchain's `ld64.lld` (`LLD 21.0.0`, swiftlang/llvm-project 4e6cdf5c), which `@nd_embedded_swift` finds, can't be used: it rejects every object whose `LC_BUILD_VERSION` says macOS ("This version of lld does not support linking for platform macOS"). That covers the kernel's and kexts' objects. Objects without a build version (`-target arm64-apple-none-macho`) link. Upstream lld doesn't have this check: Homebrew's `lld@21` (21.1.8) and `lld@22` (22.1.8) both accept the objects. So P0-02's pinned toolchain must build lld without the swiftlang restriction, or carry a patch that removes it.

**The build setting.** `--//rules:kernel_linker=lld` links `xnu_kernel` targets with `--//rules:ld64_lld=PATH`. The default path is `/opt/homebrew/opt/lld@22/bin/ld64.lld`, and P0-02 replaces it. XNU's `LD` becomes `$(KC++) --ld-path=…/ld64_lld.sh -nostdlib`. `tools/xnu/ld64_lld.sh` is copied into the tree as an overlay. It expands `-alias_list` into `-alias` pairs and adds `-no_fixup_chains`, then runs lld. The default build's action is unchanged by the setting. The whole XNU link step succeeds under lld, including the CTF, strip and dSYM stages. It produces `kernel.release.sbsa`, which is an `MH_EXECUTE` that kcgen refuses.

**Flag mapping.** These are the flags XNU's makefiles pass for `RELEASE ARM64 SBSA`. To print the full line, keep the work tree (`ND_XNU_KEEP_WORK`, below) and relink with `VERBOSE=YES`.

| ld64 flag | lld 22 | Effect on the kernel |
|---|---|---|
| `-e __start`, `-pie`, `-pagezero_size 0x0`, `-headerpad 152`, `-function_starts` | same | — |
| `-sectalign SEG SECT ALIGN` | same | — |
| `-sectcreate`, `-segprot SEG max init` | same | — |
| `-rename_section`, `-rename_segment` | same | — |
| `-exported_symbols_list all-kpi.exp` | same | identical export set (7195 symbols, given the aliases) |
| `-alias_list all-alias.exp` | **not implemented** (warns, ignores) | KPI symbols the export list or code reach only through an alias are undefined (`_MALLOC`, `IOLockLock`, `IOService::resources()`, …) → the wrapper expands the list into `-alias sym alias` pairs, which lld implements |
| `-static` | **not implemented** (warns, ignores) | lld writes a dyld executable: `DYLDLINK`/`TWOLEVEL` flags, `LC_LOAD_DYLINKER`, `LC_MAIN` instead of `LC_UNIXTHREAD`, `LC_CODE_SIGNATURE`, `LC_DATA_IN_CODE` |
| `-image_base 0xfffffe0007004000`, `-segaddr __PRELINK_TEXT …` | **not implemented** | linked at address 0 |
| `-segment_order __TEXT:__DATA_CONST:…:__BOOTDATA` | **not implemented** | lld's own order is `__TEXT, __DATA_CONST, __DATA, __TEXT_EXEC, __BOOTDATA, __KLDDATA, __KLD, __LAST, __LASTDATA_CONST, …, __LINKINFO, __LINKEDIT`. Writable segments then fall outside the `__LAST`…`__PRELINK_DATA` window that arm_vm_init and kcgen require |
| `-add_split_seg_info` (patch 0040) | **not implemented** | no `LC_SEGMENT_SPLIT_INFO`: kcgen can't move the kernel's segments apart to place kexts |
| `-version_load_command` | ignored | lld emits `LC_BUILD_VERSION` anyway |
| (implicit) local relocations for `-static -pie` | — | lld records rebases as chained fixups (`DYLD_CHAINED_PTR_64`), or with `-no_fixup_chains` as `LC_DYLD_INFO_ONLY` rebase opcodes. It never writes `LC_DYSYMTAB` local relocations, which are what kcgen reads |
| (implicit) chained fixups ⇒ init offsets | — | with chained fixups (the default for macOS ≥ 13), lld rewrites every `S_MOD_INIT_FUNC_POINTERS` section into `__TEXT,__init_offsets`. That dissolves `__LASTDATA_CONST` (`lastkernelconstructor.o`) and changes how the kernel finds its constructors → the wrapper passes `-no_fixup_chains` |
| `-kernel` (arm64e only, not SBSA) | unknown argument | — |
| `-kext` (kexts) | ignored | lld links an executable (wants `_main`). `-bundle -undefined dynamic_lookup` gives `MH_BUNDLE` with chained-fixup binds, but no external relocations and no split-seg info. kcgen needs `MH_KEXT_BUNDLE`, `nextrel` and split-seg info |

**Structural diff** (same objects, `kernel.release.sbsa.unstripped`). Code and data match: `__TEXT_EXEC` is 0x8d8000 under ld64 and 0x8d4000 under lld, `__DATA_CONST` 0x18c000 and 0x184000, while `__DATA` (0x108000), `__BOOTDATA` (0x8c000) and `__LINKINFO` (0x50000) are identical. The exported symbols are identical. The ISA audit (`isa_audit.sh --mattr +v8.2a,+rcpc,+dotprod,+aes,+sha2,+fullfp16`) passes on both: 25 findings, all in the baseline. The differences that matter are the load commands, the base address, the segment order, the fixup format and the missing split-seg info in the table. kcgen refuses lld's image: "kernel carries load command 0x80000022" (`LC_DYLD_INFO_ONLY`), or 0x80000034 (`LC_DYLD_CHAINED_FIXUPS`) without the wrapper's `-no_fixup_chains`. So no lld kernel collection exists to boot, and the QEMU A/B can't run.

**What closes it.** Either option works:

- **Patch lld** in P0-02's pinned LLVM. It needs `-static` (no dylinker, `LC_UNIXTHREAD`), `-image_base`/`-segaddr`, `-segment_order`, `-alias_list`, `-add_split_seg_info` (split-seg v1, arm64: ADRP, branch and pointer deltas, as kcgen reads them), and local relocations, or rebase info in a form kcgen reads. For kexts it also needs `-kext`: `MH_KEXT_BUNDLE` with external and local relocations.
- **Teach kcgen lld's formats.** It could read rebases from chained fixups or `LC_DYLD_INFO`, and take the entry point from `LC_MAIN`. That still leaves the base address, the segment order and split-seg info, which only the linker can produce.

The open-source `ld64` is the other fallback, and it needs none of this. It's what P0-06 adopted (§2.1.1).

**Repro.**

```sh
# The lld kernel via Bazel (overwrites bazel-bin/kernel/kernel.release.sbsa;
# rebuild without the flag afterwards).
bazel build --//rules:kernel_linker=lld //kernel:sbsa_release
bazel-bin/tools/kcgen/kcgen --kernel bazel-bin/kernel/kernel.release.sbsa --output /tmp/kc   # refused

# Link-only iteration: run tools/xnu/kernel.sh outside Bazel with the
# arguments `bazel aquery 'mnemonic(XnuKernel, //kernel:sbsa_release)'`
# prints, from a directory linking tools/, kernel/, bazel-out and
# external -> $(bazel info output_base)/external, with
# ND_XNU_KEEP_WORK=DIR. Then, in DIR/obj/RELEASE_ARM64_SBSA:
rm -f kernel.release.sbsa.unstripped kernel.release.sbsa.unstripped.noctf
mkdir -p DIR/src/tools/nd && cp tools/xnu/ld64_lld.sh DIR/src/tools/nd/
bash -c "$(cat DIR/make.sh) 'LD=\$(KC++) --ld-path=\$(SRCROOT)/tools/nd/ld64_lld.sh -nostdlib' \
    ND_LD64_LLD=/opt/homebrew/opt/lld@22/bin/ld64.lld VERBOSE=YES"
```

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
| `kext(name, srcs, info_plist, deps, bundle_id)` | `.kext` bundle with `Info.plist`, symbol set validation against the kernel's exports. Phase 1 form: `nd_kext` (`rules/kext.bzl`) runs a kext's build script against a pinned upstream tree and `//kernel:headers` (`kexts/zfs`); kcgen validates the imports when it links the collection | Xcode kext templates + `kextsymboltool` |
| `kext_collection(name, kernel, kexts, kind = "boot" \| "system" \| "aux")` | `MH_FILESET` kernel collection via `kcgen`, fixup chains emitted. Implemented in `rules/kc.bzl` for `kind = "boot"`: the kernel alone, or with `kexts` (`nd_kext` bundles, `rules/kext.bzl`) and the codeless kexts they depend on (`kernel_components`, `codeless`) since P3-01; the other kinds arrive in Phase 5 | `kmutil create` |
| `dext(name, …)` | DriverKit-style userland driver bundle | Xcode |
| `nd_package(name, manifest, files, deps, hooks)` | `.ndpkg` with signed manifest | none |
| `system_image(name, packages, kernel_collection, rootfs = "zfs" \| "hfs" \| "ramdisk")` | bootable image + ESP tree | none |
| `qemu_test(name, expect, efi, esp, machine, cpu, mem, smp, send_after, until_lines, timeout_s, extra_args)` | test that boots an EFI application (neoboot by default) with files on its ESP and requires lines on serial. Implemented in `rules/qemu.bzl` (P0-04), below | hand-written `sh_test` command lines |
| `swift_library/ swift_binary` (from `rules_swift`) | Swift 6 modules with strict concurrency | SwiftPM |
| `swift_embedded_binary`, `swift_embedded_library` | freestanding Embedded Swift (no runtime, `-no-allocations`), PE/COFF for UEFI or Mach-O/ELF | none |
| `kext_swift` (experimental) | Embedded Swift objects linked into a `kext` behind C entry points | none |

Phase 1 is implemented (`rules/xnu.bzl`, `tools/xnu/`): `nd_build_sdk` produces NeoDarwin's additions to the host SDK as real files, each action assembles its own overlay SDK, `xnu_headers` and `libfirehose_kernel` build from pinned Apple archives, and `xnu_kernel` runs the upstream makefiles, optionally in link-gap-report mode. See `kernel/README.md` for the targets and the settings that differ from Apple's.

`qemu_test` (P0-04) is a macro over `sh_test` that runs `tools/efi/qemu_efi_test.sh` and builds its command line and `data` from named attributes: `efi` (default `//boot/neoboot`, placed as `\EFI\BOOT\BOOTAA64.EFI`), `esp` (a dict from a file's label to its ESP path, as `gpt_disk_image` takes), `machine`, `cpu`, `mem`, `smp`, `send_after` (a list of `(line, text)` pairs typed on serial in order), `until_lines` (default `True`: pass once every line has appeared and stop QEMU), `timeout_s`, `expect` (the lines), and `extra_args` for the script's rarer flags (`--dump-cpus-on`, `--disk`, `--drive`, `--device`, `--peer-net`, `--link-net`, …), passed as written with their files in `data`. Strings in `expect` and `send_after` are the script's literal text: the macro single-quotes them and doubles `$` for Bazel's make-variable expansion, so a test writes `"echo hi-$((6*7))\\n"`, not `"'echo hi-$$((6*7))\\n'"`. `"qemu"` is always among the tags. The script's environment knobs (`ND_QEMU`, `ND_QEMU_LOG_DIR`, `ND_QEMU_DUMP_CPUS_ON`, …) work as before through `--test_env`. Phase 0's smoke is `bazel test //tests/qemu:smoke`: neoboot boots on QEMU virt under EDK2 and its banner and "no kernelcache" lines appear on serial, in about six seconds. `//boot/neoboot:neoboot_qemu_test`, `//kernel:sbsa_boot_test` and `//kernel:sbsa_session_boot_test` use the rule. Their arguments, data and tags are token for token the hand-written ones, so Bazel's test cache hit them unchanged. The other 59 QEMU tests in `kernel/` and `boot/neoboot/` are still hand-written `sh_test`s; converting them is follow-up work, and the ones that need `--disk`, the peer or the screen go through `extra_args` until the rule grows attributes for them.

The `xnu_kernel` rule is delivered in two phases so the kernel port is never blocked on build-system work: **phase 1** (P0) wraps the upstream makefiles in a sandboxed `genrule` with the hermetic toolchain injected via `CC=`, `LD=`, `HOST_*`, and `EXTRA_TARGET_CONFIGS`; **phase 2** (P2) replaces the makefiles with a native graph generated from XNU's `conf/files*` lists by a converter kept under `tools/xnu2bazel` so future Apple drops re-convert mechanically.

## 5. Host tools

Written in Swift 6 (language policy T1); all built by the same graph and used as Bazel `tool` inputs, so a host tool change invalidates exactly the outputs that depend on it.

| Tool | Role |
|---|---|
| `kcgen` | links kernel + kexts into an `MH_FILESET` with `LC_FILESET_ENTRY` and fixup chains; resolves kext imports against the kernel's exported symbols (its export lists) and fails on any it lacks; moves the kernel's and the kexts' segments apart with their split-segment info. Layout in `docs/kernel/arm64-sbsa-bringup.md` §2.1.1; the first kext is `zfs.kext` (P3-01) |
| `kcheck` | verifies a collection against the kernel's boot-time assumptions: flat layout, chain format and one chain per page, every fixup target inside the image, the top-level segments `arm_vm_init()` derives kext regions from, `__PRELINK_INFO`; with `--kernel`, round-trips the collection byte for byte against its source kernel |
| `dtdump` | prints/validates an Apple-format device tree against `dt-abi.md` |
| `ndimage` | assembles ESP + system image from packages |
| `ndsign` | Ed25519 signing of manifests, kernel collections and images; keys in the key chain of packaging.md §4 |
| `xnu2bazel` | converts XNU `conf/files*`, `Makefile` fragments and `MASTER*` configs into BUILD files |
| `lang-audit` | lists every first-party C/C++ file with its justification line; fails CI on a missing one |

`kcgen` and `kcheck` share `//tools/macho`, a Mach-O reader and writer with no Foundation dependency. `//tools/kcgen:selftest` exercises both on a synthetic kernel in the default `bazel test //...`. It passes a clean collection, one with a codeless kext, and requires kcheck to catch each of nine corruptions; `//kernel:sbsa_zfs_kc_check` checks a collection with a real kext.

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
| ACPICA, zstd, libarchive, sqlite | CMake/autotools | `rules_foreign_cc` or native BUILD; vendored with SHA pins |
| ports | per-port upstream build systems | built by `ndports` from recipes in a clean-room boot environment, *outside* the base Bazel graph; the outputs are packages (ports.md) |
