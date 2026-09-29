#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_dnssd from mDNSResponder-2881.0.25 (docs/base/libsystem.md): the
# DNS-SD client library only, not the daemon.
#   build.sh OUT MDNSRESPONDER_SRC SYSROOT DEPROOT...
#   (DEPROOT: kernel, platform, pthread, malloc, libc, blocks, libdispatch, asl, llvm_runtimes)
# OUT receives usr/lib/system/libsystem_dnssd.dylib.
#
# The release publishes no Xcode project: mDNSMacOSX/ (the project, and the
# Apple client sources: dnssd_clientstub_apple.c, DNSServiceDiscovery.c,
# mdns_tlv.c, bundle_utilities.m) was last published in 1310.140.1. The
# settings below are that project's libsystem_dnssd target and project-level
# Release settings; the sources are the client library the published tree
# builds (sources.txt). The published tree is Apple's own non-Apple
# configuration: mdns_strict.h fixes APPLE_OSX_mDNSResponder to 0, and
# MDNS_NO_STRICT=1 leaves out the unpublished secure_coding/strict.h, as the
# mDNSPosix build does. The Apple-only entry points (the *Ex calls and
# DNSServiceAttr* setters over XPC and Network.framework, delegate
# connections, resolver defaults, validation data) are left out with them;
# no closed library is linked.
#
# Patches: 0001 declares the private SPI the published sources define, so it
# is exported; 0002 adds kDNSServiceAttrAllowFailover, which Libinfo uses.
# The headers the project installs (dns_sd.h, dns_sd_private.h) are staged
# into the sysroot by install_headers.sh; this build takes them from the
# patched tree.
#
# libdyld isn't built yet; until it is, the dylib links it through the host
# SDK's .tbd stub, which carries Apple's install name, and is relinked when
# NeoDarwin's exists.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; M="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
M="$(stage_src "$M" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$M"

srcs=($(grep -v '^#' "$PROJ/sources.txt"))

# Project-level Release settings (GCC_PREPROCESSOR_DEFINITIONS, OTHER_CFLAGS
# less -flto=full, WARNING_CFLAGS; APPLE_OSX_mDNSResponder is fixed by the
# published mdns_strict.h) and the target's (GCC_SYMBOLS_PRIVATE_EXTERN,
# __DARWIN_NON_CANCELABLE). Exports are marked DNSSD_EXPORT. The patched
# tree's headers come first: dns_sd_private.h includes <dns_sd.h>.
flags=("${TARGET_FLAGS[@]}" -Os -fwrapv -fno-common -fvisibility=hidden
	-D__APPLE_USE_RFC_3542=1 -D__MigTypeCheck=1 -DmDNSResponderVersion=2881.0.25 -D_LEGACY_NAT_TRAVERSAL_
	-D_BUILDING_XCODE_PROJECT_=1 -DUSE_LIBIDN=1 -DUSE_SYSTEMCONFIGURATION_PRIVATE_HEADERS
	-D__DARWIN_NON_CANCELABLE=1 -DMDNS_NO_STRICT=1
	-W -Wall -Wmissing-prototypes -Wno-four-char-constants -Wno-unknown-pragmas -Wshadow -Wno-format -Wformat-security
	-I"$M/mDNSShared" -I"$M/mDNSCore" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"
compile "$B/obj" "$B/cflags" "${srcs[@]}"

# OTHER_LDFLAGS, less libxpc, libsystem_featureflags and the upward libobjc,
# which only the unpublished Apple sources use.
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_dnssd.dylib \
	-current_version 2881.0.25 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lcompiler_rt -lsystem_kernel -lsystem_platform -lsystem_pthread -lsystem_malloc \
	-lsystem_c -lsystem_blocks -ldispatch -lsystem_asl -L"$SDK/usr/lib/system" -ldyld \
	-o "$OUT/usr/lib/system/libsystem_dnssd.dylib"
