#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# zfs.kext for NeoDarwin arm64 (P3-01, docs/architecture/filesystems.md):
# OpenZFS with the OpenZFS on OS X fork's macOS OS layer.
#   kext.sh OUT ZFS_SRC KERNEL_HEADERS XNU_SRC IOSTORAGEFAMILY_SRC
# OUT receives the bundle: Contents/Info.plist and Contents/MacOS/zfs, an
# MH_KEXT_BUNDLE that kcgen links into a boot kernel collection
# (//kernel:sbsa_zfs_kc).
#
# The OS layer is module/os/neodarwin (and include/os/neodarwin, the
# userland's lib/*/os/neodarwin): a copy of the macOS layer made here, with
# kexts/zfs/patches applied on top (prepare_tree, shared with
# userland.sh). It compiles as module/os/macos/Makefile.am builds the kext
# (macos_zfs_CPPFLAGS: -mkernel, KERNEL_PRIVATE, DRIVER_PRIVATE, the SPL's
# and zstd's include roots and prefix headers) against NeoDarwin's
# Kernel.framework (//kernel:headers, Headers and PrivateHeaders) and
# IOStorageFamily-331's headers (IOKit/storage), plain arm64 for the SBSA
# baseline: -mcpu=cortex-a76, since -march=armv8.2-a alone still lets Apple
# clang use the apple-m1 default CPU's SHA-3 instructions (BCAX) when it
# vectorises. The only host SDK files are the availability headers and
# TargetConditionals.h, which Kernel.framework's headers include and the
# kernel build gets from the SDK too.
#
# Linking is ld -kext, as Xcode links kexts: libkmod's _start and _stop
# (built here from xnu's libkern/kmod), zfs_osx.cpp's KMOD_EXPLICIT_DECL
# kmod_info, and the compiler runtime for kexts (libclang_rt.cc_kext). The
# kernel's exports satisfy every import (kcgen checks this when it links
# the collection); IOStorageFamily's classes are exported since patch 0039.
source "$(dirname "$0")/common.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; KH="$(abspath "$3")"; X="$(abspath "$4")"; IOS="$(abspath "$5")"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(prepare_tree "$Z" "$B/src")"
cd "$S"   # sources and the tree's include roots by relative path, so __FILE__ names them so

KF="$KH/System/Library/Frameworks/Kernel.framework/Versions/A"
[ -d "$KF/PrivateHeaders" ] || { echo "kext.sh: no Kernel.framework PrivateHeaders under $KH" >&2; exit 1; }
mkdir -p "$B/compat/IOKit/storage"
cp "$IOS"/*.h "$B/compat/IOKit/storage/"
cp "$PROJ/../../kernel/neodarwin/pthread/compat/TargetConditionals.h" "$B/compat/"
cp "$SDK"/usr/include/Availability*.h "$B/compat/"
RES="$(xcrun clang -print-resource-dir)"

COMMON=(-arch arm64 -mmacosx-version-min=26.0 -mcpu=cortex-a76 -O2 -Wall -nostdinc -mkernel
	-fno-builtin-printf -fno-common
	-D__KERNEL__ -D_KERNEL -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -UHAVE_LARGE_STACKS
	-DNAMEDSTREAMS=1 -D__DARWIN_64_BIT_INO_T=1 -DAPPLE -DNeXT -D__NEODARWIN__=1 -DHAVE_CONFIG_H -UDEBUG -DNDEBUG
	-include "$PROJ/include/zfs_config.h"
	-Iinclude/os/neodarwin/spl -Iinclude/os/neodarwin/zfs -Imodule/icp/include -Iinclude
	-I"$PROJ/include" -I"$KF/Headers" -I"$KF/PrivateHeaders" -I"$B/compat"
	-Imodule/zstd/include -include module/zstd/include/zstd_compat_wrapper.h
	-isystem "$RES/include"
	-ffile-prefix-map="$KF/"=Kernel.framework/ -ffile-prefix-map="$B/"= -ffile-prefix-map="$PROJ/"=kexts/zfs/
	-Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-conditional-uninitialized -Wno-shadow
	-Wno-implicit-int-conversion -Wno-macro-redefined)
write_rsp "$B/cflags" "${COMMON[@]}"
write_rsp "$B/cxxflags" "${COMMON[@]}" -std=gnu++11 -fapple-kext -fno-rtti -fno-exceptions

C=(); CXX=()
while read -r f; do
	case "$f" in ''|'#'*) continue ;; *.cpp) CXX+=("$f") ;; *) C+=("$f") ;; esac
done < "$PROJ/kext_sources.txt"
compile "$B/obj" "$B/cflags" "${C[@]}"
compile "$B/obj" "$B/cxxflags" "${CXX[@]}"

# libkmod: _start/_stop and the kext's identifier accessors.
write_rsp "$B/kmodflags" -arch arm64 -mmacosx-version-min=26.0 -mcpu=cortex-a76 -O2 -nostdinc -mkernel \
	-DKERNEL -I"$KF/Headers" -I"$B/compat" -isystem "$RES/include"
compile "$B/kmod" "$B/kmodflags" "$X/libkern/kmod/c_start.c" "$X/libkern/kmod/c_stop.c"

mkdir -p "$OUT/Contents/MacOS"
xcrun ld -arch arm64 -kext -platform_version macos 26.0 26.0 -o "$OUT/Contents/MacOS/zfs" \
	"$B"/obj/*.o "$B"/kmod/*.o "$RES/lib/darwin/libclang_rt.cc_kext.a"

# Info.plist: the macOS layer's, with the release's version, required at
# boot (the Makefile's PlistBuddy edits).
cp "$S/module/os/neodarwin/zfs/Info.plist" "$OUT/Contents/Info.plist"
plutil -replace CFBundleVersion -string 2.4.1 "$OUT/Contents/Info.plist"
plutil -replace CFBundleShortVersionString -string 2.4.1 "$OUT/Contents/Info.plist"
plutil -replace OSBundleRequired -string Root "$OUT/Contents/Info.plist"
plutil -convert xml1 "$OUT/Contents/Info.plist"
