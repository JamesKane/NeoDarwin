#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# IOKit.framework from IOKitUser-100222.0.4 (P4-21 checkpoint 6b,
# docs/base/corefoundation.md): IOKitLib's core only.
#   build.sh OUT IOKITUSER_SRC SYSROOT DEPROOT...
#   (DEPROOTs: //base:root, //base:corefoundation_framework)
# OUT receives System/Library/Frameworks/IOKit.framework/Versions/A/IOKit,
# macOS's install name, current version 275 (the SDK's), and, build-only,
# the framework's headers in usr/local/frameworks/IOKit.framework/Headers:
# xnu's IOKit headers, public then private (the sysroot's partial
# IOKit.framework), with IOKitUser's own on top.
# Sources: IOKitLib.c (the registry, iterator, notification, connect and
# matching calls), IOCFSerialize.c and IOCFUnserialize.tab.c (the property
# list forms the kernel speaks), and iokitmig.c, which includes the MIG
# user stubs of xnu's device.defs, generated here as DeviceMIG.sh does.
# IOTrap.s's iokit_user_client_trap is src/nd_iotrap.c. Left out: the
# families (HID, power management, graphics, network, USB, kext, ...),
# IOCFPlugIn and IOBundle (CFPlugIn loading), IOCFURLAccess, and the data
# queues; patch 0001 leaves out IOServiceAuthorizeAgent.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; U="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
CFT=""; for d in "${DEPS[@]}"; do [ -d "$d/usr/local/frameworks/CoreFoundation.framework" ] && CFT="$d"; done
[ -n "$CFT" ] || { echo "iokit/build.sh: no DEPROOT holds CoreFoundation.framework" >&2; exit 1; }
SELF="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$U" "$B/src" "$SELF/patches")"
cd "$S"

H="$OUT/usr/local/frameworks/IOKit.framework/Headers"
mkdir -p "$H"
xh="$SYSROOT/System/Library/Frameworks/IOKit.framework/Versions/A"
(cd "$xh/Headers" && tar chf - .) | (cd "$H" && tar xf -); chmod -R u+w "$H"
(cd "$xh/PrivateHeaders" && tar chf - .) | (cd "$H" && tar xf -); chmod -R u+w "$H"
cp -f ./*.h "$H/"; chmod -R a+r,u+w "$H"

# DeviceMIG.sh: the user side of device.defs, for the kernel object calls.
xcrun mig -arch arm64 -novouchers -DKOBJECT_SERVER -I"$SYSROOT/usr/include" -server /dev/null \
	-header iokitmig64.h -user iokitmig64.c "$SYSROOT/usr/include/device/device.defs"

# xnu's private headers first, as in Apple's build against its internal
# SDK: the private <device/device_types.h> sets IOKIT_SERVER_VERSION, which
# selects the binary property calls (the kernel no longer answers the XML
# ones), and MIG's stubs call mach_msg2 with MACH64_SEND_KOBJECT_CALL, from
# the private <mach/message.h>.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu11 -I"$S" -iframework "$OUT/usr/local/frameworks" \
	-iframework "$CFT/usr/local/frameworks" \
	-isystem "$SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders" \
	$(cmd_sysroot_flags "$SYSROOT") -DND_NO_AUTHORIZE_AGENT=1 -ffile-prefix-map="$S/"=IOKitUser/
cp "$SELF/src/nd_iotrap.c" .
compile "$B/obj" "$B/cflags" IOKitLib.c IOCFSerialize.c IOCFUnserialize.tab.c nd_iotrap.c iokitmig.c

fw=System/Library/Frameworks/IOKit.framework
mkdir -p "$OUT/$fw/Versions/A"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -adhoc_codesign \
	-install_name "/$fw/Versions/A/IOKit" -current_version 275 -compatibility_version 1 \
	-syslibroot "$ROOT" "$B"/obj/*.o "$CFT/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	-lSystem -o "$OUT/$fw/Versions/A/IOKit"
