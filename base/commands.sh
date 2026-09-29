# SPDX-License-Identifier: BSD-2-Clause
# Shared by the command projects' build scripts (base/*_cmds/build.sh,
# docs/base/libsystem.md). Sourced after tools/base/common.sh.
#
# Commands link against the runtime root (//base:root, a DEPROOT): Xcode's ld
# with -syslibroot ROOT finds usr/lib/libSystem.dylib and resolves every
# library it reexports at its install path in the root, never in the host
# SDK. The root is found among the DEPROOTs as the one holding libSystem.B.
# Command projects aren't part of //base:root, so there's no cycle.

# cmd_sysroot_flags SYSROOT: the header search of a command target built
# against Apple's internal SDK: usr/local/include, then usr/include, and
# System.framework for <System/...> includes. Unlike libSystem's libraries (common.sh
# sysroot_flags), a project outside libSystem doesn't search System.framework's
# PrivateHeaders unless its settings add it, and xnu's private variants of
# shared headers differ: the private <sys/ioctl.h> reaches <sys/param.h>
# and its BSD macro, which stty uses as an enumerator.
cmd_sysroot_flags() {
	printf '%s\n' -isystem "$1/usr/local/include" -isystem "$1/usr/include" -iframework "$1/usr/local/frameworks"
}

# find_root DEPROOT...: prints the DEPROOT holding usr/lib/libSystem.B.dylib.
find_root() {
	local d; for d in "$@"; do [ -f "$d/usr/lib/libSystem.B.dylib" ] && { printf '%s' "$d"; return 0; }; done
	echo "commands.sh: no DEPROOT holds usr/lib/libSystem.B.dylib (pass //base:root)" >&2; return 1
}

# link_tool ROOT OUT OBJ-OR-LDFLAG...: an arm64 MH_EXECUTE that dyld starts
# (LC_LOAD_DYLINKER /usr/lib/dyld), linked against ROOT's libSystem, dead
# code stripped (DEAD_CODE_STRIPPING) and signed ad hoc (CODE_SIGN_IDENTITY -).
link_tool() {
	local root="$1" out="$2"; shift 2
	mkdir -p "$(dirname "$out")"
	xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dynamic -dead_strip -adhoc_codesign \
		-syslibroot "$root" "$@" -lSystem -o "$out"
}

# tool B ROOT OUT RSP SRC... [-- LDFLAG...]: compile SRCs with the flags in
# RSP into B/obj/<OUT's name>, then link_tool them into OUT.
tool() {
	local b="$1" root="$2" out="$3" rsp="$4"; shift 4
	local -a srcs=(); while [ $# -gt 0 ] && [ "$1" != "--" ]; do srcs+=("$1"); shift; done
	[ "${1:-}" = "--" ] && shift
	local obj="$b/obj/${out//\//_}"
	compile "$obj" "$rsp" "${srcs[@]}"
	link_tool "$root" "$out" "$obj"/*.o "$@"
}
