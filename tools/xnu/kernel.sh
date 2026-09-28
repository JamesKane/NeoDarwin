#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build an XNU kernel with the upstream makefiles (phase-1 wrapper).
#   kernel.sh REPORT KERNEL_OUT|- XNU_SRC SDK_DIR FIREHOSE_A DARWIN_VERSION ARCH MACHINE CONFIG OVERLAY|- [PATCH...] [-- MAKE_VAR...]
# OVERLAY lists "destination<TAB>source" files to add to the tree before patching.
# Always writes REPORT: build outcome and the sorted list of undefined symbols
# if the link fails. With KERNEL_OUT "-", a link that fails only on undefined
# symbols is a successful action (the link-gap report is the product).
source "$(dirname "$0")/common.sh"
REPORT="$(abspath "$1")"; KOUT="$2"; SRC="$(abspath "$3")"; SDK="$(abspath "$4")"
FH="$(abspath "$5")"; VER="$6"; ARCH="$7"; MACHINE="$8"; CONFIG="$9"; OVERLAY="${10}"; shift 10
[ "$KOUT" = "-" ] || KOUT="$(abspath "$KOUT")"
PATCHES=(); EXTRA=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do PATCHES+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift
EXTRA=("$@")
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
stage_tree "$SRC" "$WORK/src"
assemble_sdk "$SDK" "$WORK/MacOSX.sdk"; SDK="$WORK/MacOSX.sdk"
if [ "$OVERLAY" != "-" ]; then
	while IFS="$(printf '\t')" read -r dst src; do
		mkdir -p "$WORK/src/$(dirname "$dst")"; cp "$src" "$WORK/src/$dst"
	done < "$OVERLAY"
fi
for p in ${PATCHES[@]+"${PATCHES[@]}"}; do patch -d "$WORK/src" -p1 --quiet < "$(abspath "$p")"; done
mkdir -p "$WORK/lib"; cp "$FH" "$WORK/lib/libfirehose_kernel.a"
SDKVARS=(); while IFS= read -r v; do SDKVARS+=("$v"); done < <(xnu_sdk_vars "$SDK")
MAKEARGS=("${SDKVARS[@]}" ARCH_CONFIGS="$ARCH" MACHINE_CONFIGS="$MACHINE" KERNEL_CONFIGS="$CONFIG"
	RC_DARWIN_KERNEL_VERSION="$VER" BUILD_WERROR=0 BUILD_LTO=0
	"LDFLAGS_KERNEL_SDK=-L$WORK/lib"
	OBJROOT="$WORK/obj" SYMROOT="$WORK/sym" DSTROOT="$WORK/dst" ${EXTRA[@]+"${EXTRA[@]}"})
# Parallel pass, then a serial pass so the final diagnostics are not interleaved.
make -C "$WORK/src" -j"$(/usr/sbin/sysctl -n hw.ncpu)" -k "${MAKEARGS[@]}" > "$WORK/log.parallel" 2>&1 || true
status=0; make -C "$WORK/src" "${MAKEARGS[@]}" MAKEJOBS=-j1 > "$WORK/log" 2>&1 || status=$?

undef="$WORK/undefined.txt"
# ld64 lists each symbol as   "name", referenced from:   followed by lines of
# "      caller in object.o" or "      <initial-undefines>" (required only by an
# export list, not by code). C names lose their leading underscore; demangled
# C++ names are kept verbatim. Output: name<TAB>objects (or "export-list").
awk '
	/Undefined symbols/ { f = 1; next }
	/symbol\(s\) not found/ { f = 0 }
	f && /^  ".*", referenced from:$/ {
		if (sym != "") print sym "\t" refs
		sym = $0; sub(/^  "/, "", sym); sub(/", referenced from:$/, "", sym); sub(/^_/, "", sym)
		refs = ""; next
	}
	f && /<initial-undefines>/ { refs = (refs == "" ? "export-list" : refs " export-list"); next }
	f && / in / { n = split($0, w, " "); o = w[n]; if (index(" " refs " ", " " o " ") == 0) refs = refs (refs == "" ? "" : " ") o }
	END { if (sym != "") print sym "\t" refs }
' "$WORK/log" | sort -u > "$undef" || true
compile_errors=$(grep -E ': (fatal )?error:' "$WORK/log" | grep -vc 'linker command failed' || true)
kernel="$(find "$WORK/obj" -type f -name "kernel.*" -not -name "*.*.*.*" -perm -u+x 2>/dev/null | head -1)"

{
	echo "xnu build: ARCH=$ARCH MACHINE=$MACHINE CONFIG=$CONFIG darwin=$VER patches=${#PATCHES[@]} vars=${EXTRA[*]:-none}"
	echo "make exit status: $status"
	echo "compile error lines: $compile_errors"
	if [ -n "$kernel" ]; then echo "kernel image: built ($(basename "$kernel"))"; else echo "kernel image: not built"; fi
	echo "undefined symbols: $(wc -l < "$undef" | tr -d ' ')"
	echo "  referenced by code: $(awk -F'\t' '$2 != "export-list"' "$undef" | wc -l | tr -d ' ')"
	echo "  required only by an export list: $(awk -F'\t' '$2 == "export-list"' "$undef" | wc -l | tr -d ' ')"
	echo "--- symbol<TAB>referencing objects"
	cat "$undef"
	echo "--- raw linker output (undefined-symbols block)"
	awk '/Undefined symbols/{f=1} f{print} /symbol\(s\) not found/{f=0}' "$WORK/log"
} > "$REPORT"

if [ -n "$kernel" ]; then
	if [ "$KOUT" != "-" ]; then
		cp "$kernel" "$KOUT"
		cp "$kernel.unstripped" "$KOUT.unstripped"   # symbols, for audits and debugging
	fi
	exit 0
fi
if [ "$KOUT" = "-" ] && [ -s "$undef" ]; then
	exit 0   # link-gap report mode: compiled everything, link gaps recorded
fi
# The parallel pass ran with -k, so it saw every failing file; the serial
# pass stops at the first. Show both.
echo "--- errors from the parallel (-k) pass" >&2
grep -E ': (fatal )?error:' "$WORK/log.parallel" | sort -u | head -60 >&2 || true
echo "--- serial pass" >&2
grep -E ': (fatal )?error:' "$WORK/log" | head -40 >&2 || true
tail -30 "$WORK/log" >&2
exit 1
