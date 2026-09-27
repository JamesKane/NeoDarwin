<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Filesystems: OpenZFS as the system and data filesystem

## 1. Decision

**OpenZFS is NeoDarwin's primary filesystem: root, system sets, package store and user data all live in one ZFS pool.** HFS+ (Apple open source) remains only as the bootstrap root during kernel bring-up, and FAT on the ESP is a firmware requirement.

Why ZFS fits the charter better than any alternative:

| Charter need | What ZFS gives |
|---|---|
| Atomic, reversible upgrades of the whole system (goal 3) | **boot environments**: each system set is a dataset clone; `neoboot` and `pkgd` select one; rollback is a property flip, not a slot copy |
| Integrity of the system image | end-to-end checksums plus `zfs send` streams verified by signature; no separate verity layer needed |
| Package store that shares blocks across versions | per-package datasets or clones, `compression=zstd` default, optional dedup |
| Agent-era observability | snapshots as immutable state points; dataset properties as a metadata store readable under `/n/sys/fs` |
| Licensing | CDDL-1.0, file-scope copyleft, shippable alongside APSL/BSD code as a kext (this is how ZFS has shipped on macOS and FreeBSD for years); nothing GPL enters the kernel |
| Existing XNU port | OpenZFS has run on XNU for a decade through the OpenZFS on OS X project (`zfs.kext` + SPL); NeoDarwin inherits a working IOKit/VFS binding instead of writing a filesystem |

"Or similar" alternatives considered: **bcachefs** (GPL, Linux-only; rejected on licence and maturity), **HAMMER2** (BSD, DragonFly-only, small community, no XNU port), **btrfs** (GPL), **APFS** (closed), **XFS** (GPL, no BSD-licensed implementation; would have needed a clean-room kext). OpenZFS is the only candidate that is licence-compatible, multi-platform and already on XNU.

## 2. Source and porting surface

OpenZFS keeps platform code behind an OS layer: `module/os/<os>/spl` (locks, memory, threads, taskq, kstat, uio, vfs shims; 22 files in the FreeBSD layer) and `module/os/<os>/zfs` (VFS/vnode glue, vdev backend, zvol, ioctl device, crypto, sysctl; 25 files), with matching `include/os/<os>`. The FreeBSD-vendored copy in this workspace (`../freebsd-src/sys/contrib/openzfs`, OpenZFS 2.4.99, CDDL) carries the `freebsd` and `linux` layers and is the structural reference. The macOS/XNU layer lives in the OpenZFS on OS X fork (`module/os/macos`, `include/os/macos`, `cmd/os/macos`); its upstream-merge status is verified at P0 and the fork is mirrored as `mirror-openzfs`.

Plan:
1. **Adopt the macOS OS layer as `module/os/neodarwin`.** Because NeoDarwin owns the kernel, the private KPIs the macOS port had to work around (vnode internals, mount flags, kauth) are exported cleanly; each such export is a patch in `kernel/patches/` paired with the OS-layer file that needs it. The FreeBSD layer is the second reference wherever the macOS layer is entangled with macOS-only behaviour.
2. **Build as `zfs.kext`** (SPL included) with the `kext` rule; the userland (`libzfs`, `zpool`, `zfs`, `zed`) builds with `rules_foreign_cc` from the same mirror. `vdev` backend is `IOStorageFamily` media (the macOS port's `vdev_disk` over IOMedia).
3. **Kernel ABI contract:** `zfs.kext` declares `kernel-abi` and is part of the boot kernel collection because root is on ZFS.

## 3. Root on ZFS and boot environments

| Piece | Design |
|---|---|
| Pool layout | `ndpool/ROOT/<be-name>` (system sets, `readonly=on`), `ndpool/pkg` (package store, `compression=zstd`), `ndpool/home/<user>`, `ndpool/var`, `ndpool/n` (namespace-backed datasets, optional) |
| Root mount | `neoboot` passes `rd=zfs:ndpool/ROOT/<be>`; a small extension where `bsd_init` parses `rd=` (`bsd/kern/bsd_init.c:460`) lets `zfs.kext` import the pool from the devices named in `boot.cfg` and mount the dataset as root once IOStorageFamily has published them |
| Boot environments | `ndpkg system upgrade` clones the current BE, applies the system-set transaction inside it, snapshots it, marks it `next`; `neoboot` reads `boot.cfg` (`default`, `next`, `tries`); success promotes `next`, failure boots the previous BE. `bectl`-style commands: `ndpkg system list/activate/rollback/destroy` |
| Kernel collection placement | each BE's kernel collection lives on the ESP under `/EFI/NeoDarwin/be/<be-name>/kc.boot`, written by the same transaction, so `neoboot` needs no ZFS reader. A later epic (P3-06) adds a read-only ZFS reader to `neoboot` (FreeBSD's `stand/libsa/zfs`, BSD-licensed, is the reference) so kernels can live inside the BE |
| Integrity | system sets are received from signed `zfs send` streams (`ndsign` over the stream hash); the BE's checksum tree is the image verification |
| Encryption | ZFS native encryption for `home` and `var`; keys held by `keyd`; unlock at login via `/n/sys/keys` |
| Snapshots for agents | `ndpkg` and `nsd` take named snapshots before risky operations (`pre-upgrade`, `pre-agent-session-<id>`) and expose them under `/n/sys/fs/snapshots`, giving an undo for agent actions on user data |

## 4. Other filesystems

| Kext | Source | Role | Roadmap |
|---|---|---|---|
| `hfs.kext` | Apple open source | bootstrap root during Phase 1; read HFS+ media | P1 |
| `msdosfs` | FreeBSD `sys/fs/msdosfs` (BSD) | ESP maintenance from userland (write kernel collections, `boot.cfg`) | P2 |
| `ndfuse.kext` | port of FreeBSD `sys/fs/fuse` (BSD-2) | FUSE protocol for userland filesystems (SMB/NFS/S3 gateways, experimental formats) | P3 |
| `nd9p.kext` | port of FreeBSD `sys/fs/p9fs` (BSD-2) | mounts `/n` (namespaces design); virtio-9p shared folders for development | P3/P5 |
| exFAT, NTFS, ext4 | userland via FUSE from ports | removable media | ports |

All plug in through `vfs_fsadd` (`bsd/sys/mount.h:569-875`), the same KPI Apple's `hfs` uses; the first port teaches the pattern for the rest.

## 5. Bring-up ladder

mockfs executable ramdisk (kernel M3) → HFS+ ramdisk root (M4) → HFS+ image root plus `zfs.kext` mounting a data pool (P3-01) → root on ZFS with boot environments (P3-03) → signed `zfs send` system sets and `ndpkg system` (P2-03 completes on top of P3-03) → `neoboot` ZFS reader (P3-06).

## 6. Risks

| Risk | Mitigation |
|---|---|
| The macOS OS layer depends on XNU private KPIs that changed in xnu-12377 | export what is needed via `kernel/patches`; the FreeBSD layer is the second reference for any symbol the macOS layer abused |
| ARC memory pressure on 4–8 GB boards | `zfs_arc_max` set by `pkgd` from `hw.memsize` at boot; ARC hooks XNU memory-pressure notifications (the macOS layer already does) |
| 16K kernel page size | ZFS is page-size agnostic through `PAGE_SIZE` in SPL; CI runs 16K and 4K kernels |
| CDDL and APSL in one running kernel | kext, not static link; both licences permit it; recorded in `LICENSE.md` |
| Root-on-ZFS needs `zfs.kext` before `IOFindBSDRoot` | boot collection includes `zfs.kext` and IOStorageFamily; pool import is bounded to the devices in `boot.cfg` |
