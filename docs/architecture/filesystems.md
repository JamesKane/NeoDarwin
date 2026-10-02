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
| Observability | snapshots as immutable state points; dataset properties as a metadata store readable with `zfs get -j` and by downstreams |
| Licensing | CDDL-1.0, file-scope copyleft, shippable alongside APSL/BSD code as a kext (this is how ZFS has shipped on macOS and FreeBSD for years); nothing GPL enters the kernel |
| Existing XNU port | OpenZFS has run on XNU for a decade through the OpenZFS on OS X project (`zfs.kext` + SPL); NeoDarwin inherits a working IOKit/VFS binding instead of writing a filesystem |

"Or similar" alternatives considered: **bcachefs** (GPL, Linux-only; rejected on licence and maturity), **HAMMER2** (BSD, DragonFly-only, small community, no XNU port), **btrfs** (GPL), **APFS** (closed), **XFS** (GPL, no BSD-licensed implementation; would have needed a clean-room kext). OpenZFS is the only candidate that is licence-compatible, multi-platform and already on XNU.

## 2. Source and porting surface

OpenZFS keeps platform code behind an OS layer: `module/os/<os>/spl` (locks, memory, threads, taskq, kstat, uio, vfs shims; 22 files in the FreeBSD layer) and `module/os/<os>/zfs` (VFS/vnode glue, vdev backend, zvol, ioctl device, crypto, sysctl; 25 files), with matching `include/os/<os>`. The FreeBSD-vendored copy in this workspace (`../freebsd-src/sys/contrib/openzfs`, OpenZFS 2.4.99, CDDL) carries the `freebsd` and `linux` layers and is the structural reference. The macOS/XNU layer lives in the OpenZFS on OS X fork (`module/os/macos`, `include/os/macos`, `cmd/os/macos`).

**Upstream status (P0-08, checked 2026-10-01).** No `openzfs/zfs` release carries the macOS layer: `zfs-2.4.4`, `zfs-2.3.9` (both 2026-08-21) and `master` have `module/os/{freebsd,linux}` only, and `openzfs/zfs#12110` ("Add macOS support to OpenZFS", 2021) and its split-out PRs (#15471, #15521, #15523) are still open. The layer is maintained in `openzfsonosx/openzfs-fork` (a GitHub fork of `openzfs/zfs`; the older `openzfsonosx/openzfs` stops at `zfs-macOS-2.1.99`, 2022). Its latest release is **`zfs-macOS-2.4.1p1`** (OpenZFS 2.4.1, release published 2026-07-30; `zfs-macOS-2.4.3rc5` is a pre-release), an annotated tag on commit `a4c1b11ab900`, which has `module/os/macos`. P0-08's exit can only be met with the fork, so the fork's tag is the pin: `kexts/zfs/upstream.lock` and the `openzfs` archive in `MODULE.bazel` (sha256 `b2004967…ac37c1`). The archive is fetched by commit, not by tag name, because the tag has been moved once (its commit is dated 2026-09-28, two months after its release); the trees are identical. **`mirror-openzfs`** can't be created from the build: it is to mirror `openzfsonosx/openzfs-fork` verbatim, branches and tags, with `zfs-macOS-2.4.1p1` tagged `openzfs/zfs-macOS-2.4.1p1` at `a4c1b11ab900` (`repository.md` §2), after which the archive URL moves to the mirror with the same hash. NeoDarwin follows the fork's releases until the layer is merged upstream.

Plan:
1. **Adopt the macOS OS layer as `module/os/neodarwin`.** Because NeoDarwin owns the kernel, the private KPIs the macOS port had to work around (vnode internals, mount flags, kauth) are exported cleanly; each such export is a patch in `kernel/patches/` paired with the OS-layer file that needs it. The FreeBSD layer is the second reference wherever the macOS layer is entangled with macOS-only behaviour.
2. **Build as `zfs.kext`** (SPL included) with the `kext` rule; the userland (`libzfs`, `zpool`, `zfs`, `zed`) builds from the same pin. `vdev` backend is `IOStorageFamily` media (the macOS port's `vdev_disk` over IOMedia). As built (§7): `nd_kext` and `base_library` running `kexts/zfs`'s scripts, not `rules_foreign_cc`.
3. **Kernel ABI contract:** `zfs.kext` declares `kernel-abi` and is part of the boot kernel collection because root is on ZFS.

## 3. Root on ZFS and boot environments

| Piece | Design |
|---|---|
| Pool layout | `ndpool/ROOT/<be-name>` (system sets, `readonly=on`), `ndpool/pkg` (package store, `compression=zstd`), `ndpool/home/<user>`, `ndpool/var` |
| Root mount | `neoboot` passes `rd=zfs:ndpool/ROOT/<be>`; a small extension where `bsd_init` parses `rd=` (`bsd/kern/bsd_init.c:460`) lets `zfs.kext` import the pool from the devices named in `boot.cfg` and mount the dataset as root once IOStorageFamily has published them |
| Boot environments | `ndpkg system upgrade` clones the current BE, applies the system-set transaction inside it, snapshots it, marks it `next`; `neoboot` reads `boot.cfg` (`default`, `next`, `tries`); success promotes `next`, failure boots the previous BE. `bectl`-style commands: `ndpkg system list/activate/rollback/destroy` |
| Kernel collection placement | each BE's kernel collection lives on the ESP under `/EFI/NeoDarwin/be/<be-name>/kc.boot`, written by the same transaction, so `neoboot` needs no ZFS reader. A later epic (P3-06) adds a read-only ZFS reader to `neoboot` (FreeBSD's `stand/libsa/zfs`, BSD-licensed, is the reference) so kernels can live inside the BE |
| Integrity | system sets are received from signed `zfs send` streams (`ndsign` over the stream hash); the BE's checksum tree is the image verification |
| Encryption | ZFS native encryption for `home` and `var`; a passphrase or key file unlocked with `zfs load-key` at boot or login, as on FreeBSD (P3-10). A downstream may hold keys in its own key service |
| Snapshots before risky operations | `ndpkg` takes named snapshots (`pre-upgrade-<txn>`) before every transaction. The mechanism is available to downstreams, and Magi snapshots around agent sessions this way |

## 4. Other filesystems

| Kext | Source | Role | Roadmap |
|---|---|---|---|
| `hfs.kext` | Apple open source | bootstrap root during Phase 1; read HFS+ media. Built into the kernel until `kcgen` links kexts (M5): patch 0015, P1-08a | P1 |
| `msdosfs` | FreeBSD `sys/fs/msdosfs` (BSD) | ESP maintenance from userland (write kernel collections, `boot.cfg`) | P2 |
| `ndfuse.kext` | port of FreeBSD `sys/fs/fuse` (BSD-2) | FUSE protocol for userland filesystems (SMB/NFS/S3 gateways, experimental formats) | P3 |
| exFAT, NTFS, ext4 | userland via FUSE from ports | removable media | ports |

All plug in through `vfs_fsadd` (`bsd/sys/mount.h:569-875`), the same KPI Apple's `hfs` uses; the first port teaches the pattern for the rest. The KPI is on the exported list that downstreams rely on (`downstream.md` §3); Magi's `nd9p.kext` (a FreeBSD `p9fs` port) plugs in this way.

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

## 7. Status: P3-01 checkpoint 1 (zfs.kext on QEMU)

**`zfs.kext` loads from the boot kernel collection, its SPL and ZFS start, and on QEMU `virt` a pool on a second, blank virtio-blk disk is created, written, exported, imported from `/dev` with its data intact, and scrubbed clean** (`//kernel:sbsa_zfs_pool_test`). It is the first kext in a NeoDarwin collection.

| Piece | What |
|---|---|
| `@openzfs` (`kexts/zfs/upstream.lock`) | `zfs-macOS-2.4.1p1`, pinned by commit (§2). CDDL-1.0 (`THIRD_PARTY_NOTICES.md`) |
| `kexts/zfs/common.sh`, `patches/` | the NeoDarwin OS layer: the macOS layer's directories (`module/os`, `include/os`, `lib/libspl/include/os`, `lib/lib{spl,zfs,zfs_core,zutil}/os`, `cmd/zpool/os`) copied to `os/neodarwin`, then four patches (below). Built with `__NEODARWIN__` defined |
| `//kexts/zfs:zfs` (`kext.sh`, `kext_sources.txt`) | `zfs.kext`: SPL, ZFS, ICP, Lua and zstd as `module/os/macos/Makefile.am` builds them, against NeoDarwin's `Kernel.framework` (Headers and PrivateHeaders, `//kernel:headers`) and IOStorageFamily-331's headers; `ld -kext` with libkmod built from xnu and `libclang_rt.cc_kext`. 3.9 MB, 2,800 imports |
| kernel patches | 0039 exports IOStorageFamily's classes (the only symbols the kext needed that the kernel didn't export: the BSD, Mach, libkern and IOKit KPIs it uses, private ones included, were all exported already); 0040 links the kernel with split-segment info |
| `//tools/kcgen` | links kexts into a collection (`arm64-sbsa-bringup.md` §2.1.1): segments placed in the kext regions `arm_vm_init()` derives, references fixed from split-segment info, imports resolved against the kernel's exports, `__PRELINK_INFO` entries and `kmod_info` as kmutil writes them, codeless kexts for `com.apple.kpi.*` and IOStorageFamily |
| `//kernel:sbsa_zfs_kc` | the SBSA kernel with `zfs.kext`; `//kernel:sbsa_zfs_kc_check` runs kcheck on it |
| `//kexts/zfs:zfs_commands` (`userland.sh`) | `/sbin/zpool` and `/sbin/zfs` with libspl, libavl, libnvpair, libzfs_core, libzutil, libefi and libzfs linked in, against the base sysroot, libSystem and OpenSSL's libcrypto. `kexts/zfs/compat`: `<libintl.h>` (no NLS), libdiskmgt (a device is in use when it or a slice of it is mounted) and zlib's `crc32()` (libefi's GPT checksums; `uncompress()` fails, so resume tokens can't be decoded) |
| `//images:zfs_session_disk` | the session disk with that collection and the commands |
| `//kexts/zfs:zfs_isa_audit` | the kext against the SBSA ISA baseline (`kexts/zfs/isa_audit.txt`) |

**How it relates to the kernel.** `zfs.kext` is a separate fileset entry in the collection with its own segments and Mach-O header: CDDL code is never linked into the APSL kernel image, as §1 requires. `OSKext` reads it from `__PRELINK_INFO`, and loads it when its IOKit personality (`org_openzfsonosx_zfs_zvol` on `IOResources`, once `IOBSD` is published) matches; `zfs_osx.cpp`'s `start()` runs `spl_start()` and ZFS's initialisation, registers `zfs` with `vfs_fsadd()` and creates `/dev/zfs`. Kexts depend on `com.apple.kpi.bsd`, `iokit`, `libkern`, `mach` and `unsupported` and on `com.apple.iokit.IOStorageFamily`, all codeless kexts in the collection. It is not part of the default collection (`//kernel:sbsa_kc`) yet.

**The patches** (each with its rationale and `Rebase-risk`):

| Patch | What |
|---|---|
| `0001-spl-generic-arm64-cpu-numbers-no-i386-headers` | `getcpuid()` asks the kernel (`cpu_number()`) instead of decoding MPIDR as Apple silicon lays it out, which gave every core of a DynamIQ cluster (the Q8B) one number; `<sys/proc.h>` includes `<i386/locks.h>` on x86 only |
| `0002-libzfs-no-iokit-corefoundation-or-diskutil` | unmount through `unmount(2)` (FreeBSD's way), not `diskutil`; no IORegistry lookup of proxy disks in the mount table; no zvol media to eject; libefi without DiskArbitration |
| `0003-libzutil-pools-in-dev-whole-disks-unlabelled` | pools are found in `/dev` (no InvariantDisks `/var/run/disk/by-*`), through block devices only (character devices such as `klog` block on read: the first import hung); a whole disk is used as given, without a GPT, as on FreeBSD |
| `0004-freebsd-defaults-mountpoint-devdisk-dbgmsg` | pools mount at `/<pool>`, not `/Volumes/<pool>`; `com.apple.devdisk` defaults to off (a root dataset mounted from a ZFSDatasetProxy `/dev/disk` node was unknown to libzfs's mount table, and `zpool export` found the pool busy); `zfs_dbgmsg()` goes to the kstat only, not to the console |

**ISA.** The kext is compiled with `-mcpu=cortex-a76`. `-march=armv8.2-a+rcpc`, the kernel's flag until 2026-10-02 (patch 0019 now uses `-mcpu=cortex-a76+crypto`), still let Apple clang use the default `apple-m1` CPU's SHA-3 instructions when it vectorised `arc_init()`, and the first boot took an undefined-instruction panic (`bcax`) on the A76. The ICP's Armv8 assembly is built (AES and GHASH with the crypto extensions, SHA-256, and SHA-512, which `zfs_sha512_available()` gates on `ID_AA64ISAR0_EL1`); the audit baseline lists the SHA-512 instructions and the round-constant tables `llvm-objdump` decodes as instructions. BLAKE3's assembly isn't built: Xcode 27's assembler crashes on it, and the macOS layer leaves it unused on arm64 anyway.

**Deferred** (later checkpoints of P3-01 unless noted):
- the OpenZFS test suite subset of P3-01's exit (`tests/zfs-tests`; needs the ksh harness and its commands in the image);
- zvols, ZFSDatasetProxy disks (built; the proxy worked in a first run, before `com.apple.devdisk` defaulted to off) and snapshots mounted under `.zfs`, none tested yet, and GPT labelling of whole disks;
- `zfs.kext` in the default collection, and a `kernel-abi` declaration (§2 step 3);
- zed, the libraries as packages, zlib in the base, NLS (P3-02); root on ZFS (P3-03); native encryption beyond what the build already has (P3-10);
- the BLAKE3 and Fletcher-4 NEON paths, and the RAID-Z SIMD ones (the macOS layer builds none for arm64);
- the kext on the Q8B (the board wasn't available).

**Next checkpoints.** (2) The ZFS test suite subset: ksh and the test commands in an image, `zfs-tests.sh` with a run file of the functional groups that need no zvols (`cli_root/zpool_*`, `zfs_*`, `mount`, `snapshot`, `send`), on file vdevs and virtio disks, with the results ratcheted like the ISA audit; this meets P3-01's exit. (3) zvols and dataset proxies: the IOKit side (`zvolIO.cpp`, `ZFSDataset*`) checked on NeoDarwin's IOStorageFamily, GPT labels on whole disks with a re-probe of the new partitions, `.zfs/snapshot` mounts. (4) The kext in `//kernel:sbsa_kc` for every boot, with its personality limited so whole disks without ZFS labels aren't probed, then root on ZFS (P3-03).
