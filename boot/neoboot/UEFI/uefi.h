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
	EFI_TABLE_HEADER Hdr;
	void *GetTime, *SetTime, *GetWakeupTime, *SetWakeupTime;
	void *SetVirtualAddressMap, *ConvertPointer;
	void *GetVariable, *GetNextVariableName, *SetVariable;
	void *GetNextHighMonotonicCount;
	EFI_RESET_SYSTEM ResetSystem;
} EFI_RUNTIME_SERVICES;
typedef struct {
	EFI_TABLE_HEADER Hdr;
	CHAR16 *FirmwareVendor;
	uint32_t FirmwareRevision;
	EFI_HANDLE ConsoleInHandle; void *ConIn;
	EFI_HANDLE ConsoleOutHandle; EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
	EFI_HANDLE StandardErrorHandle; EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
	EFI_RUNTIME_SERVICES *RuntimeServices; void *BootServices;
	uint64_t NumberOfTableEntries; void *ConfigurationTable;
} EFI_SYSTEM_TABLE;
#endif
