#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Compile one file for the userland base: cc_one.sh RSP SRC OBJ
# RSP holds the flags (one per line). Assembly goes through the C preprocessor.
rsp="$1"; src="$2"; obj="$3"
case "$src" in *.s) lang="-x assembler-with-cpp" ;; *.cpp|*.cc) lang="-x c++" ;; *) lang="" ;; esac
xcrun clang @"$rsp" $lang -c "$src" -o "$obj" 2> "$obj.log" || {
	echo "FAILED: $src"; grep -m 8 "error:" "$obj.log" || sed -n "1,12p" "$obj.log"; exit 1; }
