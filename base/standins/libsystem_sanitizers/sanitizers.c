// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libsystem_sanitizers.dylib (libsanitizers: the system's
// address sanitizer and memory-error diagnosis), which Apple does not publish.
// libSystem's initializer calls _sanitizers_init() in every 64-bit process,
// between Libc's and malloc's initializers, so that a sanitizer the process
// asks for (through its environment and apple[] strings) is in place before
// the first allocation. NeoDarwin has no sanitizer runtime, so no process
// gets one and the call does nothing. More of the interface is added as
// NeoDarwin libraries come to need it (docs/base/libsystem.md).

// Declared by Libsystem's init.c, which calls it; no header publishes it.
void _sanitizers_init(const char *envp[], const char *apple[]);

void
_sanitizers_init(const char *envp[], const char *apple[])
{
	(void)envp;
	(void)apple;
}
