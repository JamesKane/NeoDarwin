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

## 7. Status: P3-01 (zfs.kext on QEMU)

### 7.1 Checkpoint 1: zfs.kext and a pool

**`zfs.kext` loads from the boot kernel collection, its SPL and ZFS start, and on QEMU `virt` a pool on a second, blank virtio-blk disk is created, written, exported, imported from `/dev` with its data intact, and scrubbed clean** (`//kernel:sbsa_zfs_pool_test`). It is the first kext in a NeoDarwin collection.

| Piece | What |
|---|---|
| `@openzfs` (`kexts/zfs/upstream.lock`) | `zfs-macOS-2.4.1p1`, pinned by commit (§2). CDDL-1.0 (`THIRD_PARTY_NOTICES.md`) |
| `kexts/zfs/common.sh`, `patches/` | the NeoDarwin OS layer: the macOS layer's directories (`module/os`, `include/os`, `lib/libspl/include/os`, `lib/lib{spl,zfs,zfs_core,zutil}/os`, `cmd/zpool/os`) copied to `os/neodarwin`, then the patches (below; 0005 to 0010 are checkpoint 2's). Built with `__NEODARWIN__` defined |
| `//kexts/zfs:zfs` (`kext.sh`, `kext_sources.txt`) | `zfs.kext`: SPL, ZFS, ICP, Lua and zstd as `module/os/macos/Makefile.am` builds them, against NeoDarwin's `Kernel.framework` (Headers and PrivateHeaders, `//kernel:headers`) and IOStorageFamily-331's headers; `ld -kext` with libkmod built from xnu and `libclang_rt.cc_kext`. 3.9 MB, 2,800 imports |
| kernel patches | 0039 exports IOStorageFamily's classes (the only symbols the kext needed that the kernel didn't export: the BSD, Mach, libkern and IOKit KPIs it uses, private ones included, were all exported already); 0040 links the kernel with split-segment info |
| `//tools/kcgen` | links kexts into a collection (`arm64-sbsa-bringup.md` §2.1.1): segments placed in the kext regions `arm_vm_init()` derives, references fixed from split-segment info, imports resolved against the kernel's exports, `__PRELINK_INFO` entries and `kmod_info` as kmutil writes them, codeless kexts for `com.apple.kpi.*` and IOStorageFamily |
| `//kernel:sbsa_zfs_kc` | the SBSA kernel with `zfs.kext`; `//kernel:sbsa_zfs_kc_check` runs kcheck on it |
| `//kexts/zfs:zfs_commands` (`userland.sh`) | `/sbin/zpool` and `/sbin/zfs` with libspl, libavl, libnvpair, libzfs_core, libzutil, libefi and libzfs linked in, against the base sysroot, libSystem and OpenSSL's libcrypto. `kexts/zfs/compat`: `<libintl.h>` (no NLS), libdiskmgt (a device is in use when it or a slice of it is mounted); zlib was a `crc32()` and a failing `uncompress()` until checkpoint 2 built Apple's (§7.2) |
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
| `0005-spl-getf-keeps-the-descriptor-offset` | `getf()` starts at, and `releasefp()` sets, a regular file's descriptor offset (kernel patch 0042's KPI): `zfs receive < file`, `zfs send -R > file` (§7.2) |
| `0006-zfs-file-getattr-type-from-the-vnode` | `zfs_file_getattr()` takes the file type from the vnode: file vdevs on a ZFS dataset |
| `0007-spl-getf-refuses-descriptors-without-a-vnode` | EBADF, not a kernel panic, for a socket or an anonymous pipe |
| `0008-ldi-vnode-synchronous-io-waits-for-the-buf` | `buf_biowait()` before a synchronous buf is freed |
| `0009-libzfs-relay-sockets-as-pipes` | libzfs relays a socket (ksh93's pipelines) through its FIFO as it does a pipe |
| `0010-vdev-file-keep-file-vdevs-open-across-unmounts` | file vdevs stay open for the pool's life, as on FreeBSD and Linux, not closed at every unmount |

**ISA.** The kext is compiled with `-mcpu=cortex-a76`. `-march=armv8.2-a+rcpc`, the kernel's flag until 2026-10-02 (patch 0019 now uses `-mcpu=cortex-a76+crypto`), still let Apple clang use the default `apple-m1` CPU's SHA-3 instructions when it vectorised `arc_init()`, and the first boot took an undefined-instruction panic (`bcax`) on the A76. The ICP's Armv8 assembly is built (AES and GHASH with the crypto extensions, SHA-256, and SHA-512, which `zfs_sha512_available()` gates on `ID_AA64ISAR0_EL1`); the audit baseline lists the SHA-512 instructions and the round-constant tables `llvm-objdump` decodes as instructions. BLAKE3's assembly isn't built: Xcode 27's assembler crashes on it, and the macOS layer leaves it unused on arm64 anyway.

### 7.2 Checkpoint 2 — in progress: the OpenZFS test suite

**State (2026-10-02): the subset runs on QEMU `virt` and the ratchet is written, but has not had a clean full run yet**, so P3-01 stays `doing`. `//kernel:sbsa_zfs_suite_test` runs the fifteen `cli_root` groups of `kexts/zfs/tests/groups.txt` (`zpool_create`, `_import`, `_export`, `_destroy`, `_status`, `_scrub`; `zfs_create`, `_destroy`, `_set`, `_get`, `_snapshot`, `_rollback`, `_send`, `_receive`, `_mount`), 300 scripts with their `setup` and `cleanup` (272 tests), each held to its expected result. It is tagged `manual` and `kernel` but not `qemu` until that clean run.

| Piece | What |
|---|---|
| ksh | `/bin/ksh` is ksh93u+m 1.0.10 (`//base:ksh_shell`, EPL-2.0), in this image only; Apple's `ksh-42` (ksh93u+ 2012) doesn't build with Xcode 27's clang (`docs/base/session.md`, "ksh") |
| runner | `kexts/zfs/tests/nd-zfs-tests.ksh` (`/usr/share/zfs/nd-zfs-tests`) replaces `zfs-tests.sh` and `test-runner.py` (no Python in the base): `test-runner.py`'s semantics (a group's `setup` first, its tests SKIP if it fails, `cleanup` always; exit 0 PASS, 4 SKIP, else FAIL; KILLED at the timeout, then the failsafe callback), the suite's environment (`default.cfg` exported, `STF_SUITE`, `STF_PATH`, `DISKS`, `FILEDIR`), a per-test cap (240 s under TCG), and a final block of results for the host. If `zpool list` hangs after a killed test, it prints where processes sleep and SKIPs the rest |
| suite | `//kexts/zfs:zfs_test_suite` (`kexts/zfs/tests/suite.sh`): the groups' directories and what they include, `include/`, `callbacks/`, `test-runner/include`, `default.cfg` from `default.cfg.in` with FreeBSD's paths, and `neodarwin.run`, the groups' tests and their `pre`, `post` and timeout from `tests/runfiles/common.run`. `kexts/zfs/tests/patches` (none needed yet) is for test changes. `@openzfs//:tests` adds `tests/` to the pinned tree without touching the kext's inputs |
| helpers | `//kexts/zfs:zfs_test_commands`: 46 of `tests/zfs-tests/cmd`'s programs (`mkfile`, `file_write`, `mkbusy`, `draid`, the checksum tests, ...) at `/usr/share/zfs/zfs-tests/bin`; the Linux-only ones aren't built. `//kexts/zfs:zfs_commands` adds `zinject`, `zstream`, `zdb` and `zhack` (the last three link libzpool, `//kexts/zfs:zfs_libs`), and `/usr/share/zfs/compatibility.d`; zlib is Apple's zlib-100.120.1 as a private static library (`//kexts/zfs:zlib`), so `uncompress()` works (resume tokens) until P3-02 puts zlib in the base |
| base commands | the forty-odd commands the suite calls that the base lacked (`awk`, `grep`, `sort`, `dd`, `truncate`, `find`, `timeout`, ...), now in `//base:system_root` (`docs/base/session.md`; the parity inventory records them) |
| image | `//images:zfs_test_disk`: the ZFS session disk plus ksh, the suite and its helpers, and `/etc/zfs` for `zpool.cache` |
| test | `//kernel:sbsa_zfs_suite_test` (`kexts/zfs/tests/suite_test.sh`): 4 GB, two cortex-a76 CPUs, four blank virtio-blk disks: a 32 GB one that `nd-zfs-tests` formats HFS+ for `FILEDIR` (file vdevs; it finds it as the one with a block past 8 GB) and three 4 GB `DISKS`. Eight shards (one boot each, `groups.txt`), 5 to 23 minutes each, about 23 minutes wall time on a 16-core host. A panic stops the run at once with every CPU's frame chain (`ND_QEMU_STOP_ON`, a new `qemu_efi_test.sh` variable, and `--dump-cpus-on`) |

**The ratchet** (`kexts/zfs/tests/expected.tsv`, one line per script: path, expected result, reason). A shard fails if a test expected to PASS doesn't (a regression), if one expected to FAIL, SKIP or be KILLED passes (raise the list), or if the run and the list disagree on which tests there are (drift). A non-passing test that fails differently is reported, not failed; FLAKY accepts any result and says why. Every run writes `results.tsv`, in the list's form, to the test's outputs.

**Latest results** (runs of 2026-10-02 with every fix below except where noted; `expected.tsv` was generated from the last two full runs, the import group's from the last): **228 PASS, 63 FAIL, 6 KILLED, 1 SKIP, 2 FLAKY** of 300. Per shard in the last run: `zpool_create` 33 PASS of 39 run (the run was stopped before its end), `zpool_import` 32 of 42 run (stopped likewise; it had 18 of 47 before patch 0010), `zpool_destroy`, `_export`, `_status`, `_scrub` 29 of 40, `zfs_create` and `_destroy` 32 of 45, `zfs_snapshot`, `_rollback`, `_mount` 29 of 36, `zfs_set` 22 of 31, `zfs_get` 7 of 12, `zfs_send` and `_receive` 42 of 47.

**What the suite found, and the fixes** (all in the tree):

| Finding | Fix |
|---|---|
| Kernel panic `copy_validate_kernel_addr(...) - kaddr not in kernel` in `copyin()` from HFS+ writes once memory filled: on `ARM_LARGE_MEMORY` kernels without a monitor, `physmap_end` left out the twig-alignment padding of the physical aperture, so the highest pages' aperture addresses lay past it | kernel patch 0041: `physmap_end` (and the aperture's L1 entries) allow `PTOV_TABLE_SIZE` twigs, as the small-memory case does |
| Kernel panic `ndcrypto: kernel PRNG generate contract violated`: xnu passes `read_random()`'s generator as `cpu_number()` read with preemption on, so two threads (or an interrupt) used one ChaCha generator at once; its `avail` underflowed and `cc_clear()` zeroed the memory before it, `ngens` with it | `kernel/neodarwin/crypto/nd_random.c`: each CPU's generator is used, and the pool's lock held, with interrupts off (`nd_platform_cpu_enter()`) |
| `zfs receive < file`: "invalid backup stream": the SPL starts every descriptor at offset 0 and never moves it, so the kernel reread the BEGIN record libzfs had read; `zfs send -R > file` overwrote libzfs's header | kernel patch 0042 exports `file_offset_get()`/`file_offset_set()`; zfs patch 0005: `getf()` starts at the descriptor's offset, `releasefp()` leaves it after the module's I/O, as on FreeBSD and Linux |
| A pool on files in a ZFS dataset: "no such device in pool": ZFS's own `getattr` leaves the file-type bits out of `va_mode`, so `vdev_file_open()`'s `S_ISREG()` failed | zfs patch 0006: `zfs_file_getattr()` takes the type from the vnode |
| Kernel panic in `spl_vn_rdwr()` (VERIFY of a NULL vnode) when `zfs send` wrote to a socket: any process could panic the kernel with a socket or an anonymous pipe | zfs patch 0007: `getf()` refuses a descriptor without a vnode (EBADF) |
| ksh93's pipelines are socketpairs, so `zfs send \| zfs recv` in a test gave the kernel sockets, which libzfs's FIFO relay didn't wrap | zfs patch 0009: libzfs relays sockets as it relays pipes |
| Kernel panic `free_io_buf: bufs_iobufinuse < 0`: `buf_strategy_vnode()` freed a synchronous buf as soon as `VNOP_STRATEGY()` had started it | zfs patch 0008: `buf_biowait()` first |
| `zdb -C` found no pools (the image had no `/etc/zfs` for `zpool.cache`); `compatibility=` found no feature files | `/etc/zfs` in the image; `compatibility.d` installed with `zpool` |
| The macOS layer closes a pool's file vdevs whenever any of its datasets is unmounted (`CLOSE_ON_UNMOUNT`, for shutdown with file vdevs on the host file system); a test that removed a vdev's file then suspended the pool, and the rest of `zpool_import` failed behind it | zfs patch 0010: `vdev_file_close_on_unmount` defaults to off, as FreeBSD and Linux behave (the tunable isn't a sysctl here); shutdown with pools imported is P3-03's question |

**Known failures** (reasons per test in `expected.tsv`): zvols (11, checkpoint 3); tunables the macOS layer lacks (`set_tunable64`, 6); missing commands: `xxh128sum` (5), `fio` (4), `bzcat` (3, bzip2 is P3-02), `python3` (1); NFS sharing (5: no NFS server, and libzfs accepts any `sharenfs` value); partitioning with `diskutil` and `gpt` (`zpool_create`'s cleanup); `draidcfg.gz` left out of the image; the in-use check that doesn't see an unmounted HFS+ volume (`zpool_create_002_pos`); a timing assertion under TCG; `mount -F`; corrective receive writing a raw pool disk; six tests KILLED at the 240 s cap (`zfs_get_001`, `_009`, `zfs_set_001_neg`, `ro_props_001_pos`, `zpool_import_010_pos`, `import_rewind_config_changed`, ...: slow under TCG or hung, not yet told apart); and 20 marked "to investigate" (`zfs_mount_005`, `_007`, `_010_neg`, `_011_neg`, `_remount`, `onoffs_001`, `readonly_001`, `property_alias_001`, `zfs_get_002`, `_008`, the livelist tests, `zpool_status_006`, `zpool_scrub_print_repairing`, `zpool_error_scrub_003`, `receive-o-x_props_override`, ...).

**To finish checkpoint 2:**
1. One clean full run against `expected.tsv`: `bazel test //kernel:sbsa_zfs_suite_test --test_output=summary` (about 25 minutes). Tests the last runs didn't reach (the end of `zpool_create` and `zpool_import`) may now pass and need raising; look at each shard's `test.log` (`RATCHET:` lines) and `test.outputs/results.tsv`, which is in `expected.tsv`'s form, and copy the right lines over. Mark anything that changes between runs FLAKY with the reason.
2. Then tag the test `qemu` (`kernel/BUILD.bazel`), set P3-01 to `done` in `roadmap/backlog.yaml`, and retitle this section.
3. Optional, raising the ratchet: the "to investigate" lines; `xxh128sum` and `fio` (ports or small helpers); the KILLED ones (run the group alone with a larger cap to tell slow from hung).

**How to work on it:**
- One group: `bazel test //kernel:sbsa_zfs_suite_test --test_sharding_strategy=disabled --test_env=ZTS_GROUPS="zfs_mount zfs_get"` (each group's `setup` and `cleanup` included). `--test_env=ZTS_TEST_TIMEOUT=600` raises the per-test cap. The serial log is `bazel-testlogs/kernel/sbsa_zfs_suite_test/test.outputs/serial.log` (per shard under `shard_N_of_8/`); every non-PASS prints its last 25 lines there as `ZTS|` lines, and the full logs are in the guest's `/private/var/tmp/zts-logs`.
- Interactively: `ND_QEMU_LOG_DIR=DIR tools/efi/qemu_efi_test.sh --mem 4G --smp 2 --disk bazel-bin/images/zfs_test_disk.img --drive zts0=32G --device virtio-blk-pci,drive=zts0,disable-legacy=on` and three `--drive ztsN=4G` ones, `--until-lines --send-after 'login: ' 'root\n' --send-after 'root@localhost ~ # ' 'COMMAND\n' - 600 LINE` (copy the image first; don't run it with the repository as the working directory). In the guest, `/usr/share/zfs/nd-zfs-tests -t 240 GROUP` runs a group.
- A kernel panic stops a run at once and puts every CPU's frame chain in `test.log` (`cpus:` lines). The kernel in the ZFS collection is moved by `kcgen`: symbolize kernel frames as `llvm-symbolizer --obj=bazel-bin/kernel/kernel.release.sbsa.unstripped` at address − `0x7c000`, and frames in `0xfffffe0007c2c000`–`0xfffffe0007e84000` against `bazel-bin/kexts/zfs/zfs.kext/Contents/MacOS/zfs` at address − `0xfffffe0007c2c000` + `0x70000` (check with `otool -l` after a layout change).
- Rebuild costs: a change to `kexts/zfs/patches` rebuilds the kext and collection in seconds to a minute; a kernel patch or `kernel/neodarwin` change about 10 minutes; the image about 15 s.
- Gotchas: `@openzfs//:all` is the kext's input, so test files come from `@openzfs//:tests`; the guest's `/var` is a link, so `FILEDIR` is `/private/var/tmp/zts` (mount paths must match); HFS+ allocates file vdevs, hence the 32 GB `FILEDIR` disk; the ratchet runs on the host in awk because `/bin/bash` there is 3.2.

### 7.3 Deferred and next

**Deferred** (later checkpoints of P3-01 unless noted):
- the known failures above, each where its reason points; the rest of the test suite's groups;
- zvols, ZFSDatasetProxy disks (built; the proxy worked in a first run, before `com.apple.devdisk` defaulted to off) and snapshots mounted under `.zfs`, none tested yet, and GPT labelling of whole disks;
- `zfs.kext` in the default collection, and a `kernel-abi` declaration (§2 step 3);
- zed, the libraries as packages, zlib in the base, NLS (P3-02); root on ZFS (P3-03); native encryption beyond what the build already has (P3-10);
- the BLAKE3 and Fletcher-4 NEON paths, and the RAID-Z SIMD ones (the macOS layer builds none for arm64);
- the kext on the Q8B (the board wasn't available).

**Next checkpoints.** (3) zvols and dataset proxies: the IOKit side (`zvolIO.cpp`, `ZFSDataset*`) checked on NeoDarwin's IOStorageFamily, GPT labels on whole disks with a re-probe of the new partitions, `.zfs/snapshot` mounts, and the suite's tests that need them (a zvol-backed `new_fs`, partitioned `DISKS` through `gpt` in place of `diskutil`) raised in the ratchet. (4) The kext in `//kernel:sbsa_kc` for every boot, with its personality limited so whole disks without ZFS labels aren't probed, then root on ZFS (P3-03).
