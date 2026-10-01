// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C login(1), which includes it.
//
// libEndpointSecuritySystem's private <EndpointSecuritySystem/ESSubmitSPI.h>
// (closed). login(1) weak-links ess_notify_login_login and
// ess_notify_login_logout and calls each only if it is non-null, as it is
// when the library isn't there. NeoDarwin has no EndpointSecurity: both are
// null, so login records its BSM audit events alone.
#ifndef ND_ESSUBMITSPI_H
#define ND_ESSUBMITSPI_H
#include <stdbool.h>
#include <sys/types.h>

#define ess_notify_login_login \
	((void (*)(bool success, const char *failure_message, const char *username, const uid_t *uid))0)
#define ess_notify_login_logout ((void (*)(const char *username, uid_t uid))0)
#endif
