<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Contributing

## Build and test

```
bazel test //...                                  # everything
bazel run //tools/lang_audit -- --tree "$PWD"      # the language-policy audit CI runs
ci/test.sh                                        # exactly what CI runs
```

Bazel is pinned by `.bazelversion`; install Bazelisk (`brew install bazelisk`) and it fetches the right version.

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
