<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Repository structure and forge portability

## 1. Shape: one monorepo plus upstream mirrors

**Hosting (decided 2026-10-06):** the monorepo lives under the maintainer's GitHub account, `github.com/JamesKane`, until the project is named and has an organisation. The `neodarwin/...` names below are the eventual organisation's; until then the same repositories sit under `JamesKane/`.

- **`neodarwin/neodarwin`** (monorepo): all first-party code, BUILD files, docs, roadmap, CI. Cloning it and running `bazel build //images:qemu-virt` produces a bootable image; Bazel fetches vendored upstream by pinned commit.
- **`neodarwin/mirror-<component>`**: verbatim mirrors of upstream drops, one repository each, tagged per upstream release (`apple/xnu-12377.1.9`, `openzfs/zfs-2.4.x`). Components: `xnu`, `dyld`, `Libc`, `libplatform`, `libpthread`, `libmalloc`, `libdispatch`, `Libinfo`, `IOPCIFamily`, `IOStorageFamily`, `IOGraphics`, `IOSerialFamily`, `hfs`, `launchd-842`, `swift-corelibs-foundation`, `openzfs` (the OpenZFS on OS X fork until the macOS layer is upstream), `acpica`, the Apple command projects (`file_cmds`, `shell_cmds`, `system_cmds`, `network_cmds`, …) and `OpenSSH`. The pilot mirrors (`plan-neo`, `nuaqua`) and `libwayland` moved to Magi. Never edited; NeoDarwin never commits to them. They keep the monorepo small and make a new upstream drop a tag, not a 400 MB commit.
- **Mirrors as built (decided 2026-10-07): fragile upstreams only.**
  - **Coverage:** an upstream gets a mirror under `github.com/JamesKane` when it could disappear or change. That means personal sites and repositories, single-maintainer projects, community forks, and GitHub-generated archives, whose bytes GitHub doesn't guarantee to stay the same. The other upstreams stay pinned by sha256 at their origins: Apple's `apple-oss-distributions`, FreeBSD's and OpenBSD's repositories, and official release assets.
  - **Format:** an archive mirror holds the exact bytes as a release asset, so the sha256 matches. A file-pinned upstream gets a full git mirror.
  - **Order:** `MODULE.bazel` lists the mirror first and the origin second, and `pinned_files` takes `mirror_url_templates`.
  - **The check:** `ci/mirror_check.sh` proves the mirrors resolve. With an empty repository cache and every fragile origin rewritten to an unreachable host, each repository must still fetch and pass its hash.

  | Mirror | Upstream | Pinned |
  |---|---|---|
  | `mirror-libtermkey` | libtermkey 0.22, leonerd.org.uk (MIT) | release asset |
  | `mirror-lpeg` | LPeg 1.1.0, inf.puc-rio.br/~roberto (MIT) | release asset |
  | `mirror-vis` | vis 0.9, github.com/martanne/vis (ISC) | release asset |
  | `mirror-ksh93` | ksh93u+m 1.0.10, github.com/ksh93/ksh (EPL-2.0) | release asset |
  | `mirror-openzfs-fork` | openzfsonosx/openzfs-fork a4c1b11ab900 (CDDL-1.0) | release asset |
  | `mirror-wide-dhcpv6` | hrs-allbsd/wide-dhcpv6 (BSD) | git mirror; raw files by commit |

  Adding an upstream that fits these criteria means adding a mirror in the same change.
- **`neodarwin/ports`**: the ports tree (`ports.md`), checked out at `/usr/ports`; separate because its cadence and licences differ.
- **Downstreams** live in their own repositories and consume this one as a Bazel module (`docs/architecture/downstream.md`). Magi is `../Magi` today.
- **`neodarwin/hardware`**: board notes, DT dumps, firmware download scripts, hardware-in-the-loop runner configs. No binaries; firmware blobs are LFS pointers to a release bucket.

## 2. Monorepo layout

```
neodarwin/
  README.md  LICENSE  THIRD_PARTY_NOTICES.md  CONTRIBUTING.md  CODEOWNERS  SECURITY.md  DCO
  MODULE.bazel  .bazelrc  .bazelversion
  platforms/  toolchains/  rules/                  # build-system.md §3–4
  boot/neoboot/                                   # UEFI loader (aarch64 first; x86_64, riscv64 later)
  kernel/
    upstream.lock                                 # mirror commit + Apple tag
    patches/NNNN-<topic>.patch                    # ordered series applied by the build (§3)
    neodarwin/                                    # NeoDarwin-owned kernel sources: SBSA board config, platform expert, GICv3, PSCI, exported KPIs for zfs/9p/fuse
    BUILD.bazel
  kexts/{ndacpi,zfs,hfs,ndfuse,msdosfs,ndfb,virtio,nvme,ahci,ndusb,…}/   # each: upstream.lock (if adopted) + patches/ + BUILD
  dexts/{hid,net-e1000,net-virtio,gpio,…}/
  base/{dyld,libc,libdispatch,libplatform,libpthread,libmalloc,launchd,foundation,…}/   # upstream.lock + patches/ + BUILD overlay
  base/{service,sysrc,…}/                         # first-party administration front ends (freebsd-parity.md §3)
  services/{pkgd,netd,consoled,zed}/
  libs/{libnd}/
  tools/{kcgen,kcheck,dtdump,ndimage,ndsign,ndports,parity,xnu2bazel,upstream-bump,backlog-sync}/
  images/                                         # system_image, esp_image, ramdisk targets
  tests/{qemu,hil,abi}/
  docs/                                           # this tree
  roadmap/ROADMAP.md  roadmap/backlog.yaml
  third_party/{acpica,libsolv,zstd,libarchive,sqlite,…}/   # BUILD + pinned archives; no vendored source
  .github/workflows/  .forgejo/workflows -> ../.github/workflows       # §5
  .github/ISSUE_TEMPLATE/  .github/PULL_REQUEST_TEMPLATE.md
```

## 3. Upstream policy (goal 1: use current source)

1. Each upstream component has `upstream.lock` (mirror repo, commit, upstream tag) and a `patches/` series in `git format-patch` form, numbered, each with a one-paragraph rationale and a `Rebase-risk:` line (`low | medium | high`).
2. The build applies the series to the pristine mirror checkout inside the sandbox; nothing in the monorepo is a modified copy of upstream.
3. New behaviour goes into NeoDarwin-owned files (`kernel/neodarwin/`, BUILD overlays) whenever possible; a patch touches upstream only for hooks and `#ifdef` gates.
4. On each upstream drop: `tools/upstream-bump <component> <tag>` re-applies the series, reports conflicts, and opens a PR with the diff of generated BUILD files. Budget: one engineer-day per component per drop; patches that exceed it are candidates to upstream or to refactor into NeoDarwin-owned files.
5. Provenance: `PROVENANCE.md` per derived driver and per referenced Linux file (drivers design §4); every upstream's licence, the CDDL notice for `zfs.kext` among them, in `THIRD_PARTY_NOTICES.md` (the licence map; `LICENSE` covers first-party code, BSD-2-Clause).

### 3.1 Reuse order

Apple's open stack comes first wherever it can do the job. For any component, take the first of these that exists:

1. **Apple open source, current drop** (APSL 2.0 or the licence in the file), adopted from `apple-oss-distributions` with a patch series. Examples: xnu, dyld, Libc, libdispatch, IOKit families, hfs, CommonCrypto, and the corecrypto subset xnu builds itself (SHA-256, HMAC, NIST DRBG, HKDF, CBC, GCM).
2. **Apple open source, an earlier drop**, when a current release moved the implementation into a closed component. Example: xnu-1699 still shipped its own AES, DES, MD5, SHA-1 and SHA-2, before the closed corecrypto kext took them over. Pin the tag in `upstream.lock` and record it in `PROVENANCE.md`.
3. **FreeBSD** (BSD-2/3), ported with headers and provenance intact.
4. **New NeoDarwin code** (BSD-2-Clause), written against Apple's published interfaces (the APSL headers) so that an Apple implementation can replace it later.

**Source-available is not open.** Apple code published under non-open terms is never vendored, copied or used as a model for NeoDarwin code. That covers the corecrypto repository (`github.com/apple/corecrypto`, an evaluation-only internal-use licence) and the Kernel Debug Kit's archives. The APSL interface headers for the same functionality (for example `EXTERNAL_HEADERS/corecrypto` in xnu) are open and are the contract NeoDarwin implements.

## 4. Governance files

`CODEOWNERS` maps directories to workstreams, generated from `tools/forge-teams.yaml`, which lists each workstream (the backlog's `owner` values) with its paths. Under a personal account there are no teams, so every path is owned by the maintainer, and the workstreams are comments. With an organisation, each workstream becomes a team (`kernel/ @<org>/kernel-bridge`, `boot/ @<org>/loader`, …). `CONTRIBUTING.md` requires DCO sign-off and SSH-signed commits, states the licence per directory, and describes the patch-series workflow. `SECURITY.md` routes reports through GitHub's private vulnerability reporting and will name the signing keys and their rotation once `ndsign` lands (P2-01). `DCO` is the Developer Certificate of Origin 1.1 that sign-offs certify. Issue templates (bug, backlog item) and the PR template are in `.github/`.

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
| Permissions | teams mirror `CODEOWNERS`; `tools/forge-teams.yaml` is the source of truth for both forges. Under the personal account, the maintainer owns everything. |

## 6. Branching

`main` is always buildable and bootable on `qemu-virt`. Feature branches per epic (`P1-03-gic-timer-group1`). Release branches `release/26.x` are cut from `main`; the kernel ABI number is frozen per release branch (packaging design §6). Every release is also a ZFS boot environment name, so "which release am I on" and "which BE is active" are the same question.
