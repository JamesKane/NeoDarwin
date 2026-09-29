#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libdispatch-1542.0.4's installed headers, for the sysroot (docs/base/libsystem.md
# §2): replays the libdispatch target's headers phase (Public to
# usr/include/dispatch, Private to usr/local/include/dispatch), its "Install
# Headers" phase (xcodescripts/install-headers.sh: os/ headers to usr/include/os
# and usr/local/include/os) and its "Postprocess Headers" phase
# (xcodescripts/postprocess-headers.sh), each through unifdef with
# libdispatch.xcconfig's COPY_HEADERS_UNIFDEF_FLAGS, as COPY_HEADERS_RUN_UNIFDEF
# does. The module maps and Dispatch.apinotes (the copy phases) are left out:
# nothing in the base builds with modules.
#   install_headers.sh LIBDISPATCH_SRC DSTROOT
set -euo pipefail
S="$1"; DST="$2"
UNIFDEF_FLAGS=(-U__DISPATCH_BUILDING_DISPATCH__ -U__linux__ -DTARGET_OS_WIN32=0 -U__ANDROID__)

# put DIR FILE...: copy each file into DSTROOT/DIR through unifdef (exit 1
# means it changed something, which is not an error).
put() {
	local dir="$DST/$1" f; shift; mkdir -p "$dir"
	for f in "$@"; do unifdef "${UNIFDEF_FLAGS[@]}" -o "$dir/${f##*/}" "$S/$f" || [ $? -eq 1 ]; done
}

put usr/include/dispatch dispatch/base.h dispatch/block.h dispatch/data.h dispatch/dispatch.h \
	dispatch/dispatch_swift_shims.h dispatch/group.h dispatch/io.h dispatch/object.h dispatch/once.h \
	dispatch/queue.h dispatch/semaphore.h dispatch/source.h dispatch/time.h dispatch/workloop.h
put usr/local/include/dispatch private/apply_private.h private/benchmark.h private/channel_private.h \
	private/data_private.h private/io_private.h private/layout_private.h private/mach_private.h private/private.h \
	private/queue_private.h private/source_private.h private/swift_concurrency_private.h private/time_private.h \
	private/workloop_private.h
# install-headers.sh copies the os/ headers as they are; postprocess-headers.sh
# then runs unifdef over four of them.
cp_() { local dir="$DST/$1" f; shift; mkdir -p "$dir"; for f in "$@"; do cp "$S/$f" "$dir/"; done; }
cp_ usr/include/os os/object.h os/workgroup.h os/workgroup_base.h os/clock.h
cp_ usr/local/include/os os/object_private.h os/voucher_private.h os/voucher_activity_private.h \
	os/workgroup_private.h os/workgroup_interval_private.h os/workgroup_object_private.h
put usr/include/os os/workgroup_object.h os/workgroup_interval.h os/workgroup_parallel.h
put usr/local/include/os os/eventlink_private.h
