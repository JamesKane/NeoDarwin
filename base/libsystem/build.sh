#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libSystem.B from Libsystem-1356 (docs/base/libsystem.md): replays
# Libsystem.xcodeproj's System target (init.c, CompatibilityHacks.c) with
# Libsystem.xcconfig, its Generate Linker Arguments target
# (xcodescripts/linker_arguments.sh) and its Generate Symlinks target
# (xcodescripts/create_dylib_symlinks.sh). Only the normal variant is built:
# debug and asan are variants for Apple's internal tooling.
#   build.sh OUT LIBSYSTEM_SRC SYSROOT DEPROOT...
#   (DEPROOT: every library libSystem reexports: kernel, platform, pthread,
#   malloc, libc, blocks, libdispatch, libmacho, asl, notify, info, libsystem_m,
#   darwin, collections, copyfile, removefile, dnssd, llvm_runtimes, standin_libs)
# OUT receives usr/lib/libSystem.B.dylib, the usr/lib/libSystem.dylib link and
# the BSD compatibility links (libc.dylib, libm.dylib, ... -> libSystem.dylib).
#
# linker_arguments.sh decides what libSystem is. For each line of requiredlibs
# and optionallibs it takes the first library present in the SDK's
# usr/lib/system and reexports it (-reexport-l), and writes config.h with
# HAVE_<LIBRARY> for each, which init.c tests. Here the libraries present are
# the ones in the DEPROOTs' usr/lib/system, so libSystem reexports what
# NeoDarwin builds (stand-ins included) and init.c's calls into closed
# libraries NeoDarwin doesn't provide (libsystem_secinit, _containermanager,
# _coreservices) compile out through Apple's own HAVE_ switches. Apple stops
# when a required library is missing; NeoDarwin lists the missing ones and
# goes on, since they are closed libraries nothing calls (docs/base/libsystem.md).
#
# libdyld isn't built yet (checkpoint 3). Until a DEPROOT provides it, it
# links through a stub made from the host SDK's .tbd, which carries Apple's
# install name: the public stub lacks the private entry points init.c calls
# (_dyld_initializer and the fork hooks), and ld refuses -U in a library
# eligible for the shared cache, so the stub adds them.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$(cd "$(dirname "$0")" && pwd)/patches")"   # patches/ applied (none yet)
cd "$S"

# --- Generate Linker Arguments (linker_arguments.sh), for arm64, normal variant.
present() {  # present LIB: whether lib$LIB.dylib is in a DEPROOT's usr/lib/system
	local d; for d in "${DEPS[@]}"; do [ -e "$d/usr/lib/system/lib$1.dylib" ] && return 0; done; return 1
}
sdk_dyld=0
if ! present dyld; then sdk_dyld=1; fi
reexports=(); missing=()
while read -r line; do
	[ -n "$line" ] || continue
	found=""
	for lib in $line; do   # a line lists alternatives (system_sim_kernel system_kernel)
		if present "$lib" || { [ "$lib" = dyld ] && [ "$sdk_dyld" = 1 ]; }; then found="$lib"; break; fi
	done
	if [ -n "$found" ]; then reexports+=("$found"); else missing+=("$line"); fi
done < <(cat requiredlibs optionallibs | LC_ALL=C sort)
echo "libSystem: not reexported (not built by NeoDarwin): ${missing[*]}" >&2
D="$B/derived"; mkdir -p "$D"
for lib in "${reexports[@]}"; do
	echo "#define HAVE_$(printf '%s' "${lib/_sim/}" | tr 'a-z' 'A-Z') 1"
done > "$D/config.arm64.normal.h"
largs=(); for lib in "${reexports[@]}"; do largs+=("-Wl,-reexport-l$lib"); done

# --- The System target: Libsystem.xcconfig's OTHER_CFLAGS (the config header,
# CURRENT_VARIANT_normal; SUPPORT_ASAN only in the asan variants), GCC_NO_COMMON_BLOCKS
# and GCC_WARN_* (GCC_TREAT_WARNINGS_AS_ERRORS' -Werror is left out: diagnostics
# change no interface).
flags=("${TARGET_FLAGS[@]}" -Os -std=gnu11 -fno-common -Wmissing-prototypes -Wshorten-64-to-32 -Wreturn-type
	-Wunused-variable -include "$D/config.arm64.normal.h" -DCURRENT_VARIANT_normal=1 $(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"
compile "$B/obj" "$B/cflags" init.c CompatibilityHacks.c

# Libsystem.xcconfig: OTHER_LDFLAGS (-search_paths_first, -nodefaultlibs, the
# linker arguments), DYLIB_CURRENT_VERSION (the project version under XBS),
# INSTALL_PATH and PRODUCT_NAME System.B. The host ld warns about static
# initializers in shared-cache libraries; libSystem_initializer is libSystem's
# purpose.
dyldstub=()
if [ "$sdk_dyld" = 1 ]; then
	mkdir -p "$B/dyld"
	{
		printf '%s\n' '--- !tapi-tbd' 'tbd-version: 4' 'targets: [ arm64-macos ]' \
			"install-name: '/usr/lib/system/libdyld.dylib'" 'current-version: 1323.3' \
			'parent-umbrella:' '  - targets: [ arm64-macos ]' '    umbrella: System' \
			'exports:' '  - targets: [ arm64-macos ]' '    symbols: ['
		{ xcrun nm -gUj --arch=arm64e "$SDK/usr/lib/system/libdyld.tbd" | grep -v '^/\|^$'
		  printf '%s\n' _dyld_initializer _dyld_atfork_prepare _dyld_atfork_parent _dyld_fork_child \
			_dyld_dlopen_atfork_prepare _dyld_dlopen_atfork_parent _dyld_dlopen_atfork_child
		} | LC_ALL=C sort -u | sed "s/.*/      '&',/; \$s/,\$//"
		printf '%s\n' '    ]' '...'
	} > "$B/dyld/libdyld.tbd"
	dyldstub=(-L"$B/dyld")
fi
mkdir -p "$OUT/usr/lib"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/libSystem.B.dylib \
	-current_version 1356 -compatibility_version 1 -Wl,-search_paths_first -Wl,-no_warn_inits "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") ${dyldstub[@]+"${dyldstub[@]}"} "${largs[@]}" \
	-o "$OUT/usr/lib/libSystem.B.dylib"

# --- Generate Symlinks (create_dylib_symlinks.sh, macosx): libSystem.dylib and
# the BSD libraries, which are all libSystem.
ln -sf libSystem.B.dylib "$OUT/usr/lib/libSystem.dylib"
for lib in c info m pthread dbm poll dl rpcsvc proc gcc_s.1; do ln -sf libSystem.dylib "$OUT/usr/lib/lib$lib.dylib"; done
