#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build a dynamically linked arm64 Darwin executable from Embedded Swift
# (language policy T3) against a NeoDarwin root.
#   build.sh TOOLCHAIN OUT MODULE INCLUDE_DIRS(colon-separated) ROOT -- SWIFT_SRCS... -- C_SRCS...
# Swift and C compile for arm64-apple-macos against the host SDK's headers,
# the Darwin userland ABI NeoDarwin keeps. Xcode's ld links them against
# ROOT's libSystem (-syslibroot ROOT: usr/lib/libSystem.dylib and the
# libraries it re-exports, at their install paths), so the executable loads
# NeoDarwin's libraries through dyld (LC_LOAD_DYLINKER /usr/lib/dyld). The
# Embedded stdlib's runtime calls (putchar, posix_memalign, free) bind to
# libSystem like any other import; String's Unicode operations (comparison,
# normalisation) take their tables from the Embedded stdlib's
# libswiftUnicodeDataTables.a, linked only as far as they're used. ld signs
# the result ad hoc.
set -euo pipefail
tc="$1"; out="$2"; module="$3"; incs="$4"; root="$5"; shift 5
[ "$1" = "--" ] && shift
swift=(); while [ $# -gt 0 ] && [ "$1" != "--" ]; do swift+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift
csrcs=("$@")
target=arm64-apple-macos26.0
# The CPU baseline, as for the kernel (docs/kernel/arm64-sbsa-bringup.md §2.1.8):
# Armv8.2 with RCpc, dot product, crypto and FP16, which the Radxa Dragon
# Q8B's Cortex-X1C/A78C and QEMU's cortex-a76 implement. Without it swiftc
# and clang target apple-m1 for arm64-apple-macos and emit LDAPUR (Armv8.4).
cpu=cortex-a76
sdk="$(xcrun --sdk macosx --show-sdk-path)"
bin="$tc/usr/bin"; work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
iflags=(); IFS=: read -ra dirs <<< "$incs"; for d in "${dirs[@]}"; do [ -n "$d" ] && iflags+=(-I "$d"); done
"$bin/swiftc" -target "$target" -target-cpu "$cpu" -sdk "$sdk" -enable-experimental-feature Embedded -wmo -parse-as-library \
	-Osize -swift-version 6 -warnings-as-errors "${iflags[@]}" -module-name "$module" \
	-c "${swift[@]}" -o "$work/swift.o"
objs=("$work/swift.o"); i=0
for c in ${csrcs[@]+"${csrcs[@]}"}; do
	"$bin/clang" --target="$target" -mcpu="$cpu" -isysroot "$sdk" -std=c23 -Os -Wall -Wextra -Werror "${iflags[@]}" \
		-c "$c" -o "$work/c$i.o"
	objs+=("$work/c$i.o"); i=$((i + 1))
done
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dynamic -dead_strip -adhoc_codesign \
	-syslibroot "$root" -lSystem -e _main -o "$out" "${objs[@]}" \
	"$tc/usr/lib/swift/embedded/arm64-apple-macos/libswiftUnicodeDataTables.a"
