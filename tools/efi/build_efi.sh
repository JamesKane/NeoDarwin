#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build a UEFI application from Embedded Swift (language policy T3).
#   build_efi.sh TOOLCHAIN OUT.efi ENTRY INCLUDE_DIRS(colon-separated) -- SWIFT_SRCS... -- C_SRCS...
# Swift compiles for aarch64-none-none-elf (the triple the Embedded stdlib
# ships for) to LLVM bitcode; clang lowers it to an aarch64 Windows COFF
# object; lld-link produces the PE32+ EFI application. Sound on AArch64 because
# UEFI there uses the standard AAPCS64 calling convention.
set -euo pipefail
tc="$1"; out="$2"; entry="$3"; incs="$4"; shift 4
[ "$1" = "--" ] && shift
swift=(); while [ $# -gt 0 ] && [ "$1" != "--" ]; do swift+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift
csrcs=("$@")
bin="$tc/usr/bin"; work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
iflags=(); IFS=: read -ra dirs <<< "$incs"; for d in "${dirs[@]}"; do [ -n "$d" ] && iflags+=(-I "$d"); done
"$bin/swiftc" -target aarch64-none-none-elf -enable-experimental-feature Embedded -wmo -parse-as-library \
	-Osize -no-allocations -Xfrontend -disable-stack-protector -swift-version 6 -warnings-as-errors \
	"${iflags[@]}" -module-name neoboot -emit-bc "${swift[@]}" -o "$work/swift.bc"
"$bin/clang" --target=aarch64-unknown-windows-msvc -Os -Wno-override-module -c "$work/swift.bc" -o "$work/swift.obj"
objs=("$work/swift.obj"); i=0
for c in ${csrcs[@]+"${csrcs[@]}"}; do
	"$bin/clang" --target=aarch64-unknown-windows-msvc -std=c23 -Os -ffreestanding -fno-builtin -fno-stack-protector \
		-Wall -Wextra -Werror "${iflags[@]}" -c "$c" -o "$work/c$i.obj"
	objs+=("$work/c$i.obj"); i=$((i + 1))
done
"$bin/lld-link" /subsystem:efi_application /entry:"$entry" /nodefaultlib /machine:arm64 /out:"$out" "${objs[@]}"
