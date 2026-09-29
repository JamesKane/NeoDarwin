// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: UEFI 2.10 data structures, which the specification defines in C; imported into Swift through a module map.
//
// Only the parts neoboot uses, in specification order. AArch64 UEFI uses the
// standard AAPCS64 calling convention, so Swift calls these function pointers
// directly (@convention(c)); no calling-convention shim is needed.
#ifndef ND_UEFI_H
#define ND_UEFI_H
#include <stdint.h>
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef struct { uint64_t Signature; uint32_t Revision, HeaderSize, CRC32, Reserved; } EFI_TABLE_HEADER;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (*EFI_TEXT_STRING)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
	void *Reset;
	EFI_TEXT_STRING OutputString;
	void *TestString, *QueryMode, *SetMode, *SetAttribute, *ClearScreen, *SetCursorPosition, *EnableCursor, *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef enum { EfiResetCold, EfiResetWarm, EfiResetShutdown, EfiResetPlatformSpecific } EFI_RESET_TYPE;
typedef void (*EFI_RESET_SYSTEM)(EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, uint64_t DataSize, void *ResetData);
typedef struct {
	uint16_t Year;
	uint8_t Month, Day, Hour, Minute, Second, Pad1;
	uint32_t Nanosecond;
	int16_t TimeZone;
	uint8_t Daylight, Pad2;
} EFI_TIME;
typedef struct {
	EFI_TABLE_HEADER Hdr;
	EFI_STATUS (*GetTime)(EFI_TIME *Time, void *Capabilities);
	void *SetTime, *GetWakeupTime, *SetWakeupTime;
	void *SetVirtualAddressMap, *ConvertPointer;
	void *GetVariable, *GetNextVariableName, *SetVariable;
	void *GetNextHighMonotonicCount;
	EFI_RESET_SYSTEM ResetSystem;
} EFI_RUNTIME_SERVICES;
typedef struct { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; } EFI_GUID;

// Boot services, in table order. Entries neoboot does not call stay void *.
enum { AllocateAnyPages, AllocateMaxAddress, AllocateAddress };
enum {
	EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData, EfiBootServicesCode, EfiBootServicesData,
	EfiRuntimeServicesCode, EfiRuntimeServicesData, EfiConventionalMemory, EfiUnusableMemory,
	EfiACPIReclaimMemory, EfiACPIMemoryNVS, EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace, EfiPalCode,
	EfiPersistentMemory,
};
typedef struct {
	uint32_t Type;
	uint64_t PhysicalStart, VirtualStart, NumberOfPages, Attribute;
} EFI_MEMORY_DESCRIPTOR;
typedef struct {
	EFI_TABLE_HEADER Hdr;
	void *RaiseTPL, *RestoreTPL;
	EFI_STATUS (*AllocatePages)(uint32_t Type, uint32_t MemoryType, uint64_t Pages, uint64_t *Memory);
	EFI_STATUS (*FreePages)(uint64_t Memory, uint64_t Pages);
	EFI_STATUS (*GetMemoryMap)(uint64_t *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap, uint64_t *MapKey,
	    uint64_t *DescriptorSize, uint32_t *DescriptorVersion);
	void *AllocatePool, *FreePool;
	void *CreateEvent, *SetTimer, *WaitForEvent, *SignalEvent, *CloseEvent, *CheckEvent;
	void *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
	EFI_STATUS (*HandleProtocol)(EFI_HANDLE Handle, const EFI_GUID *Protocol, void **Interface);
	void *Reserved, *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath, *InstallConfigurationTable;
	void *LoadImage, *StartImage, *Exit, *UnloadImage;
	EFI_STATUS (*ExitBootServices)(EFI_HANDLE ImageHandle, uint64_t MapKey);
	void *GetNextMonotonicCount, *Stall;
	EFI_STATUS (*SetWatchdogTimer)(uint64_t Timeout, uint64_t WatchdogCode, uint64_t DataSize, CHAR16 *WatchdogData);
} EFI_BOOT_SERVICES;

// EFI_LOADED_IMAGE_PROTOCOL: which device neoboot was loaded from.
typedef struct {
	uint32_t Revision;
	EFI_HANDLE ParentHandle;
	void *SystemTable;
	EFI_HANDLE DeviceHandle;
} EFI_LOADED_IMAGE_PROTOCOL;

// EFI_SIMPLE_FILE_SYSTEM_PROTOCOL and EFI_FILE_PROTOCOL: reading the ESP.
struct EFI_FILE_PROTOCOL;
typedef struct EFI_FILE_PROTOCOL {
	uint64_t Revision;
	EFI_STATUS (*Open)(struct EFI_FILE_PROTOCOL *This, struct EFI_FILE_PROTOCOL **NewHandle, CHAR16 *FileName,
	    uint64_t OpenMode, uint64_t Attributes);
	EFI_STATUS (*Close)(struct EFI_FILE_PROTOCOL *This);
	void *Delete;
	EFI_STATUS (*Read)(struct EFI_FILE_PROTOCOL *This, uint64_t *BufferSize, void *Buffer);
	void *Write;
	EFI_STATUS (*GetPosition)(struct EFI_FILE_PROTOCOL *This, uint64_t *Position);
	EFI_STATUS (*SetPosition)(struct EFI_FILE_PROTOCOL *This, uint64_t Position);
} EFI_FILE_PROTOCOL;
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
	uint64_t Revision;
	EFI_STATUS (*OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
enum { EFI_FILE_MODE_READ = 1 };

// EFI_CONFIGURATION_TABLE: where the firmware publishes the ACPI RSDP.
typedef struct {
	EFI_GUID VendorGuid;
	void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
	EFI_TABLE_HEADER Hdr;
	CHAR16 *FirmwareVendor;
	uint32_t FirmwareRevision;
	EFI_HANDLE ConsoleInHandle; void *ConIn;
	EFI_HANDLE ConsoleOutHandle; EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
	EFI_HANDLE StandardErrorHandle; EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
	EFI_RUNTIME_SERVICES *RuntimeServices; EFI_BOOT_SERVICES *BootServices;
	uint64_t NumberOfTableEntries; EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

// AArch64 operations Swift cannot express: system registers, cache
// maintenance and the exception-level switch (runtime/arm64.c).
uint64_t nd_current_el(void);
uint64_t nd_cntfrq(void);
uint64_t nd_cntpct(void);
uint64_t nd_cntvct(void);
uint64_t nd_mpidr(void);
void nd_dcache_clean_poc(uint64_t start, uint64_t length);
[[noreturn]] void nd_enter_kernel(uint64_t entry, uint64_t boot_args);
#endif
