<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Repository structure and forge portability

## 1. Shape: one monorepo plus upstream mirrors

- **`neodarwin/neodarwin`** (monorepo): all first-party code, BUILD files, docs, roadmap, CI. Cloning it and running `bazel build //images:qemu-virt` produces a bootable image; Bazel fetches vendored upstream by pinned commit.
- **`neodarwin/mirror-<component>`**: verbatim mirrors of upstream drops, one repository each, tagged per upstream release (`apple/xnu-12377.1.9`, `openzfs/zfs-2.4.x`). Components: `xnu`, `dyld`, `Libc`, `libplatform`, `libpthread`, `libmalloc`, `libdispatch`, `Libinfo`, `IOPCIFamily`, `IOStorageFamily`, `IOGraphics`, `IOSerialFamily`, `hfs`, `launchd-842`, `swift-corelibs-foundation`, `openzfs` (the OpenZFS on OS X fork until the macOS layer is upstream), `plan-neo` (the pilot; source of `libstyle`, `libagent`, `lib9p` and `wsys`'s reference implementation), `nuaqua` (the pilot; source of Vesper and Typeface), `acpica`, `libwayland`. Never edited; NeoDarwin never commits to them. They keep the monorepo small and make a new upstream drop a tag, not a 400 MB commit.
- **`neodarwin/ports`**: the pkgsrc overlay and `pkgsrc2nd` outputs (packaging design §7), separate because its cadence and licences differ.
- **`neodarwin/hardware`**: board notes, DT dumps, firmware download scripts, hardware-in-the-loop runner configs. No binaries; firmware blobs are LFS pointers to a release bucket.

## 2. Monorepo layout

```
neodarwin/
  README.md  LICENSE.md  CONTRIBUTING.md  CODEOWNERS  SECURITY.md  DCO
  MODULE.bazel  .bazelrc  .bazelversion
  platforms/  toolchains/  rules/                  # build-system.md §3–4
  boot/neoboot/                                   # UEFI loader (aarch64 first; x86_64, riscv64 later)
  kernel/
    upstream.lock                                 # mirror commit + Apple tag
    patches/NNNN-<topic>.patch                    # ordered series applied by the build (§3)
    neodarwin/                                    # NeoDarwin-owned kernel sources: SBSA board config, platform expert, GICv3, PSCI, exported KPIs for zfs/9p/fuse
    BUILD.bazel
  kexts/{ndacpi,zfs,hfs,nd9p,ndfuse,msdosfs,ndsandbox,ndfb,virtio,nvme,ahci,ndusb,…}/   # each: upstream.lock (if adopted) + patches/ + BUILD
  dexts/{hid,net-e1000,net-virtio,gpio,…}/
  base/{dyld,libc,libdispatch,libplatform,libpthread,libmalloc,launchd,foundation,…}/   # upstream.lock + patches/ + BUILD overlay
  services/{nsd,keyd,pkgd,wsys,inputd,auditd,netd,zed}/
  libs/{libns,libnd,libwayland-glue}/
  desktop/{chrome,toolkit,viewer,prefs,terminal,schemes}/   # desktop chrome and clients
  desktop/vesper/                                 # renderer + Typeface from the NuAqua pilot, namespaces refactored
  docs/desktop/                                   # window protocol, theme engine, UI configuration, agent protocol
  tools/{kcgen,kcheck,dtdump,ndimage,ndsign,xnu2bazel,pkgsrc2nd,upstream-bump,backlog-sync}/
  images/                                         # system_image, esp_image, ramdisk targets
  tests/{qemu,hil,abi}/
  docs/                                           # this tree
  roadmap/ROADMAP.md  roadmap/backlog.yaml
  third_party/{acpica,libsolv,zstd,libarchive,libwayland,sqlite,…}/   # BUILD + pinned archives; no vendored source
  .github/workflows/  .forgejo/workflows -> ../.github/workflows       # §5
  .github/ISSUE_TEMPLATE/  .github/PULL_REQUEST_TEMPLATE.md
```

## 3. Upstream policy (goal 1: use current source)

1. Each upstream component has `upstream.lock` (mirror repo, commit, upstream tag) and a `patches/` series in `git format-patch` form, numbered, each with a one-paragraph rationale and a `Rebase-risk:` line (`low | medium | high`).
2. The build applies the series to the pristine mirror checkout inside the sandbox; nothing in the monorepo is a modified copy of upstream.
3. New behaviour goes into NeoDarwin-owned files (`kernel/neodarwin/`, BUILD overlays) whenever possible; a patch touches upstream only for hooks and `#ifdef` gates.
4. On each upstream drop: `tools/upstream-bump <component> <tag>` re-applies the series, reports conflicts, and opens a PR with the diff of generated BUILD files. Budget: one engineer-day per component per drop; patches that exceed it are candidates to upstream or to refactor into NeoDarwin-owned files.
5. Provenance: `PROVENANCE.md` per derived driver and per referenced Linux file (drivers design §4); CDDL notice for `zfs.kext` in `LICENSE.md`.

## 4. Governance files

`CODEOWNERS` maps directories to workstreams (`kernel/ @kernel-bridge`, `boot/ @loader`, `kexts/zfs @storage`, …). `CONTRIBUTING.md` requires DCO sign-off and SSH-signed commits, states the licence per directory, and describes the patch-series workflow. `SECURITY.md` names the disclosure address and the signing-key rotation policy.

## 5. GitHub today, Forgejo tomorrow

Rules that make the move a `git push` plus a DNS change:

| Concern | Rule |
|---|---|
| CI | workflows in GitHub-Actions syntax under `.github/workflows/`; `.forgejo/workflows` is a symlink. Only checkout and cache actions are used; everything else is a script in `ci/`. Runners are **self-hosted** from day one (a macOS host for Swift and dyld, Linux for QEMU, the hardware box), so nothing depends on GitHub-hosted runners. |
| Issues and planning | the backlog lives in `roadmap/backlog.yaml`; `tools/backlog-sync` creates and updates issues on whichever forge is configured (both expose a compatible REST subset). GitHub Projects and Discussions are not used. |
| Templates | issue and PR templates in `.github/`, which Forgejo also reads. |
| Releases | tags plus artifacts uploaded by `ci/release.sh` through the forge API; release notes generated from `roadmap/` and the changelog, never typed into a web UI. |
| Large files | Git LFS for DT dumps and test images; Forgejo supports LFS. Firmware blobs stay outside git. |
| Identity | commit authorship by email; SSH signatures verified in CI on both forges. |
| Mirrors | `mirror-*` repos are push mirrors; Forgejo's built-in mirroring takes over on migration. |
| Permissions | teams mirror `CODEOWNERS`; `tools/forge-teams.yaml` is the source of truth for both forges. |

## 6. Branching

`main` is always buildable and bootable on `qemu-virt`. Feature branches per epic (`P1-03-gic-timer-group1`). Release branches `release/26.x` are cut from `main`; the kernel ABI number is frozen per release branch (packaging design §6). Every release is also a ZFS boot environment name, so "which release am I on" and "which BE is active" are the same question.
