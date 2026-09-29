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
// The result is one log line. Compiled into the kernel with the host bridge
// (patch 0022), matched by a built-in personality (IOPCIMatch 0x11e81234).

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

	IOMemoryMap *map;
	volatile UInt32 *regs;
	UInt32 count;
	UInt32 status;
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
	__atomic_add_fetch(&self->count, 1, __ATOMIC_RELEASE);
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
		return true;
	}
	pci->enableInterrupt(0);

	regs[kEduInterruptRaise] = kEduRaiseValue;
	uint64_t started = mach_absolute_time();
	for (int i = 0; i < 1000 && __atomic_load_n(&count, __ATOMIC_ACQUIRE) == 0; i++) {
		IOSleep(1);
	}
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time() - started, &ns);
	UInt32 n = __atomic_load_n(&count, __ATOMIC_ACQUIRE);
	pci->disableInterrupt(0);
	pci->unregisterInterrupt(0);
	if (n != 0) {
		IOLog("NeoDarwinPCIEduTest: %s: edu 0x%08x: INTA on GSIV %u (%s) reached its handler in %llu us: status 0x%x, %u interrupt%s\n",
		    where, ident, gsiv, (type & kIOInterruptTypeLevel) ? "level" : "edge", ns / 1000,
		    status, n, n == 1 ? "" : "s");
	} else {
		IOLog("NeoDarwinPCIEduTest: %s: edu 0x%08x: INTA on GSIV %u: no interrupt within 1 s (status 0x%x)\n",
		    where, ident, gsiv, regs[kEduInterruptStatus]);
	}
	return true;
}
