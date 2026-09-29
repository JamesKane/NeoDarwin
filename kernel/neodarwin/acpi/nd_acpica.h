/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: expressibility: ACPICA's interface is C headers; this includes them from C and IOKit C++. */
/*
 * ACPICA's public interface (acpi.h: types, acpixf.h, acpiosxf.h) for
 * NeoDarwin's kernel files, configured by acnd.h.
 */

#ifndef _ND_ACPICA_H
#define _ND_ACPICA_H

#include "acnd.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "acpi.h"

/* nd_acpi_osl.c: the RSDP of the copy neoboot made (/chosen acpi-rsdp), or 0. */
ACPI_PHYSICAL_ADDRESS nd_acpi_rsdp(void);

#ifdef __cplusplus
}
#endif

#endif /* _ND_ACPICA_H */
