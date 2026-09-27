<!-- SPDX-License-Identifier: BSD-2-Clause -->
# kernel

XNU. `upstream.lock` pins the `mirror-xnu` commit and Apple tag; `patches/` is the ordered series applied in the build sandbox; `neodarwin/` holds NeoDarwin-owned kernel sources (SBSA board config, platform expert, GICv3, PSCI). Design: `docs/kernel/arm64-sbsa-bringup.md`, `docs/repository.md` §3. Epics P0-03, P1-01, P1-02.
