/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: expressibility: ACPICA's host configuration is a C header of macros its C sources read. */
/*
 * ACPICA's host environment for the NeoDarwin kernel (docs/kernel/acpi.md).
 *
 * ACPICA picks its platform header in platform/acenv.h from the compiler's
 * predefined macros; clang defines __APPLE__, which selects acmacosx.h and
 * through it aclinux.h, the Linux user-space configuration. This header is
 * read first instead, without patching ACPICA: every ACPICA object is
 * compiled with `-include acnd.h` (patch 0018), and NeoDarwin's own files
 * include it through nd_acpica.h. It claims acmacosx.h's include guard, so
 * that header and aclinux.h are never read, and gives the configuration
 * they would have.
 *
 *  - 64-bit, LP64, hardware-reduced ACPI (every SBSA/SBBR machine): no SCI,
 *    GPE blocks, fixed events, PM1 registers, FACS or global lock.
 *  - No C library: ACPICA uses its own (utclib.c, utprint.c). With
 *    ND_ACPICA_CORE, set only for ACPICA's objects, their names move to
 *    AcpiNd* so they neither clash with the kernel's nor see its fortified
 *    string.h. NeoDarwin's files (nd_acpi_osl.c and the IOKit classes) see
 *    only ACPICA's public headers and use the kernel's library.
 *  - Local object caches (utcache.c), binary-semaphore mutexes, no debugger
 *    or disassembler, no debug output (errors, warnings and table headers
 *    are still printed through AcpiOsVprintf).
 */

#ifndef _ACND_H
#define _ACND_H

/* acmacosx.h (and aclinux.h behind it) stay unread: this is the host. */
#define __ACMACOSX_H__ 1
/* acgccex.h only undefines strchr, which would undo the renaming below. */
#define __ACGCCEX_H__ 1
#define _NEODARWIN_ACPICA 1

#define ACPI_MACHINE_WIDTH          64
#define COMPILER_DEPENDENT_INT64    long long
#define COMPILER_DEPENDENT_UINT64   unsigned long long
#define ACPI_USE_DO_WHILE_0
#define ACPI_USE_LOCAL_CACHE
#define ACPI_REDUCED_HARDWARE       1

/* ACPI_SIZE is the natural width, so ACPI_CPU_FLAGS holds an IOInterruptState. */
#define ACPI_CPU_FLAGS              unsigned long
#define ACPI_UINTPTR_T              unsigned long

/* The kernel's stdarg.h is the compiler's; ACPICA's objects need nothing else. */
#include <stdarg.h>

#ifdef ND_ACPICA_CORE
/*
 * ACPICA's C library (acclib.h, utclib.c, utprint.c) under its own names.
 * The ctype macros acclib.h defines (isdigit and friends) read ACPICA's
 * AcpiGbl_Ctypes table and need no renaming.
 */
#define memcmp      AcpiNdMemcmp
#define memcpy      AcpiNdMemcpy
#define memmove     AcpiNdMemmove
#define memset      AcpiNdMemset
#define strcat      AcpiNdStrcat
#define strchr      AcpiNdStrchr
#define strcmp      AcpiNdStrcmp
#define strcpy      AcpiNdStrcpy
#define strlen      AcpiNdStrlen
#define strncat     AcpiNdStrncat
#define strncmp     AcpiNdStrncmp
#define strncpy     AcpiNdStrncpy
#define strpbrk     AcpiNdStrpbrk
#define strstr      AcpiNdStrstr
#define strtok      AcpiNdStrtok
#define strtoul     AcpiNdStrtoul
#define tolower     AcpiNdTolower
#define toupper     AcpiNdToupper
#define vsnprintf   AcpiNdVsnprintf
#define snprintf    AcpiNdSnprintf
#define sprintf     AcpiNdSprintf
#endif

#endif /* _ACND_H */
