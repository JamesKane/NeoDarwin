// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's VFS registration interface and the hfs kext's entry points are C.
//
// Starts HFS+, built into the kernel from Apple's hfs-704 (APSL; patch 0015).
// On Apple systems the HFSEncodings and hfs kexts load once IOKitBSDInit()
// publishes IOBSD, and their start() routines set up the encoding converters
// and register the filesystem with vfs_fsadd(). With no kext loading at this
// stage, bsd_init() calls nd_hfs_start() right after bsd_autoconf(), so HFS+
// is registered before vfs_mountroot() looks for a root filesystem; a kext
// matched through IOKit would start asynchronously and race it.

#include <sys/mount.h>
#include <sys/vnode.h>
#include <kern/debug.h>
#include <libkern/OSKextLib.h>

extern struct vnodeopv_desc hfs_vnodeop_opv_desc;
extern struct vnodeopv_desc hfs_specop_opv_desc;
extern struct vnodeopv_desc hfs_fifoop_opv_desc;
extern struct vfsops hfs_vfsops;

void hfs_converterinit(void);
void hfs_init_zones(void);
void hfs_sysctl_register(void);
void nd_hfs_start(void);

#ifndef VFS_TBLVNOP_SECLUDE_RENAME
#define VFS_TBLVNOP_SECLUDE_RENAME 0
#endif

static vfstable_t nd_hfs_vfs;

// A kext gets its own OSKextGetCurrentLoadTag() from its build; HFS calls it
// to hold itself loaded while a volume is mounted. Code built into the
// kernel belongs to the kernel, load tag 0.
OSKextLoadTag
OSKextGetCurrentLoadTag(void)
{
	return 0;
}

// As com_apple_filesystems_hfs_encodings::start() and
// com_apple_filesystems_hfs::start() (core/hfs_iokit.cpp) do, less the
// origin-cache sizing from hw.physicalcpu, whose defaults suit one CPU.
void
nd_hfs_start(void)
{
	hfs_converterinit();

	struct vnodeopv_desc *op_descs[] = {
		&hfs_vnodeop_opv_desc,
		&hfs_specop_opv_desc,
		&hfs_fifoop_opv_desc,
	};
	struct vfs_fsentry vfe = {
		.vfe_vfsops = &hfs_vfsops,
		.vfe_vopcnt = sizeof(op_descs) / sizeof(op_descs[0]),
		.vfe_opvdescs = op_descs,
		.vfe_fsname = "hfs",
		.vfe_flags = VFS_TBLNOTYPENUM | VFS_TBLLOCALVOL | VFS_TBLREADDIR_EXTENDED
	    | VFS_TBL64BITREADY | VFS_TBLVNOP_PAGEOUTV2 | VFS_TBLVNOP_PAGEINV2
	    | VFS_TBLTHREADSAFE | VFS_TBLCANMOUNTROOT | VFS_TBLVNOP_SECLUDE_RENAME
	    | VFS_TBLNATIVEXATTR,
	};
	int error = vfs_fsadd(&vfe, &nd_hfs_vfs);
	if (error != 0) {
		panic("NeoDarwin: vfs_fsadd for HFS+ failed: %d", error);
	}

	hfs_init_zones();
	hfs_sysctl_register();
}
