<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Downstreams: NeoDarwin as a foundation

## 1. Purpose (user, 2026-09-29)

NeoDarwin is a foundation that other projects take in different directions. It stays deliberately small in policy (a Darwin with FreeBSD's command-line usability, a package manager and a ports tree) so that projects with different goals can share it:

| Downstream | Direction | Status |
|---|---|---|
| **Magi** (`../Magi`) | a 90s-lineage WIMP desktop, Plan 9 namespaces with state as files, capability security for agents, application APIs from a platform study | active; the reference downstream |
| an **OpenMacOS** | source or binary compatibility with macOS applications by reimplementing closed frameworks | hypothetical |
| a server or appliance distribution | a fixed package set, remote management, no console | hypothetical |

This document is NeoDarwin's side of the contract: what a downstream can rely on, where it plugs in, and how it asks for more. Magi's side is its `docs/architecture/platform-contract.md`, and Magi's CI is the test that this contract works.

## 2. The rule: mechanism in NeoDarwin, policy downstream

- NeoDarwin accepts a **mechanism** any downstream could use: a kernel facility, an exported KPI, a driver, a package-format field, a build rule.
- It does not carry a downstream's **policy**: its desktop, security model, service set or application API.
- A downstream never needs to fork NeoDarwin. If it does, the contract is missing something, and the fix goes here.
- NeoDarwin's non-goals (`README.md`) describe what NeoDarwin itself will not build. They do not stop a downstream. An OpenMacOS may reimplement closed frameworks in its own tree, provided it follows NeoDarwin's licence rules for anything linked into the kernel or the base.

## 3. What a downstream can rely on

| Interface | Guarantee | Where |
|---|---|---|
| **Bazel module** `neodarwin` | its rules (`nd_cc_library`, `nd_swift_library`, `kext`, `nd_package`, `system_image`, …) work when loaded from another module. Labels inside the rules use `Label()` | `rules/`, build-system.md |
| **SDK** | a sysroot plus the pinned toolchain as a tarball, for builds outside Bazel (ports, other build systems) | build-system.md §2, P2-07 |
| **Kernel ABI** | `kernel-abi` is frozen per release branch. The exported KPI list includes the VFS (`vfs_fsadd` and the vnode KPIs a filesystem needs), the TrustedBSD MAC policy KPIs, the IOKit families NeoDarwin ships, and `IOUserServer` for dexts. Additions are allowed on a release branch; removals and changes are not | packaging.md §6, `kernel/` exports list |
| **Userland ABI** | libSystem, dyld and libdispatch symbols are stable within a major release | base/ |
| **Package format** | the manifest schema is versioned (`schema = 1`); new fields are additive; `kind` includes `kext`, `dext`, `service`, `port` and `system-set` | packaging.md §3 |
| **Repositories and trust** | a downstream runs its own repositories and channels with its own key chain. `ndpkg` can trust several roots, each scoped to the package names or origins it may sign | packaging.md §4 |
| **System sets** | a downstream system set is a NeoDarwin system set plus downstream packages. It installs, upgrades and rolls back as one boot environment | packaging.md §2 |
| **Backlog** | `roadmap/backlog.yaml` is exported (`@neodarwin//roadmap:backlog.yaml`), so a downstream's backlog can depend on NeoDarwin epics as `neodarwin:<id>` and check them | roadmap/ |

## 4. Extension points

| Point | Use | Mechanism |
|---|---|---|
| Kexts | filesystems, MAC policies, drivers | kext packages in the auxiliary collection, or the boot collection for boot-critical ones (packaging.md §6) |
| Dexts | userland drivers | `NDDriverKit` packages |
| Services | daemons | launchd job plists, registered by a package hook |
| MAC policy | a security model (for example Magi's `ndsandbox`) | TrustedBSD MAC (`security/mac_policy.h`) through the exported policy KPIs |
| Authentication | login and session policy | OpenPAM modules and `/etc/pam.d` |
| Ports overlays | extra or replacement recipes | `ndports` overlays (ports.md §7) |
| Console session | what runs after login on the framebuffer | the login shell or a launchd session job; NeoDarwin's console (console.md) hands the framebuffer and input to a downstream's display server |
| Boot branding | the name in the boot menu and the boot environment prefix | `neoboot`'s `boot.cfg` |

## 5. Asking for a mechanism

A downstream that needs something below its own layers files a **mechanism proposal** against NeoDarwin. It states the need, the smallest mechanism that meets it, who else could use it, and the tests. It is accepted when:

1. it is useful without the downstream's policy;
2. it follows the reuse order (`docs/repository.md` §3.1) and the thin-patch rule;
3. it comes with tests that run in NeoDarwin's CI.

A refused proposal leaves the downstream free to ship the mechanism as its own kext, dext or library through §4, and the refusal is recorded in both projects. Magi's current proposals (timer policy by thread intent, real-time admission, heterogeneous-core placement, GPU buffer objects) are listed in its platform contract §3.

## 6. Versioning and release cadence

- A downstream pins a NeoDarwin release tag. Between releases it may track `main` with `local_path_override`; Magi does this during development.
- NeoDarwin announces a `kernel-abi` bump in its release notes before the release, with the list of changed exports, so downstream kexts can be rebuilt.
- NeoDarwin's CI builds the Magi spec checks against every NeoDarwin change once Magi is on the same forge, so a change that breaks the downstream contract fails in NeoDarwin first.
