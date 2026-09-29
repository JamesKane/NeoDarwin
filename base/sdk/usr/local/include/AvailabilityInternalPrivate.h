/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: expressibility: a C header standing in for one from Apple's internal SDK.
 *
 * Stands in for the internal SDK's AvailabilityInternalPrivate.h, which the
 * public SDK's Availability headers include when present. Apple's private
 * headers use two things from it: availability spellings for platforms the
 * public macros lack (bridgeos, and xros on older SDKs), and the SPI_*
 * annotations. NeoDarwin builds for one platform, so SPI annotations expand
 * to nothing and the extra platforms map to the public spellings.
 *
 * Apple's own copy, generated from AvailabilityVersions-155, can't be used:
 * its macros must match the public Availability.h of the same release, and
 * staging that whole 26.0 layer breaks under the host's newer clang and SDK
 * (docs/base/libsystem.md §4).
 */
#ifndef __AVAILABILITY_INTERNAL_PRIVATE__
#define __AVAILABILITY_INTERNAL_PRIVATE__

#define __API_AVAILABLE_PLATFORM_bridgeos(x) bridgeos,introduced=x
#define __API_DEPRECATED_PLATFORM_bridgeos(x, y) bridgeos,introduced=x,deprecated=y
#define __API_UNAVAILABLE_PLATFORM_bridgeos bridgeos,unavailable
#ifndef __API_AVAILABLE_PLATFORM_xros
#define __API_AVAILABLE_PLATFORM_xros(x) visionos,introduced=x
#define __API_DEPRECATED_PLATFORM_xros(x, y) visionos,introduced=x,deprecated=y
#define __API_UNAVAILABLE_PLATFORM_xros visionos,unavailable
#endif

#ifndef __SPI_AVAILABLE
#define __SPI_AVAILABLE(...)
#endif
#ifndef __SPI_DEPRECATED
#define __SPI_DEPRECATED(...)
#endif
#ifndef __SPI_DEPRECATED_WITH_REPLACEMENT
#define __SPI_DEPRECATED_WITH_REPLACEMENT(...)
#endif
#ifndef SPI_AVAILABLE
#define SPI_AVAILABLE(...)
#endif
#ifndef SPI_DEPRECATED
#define SPI_DEPRECATED(...)
#endif

#endif /* __AVAILABILITY_INTERNAL_PRIVATE__ */
