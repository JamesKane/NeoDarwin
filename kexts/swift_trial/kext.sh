#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The kext_swift trial (P0-10, docs/architecture/language-policy.md §3):
# NDSwiftTrial.kext, Embedded Swift logic behind a C++ IOService.
#   kext.sh OUT SRC KERNEL_HEADERS XNU_SRC EMBEDDED_TOOLCHAIN
#
# Swift: the swift.org toolchain's Embedded Swift for arm64-apple-macos26.0
# (Xcode has no Embedded stdlib), -no-allocations, -target-cpu cortex-a76
# (the kernel's Armv8.2 baseline). The flags stand in for clang's -mkernel,
# which Swift has no spelling for: -mkernel on arm64 means no builtins, no
# red zone (AArch64 LLVM never uses one), no unwind tables and LR reserved
# for the register allocator; Swift's object needs only memset-style calls
# the kernel exports (bzero) and its stack protector (___stack_chk_guard,
# ___stack_chk_fail, Libkern exports). The Embedded stdlib also defines
# weak runtime stubs (swift_retain, swift_once and friends) in every object;
# a kext exports every global by default, so they are unexported here
# (with Swift's own mangled symbols) and -dead_strip drops what nothing
# calls. Otherwise the bundle carries weak definitions (MH_WEAK_DEFINES,
# __weak_got) and two Swift kexts would export the same names.
#
# C++: the IOService shell compiles as zfs.kext's C++ does (-mkernel
# -fapple-kext); libkmod's _start/_stop come from xnu; ld -kext links.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; KH="$(abspath "$3")"; X="$(abspath "$4")"; TC="$5"
# The linker: the from-source ld64 when rules/kext.bzl passes it in
# ND_KEXT_LD (--//rules:kernel_linker=ld64), otherwise the host Xcode's.
KLD=(xcrun ld); [ -z "${ND_KEXT_LD:-}" ] || KLD=("$(abspath "$ND_KEXT_LD")")
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
KF="$KH/System/Library/Frameworks/Kernel.framework/Versions/A"
[ -d "$KF/Headers" ] || { echo "kext.sh: no Kernel.framework under $KH" >&2; exit 1; }
mkdir -p "$B/compat"
cp "$S/../../kernel/neodarwin/pthread/compat/TargetConditionals.h" "$B/compat/"
cp "$SDK"/usr/include/Availability*.h "$B/compat/"
RES="$(xcrun clang -print-resource-dir)"

# The clang importer reads Kernel.framework's headers as the C++ does
# (-mkernel -DKERNEL, no host includes), for KernelKPI/'s module.
XCC=(); for f in -nostdinc -mkernel -DKERNEL -DKERNEL_PRIVATE -isystem "$KF/Headers" -isystem "$KF/PrivateHeaders" \
	-isystem "$B/compat" -isystem "$RES/include"; do XCC+=(-Xcc "$f"); done
"$TC/usr/bin/swiftc" -target arm64-apple-macos26.0 -target-cpu cortex-a76 \
	-enable-experimental-feature Embedded -wmo -parse-as-library -Osize -no-allocations \
	-swift-version 6 -warnings-as-errors -module-name NDSwiftTrial -module-cache-path "$B/mc" \
	-I "$S/KernelKPI" "${XCC[@]}" -c "$S/Trial.swift" -o "$B/trial.o"

KFLAGS=(-arch arm64 -mmacosx-version-min=26.0 -mcpu=cortex-a76 -O2 -nostdinc -mkernel -fno-common
	-DKERNEL -DKERNEL_PRIVATE -isystem "$KF/Headers" -isystem "$KF/PrivateHeaders" -isystem "$B/compat" -isystem "$RES/include")
(cd "$S" && xcrun clang++ "${KFLAGS[@]}" -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions -Wall -Wextra -Werror \
	-c glue.cpp -o "$B/glue.o")
xcrun clang "${KFLAGS[@]}" -c "$X/libkern/kmod/c_start.c" -o "$B/c_start.o"
xcrun clang "${KFLAGS[@]}" -c "$X/libkern/kmod/c_stop.c" -o "$B/c_stop.o"

mkdir -p "$OUT/Contents/MacOS"
"${KLD[@]}" -arch arm64 -kext -dead_strip -unexported_symbol '_swift_*' -unexported_symbol '__swift_*' \
	-unexported_symbol '_$e*' -platform_version macos 26.0 26.0 -o "$OUT/Contents/MacOS/NDSwiftTrial" \
	"$B/glue.o" "$B/trial.o" "$B/c_start.o" "$B/c_stop.o" "$RES/lib/darwin/libclang_rt.cc_kext.a"
cp "$S/Info.plist" "$OUT/Contents/Info.plist"
