/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for launchd's bootstrap_priv.h; current launchd and libxpc are
 * not published. libsystem_asl and libsystem_notify call bootstrap_look_up2().
 * The flag values are launchd-842's (the last published launchd, which
 * NeoDarwin ports in P1-08), so NeoDarwin's code agrees with that launchd.
 * Apple's current binaries use different values (libsystem_asl passes 8 for
 * a privileged lookup); only NeoDarwin's libraries and launchd read these.
 */
#ifndef __BOOTSTRAP_PRIVATE_H__
#define __BOOTSTRAP_PRIVATE_H__

#include <servers/bootstrap.h>
#include <stdint.h>
#include <sys/types.h>

__BEGIN_DECLS

#define BOOTSTRAP_PER_PID_SERVICE   (1 << 0)
#define BOOTSTRAP_PRIVILEGED_SERVER (1 << 1)
#define BOOTSTRAP_FORCE_LOCAL       (1 << 2)
#define BOOTSTRAP_SPECIFIC_INSTANCE (1 << 3)
#define BOOTSTRAP_STRICT_CHECKIN    (1 << 4)
#define BOOTSTRAP_STRICT_LOOKUP     (1 << 5)

kern_return_t bootstrap_look_up2(mach_port_t bp, const name_t service_name, mach_port_t *sp,
    pid_t target_pid, uint64_t flags);

__END_DECLS

#endif /* __BOOTSTRAP_PRIVATE_H__ */
