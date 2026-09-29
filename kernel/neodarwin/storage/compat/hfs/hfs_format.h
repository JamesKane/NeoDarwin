// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C++ AppleFileSystemDriver, which includes it.
//
// <hfs/hfs_format.h> is in the SDK; in the kernel it is hfs-704's own copy,
// built in at libkern/ndhfs/hfs (patch 0015), which the storage objects see
// through -I$(SRCROOT)/libkern/ndhfs/hfs.
#include <core/hfs_format.h>
