/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: expressibility: an IOKit interface header, included by C++ kernel code. */
/*
 * The part of Apple's <IOKit/acpi/IOACPITypes.h> (Kernel.framework, Intel
 * era) that NeoDarwin's ACPI platform implements: the ACPI registry plane
 * and the property keys of an IOACPIPlatformDevice. Written from the
 * published interface; docs/kernel/acpi.md.
 */

#ifndef _IOKIT_IOACPITYPES_H
#define _IOKIT_IOACPITYPES_H

#include <IOKit/IORegistryEntry.h>

/* IOACPIPlane: every nub, in the ACPI namespace's hierarchy. */
extern const IORegistryPlane *gIOACPIPlane;
extern const OSSymbol *gIOACPIHardwareIDKey;    /* "_HID" */
extern const OSSymbol *gIOACPIUniqueIDKey;      /* "_UID" */
extern const OSSymbol *gIOACPIAddressKey;       /* "_ADR" */
extern const OSSymbol *gIOACPIDeviceStatusKey;  /* "_STA" */

#define kIOACPIPlane "IOACPIPlane"

#endif /* _IOKIT_IOACPITYPES_H */
