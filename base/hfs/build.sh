#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# HFS+'s userland from hfs-704.0.3.0.2, the kernel's HFS pin
# (docs/base/session.md): replays hfs.xcodeproj's mount_hfs, newfs_hfs and
# fsck_hfs targets (hfs.xcconfig: VERSIONING_SYSTEM apple-generic,
# DEAD_CODE_STRIPPING, FS_BUNDLE_BIN_PATH) and the hfs.fs target's install:
# the tools in the file system bundle, with /sbin links to them, as macOS has.
#   build.sh OUT HFS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libutil_dylib)
# OUT receives System/Library/Filesystems/hfs.fs/Contents/Resources/
# {mount_hfs,newfs_hfs,fsck_hfs} and sbin/{mount_hfs,newfs_hfs,fsck_hfs}.
# The targets link CoreFoundation and IOKit (and fsck_hfs FSKit), none of
# which NeoDarwin has; patches 0001-0004 compile their uses out, under
# HFS_NO_FRAMEWORKS for 0002-0004 (NeoDarwin's switch; without it upstream
# is unchanged):
#   mount_hfs  the sparse disk image (DiskImages2) and optical media checks
#              (optical.c is left out, as on iOS);
#   newfs_hfs  an external journal device's IOMedia UUID (-J with a device
#              fails), and CFString's canonical decomposition of the volume
#              name: an ASCII name is used as given, others fall back to
#              newfs_hfs's own "untitled";
#   fsck_hfs   FSKit's progress reports, and finding an external journal
#              device by its IOMedia UUID.
# CoreFoundation, CoreServices and CommonCrypto headers are used for types
# and constants only; nothing links beyond libSystem and libutil.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
UTIL=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"; done
[ -n "$UTIL" ] || { echo "hfs: no DEPROOT holds usr/lib/libutil.dylib (pass //base:libutil_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S"
D="$B/derived"; mkdir -p "$D"

BUNDLE=System/Library/Filesystems/hfs.fs/Contents/Resources
# hfs.xcconfig; WARNING_CFLAGS change no interface and are left out.
# CURRENT_PROJECT_VERSION is RC_ProjectSourceVersion.
base=("${TARGET_FLAGS[@]}" -Os -DFS_BUNDLE_BIN_PATH="\"/$BUNDLE\"" -DHFS_NO_FRAMEWORKS=1
	-I"$UTIL/usr/local/include" $(cmd_sysroot_flags "$SYSROOT"))
vers() { write_vers "$D/${1}_vers.c" "$1" hfs 704.0.3.0.2; printf '%s' "$D/${1}_vers.c"; }
libutil=(-L"$UTIL/usr/lib" -lutil)

# mount_hfs and newfs_hfs: libutil for mntopts(3) and wipefs(3).
write_rsp "$B/cflags" "${base[@]}"
tool "$B" "$ROOT" "$OUT/$BUNDLE/mount_hfs" "$B/cflags" mount_hfs/mount_hfs.c "$(vers mount_hfs)" -- "${libutil[@]}"
tool "$B" "$ROOT" "$OUT/$BUNDLE/newfs_hfs" "$B/cflags" newfs_hfs/newfs_hfs.c newfs_hfs/makehfs.c \
	newfs_hfs/hfs_endian.c "$(vers newfs_hfs)" -- "${libutil[@]}"

# fsck_hfs: lib_fsck_hfs and its dfalib compiled into the tool, as the
# target lists them; GCC_PREPROCESSOR_DEFINITIONS BSD=1 CONFIG_HFS_TRIM=1
# DEBUG_BUILD=0; the headers by their directories (USE_HEADERMAP).
write_rsp "$B/fsck.rsp" "${base[@]}" -DBSD=1 -DCONFIG_HFS_TRIM=1 -DDEBUG_BUILD=0 \
	-Ilib_fsck_hfs -Ilib_fsck_hfs/dfalib -Ifsck_hfs
dfalib=()
for f in SControl BlockCache BTree BTreeAllocate BTreeMiscOps BTreeNodeOps BTreeScanner BTreeTreeOps CatalogCheck \
	dirhardlink HardLinkCheck hfs_endian SAllocate SBTree SCatalog SDevice SExtents SKeyCompare SRebuildBTree SRepair \
	SStubs SUtils SVerify1 SVerify2 uuid VolumeBitmapCheck; do dfalib+=("lib_fsck_hfs/dfalib/$f.c"); done
tool "$B" "$ROOT" "$OUT/$BUNDLE/fsck_hfs" "$B/fsck.rsp" fsck_hfs/utilities.c lib_fsck_hfs/cache.c "${dfalib[@]}" \
	lib_fsck_hfs/check.c lib_fsck_hfs/lib_fsck_hfs.c fsck_hfs/fsck_hfs.c lib_fsck_hfs/fsck_debug.c \
	fsck_hfs/fsck_messages.c lib_fsck_hfs/fsck_strings.c lib_fsck_hfs/fsck_hfs_strings.c lib_fsck_hfs/fsck_journal.c \
	"$(vers fsck_hfs)"

# hfs.fs's "Create Symlink" phase: /sbin/<tool> -> the bundle's. macOS's
# links are absolute; these are relative (as macOS's apfs links are), so no
# host tool that follows links while building an image reaches the build
# machine's own /System.
mkdir -p "$OUT/sbin"
for t in newfs_hfs fsck_hfs mount_hfs; do ln -sfh "../$BUNDLE/$t" "$OUT/sbin/$t"; done
