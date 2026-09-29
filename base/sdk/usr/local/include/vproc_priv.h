/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for launchd's private <vproc_priv.h>; current launchd and libxpc
 * are not published. libdyld asks vproc_swap_integer() whether launchd
 * manages the process (LibSystemHelpers::isLaunchdOwned), and dyld includes
 * the header without using it. The key values are launchd-842's (the last
 * published launchd, which NeoDarwin ports in P1-08), as bootstrap_priv.h's
 * are. vproc_t and vproc_err_t come from the SDK's public <vproc.h>.
 * libxpc is a stand-in until then (base/standins/libxpc), and answers every
 * key with an error, as a process launchd doesn't know gets.
 */
#ifndef __VPROC_PRIVATE_H__
#define __VPROC_PRIVATE_H__

#include <stdint.h>
#include <sys/cdefs.h>
#include <vproc.h>

__BEGIN_DECLS

typedef enum {
	VPROC_GSK_ZERO,
	VPROC_GSK_LAST_EXIT_STATUS,
	VPROC_GSK_GLOBAL_ON_DEMAND,
	VPROC_GSK_MGR_UID,
	VPROC_GSK_MGR_PID,
	VPROC_GSK_IS_MANAGED,
} vproc_gsk_t;

// Swaps the value of key for the process vp (NULL: the caller): stores the
// old value in *outval and sets inval's, either pointer may be NULL.
// Returns NULL on success, or an error token.
vproc_err_t vproc_swap_integer(vproc_t vp, vproc_gsk_t key, int64_t *inval, int64_t *outval);

__END_DECLS

#endif /* __VPROC_PRIVATE_H__ */
