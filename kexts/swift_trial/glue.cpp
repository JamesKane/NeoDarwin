// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: an IOKit class (OSMetaClass, IOService's C++ vtable and the kmod_info the kernel's linker expects) cannot be written in Swift
//
// The kext_swift trial's C++ shell (P0-10, docs/architecture/language-policy.md
// §3): an IOService that matches IOResources, so the kernel starts it at
// boot, and calls the Embedded Swift logic in Trial.swift through its C
// entry point, once on a well-formed table and once on a corrupted one.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <mach/kmod.h>
#include <string.h>
#include "trial.h"

class org_neodarwin_SwiftTrial : public IOService {
	OSDeclareDefaultStructors(org_neodarwin_SwiftTrial)
public:
	bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(org_neodarwin_SwiftTrial, IOService)

static void
report(const uint8_t *tag, uint64_t value)
{
	IOLog("NDSwiftTrial: %s %llu\n", (const char *)tag, value);
}

// Records [kind][length][payload]: (1, AA BB), (2, -), (1, 01 02 03), (3, 09),
// then kind 0 with the Fletcher-32 of the bytes before it, little-endian.
static const uint8_t good_table[] = {
	1, 2, 0xaa, 0xbb, 2, 0, 1, 3, 1, 2, 3, 3, 1, 9,
	0, 4, 0xb3, 0xce, 0x1f, 0x95,
};

bool
org_neodarwin_SwiftTrial::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	IOLog("NDSwiftTrial: starting; calling Embedded Swift\n");
	int32_t r = ndswift_trial_run(good_table, sizeof(good_table), report);
	IOLog("NDSwiftTrial: well-formed table: Swift returned %d\n", r);

	uint8_t bad_table[sizeof(good_table)];
	memcpy(bad_table, good_table, sizeof(good_table));
	bad_table[2] ^= 0xff;
	r = ndswift_trial_run(bad_table, sizeof(bad_table), report);
	IOLog("NDSwiftTrial: corrupted table: Swift returned %d\n", r);

	int32_t counter = 1;
	r = ndswift_trial_kpi(&counter);
	IOLog("NDSwiftTrial: kernel KPIs from Swift: returned %d, counter %d\n", r, counter);
	registerService();
	return true;
}

extern "C" {
extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(org.neodarwin.swifttrial, "1.0.0", _start, _stop)
kmod_start_func_t *_realmain = 0;
kmod_stop_func_t *_antimain = 0;
int _kext_apple_cc = __APPLE_CC__;
}
