/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: included by Apple's C++ sources (IOPCIFamily). */
/*
 * The two platform-function names IOPCIFamily's arm64 code asks an
 * IODARTMapper for (Apple silicon's IOMMU; the header ships with the closed
 * DART driver). IOPCIFamily only calls them on a mapper whose class is
 * IODARTMapper, which NeoDarwin never has, so the strings are never sent;
 * they only have to compile. An SMMUv3 mapper (docs/kernel/pci.md) would
 * answer its own keys.
 */

#ifndef _ND_IOKIT_DART_IODARTKEYS_H_
#define _ND_IOKIT_DART_IODARTKEYS_H_

#define kIODARTFunctionGetNumAllocations "IODARTGetNumAllocations"
#define kIODARTFunctionRetrieveVMLimits  "IODARTRetrieveVMLimits"

#endif /* _ND_IOKIT_DART_IODARTKEYS_H_ */
