#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_c from Libc-1725.0.11 (docs/base/libsystem.md): replays
# Libc.xcodeproj's libsystem_c.dylib target and the archive targets it links
# (libc.xcconfig BUILD_ARCHIVES), plus the libc_dyld variant, with libc.xcconfig
# and variants.xcconfig. Source lists are in sources/, taken from the project;
# the file filters below are the xcconfigs' INCLUDED/EXCLUDED_SOURCE_FILE_NAMES
# for arm64 macOS. Variant archives that only apply to i386/x86_64 (Legacy,
# Inode32, Pre1050) are empty on arm64, as is DarwinExtsn_Cancelable, and are
# left out.
#   build.sh OUT LIBC_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, platform, pthread, malloc)
# OUT receives usr/lib/system/libsystem_c.dylib and usr/local/lib/dyld/libc.a.
# Source lists are "path<TAB>COMPILER_FLAGS", the per-file flags of the project.
# Unbuilt dependencies (libdyld, libcompiler_rt, libsystem_m, and the upward
# links) link through the host SDK's .tbd stubs until NeoDarwin's exist.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; L="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
L="$(stage_src "$L" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$L"
D="$B/derived"; mkdir -p "$D"

# Generate libc-features.h (every archive target's first script phase).
(export SRCROOT="$L" DERIVED_FILES_DIR="$D" ARCHS=arm64 CURRENT_ARCH=arm64 VARIANT_PLATFORM_NAME=macosx
 perl xcodescripts/generate_features.pl) > /dev/null
# Variant targets' "Patch Headers" phase: PrivateHeaders with variant-aware
# declarations, searched instead of the sysroot's (variants.xcconfig).
PH="$SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders"
perl xcodescripts/patch_headers_variants.pl "$PH" "$D/System.framework/Versions/B" > /dev/null

BASE_EXCLUDED="kvm.c nlist.c OSMemoryNotification.c"   # _arm64 and _macosx

# libc.xcconfig: BASE_PREPROCESSOR_MACROS, OTHER_CFLAGS, the search paths.
srcroot_paths=(-I"$L" -I"$L/include" -I"$L/gen" -I"$L/locale" -I"$L/locale/FreeBSD" -I"$L/stdtime/FreeBSD" -I"$L/darwin")
fbsd_paths=(-I"$L/fbsdcompat" -I"$L/gdtoa" -I"$L/gdtoa/FreeBSD")
common=("${TARGET_FLAGS[@]}" -Os -std=gnu11 -fdollars-in-identifiers -fno-common -fverbose-asm
	-Werror=implicit-function-declaration -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1
	-D__DARWIN_NON_CANCELABLE=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 -I"$D/arm64" -I"$D/dtrace")
plain_system=($(sysroot_flags "$SYSROOT"))
variant_system=(-isystem "$D/System.framework/Versions/B/PrivateHeaders" "${plain_system[@]}")

# select_ TARGET pick|drop|all NAMES: TARGET's list lines ("path<TAB>flags"),
# keeping (pick) or dropping (drop) files by name, as the xcconfigs'
# INCLUDED/EXCLUDED_SOURCE_FILE_NAMES do.
select_() {
	local target="$1" mode="$2" names=" $3 " line path
	grep -v '^#' "$PROJ/sources/$target.txt" | while IFS= read -r line; do
		path="${line%%	*}"
		case "$mode:$names" in
			pick:*" ${path##*/} "*|drop:*|all:*) ;;
			*) continue ;;
		esac
		case "$mode:$names" in drop:*" ${path##*/} "*) continue ;; esac
		printf '%s\n' "$line"
	done
}

# build NAME FLAGS... -- LISTFILE: compile LISTFILE's sources into lib<NAME>.a,
# each group of files sharing per-file COMPILER_FLAGS with its own flags.
build() {
	local name="$1"; shift; local -a flags=(); while [ "$1" != "--" ]; do flags+=("$1"); shift; done; shift
	local listfile="$1" n=0 group
	[ -s "$listfile" ] || return 0
	while IFS= read -r group; do
		n=$((n + 1))
		local -a extra=(); local g="${group//\$(SRCROOT)/$L}"
		g="${g//\$(FreeBSD_CFLAGS)/-include $L/fbsdcompat/_fbsd_compat_.h}"
		[ -n "$g" ] && read -r -a extra <<< "$g"
		write_rsp "$B/$name.$n.rsp" "${common[@]}" "${flags[@]}" ${extra[@]+"${extra[@]}"}
		local -a files=(); local line
		while IFS= read -r line; do
			local f="${line%%	*}" fl=""; [ "$f" != "$line" ] && fl="${line#*	}"
			[ "$fl" = "$group" ] && files+=("$f")
		done < "$listfile"
		compile "$B/obj/$name" "$B/$name.$n.rsp" "${files[@]}"
	done < <(awk -F'\t' '{ print $2 }' "$listfile" | sort -u)
	xcrun libtool -static -o "$B/lib$name.a" "$B/obj/$name"/*.o 2>/dev/null
}

# variants.xcconfig: VARIANT_<name>_INCLUDE for arm64 macOS.
CANCELABLE="forceLibcToBuild.c creat.c sigcompat.c lockf.c nanosleep.c pause.c sleep.c termios.c usleep.c wait.c waitpid.c recv.c send.c system.c"
DARWINEXTSN="forceLibcToBuild.c popen.c fdopen.c fopen.c realpath.c getgroups.c"
DYLD="forceLibcToBuild.c arc4random.c closedir.c dirfd.c getcwd.c getpagesize.c nanosleep.c opendir.c readdir.c scandir.c sysctl.c sysctlbyname.c telldir.c usleep.c atexit.c exit.c gettimeofday.c heapsort.c merge.c qsort.c reallocf.c realpath.c bcopy.c strcat.c strdup.c strrchr.c _libc_init.c subsystem.c"
mkdir -p "$B/lists"
printf 'darwin/forceLibcToBuild.c\n' > "$B/lists/Platform"   # Platform_INCLUDED_SOURCE_FILE_NAMES: no arm64/gen sources
select_ Base drop "$BASE_EXCLUDED" > "$B/lists/Base"
select_ FreeBSD drop "$BASE_EXCLUDED" > "$B/lists/FreeBSD"
select_ NetBSD all "" > "$B/lists/NetBSD"
select_ TRE all "" > "$B/lists/TRE"
select_ Variant_Cancelable pick "$CANCELABLE" > "$B/lists/vCancelable"
select_ Variant_DarwinExtsn pick "$DARWINEXTSN" > "$B/lists/vDarwinExtsn"
select_ FortifySource drop "$BASE_EXCLUDED" > "$B/lists/FortifySource"
select_ libc_dyld pick "$DYLD" > "$B/lists/dyld"
select_ libsystem_c.dylib all "" > "$B/lists/dylib"

build Platform "${plain_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/Platform"
build Base "${plain_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/Base"
build FreeBSD -include "$L/fbsdcompat/_fbsd_compat_.h" "${fbsd_paths[@]}" "${plain_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/FreeBSD"
build NetBSD -include "$L/nbsdcompat/_nbsd_compat_.h" -I"$L/nbsdcompat" "${plain_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/NetBSD"
build TRE -DHAVE_CONFIG_H -I"$L/regex/TRE" -I"$L/regex/FreeBSD" "${plain_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/TRE"
build vCancelable -DBUILDING_VARIANT -DVARIANT_CANCELABLE "${fbsd_paths[@]}" "${variant_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/vCancelable"
build vDarwinExtsn -DBUILDING_VARIANT -DVARIANT_DARWINEXTSN "${fbsd_paths[@]}" "${variant_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/vDarwinExtsn"
build FortifySource "${plain_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/FortifySource"
build dyld -UBUILDING_VARIANT -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE \
	-D__DARWIN_NON_CANCELABLE=0 -fno-stack-check "${fbsd_paths[@]}" "${variant_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/dyld"
build dylib "${plain_system[@]}" "${srcroot_paths[@]}" -- "$B/lists/dylib"

# build_linklists.sh: interposable text symbols and $VARIANT symbols to unexport.
archives=(); for a in Platform Base FreeBSD NetBSD TRE vCancelable vDarwinExtsn FortifySource; do
	[ -f "$B/lib$a.a" ] && archives+=("$B/lib$a.a"); done
: > "$B/interposable.list"; : > "$B/unexport.list"
for a in "${archives[@]}"; do
	nm -AUamgf "$a" 2>/dev/null | grep '__TEXT,__text' | grep -vE '\$VARIANT' | awk '{ print $NF }' >> "$B/interposable.list" || true
	nm -AUamgf "$a" 2>/dev/null | awk '/\$VARIANT/ { print $NF }' >> "$B/unexport.list" || true
done

mkdir -p "$OUT/usr/lib/system" "$OUT/usr/local/lib/dyld"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_c.dylib \
	-current_version 1725.0.11 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/dylib/*.o \
	-Wl,-all_load "${archives[@]}" $(dep_libdirs "${DEPS[@]}") -lsystem_kernel -lsystem_malloc -lsystem_platform \
	-lsystem_pthread -L"$SDK/usr/lib/system" -lcompiler_rt -ldyld -lsystem_m \
	-Wl,-upward-ldispatch -Wl,-upward-lmacho -Wl,-upward-lsystem_asl -Wl,-upward-lsystem_blocks -Wl,-upward-lsystem_info \
	-Wl,-upward-lsystem_notify -Wl,-upward-lxpc -Wl,-upward-lcorecrypto -Wl,-upward-lsystem_trace \
	-Wl,-interposable_list,"$B/interposable.list" -Wl,-unexported_symbols_list,"$B/unexport.list" \
	-Wl,-alias_list,"$L/xcodescripts/alias.list" -o "$OUT/usr/lib/system/libsystem_c.dylib"
cp "$B/libdyld.a" "$OUT/usr/local/lib/dyld/libc.a"
