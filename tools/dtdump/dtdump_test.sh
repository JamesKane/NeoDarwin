#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# dtdump fixture tests.
#   dtdump_test.sh ok DTDUMP ACPIDUMP EXPECTED [OPTION...]
#       passes, and prints EXPECTED exactly (OPTIONs go to dtdump)
#   dtdump_test.sh fail DTDUMP ACPIDUMP LINE       exits 1 and prints LINE
#   dtdump_test.sh broken-tree DTDUMP ACPIDUMP LINE
#       writes the tree, points /defaults serial-device at phandle 9 (no
#       node), and checks that dtdump --dt rejects it with LINE
#   dtdump_test.sh needs-gop DTDUMP ACPIDUMP LINE
#       writes the tree with --gop (no UART the kernel drives), and checks
#       that dtdump --dt rejects it with LINE without --gop and accepts it
#       with --gop
set -euo pipefail
mode="$1"; dtdump="$2"; input="$3"; want="$4"; shift 4
out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
case "$mode" in
ok)
	"$dtdump" "$@" "$input" > "$out/stdout" || { cat "$out/stdout"; echo "FAIL: dtdump exited $?"; exit 1; }
	# The input path is Bazel's; compare from the second line on, with it normalised.
	sed '1s|: [^:]*: |: INPUT: |' "$out/stdout" > "$out/got"
	diff -u "$want" "$out/got" || { echo "FAIL: output differs from $want (regenerate it if the change is intended)"; exit 1; }
	;;
fail)
	if "$dtdump" "$input" > "$out/stdout" 2>&1; then cat "$out/stdout"; echo "FAIL: dtdump accepted $input"; exit 1; fi
	grep -qF -- "$want" "$out/stdout" || { cat "$out/stdout"; echo "FAIL: missing: $want"; exit 1; }
	;;
broken-tree)
	"$dtdump" --write-dt "$out/tree" "$input" > /dev/null
	"$dtdump" --dt "$out/tree" > /dev/null
	# The property: a 32-byte NUL-padded name, a u32 length of 4, the phandle (3).
	perl -0777 -pi -e 's/(serial-device\x00{19}\x04\x00\x00\x00)\x03/${1}\x09/ or die "no serial-device\n"' "$out/tree"
	if "$dtdump" --dt "$out/tree" > "$out/stdout" 2>&1; then cat "$out/stdout"; echo "FAIL: dtdump accepted the broken tree"; exit 1; fi
	grep -qF -- "$want" "$out/stdout" || { cat "$out/stdout"; echo "FAIL: missing: $want"; exit 1; }
	;;
needs-gop)
	"$dtdump" --gop 1920x1080 --write-dt "$out/tree" "$input" > /dev/null
	"$dtdump" --gop 1920x1080 --dt "$out/tree" > /dev/null || { echo "FAIL: dtdump --gop rejected the tree"; exit 1; }
	if "$dtdump" --dt "$out/tree" > "$out/stdout" 2>&1; then cat "$out/stdout"; echo "FAIL: dtdump accepted a tree without a UART or a framebuffer"; exit 1; fi
	grep -qF -- "$want" "$out/stdout" || { cat "$out/stdout"; echo "FAIL: missing: $want"; exit 1; }
	;;
*) echo "unknown mode $mode"; exit 2 ;;
esac
echo "PASS"
