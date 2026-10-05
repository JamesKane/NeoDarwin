// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's GEOM disk ioctls over xnu's DKIOC ioctls, for diskinfo, trim and recoverdisk.
// FreeBSD's GEOM disk ioctls (DIOCG*) over xnu's (DKIOC*, <sys/disk.h>),
// for diskinfo, trim and recoverdisk (P4-21 checkpoint 6,
// docs/architecture/freebsd-parity.md §2.1). Each call answers what the
// FreeBSD ioctl named in its comment answers, from the DKIOC calls
// IOMediaBSDClient (and the md ramdisk driver) implement; patches/ changes
// the programs' ioctl calls to these. GEOM's other attributes (descr,
// ident, attachment, rotation rate, zones, the firmware geometry) have no
// xnu counterpart.
#ifndef ND_DISK_H
#define ND_DISK_H
#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <fcntl.h>

// O_DIRECT: xnu has none. A raw disk node (/dev/rdiskN) bypasses the
// buffer cache already; F_NOCACHE is the per-descriptor equivalent for
// files, which these programs only read to size.
#ifndef O_DIRECT
#define O_DIRECT 0
#endif

int nd_diocgmediasize(int fd, off_t *mediasize);     // DIOCGMEDIASIZE: block count * block size
int nd_diocgsectorsize(int fd, u_int *sectorsize);   // DIOCGSECTORSIZE: DKIOCGETBLOCKSIZE
int nd_diocgstripesize(int fd, off_t *stripesize);   // DIOCGSTRIPESIZE: the physical block size when larger, else 0
int nd_diocgdelete(int fd, const off_t range[2]);    // DIOCGDELETE: DKIOCUNMAP of one extent
bool nd_candelete(int fd);                           // GEOM::candelete: DKIOCGETFEATURES' DK_FEATURE_UNMAP
int nd_diocgflush(int fd);                           // DIOCGFLUSH: DKIOCSYNCHRONIZE of the whole media
int nd_diocgphyspath(int fd, char *buf, size_t len); // DIOCGPHYSPATH: DKIOCGETFIRMWAREPATH
#endif
