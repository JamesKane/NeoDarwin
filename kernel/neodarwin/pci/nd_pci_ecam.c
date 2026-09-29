/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: called from ACPICA's C OS layer and from IOKit C++, and calls XNU's C mapping interfaces. */
/*
 * The ECAM registry and configuration access (nd_pci.h, docs/kernel/pci.md).
 *
 * Windows are added from thread context (the ACPI platform's MCFG pass, a
 * host bridge's start) and never removed. A bus is mapped the first time it
 * is touched: one MiB of device memory (nGnRnE, VM_WIMG_IO) through
 * ml_io_map_unmappable, which maps in kernel_map. ml_io_map would map in
 * the I/O submap, which has 8 MiB in all (osfmk/arm/io_map.c): eight buses.
 * Lookups take no lock; the window count and each bus's mapping are
 * published with release stores.
 */

#include "nd_pci.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>

/* osfmk/arm/machine_routines.h, which IOKit's C files do not see. */
extern vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, uint32_t flags);
extern boolean_t ml_at_interrupt_context(void);
extern int printf(const char *format, ...) __printflike(1, 2);

/* osfmk/arm/memory_types.h: VM_WIMG_IO, device nGnRnE. */
#define ND_VM_WIMG_IO    0x7
#define ND_ECAM_BUS_SIZE (1u << 20)
#define ND_ECAM_WINDOWS  16

struct nd_ecam_window {
	uint16_t segment;
	uint8_t first_bus, last_bus;
	uint64_t base;                  /* bus 0 of the segment */
	uintptr_t bus_va[256];          /* 0 until mapped */
};

static struct nd_ecam_window nd_ecam[ND_ECAM_WINDOWS];
static unsigned int nd_ecam_count;
static IOLock *nd_ecam_lock;

static struct nd_ecam_window *
nd_ecam_find(uint16_t segment, uint8_t bus)
{
	unsigned int n = __atomic_load_n(&nd_ecam_count, __ATOMIC_ACQUIRE);
	for (unsigned int i = 0; i < n; i++) {
		struct nd_ecam_window *w = &nd_ecam[i];
		if (w->segment == segment && w->first_bus <= bus && bus <= w->last_bus) {
			return w;
		}
	}
	return NULL;
}

bool
nd_pci_ecam_add(uint16_t segment, uint8_t first_bus, uint8_t last_bus, uint64_t base)
{
	if (base == 0 || first_bus > last_bus) {
		return false;
	}
	if (nd_ecam_lock == NULL) {
		/* The first call comes from the ACPI platform's thread, alone. */
		IOLock *lock = IOLockAlloc();
		if (lock == NULL) {
			return false;
		}
		IOLock *expected = NULL;
		if (!__atomic_compare_exchange_n(&nd_ecam_lock, &expected, lock, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			IOLockFree(lock);
		}
	}
	bool ok = true;
	IOLockLock(nd_ecam_lock);
	unsigned int n = nd_ecam_count;
	for (unsigned int i = 0; i < n; i++) {
		struct nd_ecam_window *w = &nd_ecam[i];
		if (w->segment == segment && first_bus <= w->last_bus && w->first_bus <= last_bus) {
			if (w->base != base) {
				printf("nd_pci: segment %u buses %u-%u: ECAM 0x%llx disagrees with 0x%llx already registered; kept the first\n",
				    segment, first_bus, last_bus, base, w->base);
			}
			IOLockUnlock(nd_ecam_lock);
			return true;
		}
	}
	if (n == ND_ECAM_WINDOWS) {
		printf("nd_pci: more than %u ECAM windows; segment %u buses %u-%u left out\n",
		    ND_ECAM_WINDOWS, segment, first_bus, last_bus);
		ok = false;
	} else {
		struct nd_ecam_window *w = &nd_ecam[n];
		w->segment = segment;
		w->first_bus = first_bus;
		w->last_bus = last_bus;
		w->base = base;
		__atomic_store_n(&nd_ecam_count, n + 1, __ATOMIC_RELEASE);
	}
	IOLockUnlock(nd_ecam_lock);
	return ok;
}

uint64_t
nd_pci_ecam_base(uint16_t segment, uint8_t bus)
{
	struct nd_ecam_window *w = nd_ecam_find(segment, bus);
	return w != NULL ? w->base : 0;
}

/* The virtual address of a register, mapping its bus if need be; 0 when
 * there is no window or the bus can't be mapped here. */
static uintptr_t
nd_ecam_register(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, uint32_t reg)
{
	if (device > 31 || function > 7 || reg > 0xfff) {
		return 0;
	}
	struct nd_ecam_window *w = nd_ecam_find(segment, bus);
	if (w == NULL) {
		return 0;
	}
	uintptr_t va = __atomic_load_n(&w->bus_va[bus], __ATOMIC_ACQUIRE);
	if (va == 0) {
		if (ml_at_interrupt_context()) {
			return 0;
		}
		IOLockLock(nd_ecam_lock);
		va = w->bus_va[bus];
		if (va == 0) {
			va = ml_io_map_unmappable((vm_offset_t)(w->base + ((uint64_t)bus << 20)), ND_ECAM_BUS_SIZE, ND_VM_WIMG_IO);
			__atomic_store_n(&w->bus_va[bus], va, __ATOMIC_RELEASE);
		}
		IOLockUnlock(nd_ecam_lock);
		if (va == 0) {
			return 0;
		}
	}
	return va + ((uintptr_t)device << 15 | (uintptr_t)function << 12 | reg);
}

static bool
nd_ecam_width_ok(uint32_t reg, unsigned int bytes)
{
	return (bytes == 1 || bytes == 2 || bytes == 4) && (reg & (bytes - 1)) == 0;
}

bool
nd_pci_config_read(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function,
    uint32_t reg, unsigned int bytes, uint32_t *value)
{
	uintptr_t a = nd_ecam_width_ok(reg, bytes) ? nd_ecam_register(segment, bus, device, function, reg) : 0;
	if (a == 0) {
		*value = bytes == 4 ? 0xffffffffu : (1u << (8 * bytes)) - 1;
		return false;
	}
	switch (bytes) {
	case 1: *value = *(volatile uint8_t *)a; break;
	case 2: *value = *(volatile uint16_t *)a; break;
	default: *value = *(volatile uint32_t *)a; break;
	}
	return true;
}

bool
nd_pci_config_write(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function,
    uint32_t reg, unsigned int bytes, uint32_t value)
{
	uintptr_t a = nd_ecam_width_ok(reg, bytes) ? nd_ecam_register(segment, bus, device, function, reg) : 0;
	if (a == 0) {
		return false;
	}
	switch (bytes) {
	case 1: *(volatile uint8_t *)a = (uint8_t)value; break;
	case 2: *(volatile uint16_t *)a = (uint16_t)value; break;
	default: *(volatile uint32_t *)a = value; break;
	}
	/* Device memory (nGnRnE) is ordered, but a following access to another
	 * device through its BARs is not ordered against this one by the
	 * memory type alone: complete the write first. */
	__builtin_arm_dsb(0xf);
	return true;
}
