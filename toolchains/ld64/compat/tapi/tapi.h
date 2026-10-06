/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: a C++ header standing in for libtapi's, which ld64's Options.h includes.
 * NeoDarwin builds ld64 without libtapi (toolchains/ld64/README.md): the
 * kernel and kexts it links read no .tbd stubs. API version 1.0 compiles
 * out ld64's inlined-framework code; nd_stubs.cpp rejects .tbd inputs.
 */
#ifndef ND_LD64_TAPI_H
#define ND_LD64_TAPI_H
#include <string>

#define TAPI_API_VERSION_MAJOR 1
#define TAPI_API_VERSION_MINOR 0

namespace tapi {
class LinkerInterfaceFile {
public:
	std::string getInstallName() const { return std::string(); }
};
struct Version {
	static std::string getAsString() { return "none"; }
	static std::string getFullVersionAsString() { return "none (built without libtapi)"; }
};
}
#endif
