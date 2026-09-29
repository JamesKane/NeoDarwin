// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libsystem_configuration.dylib, the client library of configd's
// DNS configuration and network-state (nwi) services. Apple publishes it in
// configd (libSystemConfiguration/), but NeoDarwin doesn't build configd yet.
// libSystem's fork handlers call _libSC_info_fork_prepare/parent/child(),
// which in Apple's library hold and reset the client's connections to
// configd across fork. No NeoDarwin library uses the client yet, so there
// is no connection and the three do nothing. The stand-in gives way to
// configd's library when NeoDarwin builds configd (docs/base/libsystem.md).

// Declared by Libsystem's init.c, which calls them; configd's
// libSystemConfiguration_client.h declares them for its own build.
void _libSC_info_fork_prepare(void);
void _libSC_info_fork_parent(void);
void _libSC_info_fork_child(void);

void
_libSC_info_fork_prepare(void)
{
}

void
_libSC_info_fork_parent(void)
{
}

void
_libSC_info_fork_child(void)
{
}
