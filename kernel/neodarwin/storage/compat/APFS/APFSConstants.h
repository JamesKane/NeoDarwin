// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C++ AppleFileSystemDriver, which includes it.
//
// APFS's constants, as AppleFileSystemDriver-31 uses them: the header ships
// with the closed APFS kext. NeoDarwin has no APFS, so no IOMedia carries
// these properties and the APFS paths never match; the values are APFS's
// published ones (APFS Reference, "Volume Roles"; the registry keys an APFS
// volume's IOMedia has on macOS).
#ifndef ND_COMPAT_APFS_APFSCONSTANTS_H
#define ND_COMPAT_APFS_APFSCONSTANTS_H

#define APFS_VOLUME_OBJECT      "AppleAPFSVolume"
#define kAPFSRoleValueKey       "RoleValue"
#define kAPFSVolGroupUUIDKey    "VolGroupUUID"

#define APFS_VOL_ROLE_NONE      0x0000
#define APFS_VOL_ROLE_SYSTEM    0x0001
#define APFS_VOL_ROLE_RECOVERY  0x0004

#endif
