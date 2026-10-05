#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# CoreFoundation.framework from swift-corelibs-foundation's CoreFoundation
# (P4-21 checkpoint 6b, docs/base/corefoundation.md): the pure C CF, with no
# Swift runtime and no ICU.
#   build.sh OUT FOUNDATION_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives System/Library/Frameworks/CoreFoundation.framework/Versions/A/
# CoreFoundation, with macOS's install name and compatibility version (150,
# which programs linked against the SDK's CoreFoundation.tbd require), and,
# build-only, the headers in usr/local/frameworks/CoreFoundation.framework/
# Headers (stage_root.sh adds the framework's Versions/Current links).
# The flags are the project's _Foundation_common_build_flags (CMakeLists.txt)
# without the Swift ones (DEPLOYMENT_RUNTIME_SWIFT, -fcf-runtime-abi=swift),
# so CFRuntimeBase has Apple's layout, which clang's constant CFStrings use.
# CF_NO_ICU (patch 0002) leaves ICU out; CF_NO_SYSDIR (0003) leaves out
# libsystem_coreservices' sysdir. The sources the CMake list builds, less
# the ones built on ICU (OUT_OF_BUILD) and the Windows ones; plus
# NeoDarwin's CFLocale (an identifier) and CFMachPort (src/).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
SELF="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$F/Sources/CoreFoundation" "$B/src" /dev/null)"
for p in "$SELF"/patches/*.patch; do patch -d "$B/src" -p3 --quiet < "$p"; done
cd "$S"

# Built on ICU (locales, calendars, formatters, regular expressions,
# transforms) or for Windows only.
OUT_OF_BUILD=" CFCalendar.c CFCalendar_Enumerate.c CFDateComponents.c CFDateFormatter.c CFDateIntervalFormatter.c 
 CFICUConverters.c CFListFormatter.c CFLocale.c CFLocaleIdentifier.c CFNumberFormatter.c CFRegularExpression.c 
 CFRelativeDateTimeFormatter.c CFStringTransform.c CFWindowsUtilities.c CFTimeZone_WindowsMapping.c "
srcs=()
for f in $(sed -n '/^add_library(CoreFoundation/,/)/p' CMakeLists.txt | grep -oE '[A-Za-z_]+\.c'); do
	case "$OUT_OF_BUILD" in *" $f "*) ;; *) srcs+=("$f") ;; esac
done
cp "$SELF"/src/*.c .
srcs+=(nd_cflocale.c nd_cfmachport.c)

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu11 $(cmd_sysroot_flags "$SYSROOT") \
	-I"$S/include" -I"$S/internalInclude" -include "$S/internalInclude/CoreFoundation_Prefix.h" \
	-DDEPLOYMENT_RUNTIME_SWIFT=0 -DCF_BUILDING_CF -DHAVE_STRUCT_TIMESPEC -DCF_NO_ICU=1 -DCF_NO_SYSDIR=1 \
	-fconstant-cfstrings -fdollars-in-identifiers -fno-common -fblocks -fexceptions \
	-Wno-shorten-64-to-32 -Wno-deprecated-declarations -Wno-unreachable-code -Wno-conditional-uninitialized \
	-Wno-unused-variable -Wno-unused-function -Wno-int-conversion -Wno-switch -ffile-prefix-map="$S/"=CoreFoundation/
compile "$B/obj" "$B/cflags" "${srcs[@]}"

# The public kCF* names that alias CF's internal ones (SymbolAliases, the
# list the Darwin build links with -alias_list), for the names CF defines.
nm -gUj "$B"/obj/*.o | sort -u > "$B/defined"
grep -v '^#' SymbolAliases | awk 'NF == 2' | while read -r name alias; do
	grep -qxF "$name" "$B/defined" && printf '%s %s\n' "$name" "$alias"
done > "$B/aliases"

fw=System/Library/Frameworks/CoreFoundation.framework
mkdir -p "$OUT/$fw/Versions/A" "$OUT/usr/local/frameworks/CoreFoundation.framework/Headers"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -adhoc_codesign \
	-install_name "/$fw/Versions/A/CoreFoundation" -current_version 150 -compatibility_version 150 \
	-alias_list "$B/aliases" -syslibroot "$ROOT" "$B"/obj/*.o -lSystem -o "$OUT/$fw/Versions/A/CoreFoundation"
install -m 0444 include/*.h "$OUT/usr/local/frameworks/CoreFoundation.framework/Headers/"
