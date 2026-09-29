#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Install the LLVM runtimes' headers as Apple's SDK has them (docs/base/libsystem.md):
#   install_headers.sh DEST LLVM_PROJECT_SRC
# DEST receives
#   usr/include/c++/v1/   libc++'s headers (libcxx/include), libc++abi's (cxxabi.h,
#                         __cxxabi_config.h), and the two files libc++'s CMake
#                         generates: __config_site and __assertion_handler;
#   usr/include/          libunwind's headers (libunwind.h, unwind.h, ...).
# The sysroot stage runs it so that C++ code in the base builds against the libc++
# that runs with it; build.sh runs it for its own compiles.
set -euo pipefail
abspath() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
DEST="$(abspath "$1")"; L="$(abspath "$2")"
V1="$DEST/usr/include/c++/v1"
mkdir -p "$V1"

# libcxx/include/CMakeLists.txt installs every header in the directory: its
# file list is all of it but CMakeLists.txt and __config_site.in.
(cd "$L/libcxx/include" && find -L . -type f ! -name CMakeLists.txt ! -name __config_site.in -print0 |
	xargs -0 tar chf -) | (cd "$V1" && tar xf -)
cp "$L/libcxxabi/include/cxxabi.h" "$L/libcxxabi/include/__cxxabi_config.h" "$V1/"

# __assertion_handler: LIBCXX_ASSERTION_HANDLER_FILE's default, copied as is.
cp "$L/libcxx/vendor/llvm/default_assertion_handler.in" "$V1/__assertion_handler"

# __config_site: __config_site.in configured as libcxx/cmake/caches/Apple.cmake
# configures it for arm64 Darwin, which is what CMake writes and matches the
# macOS SDK's (in its newer spelling): ABI version 1 in namespace __1, pthreads
# found through the platform (no THREAD_API define), vendor availability
# annotations on, no time zone database, the libdispatch PSTL backend, and the
# hardening mode "none" (2 in this release's numbering). Every other
# #cmakedefine stays undefined; the ABI and extra-site hooks are empty.
sed -E \
	-e 's|^#cmakedefine _LIBCPP_ABI_VERSION @_LIBCPP_ABI_VERSION@$|#define _LIBCPP_ABI_VERSION 1|' \
	-e 's|^#cmakedefine _LIBCPP_ABI_NAMESPACE @_LIBCPP_ABI_NAMESPACE@$|#define _LIBCPP_ABI_NAMESPACE __1|' \
	-e 's|^#cmakedefine (_LIBCPP_HAS_NO_TIME_ZONE_DATABASE)$|#define \1|' \
	-e 's|^#cmakedefine (_LIBCPP_PSTL_BACKEND_LIBDISPATCH)$|#define \1|' \
	-e 's|^#cmakedefine _LIBCPP_HARDENING_MODE_DEFAULT @_LIBCPP_HARDENING_MODE_DEFAULT@$|#define _LIBCPP_HARDENING_MODE_DEFAULT 2|' \
	-e 's|^#cmakedefine ([A-Za-z0-9_]+).*$|/* #undef \1 */|' \
	-e 's|^@_LIBCPP_ABI_DEFINES@$||' -e 's|^@_LIBCPP_EXTRA_SITE_DEFINES@$||' \
	"$L/libcxx/include/__config_site.in" > "$V1/__config_site"
if grep -q '@\|#cmakedefine' "$V1/__config_site"; then
	echo "install_headers.sh: __config_site.in has settings this script doesn't know" >&2; exit 1
fi

# libunwind/include/CMakeLists.txt's file list, into usr/include.
(cd "$L/libunwind/include" && tar chf - __libunwind_config.h libunwind.h libunwind.modulemap \
	mach-o/compact_unwind_encoding.h unwind_arm_ehabi.h unwind_itanium.h unwind.h) |
	(cd "$DEST/usr/include" && tar xf -)
chmod -R u+w,a+r "$DEST/usr/include"
