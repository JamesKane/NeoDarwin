<!-- SPDX-License-Identifier: BSD-2-Clause -->
# XNU patch series

Ordered patches applied to the pristine `xnu-12377.1.9` archive inside the build sandbox (`docs/repository.md` §3). A target lists the patches it applies in its `patches` attribute; nothing here edits a checkout. Applying 0001 to 0005 in order to the pristine archive must reproduce the tree the SBSA kernel is built from; each patch carries a rationale and a `Rebase-risk:` line.

| Patch | What it does | Used by |
|---|---|---|
| `0001-config-build-arm64-machine-code-and-pmap-from-source.patch` | enables `nos_arm_asm`/`nos_arm_pmap`, so the in-tree ARM64 machine code and pmap build instead of coming from Apple's closed per-SoC archive | `sbsa_release` |
| `0002-sbsa-board-config.patch` | adds `ARM64_BOARD_CONFIG_SBSA`: `SBSA.h`, `generic_arm64_common.h` (no `APPLE_ARM64_ARCH_FAMILY`), MakeInc and pexpert wiring | `sbsa_release` |
| `0003-osfmk-generic-arm64-guards.patch` | `GENERIC_ARM64_PLATFORM` guards in pmap, VM init, CPU exit, Apple CPU headers; empty tunables; unpublished `amcc_rorgn` sources behind a never-enabled option | `sbsa_release` |
| `0004-config-sbsa-exports.patch` | SBSA exports no Tightbeam or Apple-SoC-only symbols; all export consumers read a filtered `EXPORTS_DIR` | `sbsa_release` |
| `0005-bsd-neodarwin-trustcache-runtime.patch` | NeoDarwin's `trustCacheInitializeRuntime` and `TCTypeConfig`, default deny | `sbsa_release` |
