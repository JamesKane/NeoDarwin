// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's GEOM disk ioctls over xnu's DKIOC ioctls, for diskinfo, trim and recoverdisk.
// FreeBSD's GEOM disk ioctls over xnu's (nd_disk.h).
#include "nd_disk.h"

#include <sys/disk.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

int
nd_diocgmediasize(int fd, off_t *mediasize)
{
	uint32_t bs;
	uint64_t n;

	if (ioctl(fd, DKIOCGETBLOCKSIZE, &bs) < 0 || ioctl(fd, DKIOCGETBLOCKCOUNT, &n) < 0)
		return (-1);
	*mediasize = (off_t)(n * bs);
	return (0);
}

int
nd_diocgsectorsize(int fd, u_int *sectorsize)
{
	uint32_t bs;

	if (ioctl(fd, DKIOCGETBLOCKSIZE, &bs) < 0)
		return (-1);
	*sectorsize = bs;
	return (0);
}

int
nd_diocgstripesize(int fd, off_t *stripesize)
{
	uint32_t bs, pbs;

	if (ioctl(fd, DKIOCGETBLOCKSIZE, &bs) < 0 || ioctl(fd, DKIOCGETPHYSICALBLOCKSIZE, &pbs) < 0)
		return (-1);
	// GEOM reports a disk's physical sector as its stripe when it is
	// larger than the logical one (a 512e disk's 4096), and 0 otherwise.
	*stripesize = pbs > bs ? pbs : 0;
	return (0);
}

int
nd_diocgdelete(int fd, const off_t range[2])
{
	dk_extent_t ext = { .offset = (uint64_t)range[0], .length = (uint64_t)range[1] };
	dk_unmap_t um;

	memset(&um, 0, sizeof(um));
	um.extents = &ext;
	um.extentsCount = 1;
	if (ioctl(fd, DKIOCUNMAP, &um) < 0) {
		// FreeBSD answers EOPNOTSUPP where the provider can't delete.
		if (errno == ENOTTY || errno == ENOTSUP)
			errno = EOPNOTSUPP;
		return (-1);
	}
	return (0);
}

bool
nd_candelete(int fd)
{
	uint32_t f;

	return (ioctl(fd, DKIOCGETFEATURES, &f) == 0 && (f & DK_FEATURE_UNMAP) != 0);
}

int
nd_diocgflush(int fd)
{
	dk_synchronize_t s;

	memset(&s, 0, sizeof(s));
	return (ioctl(fd, DKIOCSYNCHRONIZE, &s));
}

int
nd_diocgphyspath(int fd, char *buf, size_t len)
{
	dk_firmware_path_t p;

	memset(&p, 0, sizeof(p));
	if (ioctl(fd, DKIOCGETFIRMWAREPATH, &p) < 0)
		return (-1);
	if (p.path[0] == '\0') {
		errno = ENOENT;
		return (-1);
	}
	strlcpy(buf, p.path, len);
	return (0);
}
