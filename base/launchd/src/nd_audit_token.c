// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: one call of libbsm's C interface, linked into launchd-842's C sources.
//
// launchd links libbsm (/usr/lib/libbsm.0.dylib, OpenBSM with Apple's
// additions) for audit_token_to_au32() alone: runtime.c reads each request's
// sender from the audit trailer. NeoDarwin doesn't build libbsm, so launchd
// carries the one call, hidden like the rest of its symbols. It reads the
// token as libbsm does, in the layout xnu fills in (bsd/kern/kern_prot.c,
// proc_calc_audit_token): audit user ID, effective user and group IDs, real
// user and group IDs, process ID, audit session ID, process ID version.
// The terminal ID is no longer in the token: as <bsm/libbsm.h> documents,
// the last parameter receives the process ID version (as its port).

#include <bsm/libbsm.h>
#include <string.h>

__attribute__((visibility("hidden"))) void
audit_token_to_au32(audit_token_t atoken, uid_t *auidp, uid_t *euidp, gid_t *egidp, uid_t *ruidp, gid_t *rgidp,
    pid_t *pidp, au_asid_t *asidp, au_tid_t *tidp)
{
	if (auidp != NULL) {
		*auidp = (uid_t)atoken.val[0];
	}
	if (euidp != NULL) {
		*euidp = (uid_t)atoken.val[1];
	}
	if (egidp != NULL) {
		*egidp = (gid_t)atoken.val[2];
	}
	if (ruidp != NULL) {
		*ruidp = (uid_t)atoken.val[3];
	}
	if (rgidp != NULL) {
		*rgidp = (gid_t)atoken.val[4];
	}
	if (pidp != NULL) {
		*pidp = (pid_t)atoken.val[5];
	}
	if (asidp != NULL) {
		*asidp = (au_asid_t)atoken.val[6];
	}
	if (tidp != NULL) {
		memset(tidp, 0, sizeof(*tidp));
		tidp->port = (dev_t)atoken.val[7];
	}
}
