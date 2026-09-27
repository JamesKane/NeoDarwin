<!-- SPDX-License-Identifier: BSD-2-Clause -->
# XNU patch series

Ordered patches applied to the pristine `xnu-12377.1.9` archive inside the build sandbox (`docs/repository.md` §3). A target lists the patches it applies in its `patches` attribute; nothing here edits a checkout.

| Patch | Status | Used by |
|---|---|---|
| `0001-config-build-arm64-machine-code-and-pmap-from-source.patch` | staged for P1-01; needs the SBSA board config's guards before it compiles | none yet |

Each patch carries a rationale paragraph and a `Rebase-risk:` line.
