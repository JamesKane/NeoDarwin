#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# mount, umount and fsck from diskdev_cmds-751 (docs/base/session.md):
# replays diskdev_cmds.xcodeproj's mount, umount and fsck targets and the
# libdisk archive mount and umount link (project settings: DEAD_CODE_STRIPPING;
# each target installs in /sbin).
#   build.sh OUT DISKDEV_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives sbin/{mount,umount,fsck}.
# The targets link CoreFoundation, IOKit and APFS.framework, and mount also
# FSKit, LiveFS and UserManagementLayout, all closed. On macOS their code is
# all in iOS-only blocks (edt_fstab.c, mount_tmpfs.c and mount's EDT and
# media-key paths compile to nothing), except FSKit: fskit_support.m falls
# back to returning ENOTSUP without FSKit's private headers (patch 0002), so
# mount runs mount_<type> from /sbin, and APFS's headers, which mount.c
# includes everywhere (patch 0001). Nothing links beyond libSystem.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S"
D="$B/derived"; mkdir -p "$D"

# Release settings; WARNING_CFLAGS change no interface and are left out.
# CURRENT_PROJECT_VERSION comes from the build system (RC_ProjectSourceVersion).
base=("${TARGET_FLAGS[@]}" -Os $(cmd_sysroot_flags "$SYSROOT"))
vers() { write_vers "$D/${1}_vers.c" "$1" diskdev_cmds 751; printf '%s' "$D/${1}_vers.c"; }

# libdisk (a static library; HEADER_SEARCH_PATHS System.framework's
# PrivateHeaders, here the sysroot's).
write_rsp "$B/libdisk.rsp" "${base[@]}" \
	-isystem "$SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders"
compile "$B/obj/libdisk" "$B/libdisk.rsp" disklib/dkcksum.c disklib/dksecsize.c disklib/preen.c \
	disklib/vfslist.c disklib/dkdisklabel.c disklib/dkopen.c
xcrun libtool -static -no_warning_for_no_symbols -o "$B/libdisk.a" "$B"/obj/libdisk/*.o

# The tools: the project's own headers by their directories (USE_HEADERMAP).
write_rsp "$B/cflags" "${base[@]}" -Idisklib -Iedt_fstab -Imount_flags_dir -Ifsck.tproj -Imount.tproj
# fskit_support.m is compiled with ARC; without FSKit it holds no Objective-C.
write_rsp "$B/objc.rsp" "${base[@]}" -fobjc-arc -Idisklib

compile "$B/obj/fskit" "$B/objc.rsp" disklib/fskit_support.m
tool "$B" "$ROOT" "$OUT/sbin/mount" "$B/cflags" mount.tproj/mount_tmpfs.c edt_fstab/edt_fstab.c mount.tproj/mount.c \
	mount_flags_dir/mount_flags.c "$(vers mount)" -- "$B"/obj/fskit/*.o "$B/libdisk.a"
tool "$B" "$ROOT" "$OUT/sbin/umount" "$B/cflags" edt_fstab/edt_fstab.c umount.tproj/umount.c "$(vers umount)" \
	-- "$B/libdisk.a"
tool "$B" "$ROOT" "$OUT/sbin/fsck" "$B/cflags" edt_fstab/edt_fstab.c fsck.tproj/fsck.c "$(vers fsck)"

# fstyp and its helpers (P4-21 checkpoint 6, docs/architecture/freebsd-parity.md
# §2.1): the fstyp, fstyp_msdos, fstyp_ntfs and fstyp_udf targets, one
# source each, INSTALL_PATH /sbin; their pages are fstyp.tproj's. fstyp runs
# each fstyp_* it finds in /bin, /sbin, /usr/bin, /usr/sbin and
# /usr/local/{bin,sbin} on the device and prints the type of the first
# that matches.
write_rsp "$B/fstyp.rsp" "${base[@]}"
for t in fstyp fstyp_msdos fstyp_ntfs fstyp_udf; do
	tool "$B" "$ROOT" "$OUT/sbin/$t" "$B/fstyp.rsp" "fstyp.tproj/$t.c" "$(vers "$t")"
done
