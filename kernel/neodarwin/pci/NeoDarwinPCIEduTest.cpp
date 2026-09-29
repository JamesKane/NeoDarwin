// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: IOKit drivers are C++ IOService subclasses.
//
// A proof that a PCI device's legacy interrupt reaches its handler
// (docs/kernel/pci.md, P1-09 checkpoint 2): a driver for QEMU's "edu"
// teaching device (1234:11e8, docs/specs/edu.rst in QEMU), which only a
// test machine has. Its BAR0 has an interrupt-raise register: writing it
// sets bits in the interrupt status register and asserts INTA until the
// driver acknowledges them. The driver resolves INTA through IOPCIFamily and
// the ACPI nub's _PRT, registers a handler on the GIC SPI that names, raises
// the interrupt and waits for the handler, which acknowledges it:
//
//   INTA -> _PRT -> link device L00x -> GSIV -> NeoDarwinGICv3 SPI
//   (Group 1 Non-secure, level, routed to the boot CPU) -> IRQ -> handler
//
// Then, if IOPCIFamily gave the device an MSI (edu has a one-vector MSI
// capability; checkpoint 3, docs/kernel/gic-its.md), the same through it:
//
//   MSI write of EventID 0 to GITS_TRANSLATER -> ITS (DeviceID from the
//   IORT, MAPTI) -> LPI on the boot CPU's redistributor -> IRQ -> handler
//
// Registering the MSI source makes IOPCIFamily enable MSI in the capability
// and disable INTx (command register); unregistering it undoes both.
// Each result is one log line, with the time from the raise to the handler.
// Compiled into the kernel with the host bridge (patch 0022), matched by a
// built-in personality (IOPCIMatch 0x11e81234).

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>

class NeoDarwinPCIEduTest : public IOService
{
	OSDeclareDefaultStructors(NeoDarwinPCIEduTest);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;

private:
	static void interrupt(OSObject *target, void *refCon, IOService *nub, int source);
	// Raises the interrupt and waits up to a second for the handler of
	// `source`; the nanoseconds from the raise to the handler, or 0.
	uint64_t raise(IOPCIDevice *pci, int source);
	void testMSI(IOPCIDevice *pci, const char *where, UInt32 ident);

	IOMemoryMap *map;
	volatile UInt32 *regs;
	UInt32 count;
	UInt32 status;
	uint64_t raisedAt;
	uint64_t handledAt;
};

#define super IOService
OSDefineMetaClassAndStructors(NeoDarwinPCIEduTest, IOService);

// BAR0 registers (QEMU docs/specs/edu.rst).
enum {
	kEduIdentification  = 0x00 / 4,    // 0x010000ed: version 1.0, "ed"
	kEduInterruptStatus = 0x24 / 4,
	kEduInterruptRaise  = 0x60 / 4,
	kEduInterruptAck    = 0x64 / 4,
};
static const UInt32 kEduRaiseValue = 0x4e440000;   // any bits; "ND"

// Primary interrupt context: acknowledge in the device (INTx is level), then
// count. The GIC completes the interrupt when this returns.
void
NeoDarwinPCIEduTest::interrupt(OSObject *target, void *refCon, IOService *nub, int source)
{
	(void)refCon; (void)nub; (void)source;
	NeoDarwinPCIEduTest *self = static_cast<NeoDarwinPCIEduTest *>(target);
	UInt32 s = self->regs[kEduInterruptStatus];
	self->regs[kEduInterruptAck] = s;
	__atomic_store_n(&self->status, s, __ATOMIC_RELAXED);
	if (__atomic_load_n(&self->count, __ATOMIC_RELAXED) == 0) {
		self->handledAt = mach_absolute_time();
	}
	__atomic_add_fetch(&self->count, 1, __ATOMIC_RELEASE);
}

uint64_t
NeoDarwinPCIEduTest::raise(IOPCIDevice *pci, int source)
{
	__atomic_store_n(&count, 0, __ATOMIC_RELEASE);
	pci->enableInterrupt(source);
	raisedAt = mach_absolute_time();
	regs[kEduInterruptRaise] = kEduRaiseValue;
	for (int i = 0; i < 1000 && __atomic_load_n(&count, __ATOMIC_ACQUIRE) == 0; i++) {
		IOSleep(1);
	}
	uint64_t ns = 0;
	if (__atomic_load_n(&count, __ATOMIC_ACQUIRE) != 0) {
		absolutetime_to_nanoseconds(handledAt - raisedAt, &ns);
		ns = ns != 0 ? ns : 1;
	}
	pci->disableInterrupt(source);
	return ns;
}

// The first source IOPCIFamily typed as messaged (MSI vectors follow INTx).
void
NeoDarwinPCIEduTest::testMSI(IOPCIDevice *pci, const char *where, UInt32 ident)
{
	int source = -1, type = 0;
	for (int i = 0; source < 0 && pci->getInterruptType(i, &type) == kIOReturnSuccess; i++) {
		if (type & kIOInterruptTypePCIMessaged) {
			source = i;
		}
	}
	if (source < 0) {
		IOLog("NeoDarwinPCIEduTest: %s: edu 0x%08x: no MSI (INTx only)\n", where, ident);
		return;
	}
	OSNumber *lpi = OSDynamicCast(OSNumber, pci->getProperty("msi-lpi-base"));
	OSNumber *dev = OSDynamicCast(OSNumber, pci->getProperty("msi-device-id"));
	IOReturn ret = pci->registerInterrupt(source, this, &NeoDarwinPCIEduTest::interrupt, NULL);
	if (ret != kIOReturnSuccess) {
		IOLog("NeoDarwinPCIEduTest: %s: edu 0x%08x: MSI (source %d) not registered (0x%x)\n", where, ident, source, ret);
		return;
	}
	uint64_t ns = raise(pci, source);
	UInt32 n = __atomic_load_n(&count, __ATOMIC_ACQUIRE);
	UInt16 command = pci->configRead16(kIOPCIConfigCommand);
	pci->unregisterInterrupt(source);
	if (n != 0) {
		IOLog("NeoDarwinPCIEduTest: %s: edu 0x%08x: MSI on LPI %u (DeviceID 0x%x, EventID 0) reached its handler in %llu us: status 0x%x, %u interrupt%s, INTx %s\n",
		    where, ident, lpi ? lpi->unsigned32BitValue() : 0, dev ? dev->unsigned32BitValue() : 0, ns / 1000, status, n,
		    n == 1 ? "" : "s", (command & kIOPCICommandInterruptDisable) ? "disabled" : "enabled");
	} else {
		IOLog("NeoDarwinPCIEduTest: %s: edu 0x%08x: MSI on LPI %u: no interrupt within 1 s (status 0x%x)\n",
		    where, ident, lpi ? lpi->unsigned32BitValue() : 0, regs[kEduInterruptStatus]);
	}
}

bool
NeoDarwinPCIEduTest::start(IOService *provider)
{
	IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
	if (pci == NULL || !super::start(provider)) {
		return false;
	}
	char where[16];
	snprintf(where, sizeof(where), "%02x:%02x.%x", pci->getBusNumber(), pci->getDeviceNumber(), pci->getFunctionNumber());
	pci->setMemoryEnable(true);
	map = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
	if (map == NULL) {
		IOLog("NeoDarwinPCIEduTest: %s: BAR0 not mapped\n", where);
		return false;
	}
	regs = (volatile UInt32 *)map->getVirtualAddress();
	UInt32 ident = regs[kEduIdentification];
	// INTx enabled (command bit 10 clear).
	pci->configWrite16(kIOPCIConfigCommand, pci->configRead16(kIOPCIConfigCommand) & ~0x0400);

	int type = 0;
	OSArray *specs = OSDynamicCast(OSArray, pci->getProperty(gIOInterruptSpecifiersKey));
	OSData *spec = specs != NULL && specs->getCount() != 0 ? OSDynamicCast(OSData, specs->getObject(0)) : NULL;
	UInt32 gsiv = spec != NULL ? *(const UInt32 *)spec->getBytesNoCopy() : 0;
	IOReturn ret = pci->getInterruptType(0, &type);
	if (ret == kIOReturnSuccess) {
		ret = pci->registerInterrupt(0, this, &NeoDarwinPCIEduTest::interrupt, NULL);
	}
	if (ret != kIOReturnSuccess) {
		IOLog("NeoDarwinPCIEduTest: %s: edu 0x%08x: INTA not registered (0x%x)\n", where, ident, ret);
		testMSI(pci, where, ident);
		return true;
	}
	uint64_t ns = raise(pci, 0);
	UInt32 n = __atomic_load_n(&count, __ATOMIC_ACQUIRE);
	pci->unregisterInterrupt(0);
	if (n != 0) {
		IOLog("NeoDarwinPCIEduTest: %s: edu 0x%08x: INTA on GSIV %u (%s) reached its handler in %llu us: status 0x%x, %u interrupt%s\n",
		    where, ident, gsiv, (type & kIOInterruptTypeLevel) ? "level" : "edge", ns / 1000,
		    status, n, n == 1 ? "" : "s");
	} else {
		IOLog("NeoDarwinPCIEduTest: %s: edu 0x%08x: INTA on GSIV %u: no interrupt within 1 s (status 0x%x)\n",
		    where, ident, gsiv, regs[kEduInterruptStatus]);
	}
	testMSI(pci, where, ident);
	return true;
}
