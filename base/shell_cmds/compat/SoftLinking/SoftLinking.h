// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C su(1), which includes it.
//
// Apple's private <SoftLinking/SoftLinking.h> (not published): weak links to
// a library resolved at run time. su(1) uses it for libEndpointSecuritySystem
// (closed): SOFT_LINK_DYLIB(lib) opens /usr/lib/lib.dylib on first use, and
// SOFT_LINK_FUNCTION(lib, name, localname, ...) defines localname, which calls
// name there, and is<lib><name>Available(), which says whether it was found.
// NeoDarwin has no libEndpointSecuritySystem, so su never calls it.
#ifndef ND_SOFTLINKING_H
#define ND_SOFTLINKING_H
#include <dlfcn.h>
#include <stdbool.h>
#include <stddef.h>

#define SOFT_LINK_DYLIB(lib)                                                    \
	static void *lib##Library(void)                                         \
	{                                                                       \
		static void *handle;                                            \
		static bool tried;                                              \
		if (!tried) {                                                   \
			tried = true;                                           \
			handle = dlopen("/usr/lib/" #lib ".dylib", RTLD_NOW);   \
		}                                                               \
		return handle;                                                  \
	}

#define SOFT_LINK_FUNCTION(lib, name, localname, ret, params, args)             \
	static ret(*localname##Pointer) params;                                 \
	static bool is##lib##name##Available(void)                              \
	{                                                                       \
		void *handle = lib##Library();                                  \
		if (handle != NULL && localname##Pointer == NULL)               \
			localname##Pointer = (ret(*) params)dlsym(handle, #name); \
		return localname##Pointer != NULL;                              \
	}                                                                       \
	static ret localname params                                             \
	{                                                                       \
		(void)is##lib##name##Available();                               \
		return localname##Pointer args;                                 \
	}
#endif
