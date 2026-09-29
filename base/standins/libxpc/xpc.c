// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libxpc.dylib, which Apple does not publish; it also carries
// launchd's client library (bootstrap_*). Libc uses bootstrap_parent().
// NeoDarwin has one bootstrap namespace until launchd-842 lands (P1-08), so
// every bootstrap port is its own parent, as the root of the tree is. More of
// libxpc is added as NeoDarwin libraries come to need it
// (docs/base/libsystem.md).

#include <mach/mach.h>
#include <servers/bootstrap.h>

kern_return_t
bootstrap_parent(mach_port_t bp, mach_port_t *parent_port)
{
	*parent_port = bp;   // declared nonnull in <servers/bootstrap.h>
	return KERN_SUCCESS;
}
