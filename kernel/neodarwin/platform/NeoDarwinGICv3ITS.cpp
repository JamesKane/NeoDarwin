// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: an IOKit libkern C++ object beside NeoDarwinGICv3, driving the ITS through MMIO and memory it reads.
//
// A GICv3 Interrupt Translation Service (Arm IHI 0069 chapter 5,
// docs/kernel/gic-its.md). PCI MSIs are writes of an EventID to
// GITS_TRANSLATER; the ITS takes the DeviceID from the write's requester
// (the PCIe requester ID, through the IORT's mappings) and looks both up in
// tables in memory: the device table gives the device's Interrupt
// Translation Table (ITT), the ITT the LPI and collection for the event,
// and the collection table the redistributor the LPI goes to.
//
// Software fills those tables only through commands, in a queue in memory
// (GITS_CBASER) that the ITS consumes from GITS_CREADR up to GITS_CWRITER.
// It provides the memory: GITS_BASER<n> says, per table, the entry size and
// type; we choose the page size (64, 16 or 4 KiB, whichever the register
// keeps), flat or two-level for the device table (two-level when the flat
// one would exceed 256 KiB and the ITS supports it: level-2 pages are then
// allocated as DeviceIDs are mapped), and the attributes (Inner Shareable,
// write-back, downgraded to non-cacheable, with cache maintenance on every
// write we make, if the shareability reads back 0). FreeBSD's
// sys/arm64/arm64/gicv3_its.c and Linux's irq-gic-v3-its.c were the
// references for the sequences.
//
// Collections: one per CPU the kernel runs, collection N for logical CPU N,
// mapped (MAPC) to that CPU's redistributor, by physical address when
// GITS_TYPER.PTA is 1, else by processor number. MSIs target the boot CPU's
// collection for now; the others exist for affinity later.

#include "NeoDarwinPlatform.h"
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>

extern "C" vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define super OSObject
OSDefineMetaClassAndStructors(NeoDarwinGICv3ITS, OSObject);

// Registers (Arm IHI 0069 12.19).
#define GITS_CTLR               0x0000
#define GITS_CTLR_ENABLED       0x1
#define GITS_CTLR_QUIESCENT     0x80000000u
#define GITS_IIDR               0x0004
#define GITS_TYPER              0x0008
#define GITS_CBASER             0x0080
#define GITS_CWRITER            0x0088
#define GITS_CREADR             0x0090
#define GITS_CREADR_STALLED     0x1
#define GITS_BASER(n)           (0x0100 + 8 * (n))
#define GITS_SIZE               0x20000   // ITS_base and translation frames

#define TYPER_PHYSICAL          (1ULL << 0)
#define TYPER_ITT_ESZ(t)        ((uint32_t)(((t) >> 4) & 0xf) + 1)
#define TYPER_IDBITS(t)         ((uint32_t)(((t) >> 8) & 0x1f) + 1)
#define TYPER_DEVBITS(t)        ((uint32_t)(((t) >> 13) & 0x1f) + 1)
#define TYPER_PTA               (1ULL << 19)
#define TYPER_HCC(t)            ((uint32_t)(((t) >> 24) & 0xff))

#define BASER_VALID             (1ULL << 63)
#define BASER_INDIRECT          (1ULL << 62)
#define BASER_INNER_SHIFT       59
#define BASER_INNER_MASK        (7ULL << 59)
#define BASER_TYPE(b)           ((uint32_t)(((b) >> 56) & 7))
#define BASER_ESZ(b)            ((uint32_t)(((b) >> 48) & 0x1f) + 1)
#define BASER_PA_MASK           0x0000fffffffff000ULL   // [47:12]; PA[51:48] would go in [15:12] with 64 KiB pages
#define BASER_SHARE_SHIFT       10
#define BASER_SHARE_MASK        (3ULL << 10)
#define BASER_PSZ_SHIFT         8
#define BASER_PSZ_MASK          (3ULL << 8)
#define BASER_SIZE_MASK         0xffULL
#define BASER_TYPE_DEVICES      1
#define BASER_TYPE_VPES         2
#define BASER_TYPE_COLLECTIONS  4

#define CACHE_NC                1ULL
#define CACHE_RAWAWB            7ULL
#define SHARE_INNER             1ULL

// Commands (Arm IHI 0069 5.3): 32 bytes, opcode in DW0 [7:0].
#define CMD_MOVI                0x01
#define CMD_INT                 0x03
#define CMD_CLEAR               0x04
#define CMD_SYNC                0x05
#define CMD_MAPD                0x08
#define CMD_MAPC                0x09
#define CMD_MAPTI               0x0a
#define CMD_INV                 0x0c
#define CMD_INVALL              0x0d
#define CMD_DISCARD             0x0f
#define CMD_SIZE                32
#define QUEUE_SIZE              0x10000    // 64 KiB: 2048 commands
#define ITT_ALIGN               256
#define FLAT_DEVICE_TABLE_MAX   (256 * 1024)
#define COMMAND_TIMEOUT_US      1000000

static inline uint32_t
rd32(vm_offset_t a)
{
	return *(volatile uint32_t *)a;
}

static inline uint64_t
rd64(vm_offset_t a)
{
	return *(volatile uint64_t *)a;
}

static inline void
wr32(vm_offset_t a, uint32_t v)
{
	*(volatile uint32_t *)a = v;
}

static inline void
wr64(vm_offset_t a, uint64_t v)
{
	*(volatile uint64_t *)a = v;
}

static const char *
baser_type_name(uint32_t type)
{
	switch (type) {
	case BASER_TYPE_DEVICES: return "device";
	case BASER_TYPE_VPES: return "vPE";
	case BASER_TYPE_COLLECTIONS: return "collection";
	default: return "unknown";
	}
}

NeoDarwinGICv3ITS *
NeoDarwinGICv3ITS::withAddress(NeoDarwinGICv3 *gic, uint64_t base, uint32_t id, char *why, size_t whyLength)
{
	NeoDarwinGICv3ITS *its = OSTypeAlloc(NeoDarwinGICv3ITS);
	if (its == NULL || !its->init()) {
		OSSafeReleaseNULL(its);
		snprintf(why, whyLength, "no memory");
		return NULL;
	}
	if (!its->start(gic, base, id, why, whyLength)) {
		its->release();
		return NULL;
	}
	return its;
}

void
NeoDarwinGICv3ITS::free(void)
{
	// An ITS that was set up stays in use until reboot; this runs only for
	// one that failed, which is disabled again first.
	if (regs != 0) {
		wr32(regs + GITS_CTLR, rd32(regs + GITS_CTLR) & ~GITS_CTLR_ENABLED);
	}
	for (unsigned int i = 0; i < 8; i++) {
		if (regs != 0) {
			wr64(regs + GITS_BASER(i), 0);
		}
		ndGICTableFree(&tables[i]);
	}
	if (regs != 0) {
		wr64(regs + GITS_CBASER, 0);
	}
	ndGICTableFree(&queue);
	if (devL2 != NULL) {
		for (uint32_t i = 0; i < devL1Entries; i++) {
			ndGICTableFree(&devL2[i]);
		}
		IODelete(devL2, NDGICTable, devL1Entries);
	}
	if (devices != NULL) {
		for (unsigned int i = 0; i < deviceCount; i++) {
			ndGICTableFree(&devices[i].itt);
		}
		IODelete(devices, Device, deviceCapacity);
	}
	if (lock != NULL) {
		IOLockFree(lock);
	}
	super::free();
}

bool
NeoDarwinGICv3ITS::start(NeoDarwinGICv3 *g, uint64_t base, uint32_t id, char *why, size_t whyLength)
{
	gic = g;
	itsPhys = base;
	itsID = id;
	devTable = -1;
	lock = IOLockAlloc();
	regs = ml_io_map((vm_offset_t)base, GITS_SIZE);
	if (lock == NULL || regs == 0) {
		snprintf(why, whyLength, "cannot map the ITS at 0x%llx", base);
		return false;
	}
	typer = rd64(regs + GITS_TYPER);
	if ((typer & TYPER_PHYSICAL) == 0) {
		snprintf(why, whyLength, "GITS_TYPER 0x%llx: no physical LPIs", typer);
		return false;
	}
	devBits = TYPER_DEVBITS(typer);
	eventBits = TYPER_IDBITS(typer);
	ittEntrySize = TYPER_ITT_ESZ(typer);
	pta = (typer & TYPER_PTA) != 0;

	// Disabled and quiescent before its tables change (firmware may have
	// used it).
	uint32_t ctlr = rd32(regs + GITS_CTLR);
	if (ctlr & GITS_CTLR_ENABLED) {
		wr32(regs + GITS_CTLR, ctlr & ~GITS_CTLR_ENABLED);
	}
	for (int i = 0; i < COMMAND_TIMEOUT_US && (rd32(regs + GITS_CTLR) & GITS_CTLR_QUIESCENT) == 0; i++) {
		IODelay(1);
	}
	if ((rd32(regs + GITS_CTLR) & GITS_CTLR_QUIESCENT) == 0) {
		snprintf(why, whyLength, "the ITS does not become quiescent (GITS_CTLR 0x%x)", rd32(regs + GITS_CTLR));
		return false;
	}

	for (unsigned int n = 0; n < 8; n++) {
		if (!setupTable(n, why, whyLength)) {
			return false;
		}
	}
	if (devTable < 0) {
		snprintf(why, whyLength, "no GITS_BASER describes a device table");
		return false;
	}
	if (!setupCommandQueue(why, whyLength)) {
		return false;
	}
	wr32(regs + GITS_CTLR, rd32(regs + GITS_CTLR) | GITS_CTLR_ENABLED);
	if ((rd32(regs + GITS_CTLR) & GITS_CTLR_ENABLED) == 0) {
		snprintf(why, whyLength, "GITS_CTLR.Enabled does not stick");
		return false;
	}

	// A collection per CPU, then make sure the redistributors have loaded
	// their LPI configuration (INVALL) and everything is done (SYNC).
	unsigned int mapped = 0;
	for (unsigned int cpu = 0; cpu < gic->cpuCount(); cpu++) {
		uint64_t target = gic->collectionTarget(cpu, pta);
		if (target == ~0ULL) {
			continue;
		}
		uint64_t mapc[4] = { CMD_MAPC, 0, (1ULL << 63) | (target & 0x000fffffffff0000ULL) | cpu, 0 };
		uint64_t invall[4] = { CMD_INVALL, 0, cpu, 0 };
		if (!command(mapc, "MAPC") || !command(invall, "INVALL")) {
			snprintf(why, whyLength, "MAPC for CPU %u failed", cpu);
			return false;
		}
		mapped++;
	}
	if (!sync(gic->bootCPU())) {
		snprintf(why, whyLength, "SYNC after MAPC failed");
		return false;
	}

	uint32_t iidr = rd32(regs + GITS_IIDR);
	snprintf(summary, sizeof(summary),
	    "ITS %u at 0x%llx (IIDR 0x%08x): %u DeviceID bits, %u EventID bits, ITT entries %u bytes, %s; %u collection%s by %s%s",
	    itsID, itsPhys, iidr, devBits, eventBits, ittEntrySize,
	    devIndirect ? "two-level device table" : "flat device table", mapped, mapped == 1 ? "" : "s",
	    pta ? "redistributor address" : "processor number", queueFlush ? "; non-cacheable command queue" : "");
	return true;
}

// Chooses a page size the register keeps: 64 KiB, else 16 KiB, else 4 KiB.
// Only the page size field is written (Valid clear), so nothing is live.
static uint32_t
baser_page_size(vm_offset_t reg, uint64_t val)
{
	static const uint32_t sizes[3] = { 0x10000, 0x4000, 0x1000 };
	static const uint64_t codes[3] = { 2, 1, 0 };
	for (unsigned int i = 0; i < 3; i++) {
		uint64_t v = (val & ~(BASER_PSZ_MASK | BASER_VALID)) | codes[i] << BASER_PSZ_SHIFT;
		wr64(reg, v);
		if (((rd64(reg) & BASER_PSZ_MASK) >> BASER_PSZ_SHIFT) == codes[i]) {
			return sizes[i];
		}
	}
	return 0;
}

bool
NeoDarwinGICv3ITS::setupTable(unsigned int n, char *why, size_t whyLength)
{
	vm_offset_t reg = regs + GITS_BASER(n);
	uint64_t val = rd64(reg);
	uint32_t type = BASER_TYPE(val);
	if (type == 0) {
		return true;                          // not implemented
	}
	uint32_t esz = BASER_ESZ(val);
	if (type != BASER_TYPE_DEVICES && type != BASER_TYPE_COLLECTIONS) {
		wr64(reg, val & ~BASER_VALID);        // vPEs: GICv4, not used
		return true;
	}
	uint32_t psz = baser_page_size(reg, val);
	if (psz == 0) {
		snprintf(why, whyLength, "GITS_BASER%u keeps no page size", n);
		return false;
	}

	// How much table. Devices: an entry per DeviceID, flat or through a
	// level-1 table of 8-byte pointers to pages of entries. Collections: an
	// entry per CPU, at least a page.
	uint64_t entries = type == BASER_TYPE_DEVICES ? (1ULL << devBits) : gic->cpuCount();
	uint64_t flat = entries * esz;
	bool indirect = false;
	uint64_t bytes = flat;
	uint32_t forceFlat = 0;
	PE_parse_boot_argn("nd_its_flat", &forceFlat, sizeof(forceFlat));
	if (type == BASER_TYPE_DEVICES && forceFlat == 0 && (flat > FLAT_DEVICE_TABLE_MAX || flat > 256ULL * psz)) {
		// Try two-level: does the Indirect bit stick?
		wr64(reg, (rd64(reg) & ~BASER_VALID) | BASER_INDIRECT);
		indirect = (rd64(reg) & BASER_INDIRECT) != 0;
		if (indirect) {
			uint64_t perPage = psz / esz;
			bytes = ((entries + perPage - 1) / perPage) * 8;
		}
	}
	uint64_t pages = (bytes + psz - 1) / psz;
	if (pages == 0) {
		pages = 1;
	}
	if (pages > 256) {
		// The largest table the register describes; DeviceIDs past it fail MAPD.
		pages = 256;
	}
	if (!ndGICTableAlloc(&tables[n], (size_t)(pages * psz), psz)) {
		snprintf(why, whyLength, "no memory for the %s table (%llu KiB)", baser_type_name(type), pages * psz / 1024);
		return false;
	}
	uint64_t pszCode = psz == 0x10000 ? 2 : psz == 0x4000 ? 1 : 0;
	uint64_t v = BASER_VALID | (indirect ? BASER_INDIRECT : 0) | CACHE_RAWAWB << BASER_INNER_SHIFT |
	    (val & ((7ULL << 56) | (0x1fULL << 48))) |           // Type and Entry_Size, read-only
	    (tables[n].pa & BASER_PA_MASK) | SHARE_INNER << BASER_SHARE_SHIFT | pszCode << BASER_PSZ_SHIFT | (pages - 1);
	wr64(reg, v);
	uint64_t back = rd64(reg);
	if ((back & BASER_SHARE_MASK) == 0 || gic->forceNonCacheable()) {
		v = (v & ~(BASER_SHARE_MASK | BASER_INNER_MASK)) | CACHE_NC << BASER_INNER_SHIFT;
		wr64(reg, v);
		back = rd64(reg);
		tableFlush[n] = true;
	}
	if ((back & BASER_VALID) == 0 || (back & BASER_PA_MASK) != (tables[n].pa & BASER_PA_MASK)) {
		snprintf(why, whyLength, "GITS_BASER%u does not keep 0x%llx (reads 0x%llx)", n, v, back);
		return false;
	}
	if (type == BASER_TYPE_DEVICES) {
		devTable = (int)n;
		devIndirect = indirect;
		devPageSize = psz;
		devEntrySize = esz;
		if (indirect) {
			devL1Entries = (uint32_t)(pages * psz / 8);
			devL2 = IONewZero(NDGICTable, devL1Entries);
			if (devL2 == NULL) {
				snprintf(why, whyLength, "no memory");
				return false;
			}
		}
	}
	IOLog("NeoDarwinGICv3ITS: ITS %u: GITS_BASER%u: %s table, %u-byte entries, %s%llu x %u KiB pages at 0x%llx, %s\n",
	    itsID, n, baser_type_name(type), esz, indirect ? "two-level, " : "", pages, psz / 1024, tables[n].pa,
	    tableFlush[n] ? "non-shareable, non-cacheable" : "inner shareable, write-back");
	return true;
}

bool
NeoDarwinGICv3ITS::setupCommandQueue(char *why, size_t whyLength)
{
	if (!ndGICTableAlloc(&queue, QUEUE_SIZE, 0x10000)) {
		snprintf(why, whyLength, "no memory for the command queue");
		return false;
	}
	// Size: 4 KiB pages minus one. Inner Shareable, write-back.
	uint64_t v = (1ULL << 63) | CACHE_RAWAWB << BASER_INNER_SHIFT | (queue.pa & 0x000ffffffffff000ULL) |
	    SHARE_INNER << BASER_SHARE_SHIFT | (QUEUE_SIZE / 4096 - 1);
	wr64(regs + GITS_CBASER, v);
	uint64_t back = rd64(regs + GITS_CBASER);
	if ((back & BASER_SHARE_MASK) == 0 || gic->forceNonCacheable()) {
		v = (v & ~(BASER_SHARE_MASK | BASER_INNER_MASK)) | CACHE_NC << BASER_INNER_SHIFT;
		wr64(regs + GITS_CBASER, v);
		back = rd64(regs + GITS_CBASER);
		queueFlush = true;
	}
	if ((back & (1ULL << 63)) == 0) {
		snprintf(why, whyLength, "GITS_CBASER does not keep 0x%llx", v);
		return false;
	}
	wr64(regs + GITS_CWRITER, 0);
	queueNext = 0;
	return true;
}

// Puts one command in the queue and waits until the ITS has read past it
// (GITS_CREADR), at most a second. A stalled queue (a command error, with
// GITS_CREADR.Stalled) is logged.
bool
NeoDarwinGICv3ITS::command(const uint64_t cmd[4], const char *what)
{
	IOLockLock(lock);
	uint32_t at = queueNext;
	uint32_t next = (at + CMD_SIZE) % QUEUE_SIZE;
	// The queue is drained after every command, so it is never full.
	volatile uint64_t *slot = (volatile uint64_t *)(queue.va + at);
	for (int i = 0; i < 4; i++) {
		slot[i] = cmd[i];
	}
	if (queueFlush) {
		ndGICFlush(queue.va + at, CMD_SIZE);
	} else {
		__builtin_arm_dsb(0xa);                // DSB ISHST: the command before the doorbell
	}
	wr64(regs + GITS_CWRITER, next);
	queueNext = next;
	bool ok = false;
	uint64_t creadr = 0;
	for (int i = 0; i < COMMAND_TIMEOUT_US; i++) {
		creadr = rd64(regs + GITS_CREADR);
		if (creadr & GITS_CREADR_STALLED) {
			break;
		}
		if ((creadr & 0xfffe0) == next) {
			ok = true;
			break;
		}
		IODelay(1);
	}
	IOLockUnlock(lock);
	if (!ok) {
		IOLog("NeoDarwinGICv3ITS: ITS %u: %s (0x%016llx 0x%016llx 0x%016llx) %s: GITS_CREADR 0x%llx, GITS_CWRITER 0x%x\n",
		    itsID, what, cmd[0], cmd[1], cmd[2], (creadr & GITS_CREADR_STALLED) ? "stalled the queue" : "timed out",
		    creadr, next);
	}
	return ok;
}

bool
NeoDarwinGICv3ITS::sync(unsigned int cpu)
{
	uint64_t target = gic->collectionTarget(cpu, pta);
	if (target == ~0ULL) {
		return false;
	}
	uint64_t cmd[4] = { CMD_SYNC, 0, target & 0x000fffffffff0000ULL, 0 };
	return command(cmd, "SYNC");
}

// With a two-level device table, the level-2 page holding `deviceID`'s entry
// must exist before MAPD; a flat table covers what its size covers.
bool
NeoDarwinGICv3ITS::deviceTableCovers(uint32_t deviceID)
{
	if (devBits < 32 && (deviceID >> devBits) != 0) {
		return false;
	}
	NDGICTable *l1 = &tables[devTable];
	uint64_t perPage = devPageSize / devEntrySize;
	if (!devIndirect) {
		return (uint64_t)deviceID < l1->size / devEntrySize;
	}
	uint64_t index = deviceID / perPage;
	if (index >= devL1Entries) {
		return false;
	}
	if (devL2[index].va == NULL) {
		if (!ndGICTableAlloc(&devL2[index], devPageSize, devPageSize)) {
			return false;
		}
		volatile uint64_t *entry = (volatile uint64_t *)(l1->va + 8 * index);
		*entry = (1ULL << 63) | (devL2[index].pa & 0x000ffffffffff000ULL);
		if (tableFlush[devTable]) {
			ndGICFlush((const void *)entry, 8);
		} else {
			__builtin_arm_dsb(0xa);
		}
	}
	return true;
}

bool
NeoDarwinGICv3ITS::mapDevice(uint32_t deviceID, uint32_t events)
{
	if (events < 2) {
		events = 2;
	}
	uint32_t bits = 1;
	while ((1u << bits) < events) {
		bits++;
	}
	if (bits > eventBits) {
		IOLog("NeoDarwinGICv3ITS: ITS %u: DeviceID 0x%x: %u events, but the ITS has %u EventID bits\n",
		    itsID, deviceID, events, eventBits);
		return false;
	}
	for (unsigned int i = 0; i < deviceCount; i++) {
		if (devices[i].id == deviceID && devices[i].events >= (1u << bits)) {
			return true;
		}
	}
	IOLockLock(lock);
	bool covered = deviceTableCovers(deviceID);
	IOLockUnlock(lock);
	if (!covered) {
		IOLog("NeoDarwinGICv3ITS: ITS %u: DeviceID 0x%x is outside the device table (%u DeviceID bits)\n",
		    itsID, deviceID, devBits);
		return false;
	}
	Device *d = NULL;
	for (unsigned int i = 0; i < deviceCount; i++) {
		if (devices[i].id == deviceID) {
			d = &devices[i];        // remapped with a larger ITT: its events are lost
			uint64_t unmap[4] = { CMD_MAPD | (uint64_t)deviceID << 32, 0, 0, 0 };
			command(unmap, "MAPD (unmap)");
			ndGICTableFree(&d->itt);
		}
	}
	if (d == NULL) {
		if (deviceCount == deviceCapacity) {
			unsigned int capacity = deviceCapacity ? 2 * deviceCapacity : 16;
			Device *grown = IONewZero(Device, capacity);
			if (grown == NULL) {
				return false;
			}
			if (devices != NULL) {
				memcpy(grown, devices, deviceCount * sizeof(Device));
				IODelete(devices, Device, deviceCapacity);
			}
			devices = grown;
			deviceCapacity = capacity;
		}
		d = &devices[deviceCount++];
		d->id = deviceID;
	}
	d->events = 1u << bits;
	if (!ndGICTableAlloc(&d->itt, (size_t)d->events * ittEntrySize, ITT_ALIGN)) {
		d->events = 0;
		return false;
	}
	uint64_t mapd[4] = { CMD_MAPD | (uint64_t)deviceID << 32, bits - 1, (1ULL << 63) | (d->itt.pa & 0x000fffffffffff00ULL), 0 };
	if (!command(mapd, "MAPD")) {
		ndGICTableFree(&d->itt);
		d->events = 0;
		return false;
	}
	return true;
}

bool
NeoDarwinGICv3ITS::mapEvent(uint32_t deviceID, uint32_t eventID, uint32_t lpi, unsigned int cpu)
{
	gic->configureLPI(lpi, true);
	uint64_t mapti[4] = { CMD_MAPTI | (uint64_t)deviceID << 32, eventID | (uint64_t)lpi << 32, cpu, 0 };
	uint64_t inv[4] = { CMD_INV | (uint64_t)deviceID << 32, eventID, 0, 0 };
	if (!command(mapti, "MAPTI") || !command(inv, "INV") || !sync(cpu)) {
		gic->configureLPI(lpi, false);
		return false;
	}
	return true;
}

bool
NeoDarwinGICv3ITS::discardEvent(uint32_t deviceID, uint32_t eventID, unsigned int cpu)
{
	uint64_t discard[4] = { CMD_DISCARD | (uint64_t)deviceID << 32, eventID, 0, 0 };
	bool ok = command(discard, "DISCARD") && sync(cpu);
	return ok;
}
