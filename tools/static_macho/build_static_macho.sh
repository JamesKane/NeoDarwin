#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build a static arm64 Darwin executable from Embedded Swift (language policy T3).
#   build_static_macho.sh TOOLCHAIN OUT ENTRY MODULE INCLUDE_DIRS(colon-separated) -- SWIFT_SRCS... -- C_SRCS...
# Swift and C compile for arm64-apple-macos, the Darwin userland ABI NeoDarwin
# keeps. Xcode's ld links them -static: an MH_EXECUTE with an LC_UNIXTHREAD
# entry and no LC_LOAD_DYLINKER, so the kernel starts it without dyld or
# libSystem. The toolchain's ld64.lld cannot: it implements neither -static
# nor LC_UNIXTHREAD. ld signs the result ad hoc, which the arm64 kernel
# requires of every executable page.
set -euo pipefail
tc="$1"; out="$2"; entry="$3"; module="$4"; incs="$5"; shift 5
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
bin="$tc/usr/bin"; work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
iflags=(); IFS=: read -ra dirs <<< "$incs"; for d in "${dirs[@]}"; do [ -n "$d" ] && iflags+=(-I "$d"); done
"$bin/swiftc" -target "$target" -target-cpu "$cpu" -enable-experimental-feature Embedded -wmo -parse-as-library \
	-Osize -no-allocations -Xfrontend -disable-stack-protector -swift-version 6 -warnings-as-errors \
	"${iflags[@]}" -module-name "$module" -c "${swift[@]}" -o "$work/swift.o"
objs=("$work/swift.o"); i=0
for c in ${csrcs[@]+"${csrcs[@]}"}; do
	"$bin/clang" --target="$target" -mcpu="$cpu" -std=c23 -Os -ffreestanding -fno-builtin -fno-stack-protector \
		-Wall -Wextra -Werror "${iflags[@]}" -c "$c" -o "$work/c$i.o"
	objs+=("$work/c$i.o"); i=$((i + 1))
done
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -static -dead_strip -adhoc_codesign \
	-e "_$entry" -o "$out" "${objs[@]}"
