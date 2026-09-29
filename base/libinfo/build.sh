#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_info from Libinfo-600 (docs/base/libsystem.md): replays
# Libinfo.xcodeproj's Libinfo target with xcodescripts/Libinfo.xcconfig. The
# source list is sources.txt, taken from the project (no file has per-file
# COMPILER_FLAGS). The headers the project installs are already in the
# sysroot (its install_files.sh runs in the sysroot stage).
#   build.sh OUT LIBINFO_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, platform, pthread, malloc, libc, blocks, llvm_runtimes,
#   libdispatch, libsystem_dnssd, libsystem_notify, libsystem_darwin, standin_libs)
# OUT receives usr/lib/system/libsystem_info.dylib.
#
# Configuration. The xcconfig's macOS defines are CONFIG_MAC, SYNTH_ROOTFS,
# DS_AVAILABLE and DARWIN_DIRECTORY_AVAILABLE. NeoDarwin keeps the first two
# and leaves out the last two, which only switch on clients of closed
# components:
#  - DS_AVAILABLE compiles ds_module.c (lookups through opendirectoryd over an
#    XPC pipe, using the unpublished <opendirectory/odipc.h>), membership.c's
#    opendirectoryd calls, and the exports _ds_running,
#    _si_disable_opendirectory and lookup_close_connections. Without it
#    membership.c takes the path Apple builds for iOS: compatibility UUIDs
#    derived from uids and gids, names through getpwuid_r and getgrgid_r.
#  - DARWIN_DIRECTORY_AVAILABLE compiles darwin_directory.c and membership.c's
#    calls into the closed libsystem_darwindirectory, and drops its link.
# The search module then answers from the cache, file (/etc) and mdns
# modules. MUSER_AVAILABLE is for Apple's other platforms and stays off, as on
# macOS.
#
# Headers: the sysroot's, which include libdispatch's private headers and
# libsystem_darwin's (Libc's os/variant_private.h) and base/sdk's xpc/private.h;
# and include/: a stand-in, for this library only, for a header of a project
# NeoDarwin doesn't pin yet (configd's dnsinfo.h).
#
# libxpc and libsystem_trace are NeoDarwin's stand-ins. libdyld isn't built
# yet; until it is, the dylib links it through the host SDK's .tbd stub, which
# carries Apple's install name, and is relinked when NeoDarwin's exists.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; I="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$I"

srcs=($(grep -v '^#' "$PROJ/sources.txt"))

# Libinfo.xcconfig: GCC_PREPROCESSOR_DEFINITIONS (Release, macOS, less the two
# defines above), GCC_SYMBOLS_PRIVATE_EXTERN (exports are marked
# LIBINFO_EXPORT), GCC_NO_COMMON_BLOCKS, gnu99, -Os, HEADER_SEARCH_PATHS. The
# project's headers are found by quoted include from any subproject, as
# Xcode's header map gives them.
flags=("${TARGET_FLAGS[@]}" -Os -std=gnu99 -fno-common -fvisibility=hidden
	-DCONFIG_MAC -DSYNTH_ROOTFS -DDEBUG=0 -D__DARWIN_NON_CANCELABLE=1 -D__MigTypeCheck=1 -DINET6=1
	-iquote "$I/Libinfo" -iquote "$I/lookup.subproj" -iquote "$I/membership.subproj" -iquote "$I/gen.subproj"
	-iquote "$I/dns.subproj" -iquote "$I/nis.subproj" -iquote "$I/rpc.subproj" -iquote "$I/util.subproj"
	-isystem "$PROJ/include" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"
compile "$B/obj" "$B/cflags" "${srcs[@]}"

# OTHER_LDFLAGS, without libsystem_darwindirectory, and without libsystem_asl
# and libsystem_featureflags, which only the two left-out defines' code uses.
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_info.dylib \
	-current_version 600 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lsystem_kernel -lsystem_malloc -lsystem_platform -lsystem_pthread -lsystem_c \
	-lsystem_blocks -lcompiler_rt -ldispatch -lsystem_dnssd -lsystem_notify -lxpc -lsystem_trace -lsystem_darwin \
	-L"$SDK/usr/lib/system" -ldyld \
	-o "$OUT/usr/lib/system/libsystem_info.dylib"
