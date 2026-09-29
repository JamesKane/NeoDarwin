#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Stage the header sysroot the userland base builds against (docs/base/libsystem.md §2).
#   stage_sysroot.sh OUT XNU_HEADERS XNU_SRC LIBPLATFORM LIBPTHREAD LIBMALLOC AVAILABILITY DYLD LIBC SHIMS
# Each project's headers go where its Xcode headers phase installs them:
# Public to usr/include, Private to usr/local/include. The projects include
# each other's private headers, so this runs before any library builds.
# NeoDarwin's shims for internal-SDK headers go last and win.
set -euo pipefail
abspath() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
OUT="$(abspath "$1")"; XH="$(abspath "$2")"; XNU="$(abspath "$3")"; PLAT="$(abspath "$4")"
PTH="$(abspath "$5")"; MAL="$(abspath "$6")"; AV="$(abspath "$7")"; DYLD="$(abspath "$8")"; LIBC="$(abspath "$9")"; SHIMS="$(abspath "${10}")"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
rm -rf "$OUT"; mkdir -p "$OUT"
PUB="$OUT/usr/include"; PRIV="$OUT/usr/local/include"
put() { mkdir -p "$2"; cp "$1" "$2/"; }                     # put FILE DIR

# xnu: make installhdrs (//kernel:headers).
(cd "$XH" && tar chf - .) | (cd "$OUT" && tar xf -); chmod -R u+w "$OUT"

# Libsyscall (xnu libsyscall/, Libsyscall_static headers phase and copy phases).
L="$XNU/libsyscall"
for h in wrappers/gethostuuid.h wrappers/spawn/spawn.h; do put "$L/$h" "$PUB"; done
for h in wrappers/gethostuuid_private.h wrappers/libproc/libproc_private.h wrappers/spawn/spawn_filtering_private.h \
	wrappers/_libkernel_init.h wrappers/spawn/spawn_private.h wrappers/libproc/libproc_internal.h; do put "$L/$h" "$PRIV"; done
put "$L/wrappers/libproc/libproc.h" "$PUB"
for h in os/thread_self_restrict.h os/tsd.h; do put "$L/$h" "$PRIV/os"; done
put "$L/os/proc.h" "$PUB/os"
# MIG-generated mach headers, by Apple's own script.
mkdir -p "$work/obj" "$work/products"
(export SRCROOT="$L" OBJROOT="$work/obj" BUILT_PRODUCTS_DIR="$work/products" SDKROOT="$(xcrun --show-sdk-path)" \
	ARCHS=arm64 PLATFORM_NAME=macosx DSTROOT="$work/dst"; cd "$work/obj"; bash "$L/xcodescripts/mach_install_mig.sh") >/dev/null
cp -R "$work/products/mig_hdr/include/." "$PUB/"
cp -R "$work/products/mig_hdr/local/include/." "$PRIV/"

# corecrypto's interface headers, which xnu publishes (EXTERNAL_HEADERS, APSL);
# libmalloc and dyld include them. The implementation is not published.
mkdir -p "$PRIV/corecrypto"; cp "$XNU"/EXTERNAL_HEADERS/corecrypto/*.h "$PRIV/corecrypto/"

# libpthread (libsystem_pthread headers phase; install-sys-headers.sh; install-symlinks.sh).
for h in stack_np.h qos.h pthread.h pthread_impl.h pthread_spis.h introspection.h sched.h spawn.h; do
	put "$PTH/include/pthread/$h" "$PUB/pthread"; done
for h in introspection_private.h tsd_private.h posix_sched.h workgroup_private.h jit_private.h qos_private.h \
	spinlock_private.h workqueue_private.h dependency_private.h private.h; do put "$PTH/private/pthread/$h" "$PRIV/pthread"; done
mkdir -p "$PUB/sys" "$PRIV/sys"; cp -R "$PTH/include/sys/." "$PUB/sys/"; cp -R "$PTH/private/sys/." "$PRIV/sys/"
for f in $(find "$PUB/pthread" "$PRIV/pthread" "$PUB/sys" "$PRIV/sys" -name '*.h'); do
	unifdef -t -U__PTHREAD_BUILDING_PTHREAD__ -o "$f" "$f" || [ $? -eq 1 ]; done
ln -sf pthread/pthread.h "$PUB/pthread.h"; ln -sf pthread/pthread_impl.h "$PUB/pthread_impl.h"
ln -sf pthread/pthread_spis.h "$PUB/pthread_spis.h"; ln -sf pthread/sched.h "$PUB/sched.h"
ln -sf pthread/posix_sched.h "$PRIV/posix_sched.h"; ln -sf pthread/spinlock_private.h "$PRIV/pthread_spinlock.h"
ln -sf pthread/workqueue_private.h "$PRIV/pthread_workqueue.h"

# libplatform: include/ is public, private/ is private (its xcconfigs' header paths).
cp -R "$PLAT/include/." "$PUB/"; rm -rf "$PUB/exclavekit"
cp -R "$PLAT/private/." "$PRIV/"

# libmalloc (libsystem_malloc headers phase).
mkdir -p "$PUB/malloc"; cp "$MAL"/include/malloc/*.h "$PUB/malloc/"
for h in malloc_private.h malloc_type_private.h stack_logging.h malloc_implementation.h; do put "$MAL/private/$h" "$PRIV"; done

# AvailabilityVersions-155 (its CMake install): dyld's version tables only.
# The availability headers themselves stay the host SDK's, with base/sdk's
# AvailabilityInternalPrivate.h: the 26.0 layer, generated here, breaks under
# the host's newer clang (docs/base/libsystem.md §4). As its CMakeLists does,
# stamp the generator with the version, then expand with the stamped copy.
python3 "$AV/availability" --av_version AvailabilityVersions-155 --preprocess "$AV/availability" "$work/availability"
mkdir -p "$PRIV/dyld"
for t in VersionMap.h for_dyld_priv.inc; do python3 "$work/availability" --preprocess "$AV/templates/$t" "$PRIV/dyld/$t"; done

# dyld's private headers (libdyld.dylib headers phase;
# libdyld-generate-version-headers.sh splices the version table into dyld_priv.h).
# The public ones (dyld.h, dyld_images.h, fixup-chains.h, utils.h) stay the
# host SDK's: dyld's use availability spellings (bridgeos) that only Apple's
# internal clang accepts.
for h in dyld_cache_format.h dyld_introspection.h dyld-interposing.h dyld_process_info.h function-variant-macros.h \
	utils_priv.h; do put "$DYLD/include/mach-o/$h" "$PRIV/mach-o"; done
while IFS="" read -r line || [ -n "$line" ]; do
	case "$line" in *@VERSION_DEFS*) cat "$PRIV/dyld/for_dyld_priv.inc" ;; *) printf '%s\n' "$line" ;; esac
done < "$DYLD/include/mach-o/dyld_priv.h" > "$PRIV/mach-o/dyld_priv.h"

# Libc, by its own xcodescripts/headers.sh (which runs generate_features.pl);
# its public headers replace the host SDK's of the same names.
(cd "$LIBC" && env SRCROOT="$LIBC" DSTROOT="$work/libc" DERIVED_FILES_DIR="$work/libc-derived" SDK_INSTALL_HEADERS_ROOT= \
	DEPLOYMENT_LOCATION=YES PLATFORM_NAME=macosx VARIANT_PLATFORM_NAME=macosx ARCHS=arm64 CURRENT_ARCH=arm64 \
	bash -e xcodescripts/headers.sh) > "$work/libc-headers.log" 2>&1 || { tail -20 "$work/libc-headers.log" >&2; exit 1; }
cp -R "$work/libc/." "$OUT/"

# NeoDarwin's internal-SDK shims (base/sdk).
cp -R "$SHIMS/." "$OUT/"
chmod -R a+rX,u+w "$OUT"
