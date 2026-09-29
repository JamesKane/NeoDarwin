/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: expressibility: ACPICA calls its OS layer through C prototypes (acpiosxf.h), and this layer calls XNU's C interfaces. */
/*
 * ACPICA's OS services layer (AcpiOs*) on XNU. docs/kernel/acpi.md has the
 * mapping table; in short:
 *
 *  - Tables: neoboot copied them into DRAM below topOfKernelData
 *    (/chosen acpi-tables, dt-abi.md), so they are read through the
 *    kernel's physmap, as any DRAM (/chosen dram-base, dram-size) is. Everything else is
 *    MMIO (AML OperationRegions, GAS registers): an uncached device mapping
 *    made for the request and removed when ACPICA unmaps it.
 *  - Spinlocks are IOSimpleLocks taken with interrupts off; semaphores (and
 *    so ACPICA's mutexes) are an IOLock, a count and IOLockSleepDeadline.
 *  - Deferred work (notify handlers) runs on a kernel thread per call.
 *  - Output goes to the kernel log one line at a time, formatted by
 *    ACPICA's own vsnprintf (its format strings rely on precision).
 *  - PCI configuration space is ECAM (nd_pci.h), by segment, from MCFG.
 *  - Not on arm64: port I/O (no such space), interrupt handlers
 *    (hardware-reduced ACPI has no SCI).
 */

#include "nd_acpica.h"
#include "nd_pci.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <pexpert/device_tree.h>

/* osfmk/arm/machine_routines.h, which IOKit's C files do not see. */
extern vm_offset_t ml_static_ptovirt(vm_offset_t paddr);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, uint32_t flags);
extern void ml_io_unmap(vm_offset_t addr, vm_size_t sz);
/* osfmk/kern/misc_protos.h */
extern int printf(const char *format, ...) __printflike(1, 2);
/* utprint.c, under the name acnd.h gives ACPICA's C library. */
extern int AcpiNdVsnprintf(char *String, ACPI_SIZE Size, const char *Format, va_list Args);

/* osfmk/arm/memory_types.h: VM_WIMG_IO, strongly ordered device memory. */
#define ND_VM_WIMG_IO 0x7
#define ND_PAGE_MASK  ((UINT64)PAGE_SIZE - 1)

/* ------------------------------------------------------------------------
 * The RSDP and DRAM, from the device tree.
 */

static UINT64 nd_dram_base, nd_dram_len;
static ACPI_PHYSICAL_ADDRESS nd_rsdp;

static int
nd_chosen_u64s(const char *name, UINT64 *out, unsigned int count)
{
	DTEntry chosen;
	void const *prop;
	unsigned int size;
	if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
	    SecureDTGetProperty(chosen, name, &prop, &size) != kSuccess ||
	    size != count * sizeof(UINT64)) {
		return 0;
	}
	memcpy(out, prop, size);
	return 1;
}

static void
nd_acpi_read_chosen(void)
{
	static int done;
	UINT64 v[2];
	if (done) {
		return;
	}
	done = 1;
	if (nd_chosen_u64s("acpi-rsdp", v, 1)) {
		nd_rsdp = v[0];
	}
	if (nd_chosen_u64s("dram-base", v, 1)) {
		nd_dram_base = v[0];
	}
	if (nd_chosen_u64s("dram-size", v, 1)) {
		nd_dram_len = v[0];
	}
}

ACPI_PHYSICAL_ADDRESS
nd_acpi_rsdp(void)
{
	nd_acpi_read_chosen();
	return nd_rsdp;
}

static int
nd_in(UINT64 addr, UINT64 len, UINT64 base, UINT64 size)
{
	return size != 0 && addr >= base && len <= size && addr - base <= size - len;
}

/* DRAM the kernel maps (the ACPI copy is inside it): cacheable, through the physmap. */
static int
nd_is_dram(UINT64 addr, UINT64 len)
{
	nd_acpi_read_chosen();
	return nd_in(addr, len, nd_dram_base, nd_dram_len);
}

/* ------------------------------------------------------------------------
 * Environment and tables
 */

static IOSimpleLock *nd_mappings_lock, *nd_line_lock;
static IOLock *nd_work_lock;

/* The first call ACPICA makes (AcpiInitializeSubsystem). */
ACPI_STATUS
AcpiOsInitialize(void)
{
	nd_acpi_read_chosen();
	if (nd_mappings_lock == NULL) {
		nd_mappings_lock = IOSimpleLockAlloc();
		nd_line_lock = IOSimpleLockAlloc();
		nd_work_lock = IOLockAlloc();
	}
	if (nd_mappings_lock == NULL || nd_line_lock == NULL || nd_work_lock == NULL) {
		return AE_NO_MEMORY;
	}
	return AE_OK;
}

ACPI_STATUS
AcpiOsTerminate(void)
{
	return AE_OK;
}

ACPI_PHYSICAL_ADDRESS
AcpiOsGetRootPointer(void)
{
	return nd_acpi_rsdp();
}

ACPI_STATUS
AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *InitVal, ACPI_STRING *NewVal)
{
	(void)InitVal;
	*NewVal = NULL;
	return AE_OK;
}

ACPI_STATUS
AcpiOsTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_TABLE_HEADER **NewTable)
{
	(void)ExistingTable;
	*NewTable = NULL;
	return AE_OK;
}

ACPI_STATUS
AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_PHYSICAL_ADDRESS *NewAddress,
    UINT32 *NewTableLength)
{
	(void)ExistingTable;
	*NewAddress = 0;
	*NewTableLength = 0;
	return AE_OK;
}

/* ------------------------------------------------------------------------
 * Memory
 *
 * AcpiOsFree has no size, and IOFree needs one: each block carries it in a
 * 16-byte header, which keeps ACPICA's 64-bit fields aligned.
 */

union nd_alloc_header {
	ACPI_SIZE size;
	UINT8 pad[16];
};

void *
AcpiOsAllocate(ACPI_SIZE Size)
{
	if (Size > (ACPI_SIZE)-1 - sizeof(union nd_alloc_header)) {
		return NULL;
	}
	union nd_alloc_header *h = IOMalloc(Size + sizeof(*h));
	if (h == NULL) {
		return NULL;
	}
	h->size = Size;
	return h + 1;
}

void
AcpiOsFree(void *Memory)
{
	if (Memory == NULL) {
		return;
	}
	union nd_alloc_header *h = (union nd_alloc_header *)Memory - 1;
	IOFree(h, h->size + sizeof(*h));
}

/*
 * MMIO mappings, so that AcpiOsUnmapMemory can tell them from the physmap
 * and release them. There are few: one per OperationRegion window.
 */
struct nd_mapping {
	struct nd_mapping *next;
	vm_offset_t va;         /* page-aligned */
	vm_size_t size;         /* whole pages */
};
static struct nd_mapping *nd_mappings;

void *
AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS Where, ACPI_SIZE Length)
{
	if (Length == 0) {
		Length = 1;
	}
	if (nd_is_dram(Where, Length)) {
		return (void *)ml_static_ptovirt((vm_offset_t)Where);
	}
	UINT64 base = Where & ~ND_PAGE_MASK;
	UINT64 size = (Where - base + Length + ND_PAGE_MASK) & ~ND_PAGE_MASK;
	struct nd_mapping *m = IOMalloc(sizeof(*m));
	if (m == NULL) {
		return NULL;
	}
	m->va = ml_io_map_unmappable((vm_offset_t)base, (vm_size_t)size, ND_VM_WIMG_IO);
	if (m->va == 0) {
		IOFree(m, sizeof(*m));
		return NULL;
	}
	m->size = (vm_size_t)size;
	IOSimpleLockLock(nd_mappings_lock);
	m->next = nd_mappings;
	nd_mappings = m;
	IOSimpleLockUnlock(nd_mappings_lock);
	return (void *)(m->va + (vm_offset_t)(Where - base));
}

void
AcpiOsUnmapMemory(void *LogicalAddress, ACPI_SIZE Size)
{
	(void)Size;
	vm_offset_t va = (vm_offset_t)LogicalAddress & ~(vm_offset_t)ND_PAGE_MASK;
	IOSimpleLockLock(nd_mappings_lock);
	struct nd_mapping **pp = &nd_mappings, *m;
	while ((m = *pp) != NULL && !(va >= m->va && va < m->va + m->size)) {
		pp = &m->next;
	}
	if (m != NULL) {
		*pp = m->next;
	}
	IOSimpleLockUnlock(nd_mappings_lock);
	if (m != NULL) {
		ml_io_unmap(m->va, m->size);
		IOFree(m, sizeof(*m));
	}
	/* Otherwise it is a physmap address: nothing to undo. */
}

/* Registers named by generic addresses (GAS) in system memory. */
ACPI_STATUS
AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 *Value, UINT32 Width)
{
	if ((Width != 8 && Width != 16 && Width != 32 && Width != 64) || (Address & (Width / 8 - 1)) != 0) {
		return AE_BAD_PARAMETER;
	}
	volatile void *p = AcpiOsMapMemory(Address, Width / 8);
	if (p == NULL) {
		return AE_NO_MEMORY;
	}
	switch (Width) {
	case 8:  *Value = *(volatile UINT8 *)p; break;
	case 16: *Value = *(volatile UINT16 *)p; break;
	case 32: *Value = *(volatile UINT32 *)p; break;
	default: *Value = *(volatile UINT64 *)p; break;
	}
	AcpiOsUnmapMemory((void *)(uintptr_t)p, Width / 8);
	return AE_OK;
}

ACPI_STATUS
AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 Value, UINT32 Width)
{
	if ((Width != 8 && Width != 16 && Width != 32 && Width != 64) || (Address & (Width / 8 - 1)) != 0) {
		return AE_BAD_PARAMETER;
	}
	volatile void *p = AcpiOsMapMemory(Address, Width / 8);
	if (p == NULL) {
		return AE_NO_MEMORY;
	}
	switch (Width) {
	case 8:  *(volatile UINT8 *)p = (UINT8)Value; break;
	case 16: *(volatile UINT16 *)p = (UINT16)Value; break;
	case 32: *(volatile UINT32 *)p = (UINT32)Value; break;
	default: *(volatile UINT64 *)p = Value; break;
	}
	AcpiOsUnmapMemory((void *)(uintptr_t)p, Width / 8);
	return AE_OK;
}

/* Arm has no I/O port space: an AML SystemIO region fails its access. */
ACPI_STATUS
AcpiOsReadPort(ACPI_IO_ADDRESS Address, UINT32 *Value, UINT32 Width)
{
	(void)Address; (void)Width;
	*Value = 0;
	return AE_SUPPORT;
}

ACPI_STATUS
AcpiOsWritePort(ACPI_IO_ADDRESS Address, UINT32 Value, UINT32 Width)
{
	(void)Address; (void)Value; (void)Width;
	return AE_SUPPORT;
}

/* PCI configuration space through ECAM (nd_pci.h, docs/kernel/pci.md): the
 * ACPI platform registers MCFG's windows before any AML runs. ACPICA's
 * PCI_Config handler passes the register and the field's access width.
 * ECAM performs naturally aligned accesses of 1, 2 or 4 bytes; a 64-bit
 * access is two 32-bit ones, low first. A misaligned read is assembled from
 * the aligned bytes it covers; a misaligned write is refused, since
 * configuration registers may have side effects on access. */
static UINT32
nd_pci_read_bytes(ACPI_PCI_ID *id, UINT32 reg, UINT32 bytes, UINT64 *value, int *ok)
{
	uint32_t v;
	if ((reg & (bytes - 1)) == 0) {
		*ok &= nd_pci_config_read(id->Segment, (uint8_t)id->Bus, (uint8_t)id->Device, (uint8_t)id->Function, reg, bytes, &v);
		*value = v;
		return bytes;
	}
	*value = 0;
	for (UINT32 i = 0; i < bytes; i++) {
		*ok &= nd_pci_config_read(id->Segment, (uint8_t)id->Bus, (uint8_t)id->Device, (uint8_t)id->Function, reg + i, 1, &v);
		*value |= (UINT64)(v & 0xff) << (8 * i);
	}
	return bytes;
}

ACPI_STATUS
AcpiOsReadPciConfiguration(ACPI_PCI_ID *PciId, UINT32 Reg, UINT64 *Value, UINT32 Width)
{
	int ok = 1;
	UINT64 lo, hi;
	switch (Width) {
	case 8:
	case 16:
	case 32:
		nd_pci_read_bytes(PciId, Reg, Width / 8, Value, &ok);
		break;
	case 64:
		nd_pci_read_bytes(PciId, Reg, 4, &lo, &ok);
		nd_pci_read_bytes(PciId, Reg + 4, 4, &hi, &ok);
		*Value = lo | hi << 32;
		break;
	default:
		*Value = 0;
		return AE_BAD_PARAMETER;
	}
	return ok ? AE_OK : AE_NOT_EXIST;
}

ACPI_STATUS
AcpiOsWritePciConfiguration(ACPI_PCI_ID *PciId, UINT32 Reg, UINT64 Value, UINT32 Width)
{
	uint8_t bus = (uint8_t)PciId->Bus, dev = (uint8_t)PciId->Device, fn = (uint8_t)PciId->Function;
	int ok;
	switch (Width) {
	case 8:
	case 16:
	case 32:
		if ((Reg & (Width / 8 - 1)) != 0) {
			return AE_BAD_PARAMETER;
		}
		ok = nd_pci_config_write(PciId->Segment, bus, dev, fn, Reg, Width / 8, (uint32_t)Value);
		break;
	case 64:
		if ((Reg & 3) != 0) {
			return AE_BAD_PARAMETER;
		}
		ok = nd_pci_config_write(PciId->Segment, bus, dev, fn, Reg, 4, (uint32_t)Value) &&
		    nd_pci_config_write(PciId->Segment, bus, dev, fn, Reg + 4, 4, (uint32_t)(Value >> 32));
		break;
	default:
		return AE_BAD_PARAMETER;
	}
	return ok ? AE_OK : AE_NOT_EXIST;
}

/* ------------------------------------------------------------------------
 * Spinlocks and semaphores
 */

ACPI_STATUS
AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle)
{
	IOSimpleLock *l = IOSimpleLockAlloc();
	if (l == NULL) {
		return AE_NO_MEMORY;
	}
	*OutHandle = l;
	return AE_OK;
}

void
AcpiOsDeleteLock(ACPI_SPINLOCK Handle)
{
	if (Handle != NULL) {
		IOSimpleLockFree((IOSimpleLock *)Handle);
	}
}

ACPI_CPU_FLAGS
AcpiOsAcquireLock(ACPI_SPINLOCK Handle)
{
	return (ACPI_CPU_FLAGS)IOSimpleLockLockDisableInterrupt((IOSimpleLock *)Handle);
}

void
AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags)
{
	IOSimpleLockUnlockEnableInterrupt((IOSimpleLock *)Handle, (IOInterruptState)Flags);
}

struct nd_semaphore {
	IOLock *lock;
	UINT32 units;
	UINT32 max;
};

ACPI_STATUS
AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle)
{
	if (OutHandle == NULL || InitialUnits > MaxUnits) {
		return AE_BAD_PARAMETER;
	}
	struct nd_semaphore *s = IOMalloc(sizeof(*s));
	if (s == NULL) {
		return AE_NO_MEMORY;
	}
	s->lock = IOLockAlloc();
	if (s->lock == NULL) {
		IOFree(s, sizeof(*s));
		return AE_NO_MEMORY;
	}
	s->units = InitialUnits;
	s->max = MaxUnits;
	*OutHandle = s;
	return AE_OK;
}

ACPI_STATUS
AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle)
{
	struct nd_semaphore *s = Handle;
	if (s == NULL) {
		return AE_BAD_PARAMETER;
	}
	IOLockFree(s->lock);
	IOFree(s, sizeof(*s));
	return AE_OK;
}

/* Timeout in milliseconds; ACPI_WAIT_FOREVER (0xFFFF) waits without one. */
ACPI_STATUS
AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout)
{
	struct nd_semaphore *s = Handle;
	if (s == NULL || Units > s->max) {
		return AE_BAD_PARAMETER;
	}
	uint64_t deadline = 0;
	if (Timeout != ACPI_WAIT_FOREVER && Timeout != 0) {
		clock_interval_to_deadline(Timeout, kMillisecondScale, &deadline);
	}
	ACPI_STATUS status = AE_OK;
	IOLockLock(s->lock);
	while (s->units < Units) {
		if (Timeout == 0) {
			status = AE_TIME;
			break;
		}
		int r = Timeout == ACPI_WAIT_FOREVER
		    ? IOLockSleep(s->lock, s, THREAD_UNINT)
		    : IOLockSleepDeadline(s->lock, s, deadline, THREAD_UNINT);
		if (r == THREAD_TIMED_OUT && s->units < Units) {
			status = AE_TIME;
			break;
		}
	}
	if (status == AE_OK) {
		s->units -= Units;
	}
	IOLockUnlock(s->lock);
	return status;
}

ACPI_STATUS
AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units)
{
	struct nd_semaphore *s = Handle;
	if (s == NULL) {
		return AE_BAD_PARAMETER;
	}
	ACPI_STATUS status = AE_OK;
	IOLockLock(s->lock);
	if (Units > s->max - s->units) {
		status = AE_LIMIT;
	} else {
		s->units += Units;
		IOLockWakeup(s->lock, s, false);
	}
	IOLockUnlock(s->lock);
	return status;
}

/* ------------------------------------------------------------------------
 * Threads, time
 */

ACPI_THREAD_ID
AcpiOsGetThreadId(void)
{
	return (ACPI_THREAD_ID)(uintptr_t)current_thread();
}

struct nd_work {
	ACPI_OSD_EXEC_CALLBACK function;
	void *context;
};

static UINT32 nd_work_pending;

static void
nd_work_thread(void *param, wait_result_t wr)
{
	(void)wr;
	struct nd_work w = *(struct nd_work *)param;
	IOFree(param, sizeof(w));
	w.function(w.context);
	IOLockLock(nd_work_lock);
	if (--nd_work_pending == 0) {
		IOLockWakeup(nd_work_lock, &nd_work_pending, false);
	}
	IOLockUnlock(nd_work_lock);
	thread_terminate(current_thread());
}

/*
 * Deferred work: Notify() handlers (OSL_NOTIFY_HANDLER), GPE and debugger
 * work. Each call gets a kernel thread of its own; there are few.
 */
ACPI_STATUS
AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void *Context)
{
	(void)Type;
	if (Function == NULL) {
		return AE_BAD_PARAMETER;
	}
	struct nd_work *w = IOMalloc(sizeof(*w));
	if (w == NULL) {
		return AE_NO_MEMORY;
	}
	w->function = Function;
	w->context = Context;
	IOLockLock(nd_work_lock);
	nd_work_pending++;
	IOLockUnlock(nd_work_lock);
	thread_t thread;
	if (kernel_thread_start(nd_work_thread, w, &thread) != KERN_SUCCESS) {
		IOLockLock(nd_work_lock);
		nd_work_pending--;
		IOLockUnlock(nd_work_lock);
		IOFree(w, sizeof(*w));
		return AE_NO_MEMORY;
	}
	thread_deallocate(thread);
	return AE_OK;
}

void
AcpiOsWaitEventsComplete(void)
{
	IOLockLock(nd_work_lock);
	while (nd_work_pending != 0) {
		IOLockSleep(nd_work_lock, &nd_work_pending, THREAD_UNINT);
	}
	IOLockUnlock(nd_work_lock);
}

void
AcpiOsSleep(UINT64 Milliseconds)
{
	IOSleep((unsigned)Milliseconds);
}

void
AcpiOsStall(UINT32 Microseconds)
{
	IODelay(Microseconds);
}

/* 100-nanosecond units. */
UINT64
AcpiOsGetTimer(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 100;
}

/* ------------------------------------------------------------------------
 * Interrupts: hardware-reduced ACPI has no SCI. Devices' interrupts (GED
 * events among them) go through IOKit and the GIC, not through here.
 */

ACPI_STATUS
AcpiOsInstallInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER ServiceRoutine, void *Context)
{
	(void)InterruptNumber; (void)ServiceRoutine; (void)Context;
	return AE_SUPPORT;
}

ACPI_STATUS
AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER ServiceRoutine)
{
	(void)InterruptNumber; (void)ServiceRoutine;
	return AE_NOT_EXIST;
}

ACPI_STATUS
AcpiOsSignal(UINT32 Function, void *Info)
{
	if (Function == ACPI_SIGNAL_FATAL) {
		ACPI_SIGNAL_FATAL_INFO *f = Info;
		printf("ACPI: AML Fatal: type 0x%x code 0x%x argument 0x%x\n", f->Type, f->Code, f->Argument);
	}
	/* ACPI_SIGNAL_BREAKPOINT: no debugger; carry on. */
	return AE_OK;
}

ACPI_STATUS
AcpiOsEnterSleep(UINT8 SleepState, UINT32 RegaValue, UINT32 RegbValue)
{
	(void)SleepState; (void)RegaValue; (void)RegbValue;
	return AE_OK;
}

/* ------------------------------------------------------------------------
 * Output: ACPICA prints in fragments; the kernel log gets whole lines.
 */

static char nd_line[256];
static size_t nd_line_len;

void ACPI_INTERNAL_VAR_XFACE
AcpiOsPrintf(const char *Format, ...)
{
	va_list args;
	va_start(args, Format);
	AcpiOsVprintf(Format, args);
	va_end(args);
}

void
AcpiOsVprintf(const char *Format, va_list Args)
{
	char buf[256];
	int n = AcpiNdVsnprintf(buf, sizeof(buf), Format, Args);
	if (n <= 0) {
		return;
	}
	size_t len = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1;

	for (size_t i = 0; i < len;) {
		char out[sizeof(nd_line)];
		int flush = 0;
		IOInterruptState is = IOSimpleLockLockDisableInterrupt(nd_line_lock);
		for (; i < len; i++) {
			if (buf[i] == '\n' || nd_line_len == sizeof(nd_line) - 1) {
				flush = 1;
				if (buf[i] == '\n') {
					i++;
				}
				break;
			}
			nd_line[nd_line_len++] = buf[i];
		}
		if (flush) {
			memcpy(out, nd_line, nd_line_len);
			out[nd_line_len] = '\0';
			nd_line_len = 0;
		}
		IOSimpleLockUnlockEnableInterrupt(nd_line_lock, is);
		if (flush) {
			printf("%s\n", out);
		}
	}
}

void
AcpiOsRedirectOutput(void *Destination)
{
	(void)Destination;
}
