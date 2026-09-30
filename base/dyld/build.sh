#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# dyld and libdyld from dyld-1323.3 (docs/base/libsystem.md): replays
# dyld.xcodeproj's dyld target (configs/dyld.xcconfig) and libdyld.dylib target
# (configs/libdyld.xcconfig), each with the libmach_o archive it links
# (configs/libmach_o.xcconfig), over configs/base.xcconfig and the project's
# Release settings. Plain arm64.
#   build.sh OUT DYLD_SRC SYSROOT DEPROOT...
#   (DEPROOT: kernel, platform, pthread, malloc, c, blocks, dispatch, llvm_runtimes, standin_libs)
# OUT receives usr/lib/dyld and usr/lib/system/libdyld.dylib.
#
# dyld links statically: every archive the dependencies install in
# usr/local/lib/dyld (configure-dyld-archives.sh's list: libsystem_kernel.a,
# libplatform.a, libpthread.a, libc.a), plus what Apple's dyld takes from the
# closed static archives of its SDK, supplied here (src/):
#   - corecrypto's digests for code-directory hashes (libcorecrypto_static.a):
#     xnu's own corecrypto subset (the ccdigest framework and SHA-256) and
#     ndcrypto's SHA-1 and SHA-384 descriptors over FreeBSD's block functions,
#     the kernel provider's code (docs/kernel/crypto-provider.md), compiled for
#     userland; src/nd_digest_di.c selects them;
#   - amfi_check_dyld_policy_self (libamfi): src/nd_amfi.c, AMFI's rule for
#     restricted processes over what the kernel reports (P1-15);
#   - sandbox_check (libsystem_sandbox's dyld archive): src/nd_sandbox.c,
#     allowed, as the libsystem_sandbox stand-in does;
#   - compiler-rt's builtins (-fapple-link-rtlib, the toolchain's closed
#     libclang_rt.osx.a): dyld references only ___chkstk_darwin, which isn't in
#     compiler-rt's source; src/nd_chkstk_darwin.c.
# The digest sources are found among the other external repositories next to
# DYLD_SRC (@apple_xnu and @freebsd_crypto, in the target's data) and in
# kernel/neodarwin/crypto, relative to this script.
#
# Left out, arm64e or Apple-internal only: the MTE target feature
# (base.xcconfig, arm64e), pointer authentication (the sources test
# __has_feature(ptrauth_calls)), hardware TPRO (DYLD_FEATURE_USE_HW_TPRO is
# arm64e only: __TPRO_CONST then holds only glue.c's data, and dyld guards it
# and its allocator with vm_protect instead), libCrashReporterClient.a (base/sdk's
# CrashReporterClient.h is header-only), the dSYM, map file and AMFI-trusted
# signing identity. Both images are ad-hoc signed by the linker.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; SRC="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ME="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT

# find_tree FILE: the repository next to DYLD_SRC that holds FILE.
find_tree() {
	local d; for d in "$(dirname "$SRC")"/*/; do [ -f "$d$1" ] && { printf '%s' "${d%/}"; return; }; done
	echo "build.sh: no repository next to $SRC holds $1" >&2; exit 1
}
XNU="$(find_tree osfmk/corecrypto/ccdigest_init.c)"
FREEBSD="$(find_tree sys/crypto/sha2/sha512c.c)"
NDCRYPTO="$(cd "$ME/../../kernel/neodarwin/crypto" && pwd)"

L="$(stage_src "$SRC" "$B/src" "$ME/patches")"   # patches/ applied
cd "$L"
G="$B/gen"; mkdir -p "$G/mach-o"
VERSION=1323.3   # RC_ProjectSourceVersion

# libdyld-generate-version-headers.sh: dyld_priv.h with the version sets
# (for_dyld_priv.inc, staged in the sysroot) spliced in. The sources see it
# ahead of include/mach-o's template, as Apple's SDK copy is.
while IFS="" read -r p || [ -n "$p" ]; do
	case "$p" in *@VERSION_DEFS*) cat "$SYSROOT/usr/local/include/dyld/for_dyld_priv.inc" ;; *) printf '%s\n' "$p" ;; esac
done < include/mach-o/dyld_priv.h > "$G/mach-o/dyld_priv.h"

# Search paths. Xcode's header map finds a project header by name from any
# directory (USE_HEADERMAP defaults to YES everywhere but libmach_o), so quoted
# includes search every source directory; <...> finds ./include
# (SYSTEM_HEADER_SEARCH_PATHS, HEADER_SEARCH_PATHS) ahead of the sysroot.
# base/dyld/sdk stands in for the internal SDK's libamfi.h.
# libc++'s headers go first, as clang orders them itself.
CXXINC=(-nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1")
PATHS=(-iquote "$G" -iquote "$L/dyld" -iquote "$L/common" -iquote "$L/libdyld" -iquote "$L/mach_o" -iquote "$L/lsl"
	-iquote "$L/include/mach-o" -iquote "$L/include" -iquote "$L/libdyld_introspection" -iquote "$L/cache_builder"
	-I"$L/other-tools" -isystem "$G" -isystem "$L/include" -isystem "$ME/sdk" $(sysroot_flags "$SYSROOT"))
# base.xcconfig and the project: -Os, C2x and C++20 without exceptions or RTTI,
# GCC_INLINES_ARE_PRIVATE_EXTERN, GCC_NO_COMMON_BLOCKS, libc++ hardening off.
CXX=(-std=c++20 -fno-exceptions -fno-rtti -fvisibility-inlines-hidden -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE)
BASE=("${TARGET_FLAGS[@]}" -Os -fno-common -Wno-deprecated-declarations)

# PrebuiltLoader_version.h (prebuilt-loader-hash.sh): a hash of the record
# layouts that prebuilt closures store, so dyld rejects closures from another
# layout. Computed as the script does, from clang's layout dump of the same
# headers (with this build's SDK and flags rather than macosx.internal).
printf '#include "PrebuiltLoader.h"\n#include "PrebuiltObjC.h"\n#include "OptimizerObjC.h"\nint foo() { return sizeof(dyld4::PrebuiltLoader)+sizeof(mach_o::LinkedDylibAttributes)+sizeof(dyld4::PrebuiltLoaderSet)+sizeof(dyld4::ObjCBinaryInfo)+sizeof(dyld4::Loader::DylibPatch)+sizeof(dyld4::Loader::FileValidationInfo)+sizeof(prebuilt_objc::ObjCSelectorMapOnDisk)+sizeof(prebuilt_objc::ObjCObjectMapOnDisk)+sizeof(objc::SelectorHashTable); }\n' > "$B/pbl.cpp"
xcrun clang "${CXXINC[@]}" "${BASE[@]}" "${CXX[@]}" -DBUILDING_DYLD=1 -w -fsyntax-only -Xclang -fdump-record-layouts \
	-Icommon -Idyld -Iinclude -Icache_builder -Ilsl -Imach_o -Iinclude/mach-o "${PATHS[@]}" -x c++ "$B/pbl.cpp" > "$B/pbl.out"
for rec in "class dyld4::PrebuiltLoader:100" "struct dyld4::PrebuiltLoaderSet:100" "struct dyld4::ObjCBinaryInfo:100" \
	"union mach_o::LinkedDylibAttributes:100" "struct dyld4::Loader::DylibPatch:100" "struct dyld4::Loader::FileValidationInfo:100" \
	"class dyld3::MapView<struct prebuilt_objc::ObjCStringKeyOnDisk:100" "class dyld3::MultiMapView<struct prebuilt_objc::ObjCStringKeyOnDisk:100" \
	"class objc::SelectorHashTable:100" "struct dyld4::PrebuiltLoader::BindTargetRef::Absolute:6"; do
	grep -A"${rec##*:}" "${rec%:*}" "$B/pbl.out" | grep -B100 -m1 sizeof=
done | /sbin/md5 | awk '{ print "#define PREBUILTLOADER_VERSION 0x" substr($0,0,8) }' > "$G/PrebuiltLoader_version.h"

# libmach_o (libmach_o.xcconfig): GCC_SYMBOLS_PRIVATE_EXTERN, INTERNAL_BUILD
# as B&I sets it for OS builds, deployment target 13.0; no BUILDING_* define.
MACHO=(mach_o/Architecture.cpp mach_o/Error.cpp mach_o/GradedArchitectures.cpp mach_o/Header.cpp mach_o/LoggingStub.cpp
	mach_o/Platform.cpp mach_o/FunctionStarts.cpp mach_o/CompactUnwind.cpp mach_o/Symbol.cpp mach_o/ExportsTrie.cpp
	mach_o/Policy.cpp mach_o/Fixups.cpp mach_o/ChainedFixups.cpp mach_o/BindOpcodes.cpp mach_o/RebaseOpcodes.cpp
	mach_o/LinkerOptimizationHints.cpp mach_o/NListSymbolTable.cpp mach_o/Instructions.cpp mach_o/Universal.cpp
	mach_o/Archive.cpp mach_o/Misc.cpp mach_o/Image.cpp mach_o/DwarfDebug.cpp mach_o/SplitSeg.cpp mach_o/DataInCode.cpp
	mach_o/ObjC.cpp mach_o/FunctionVariants.cpp mach_o/Version32.cpp mach_o/Version64.cpp)
write_rsp "$B/macho.rsp" "${CXXINC[@]}" "${BASE[@]}" -mmacosx-version-min=13.0 "${CXX[@]}" -fvisibility=hidden \
	-DINTERNAL_BUILD=1 -iquote "$L/dyld" -iquote "$L/common" -iquote "$L/mach_o" -iquote "$L/lsl" -isystem "$L/include" \
	$(sysroot_flags "$SYSROOT")
compile "$B/macho" "$B/macho.rsp" "${MACHO[@]}"
xcrun libtool -static -o "$B/libmach_o.a" "$B"/macho/*.o 2>/dev/null

# The dyld target's sources phase (common/TargetPolicy.h, a header, left out).
DYLD=(dyld/dyldStartup.s dyld/dyldMain.cpp dyld/DyldProcessConfig.cpp dyld/DyldRuntimeState.cpp common/OptimizerSwift.cpp
	dyld/PrebuiltSwift.cpp dyld/Loader.cpp dyld/JustInTimeLoader.cpp dyld/PrebuiltLoader.cpp common/PerfectHash.cpp
	dyld/DyldDelegates.cpp common/CachePatching.cpp lsl/Allocator.cpp common/MurmurHash.cpp dyld/SharedCacheRuntime.cpp
	dyld/PremappedLoader.cpp dyld/RemoteNotificationResponder.cpp common/AAREncoder.cpp dyld/DyldAPIs.cpp
	dyld/PrebuiltObjC.cpp dyld/Tracing.cpp dyld/ExternallyViewableState.cpp dyld/glue.c common/DyldSharedCache.cpp
	common/ObjCVisitor.cpp common/SwiftVisitor.cpp common/MachOFile.cpp common/MachOLoaded.cpp common/MachOLayout.cpp
	common/Utilities.cpp common/MachOAnalyzer.cpp common/Cksum.cpp common/MetadataVisitor.cpp common/Diagnostics.cpp
	common/PropertyList.cpp lsl/ProtectedStack.s lsl/PVLEInt64.cpp libdyld_introspection/dyld_introspection.cpp
	common/TargetPolicy.cpp common/FileManager.cpp lsl/CRC32c.cpp common/ProcessAtlas.cpp)
# dyld.xcconfig's GCC_PREPROCESSOR_DEFINITIONS (Release).
DYLD_DEFS=(-DDYLD_VERSION="$VERSION" -DBUILDING_DYLD=1 -D_LIBCPP_NO_EXCEPTIONS=1)
write_vers "$B/dyld_vers.c" dyld dyld "$VERSION"   # VERSIONING_SYSTEM = apple-generic
write_rsp "$B/dyld_cxx.rsp" "${CXXINC[@]}" "${BASE[@]}" "${CXX[@]}" "${DYLD_DEFS[@]}" "${PATHS[@]}"
write_rsp "$B/dyld_c.rsp" "${BASE[@]}" -std=c2x "${DYLD_DEFS[@]}" "${PATHS[@]}"
compile "$B/dyld" "$B/dyld_cxx.rsp" $(printf '%s\n' "${DYLD[@]}" | grep '\.cpp$')
compile "$B/dyld" "$B/dyld_c.rsp" $(printf '%s\n' "${DYLD[@]}" | grep -v '\.cpp$') "$B/dyld_vers.c"

# The closed archives' interfaces (above), private to dyld. xnu's corecrypto
# builds in its userland configuration (CC_KERNEL=0, no assembly, as ndcrypto's
# host build); FreeBSD's sources through ndcrypto's compat prelude.
CC=("${BASE[@]}" -std=c17 -fvisibility=hidden -DCC_USE_ASM=0 -D__STDC_WANT_LIB_EXT1__=1 -isystem "$ME/sdk" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/xnu_cc.rsp" "${CC[@]}" -w -I"$XNU/osfmk/corecrypto"
write_rsp "$B/fbsd.rsp" "${CC[@]}" -w -include nd_freebsd.h -I"$NDCRYPTO/compat" -I"$FREEBSD/sys"
write_rsp "$B/nd.rsp" "${CC[@]}" -include nd_freebsd.h -I"$NDCRYPTO/compat" -I"$FREEBSD/sys" -I"$XNU/libkern" \
	-I"$XNU/osfmk/corecrypto" -I"$NDCRYPTO"
compile "$B/cc" "$B/xnu_cc.rsp" $(for f in cc_clear cc_dit ccdigest_init ccdigest_update ccdigest_final_64be ccsha256_di \
	ccsha256_initial_state ccsha256_K ccsha256_ltc_compress ccsha256_ltc_di; do printf '%s\n' "$XNU/osfmk/corecrypto/$f.c"; done)
compile "$B/cc" "$B/fbsd.rsp" "$FREEBSD/sys/crypto/sha1.c" "$FREEBSD/sys/crypto/sha2/sha512c.c" "$FREEBSD/sys/crypto/md5c.c"
compile "$B/cc" "$B/nd.rsp" "$NDCRYPTO/nd_digest.c" "$ME/src/nd_digest_di.c" "$ME/src/nd_amfi.c" "$ME/src/nd_sandbox.c" \
	"$ME/src/nd_chkstk_darwin.c"
xcrun libtool -static -o "$B/libdyld_standins.a" "$B"/cc/*.o 2>/dev/null

# configure-dyld-archives.sh: every archive in usr/local/lib/dyld but
# compiler-rt's, libunwind's and libc++abi's.
ARCHIVES=()
for d in "${DEPS[@]}"; do
	for a in "$d"/usr/local/lib/dyld/*.a; do
		case "${a##*/}" in *compiler_rt*|*unwind*|*c++abi*) ;; *) [ -f "$a" ] && ARCHIVES+=("$a") ;; esac
	done
done

# dyld.xcconfig's OTHER_LDFLAGS (macOS) and DEAD_CODE_STRIPPING.
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -nostdlib -e __dyld_start -Wl,-exported_symbol,__dyld_start \
	-Wl,-exported_symbol,_lldb_image_notifier -Wl,-exported_symbol,_dyld_all_image_infos -Wl,-no_inits \
	-Wl,-dylinker -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-fixup_chains -Wl,-data_const -Wl,-dead_strip \
	-Wl,-dirty_data_list,"$L/dyld/dyld.dirty" -Wl,-section_order,__TPRO_CONST,__data:__allocator \
	"$B"/dyld/*.o "$B/libmach_o.a" "$B/libdyld_standins.a" "${ARCHIVES[@]}" -o "$OUT/usr/lib/dyld"

# The libdyld.dylib target's sources phase and libdyld.xcconfig.
LIBDYLD=(libdyld/libdyldGlue.cpp libdyld/threadLocalHelpers.s libdyld/ThreadLocalVariables.cpp libdyld/LibSystemHelpers.cpp
	libdyld/dyld_process_info.cpp libdyld/dyld_process_info_notify.cpp libdyld/utils.cpp
	libdyld_introspection/dyld_introspection.cpp common/Diagnostics.cpp common/TargetPolicy.cpp common/Utilities.cpp
	common/MachOLayout.cpp common/DyldSharedCache.cpp common/MachOFile.cpp common/MachOLoaded.cpp
	common/SafeVMPrimitives.cpp common/ProcessAtlas.cpp common/FileManager.cpp lsl/Allocator.cpp lsl/PVLEInt64.cpp
	lsl/CRC32c.cpp)
write_vers "$B/libdyld_vers.c" dyld dyld "$VERSION"
sed -i '' 's/^ const/ __attribute__((visibility("default"))) const/' "$B/libdyld_vers.c"   # VERSION_INFO_EXPORT_DECL
write_rsp "$B/lib_cxx.rsp" "${CXXINC[@]}" "${BASE[@]}" "${CXX[@]}" -DBUILDING_LIBDYLD=1 -I"$L/include" "${PATHS[@]}"
write_rsp "$B/lib_c.rsp" "${BASE[@]}" -std=c2x -DBUILDING_LIBDYLD=1 -I"$L/include" "${PATHS[@]}"
compile "$B/libdyld" "$B/lib_cxx.rsp" $(printf '%s\n' "${LIBDYLD[@]}" | grep '\.cpp$')
compile "$B/libdyld" "$B/lib_c.rsp" $(printf '%s\n' "${LIBDYLD[@]}" | grep -v '\.cpp$') "$B/libdyld_vers.c" \
	"$ME/src/nd_libcpp_verbose_abort.c"   # std::__libcpp_verbose_abort, as dyld/glue.c has for dyld

# libdyld.xcconfig's OTHER_LDFLAGS (macOS): the libSystem libraries upward,
# -umbrella System, C++20's comparison helpers unexported, the dirty-data list.
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libdyld.dylib \
	-current_version "$VERSION" -compatibility_version 1 -Wl,-umbrella,System -Wl,-no_inits -Wl,-dead_strip \
	"$B"/libdyld/*.o "$B/libmach_o.a" $(dep_libdirs "${DEPS[@]}") -Wl,-no_warn_unused_dylibs \
	-Wl,-upward-lsystem_platform -Wl,-upward-lsystem_malloc -Wl,-upward-lsystem_c -Wl,-upward-lsystem_pthread \
	-Wl,-upward-lxpc -Wl,-upward-lsystem_blocks -Wl,-upward-lsystem_kernel -Wl,-upward-ldispatch \
	-Wl,-upward-lcompiler_rt -Wl,-unexported_symbol,'__ZNSt3*' \
	-Wl,-dirty_data_list,"$L/configs/libdyld.dirty" -Wl,-data_const -o "$OUT/usr/lib/system/libdyld.dylib"
