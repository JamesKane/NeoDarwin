// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for FreeBSD's C rtsold, which includes it.
//
// FreeBSD's <libcasper_service.h>, for rtsold: only the WITH_CASPER halves
// of its services use it (CREATE_SERVICE), and NeoDarwin builds without.
#ifndef ND_RTSOLD_LIBCASPER_SERVICE_H
#define ND_RTSOLD_LIBCASPER_SERVICE_H
#include <libcasper.h>
#endif
