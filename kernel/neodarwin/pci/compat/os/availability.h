/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: included by Apple's C++ sources (IOPCIFamily). */
/*
 * <os/availability.h> for IOPCIFamily's IOPCIFamilyDefinitions.h, which
 * includes for the availability attributes on a few of IOPCIDevice's
 * methods. xnu's kernel search path has EXTERNAL_HEADERS/Availability.h but
 * no os/availability.h (the SDK's user-space header); in a kernel built from
 * source the attributes mean nothing, so they expand to nothing.
 */

#ifndef _ND_OS_AVAILABILITY_H_
#define _ND_OS_AVAILABILITY_H_

#include <Availability.h>

#ifndef API_AVAILABLE
#define API_AVAILABLE(...)
#endif
#ifndef API_UNAVAILABLE
#define API_UNAVAILABLE(...)
#endif
#ifndef API_DEPRECATED
#define API_DEPRECATED(...)
#endif
#ifndef API_DEPRECATED_WITH_REPLACEMENT
#define API_DEPRECATED_WITH_REPLACEMENT(...)
#endif

#endif /* _ND_OS_AVAILABILITY_H_ */
