#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build the NeoDarwin additions to the XNU build SDK: the files XNU expects in
# Apple's internal SDK, produced from Apple open source and NeoDarwin shims.
# Real files only; assemble_sdk (common.sh) overlays them on the host SDK.
#   mksdk.sh OUT_DIR AVAILABILITY_SRC LIBDISPATCH_SRC SHIM_ROOT
source "$(dirname "$0")/common.sh"
OUT="$(abspath "$1")"; AV="$(abspath "$2")"; DISPATCH="$(abspath "$3")"; SHIMS="$(abspath "$4")"
BASE="$(host_sdk)"
rm -rf "$OUT"; mkdir -p "$OUT"

# Real SDK settings files (xcodebuild rejects symlinked ones).
for f in SDKSettings.plist SDKSettings.json; do cp "$BASE/$f" "$OUT/$f"; done

# Include directories XNU passes that exist only in Apple's internal SDK
# (empty unless a shim below fills them; -Wmissing-include-dirs needs them).
mkdir -p "$OUT/usr/local/include/kernel" \
	"$OUT/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/AppleFeatures" \
	"$OUT/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/platform"

# AvailabilityVersions: availability.pl, installed as its CMake install does.
mkdir -p "$OUT/usr/local/libexec"
python3 "$AV/availability" --av_version AvailabilityVersions-155 --preprocess "$AV/availability" \
	"$OUT/usr/local/libexec/availability.pl"
chmod +x "$OUT/usr/local/libexec/availability.pl"

# libdispatch: the kernel firehose header, with libfirehose_kernel.xcconfig's unifdef flags.
mkdir -p "$OUT/usr/local/include/kernel/os"
unifdef -DKERNEL=1 -DOS_FIREHOSE_SPI=1 -DOS_VOUCHER_ACTIVITY_SPI_TYPES=1 -UOS_VOUCHER_ACTIVITY_SPI \
	-o "$OUT/usr/local/include/kernel/os/firehose_buffer_private.h" "$DISPATCH/os/firehose_buffer_private.h" || true

# NeoDarwin shims (kernel/sdk) at the same relative paths.
(cd "$SHIMS" && tar chf - .) | (cd "$OUT" && tar xf -)
