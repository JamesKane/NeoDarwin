/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: a C interface shared by ACPICA's OS layer (C) and the PCI host bridge (IOKit C++). */
/*
 * PCI configuration space through ECAM (PCIe 6.0 §7.2.2), by segment
 * (docs/kernel/pci.md). One registry for the whole kernel: the ACPI
 * platform registers every MCFG allocation as soon as ACPICA has loaded the
 * tables, before any AML runs, so that PCI_Config operation regions, _OSC
 * and _DSM reach the hardware; each host bridge then adds its own window
 * (_CBA, else the MCFG entry for its _SEG and _BBN).
 *
 * The configuration space of bus B is at base + (B << 20), where base is
 * the address of bus 0 of the segment, as MCFG and _CBA give it; device D,
 * function F at + (D << 15 | F << 12). Buses are mapped one MiB at a time,
 * as device memory, the first time they are touched, so a segment with 256
 * buses costs address space only for the buses that exist.
 */

#ifndef _ND_PCI_H
#define _ND_PCI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Adds the ECAM window of `segment`, buses first..last, whose bus 0 would
 * be at `base`. A window that overlaps one already registered for the same
 * segment is ignored (MCFG and _CBA normally agree). Returns true when the
 * window is registered or was already. Thread context only. */
bool nd_pci_ecam_add(uint16_t segment, uint8_t first_bus, uint8_t last_bus, uint64_t base);

/* The ECAM base (of bus 0) registered for segment/bus, or 0. */
uint64_t nd_pci_ecam_base(uint16_t segment, uint8_t bus);

/* Configuration read and write of 1, 2 or 4 bytes at register `reg`
 * (0..4095) of segment:bus:device.function. The access must be naturally
 * aligned; ECAM performs it as one access of that width. Returns false,
 * and reads all ones, when no ECAM window covers the bus, the arguments are
 * out of range, or the bus is not mapped yet and the caller is in
 * interrupt context. */
bool nd_pci_config_read(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function,
    uint32_t reg, unsigned int bytes, uint32_t *value);
bool nd_pci_config_write(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function,
    uint32_t reg, unsigned int bytes, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* _ND_PCI_H */
