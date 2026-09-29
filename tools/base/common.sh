# SPDX-License-Identifier: BSD-2-Clause
# Shared helpers for the userland base builds (docs/base/libsystem.md §2). Sourced.
# Phase 1: host Xcode clang/ld against the public macOS SDK, plain arm64, with
# the staged sysroot's headers ahead of the SDK's.
set -euo pipefail
abspath() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK="$(xcrun --show-sdk-path)"
JOBS="$(/usr/sbin/sysctl -n hw.ncpu)"
# Deployment target: the macOS release whose sources these are. Current clang
# makes -Wint-conversion an error by default; these sources compile with it as
# the warning Apple's toolchain for this release gives (Libsyscall's xcconfig
# turns it off outright).
TARGET_FLAGS=(-arch arm64 -mmacosx-version-min=26.0 -isysroot "$SDK" -Wno-error=int-conversion)

# sysroot_flags SYSROOT: the staged headers ahead of the SDK's, in the order of
# Apple's builds: System.framework's PrivateHeaders (xnu's private variants of
# shared headers), then usr/local/include, then usr/include.
sysroot_flags() {
	printf '%s\n' -isystem "$1/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders" \
		-isystem "$1/usr/local/include" -isystem "$1/usr/include"
}

# compile OBJDIR RSP SRC...: compile each source in parallel into OBJDIR with
# the flags in RSP; object names keep the source path so they never collide.
# Prints the failures and returns 1 if any source fails.
compile() {
	local objdir="$1" rsp="$2"; shift 2
	mkdir -p "$objdir"
	local s
	for s in "$@"; do printf '%s\n%s\n' "$s" "$objdir/$(printf '%s' "$s" | tr '/' '_' | sed 's/^_*//').o"; done |
		xargs -P "$JOBS" -n 2 "$HERE/cc_one.sh" "$rsp"
}

# write_rsp FILE ARG...: one flag per line, for clang @FILE.
write_rsp() { local f="$1"; shift; printf '%s\n' "$@" > "$f"; }

# write_vers FILE PRODUCT PROJECT VERSION [PREFIX]: the version symbols Xcode
# generates for VERSIONING_SYSTEM = apple-generic (PREFIX = VERSION_INFO_PREFIX).
write_vers() {
	local f="$1" product="$2" project="$3" version="$4" prefix="${5:-}"
	cat > "$f" <<VERS
 extern const unsigned char ${prefix}${product}VersionString[];
 extern const double ${prefix}${product}VersionNumber;
 const unsigned char ${prefix}${product}VersionString[] __attribute__ ((used)) = "@(#)PROGRAM:${product}  PROJECT:${project}-${version}" "\\n";
 const double ${prefix}${product}VersionNumber __attribute__ ((used)) = (double)${version%%.*}.${version#*.};
VERS
	# the number keeps one fractional component, as Xcode's does
	sed -i '' -E 's/\(double\)([0-9]+)\.([0-9]+)\.[0-9.]*;/(double)\1.\2;/' "$f"
}

# dep_libdirs DEPROOT...: -L flags for the dependencies' installed dylibs.
dep_libdirs() { local d; for d in "$@"; do printf '%s\n' "-L$d/usr/lib/system" "-L$d/usr/lib"; done; }

# stage_src SRC DEST PATCHDIR: copy an upstream tree and apply its numbered
# patches (repository.md §3); prints DEST.
stage_src() {
	mkdir -p "$2"; (cd "$1" && tar chf - .) | (cd "$2" && tar xf -); chmod -R u+w "$2"
	local p; for p in "$3"/*.patch; do [ -e "$p" ] && patch -d "$2" -p1 --quiet < "$p"; done
	printf '%s' "$2"
}
