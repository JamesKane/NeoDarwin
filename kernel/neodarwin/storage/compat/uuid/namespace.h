// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C++ AppleFileSystemDriver, which includes it.
//
// The name space of HFS+ volume UUIDs: hfs.util and AppleFileSystemDriver
// make a volume's UUID from the 64-bit identifier in its Finder information
// as a version 3 (MD5) UUID in this name space, B3E20F39-F292-11D6-97A4-
// 00306543ECAC. The header is in Libc's private headers, not the kernel's.
#ifndef ND_COMPAT_UUID_NAMESPACE_H
#define ND_COMPAT_UUID_NAMESPACE_H

#include <uuid/uuid.h>

static const uuid_t kFSUUIDNamespaceSHA1 = {
	0xB3, 0xE2, 0x0F, 0x39, 0xF2, 0x92, 0x11, 0xD6, 0x97, 0xA4, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC
};

#endif
