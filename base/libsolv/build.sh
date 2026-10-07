#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsolv 0.7.40 (P2-02, docs/architecture/packaging.md §5): ndpkg's
# dependency solver, as a static library.
#   build.sh OUT LIBSOLV_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives, build-only, usr/local/lib/libsolv.a and libsolv's public
# headers in usr/local/include/solv; nothing of it is installed in an image:
# ndpkg (//base/ndpkg) links it in.
# What: src/CMakeLists.txt's libsolv_SRCS with its defaults, no ext/
# (libsolvext's repository readers and writers: ndpkg gives the solver its
# packages through the pool API) and no optional features (conda, complex
# or multi-semantics dependencies, linked packages), so solvversion.h's
# feature macros are all left undefined. Without HAVE_QSORT_R, util.c
# compiles its own qsort_r.c (macOS's qsort_r takes its arguments in another
# order). HAVE_STRCHRNUL and HAVE_FUNOPEN, as CMake would find them:
# NeoDarwin's libsystem_c exports both. The default version comparison (no
# DEBIAN, ARCH or HAIKU build) is RPM's, which orders the dotted numeric
# versions ndpkg's manifests carry.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$S"
eval "$(sed -nE 's/^SET\(LIBSOLV_(MAJOR|MINOR|PATCH) "([0-9]+)"\)$/\1=\2/p' VERSION.cmake)"
[ "$MAJOR.$MINOR.$PATCH" = 0.7.40 ] || { echo "libsolv/build.sh: VERSION.cmake says $MAJOR.$MINOR.$PATCH" >&2; exit 1; }
mkdir -p "$B/gen"
sed -e "s/@VERSION@/$MAJOR.$MINOR.$PATCH/" -e "s/@LIBSOLV_MAJOR@/$MAJOR/" -e "s/@LIBSOLV_MINOR@/$MINOR/" \
	-e "s/@LIBSOLV_PATCH@/$PATCH/" -e 's|^#cmakedefine \(.*\)$|/* #undef \1 */|' src/solvversion.h.in > "$B/gen/solvversion.h"

srcs=(bitmap.c poolarch.c poolvendor.c poolid.c pooldep.c poollang.c
	poolwhatprovides.c pool.c strpool.c dirpool.c
	solver.c solverdebug.c repo_solv.c repo_write.c evr.c
	queue.c repo.c repodata.c repopage.c util.c policy.c solvable.c
	transaction.c order.c rules.c problems.c linkedpkg.c cplxdeps.c
	chksum.c chksum_impl.c md5.c sha1.c sha2.c solvversion.c selection.c
	fileprovides.c diskusage.c suse.c solver_util.c cleandeps.c
	userinstalled.c filelistfilter.c decision.c)
write_rsp "$B/lib.rsp" "${TARGET_FLAGS[@]}" -mcpu=cortex-a76 -O2 -fno-common -w -DLIBSOLV_INTERNAL=1 -DHAVE_STRCHRNUL=1 -DHAVE_FUNOPEN=1 \
	-I"$B/gen" -Isrc $(cmd_sysroot_flags "$SYSROOT") -ffile-prefix-map="$S/"=libsolv/ -ffile-prefix-map="$B/"=libsolv-build/
compile "$B/obj" "$B/lib.rsp" "${srcs[@]/#/src/}"
mkdir -p "$OUT/usr/local/lib" "$OUT/usr/local/include/solv"
rm -f "$OUT/usr/local/lib/libsolv.a"
xcrun libtool -static -no_warning_for_no_symbols -o "$OUT/usr/local/lib/libsolv.a" "$B"/obj/*.o
for h in bitmap.h evr.h hash.h policy.h poolarch.h poolvendor.h pool.h poolid.h pooltypes.h queue.h solvable.h \
	solver.h solverdebug.h repo.h repodata.h repo_solv.h repo_write.h util.h selection.h strpool.h dirpool.h \
	knownid.h transaction.h rules.h problems.h chksum.h dataiterator.h; do
	install -m 0444 "src/$h" "$OUT/usr/local/include/solv/"
done
install -m 0444 "$B/gen/solvversion.h" "$OUT/usr/local/include/solv/"
