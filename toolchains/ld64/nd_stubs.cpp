// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: replaces three of ld64's C++ source files, so it must match their C++ interfaces.
//
// ld64 without libLTO, libtapi or bitcode bundles (toolchains/ld64/README.md).
// The kernel and kexts are neither LTO-built nor linked against .tbd stubs,
// and bitcode is gone from Apple's toolchains. These definitions replace
// parsers/lto_file.cpp, parsers/textstub_dylib_file.cpp and
// passes/bitcode_bundle.cpp: LLVM bitcode and .tbd inputs are recognised and
// rejected with an error that says why, and -bitcode_bundle fails.

#include <stdint.h>
#include <string.h>
#include <vector>
#include <string>

#include "Options.h"
#include "ld.hpp"
#include "lto_file.h"
#include "textstub_dylib_file.hpp"
#include "bitcode_bundle.h"

namespace {
bool isBitcode(const uint8_t* p, uint64_t len)
{
	// raw bitcode ('BC' 0xC0DE) or the bitcode wrapper (0x0B17C0DE)
	if ( len >= 4 && p[0] == 'B' && p[1] == 'C' && p[2] == 0xC0 && p[3] == 0xDE )
		return true;
	if ( len >= 4 && p[0] == 0xDE && p[1] == 0xC0 && p[2] == 0x17 && p[3] == 0x0B )
		return true;
	return false;
}
}

namespace lto {

const char* version() { return nullptr; }
unsigned int runtime_api_version() { return 0; }
unsigned int static_api_version() { return 0; }
// clang passes -lto_library whenever its toolchain has libLTO; without LTO
// inputs it is unused, and bitcode inputs fail in parse().
void set_library(const char*) {}
bool libLTOisLoaded() { return false; }

const char* archName(const uint8_t*, uint64_t) { return nullptr; }

bool isObjectFile(const uint8_t*, uint64_t, cpu_type_t, cpu_subtype_t) { return false; }
bool hasObjCCategory(const uint8_t*, uint64_t) { return false; }
std::vector<std::string> softloadRuntimeSymbols(cpu_type_t) { return {}; }

ld::relocatable::File* parse(const uint8_t* p, uint64_t len, const char* path, time_t, ld::File::Ordinal,
							 cpu_type_t, cpu_subtype_t, bool, bool)
{
	if ( isBitcode(p, len) )
		throwf("%s is LLVM bitcode: this ld64 is built without LTO support", path);
	return nullptr;
}

bool optimize(const std::vector<const ld::Atom*>&, ld::Internal&, const Options&, const OptimizeOptions&,
			  ld::File::AtomHandler&, std::vector<const ld::Atom*>&, std::vector<const char*>&)
{
	return false;
}

} // namespace lto

namespace textstub {
namespace dylib {

bool isTextStubFile(const uint8_t* p, uint64_t len, const char*)
{
	static const char tag[] = "--- !tapi";
	return len >= sizeof(tag) - 1 && memcmp(p, tag, sizeof(tag) - 1) == 0;
}

ld::dylib::File* parse(const uint8_t* p, uint64_t len, const char* path, time_t, const Options&,
					   ld::File::Ordinal, bool, bool, bool)
{
	if ( isTextStubFile(p, len, path) )
		throwf("%s is a text-based stub (.tbd): this ld64 is built without libtapi", path);
	return nullptr;
}

ld::dylib::File* parse(const char* path, tapi::LinkerInterfaceFile*, time_t, ld::File::Ordinal,
					   const Options&, bool, bool)
{
	throwf("%s: this ld64 is built without libtapi", path);
}

} // namespace dylib
} // namespace textstub

namespace ld {
namespace passes {
namespace bitcode_bundle {

void doPass(const Options& opts, ld::Internal&)
{
	if ( opts.bundleBitcode() )
		throwf("-bitcode_bundle: this ld64 is built without bitcode support");
}

} // namespace bitcode_bundle
} // namespace passes
} // namespace ld
