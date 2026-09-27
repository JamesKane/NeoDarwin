#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build libfirehose_kernel.a from libdispatch: the whole Xcode target is one
# file, src/firehose/firehose_buffer.c, with libfirehose_kernel.xcconfig's flags.
#   firehose.sh OUT_A LIBDISPATCH_SRC XNU_HEADERS SDK_DIR ARCH
source "$(dirname "$0")/common.sh"
OUT="$(abspath "$1")"; L="$(abspath "$2")"; H="$(abspath "$3")"; SDK="$(abspath "$4")"; ARCH="$5"
K="$H/System/Library/Frameworks/Kernel.framework/Versions/A"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
"$(xcrun -sdk macosx -find clang)" -arch "$ARCH" -mmacosx-version-min="$(host_sdk_ver)" \
	-mkernel -nostdinc -Wno-packed -O2 -std=gnu11 \
	-DKERNEL=1 -DDISPATCH_USE_DTRACE=0 -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DOS_ATOMIC_CONFIG_STARVATION_FREE_ONLY=0 \
	-I"$L" -I"$K/PrivateHeaders" -I"$K/Headers" -I"$(host_sdk)/System/Library/Frameworks/Kernel.framework/Headers" \
	-I"$H/usr/local/include/os" -I"$H/usr/local/include/firehose" -I"$SDK/usr/local/include/kernel" \
	-c "$L/src/firehose/firehose_buffer.c" -o "$WORK/firehose_buffer.o"
xcrun libtool -static -o "$OUT" "$WORK/firehose_buffer.o"
