// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: replaces the C library OpenZFS's zpool links (lib/os/macos/libdiskmgt), in its C ABI.
/*
 * NeoDarwin's libdiskmgt for zpool: the in-use checks of OpenZFS on OS X's
 * lib/os/macos/libdiskmgt, which ask DiskArbitration and IOKit about
 * CoreStorage, partitions and filesystems. NeoDarwin has neither in
 * userland, so a device is in use when it, or a slice of it, is mounted
 * (getmntinfo(3)); pools in use are found by libzfs itself.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <libdiskmgt.h>

void
libdiskmgt_init(void)
{
}

void
libdiskmgt_fini(void)
{
}

/* NeoDarwin keeps no swap files in a directory of their own yet. */
int
dm_in_swap_dir(const char *dev_name)
{
	(void) dev_name;
	return (0);
}

/* True if MOUNTED is DEV, or a slice of the whole disk DEV (disk1 -> disk1s2). */
static int
same_or_slice(const char *mounted, const char *dev)
{
	size_t n = strlen(dev);

	if (strncmp(mounted, "/dev/r", 6) == 0 && strncmp(dev, "/dev/r", 6) != 0)
		mounted += 1;	/* compare /dev/rdiskN as /dev/diskN */
	if (strncmp(mounted, dev, n) != 0)
		return (0);
	return (mounted[n] == '\0' || mounted[n] == 's');
}

int
dm_inuse(char *dev_name, char **msg, dm_who_type_t who, int *errp)
{
	struct statfs *mnt;
	const char *dev = dev_name;
	char raw[MAXPATHLEN];
	int i, n;

	(void) who;
	*errp = 0;
	*msg = NULL;
	if (strncmp(dev, "/dev/rdisk", 10) == 0) {	/* the block device's name */
		(void) snprintf(raw, sizeof (raw), "/dev/%s", dev + 6);
		dev = raw;
	}
	if ((n = getmntinfo(&mnt, MNT_NOWAIT)) <= 0)
		return (0);
	for (i = 0; i < n; i++) {
		if (!same_or_slice(mnt[i].f_mntfromname, dev))
			continue;
		if (asprintf(msg, "%s is mounted on %s (%s)\n", mnt[i].f_mntfromname,
		    mnt[i].f_mntonname, mnt[i].f_fstypename) < 0)
			*msg = NULL;
		return (1);
	}
	return (0);
}
