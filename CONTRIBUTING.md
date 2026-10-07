<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Contributing

## Build and test

```
bazel test //...                                  # everything
bazel run //tools/lang_audit -- --tree "$PWD"      # the language-policy audit CI runs
ci/test.sh                                        # exactly what CI runs
```

Bazel is pinned by `.bazelversion`; install Bazelisk (`brew install bazelisk`) and it fetches the right version.

**Setup.** Builds run on macOS (arm64). You need:

- **The Command Line Tools for Xcode 27.0** (`xcode-select --install`): they install the macOS 27.0 SDK the build pins (`/Library/Developer/CommandLineTools/SDKs/MacOSX27.0.sdk`). Bazel checks its version and content hash when it fetches `@nd_macos_sdk`, and the error names the SDK it wants.
- **Nothing else for the toolchain.** Bazel downloads the pinned swift.org Swift 6.4.0 and llvm.org LLVM 23.1.3 releases by sha256 (`toolchains/upstream.lock`, about 3 GB on first fetch). Embedded Swift (neoboot, PID 1, the T2/T3 checks, the kext trial) always uses them. swiftly is no longer needed.
- **Xcode, for now, for the rest of the tree.** `--config=pinned` builds hosted C and Swift with the pinned toolchain and no Xcode (`ci/no_xcode.sh` proves it for `//toolchains:hello_cc`, `:hello_swift` and `//tests/smoke`). The default configuration, the base, XNU and the kexts still use the selected Xcode until P2-12.

## Language policy

New code is Swift 6+ (`docs/architecture/language-policy.md`). Use the NeoDarwin macros, not the raw rules:

| Load | Use for |
|---|---|
| `//rules:swift.bzl` → `nd_swift_library`, `nd_swift_binary` | all Swift; Swift 6 language mode and warnings-as-errors are applied for you |
| `//rules:cc.bzl` → `nd_cc_library`, `nd_cc_binary` | justified C/C++ only; each target gets a `_lang_audit` test |

Every first-party C, C++, Objective-C or assembly file carries a justification in its first 20 lines naming one of the three grounds:

```
// NeoDarwin-Language: portability: <reason>
// NeoDarwin-Language: performance: <reason>
// NeoDarwin-Language: expressibility: <reason>
```

## Commits

Sign off every commit (DCO: `git commit -s`). Upstream components change only through numbered patches under their `patches/` directory, never by editing a mirror (`docs/repository.md` §3).
