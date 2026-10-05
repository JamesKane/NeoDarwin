<!-- SPDX-License-Identifier: BSD-2-Clause -->
# CoreFoundation and IOKitLib (P4-21 checkpoint 6b)

**Goal.** The base gets its first frameworks: `CoreFoundation.framework` and `IOKit.framework`, with Apple's install names, so that IOKitLib and the Apple tools built on it (`ioreg`, `iostat`, `top`) build from source and run. `ioreg` is the proof (`docs/architecture/freebsd-parity.md` §2.1).

## 1. Decision: swift-corelibs-foundation's CoreFoundation (user, 2026-10-05)

macOS 26's release set (`distribution-macOS` tag `macos-260`) publishes IOKitUser-100222.0.4 (IOKitLib), IOKitTools-125 (`ioreg`), top-144 and system_cmds' `iostat`. It doesn't publish CoreFoundation, and all of them need it. Apple's last CF-Lite release (CF-1153.18, APSL) is a decade old.

The base's CoreFoundation is **swift-corelibs-foundation's** (Apache-2.0 with the Runtime Library Exception), built as the pure C CF that swift-corelibs uses on its own: no Swift runtime. This is the CoreFoundation that open-source Foundation is built on, and the base is planned to gain Foundation later (P2-07).

**No ICU.** The release set has ICU-76133, but its data is about 30 MB. CF builds without it (`CF_NO_ICU`, below). Adding ICU later means building ICU-76133 and dropping patch 0002 and `nd_cflocale.c`.

## 2. CoreFoundation.framework

| | |
|---|---|
| Source | `swift-corelibs-foundation` tag `swift-6.4.0-RELEASE`, `Sources/CoreFoundation` (pinned by sha256 in `MODULE.bazel` and `base/upstream.lock`) |
| Target | `//base:corefoundation_framework`, `base/corefoundation/build.sh` |
| Installs | `/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation` (1.7 MB), install name as on macOS, compatibility version 150 (the SDK's; programs linked against `CoreFoundation.tbd` require it). `tools/base/stage_root.sh` adds `Versions/Current` and the top-level link |
| Headers | build-only, `usr/local/frameworks/CoreFoundation.framework/Headers` of the install tree; base programs compile against them with `-iframework` ahead of the SDK's |
| Exports | 1,592; 1,501 are in the macOS 27 SDK's `CoreFoundation.tbd` (3,001 C symbols there, plus its Objective-C classes), 91 are swift-corelibs' own SPI. `base/corefoundation/exports.txt` (`//base:CoreFoundation_exports_test`) |

**How it builds.** The flags are the project's `_Foundation_common_build_flags` (its `CMakeLists.txt`) without the Swift ones. `DEPLOYMENT_RUNTIME_SWIFT` is 0, and there's no `-fcf-runtime-abi=swift`. So `CFRuntimeBase` has Apple's layout (isa, then the info word), which is the layout clang's `CFSTR` constants use on Darwin. Constant strings in any program, ours or built against the SDK, point at `___CFConstantStringClassReference`, which CF defines. The public `kCF*` names that alias CF's internal ones come from the project's `SymbolAliases`, linked with `-alias_list`, as its Darwin build does.

**What's in.**
- The collections.
- CFString with every built-in encoding: UTF-8, UTF-16 and UTF-32, ASCII, ISO Latin 1, Mac Roman, the Windows and NextStep sets, and the rest of `CFBuiltinConverters.c`.
- CFNumber, CFData, CFDate, CFUUID, CFURL, CFError, CFCharacterSet.
- Property lists: XML, binary and the old style.
- CFBundle, CFPlugIn, CFPreferences.
- CFRunLoop with Mach port sources, CFSocket, CFStream.
- CFTimeZone: its own TZif reader; offsets, abbreviations and transitions.
- CFAttributedString, CFTree, CFStorage, CFBurstTrie.

**What's out, and what changes without ICU** (patch 0002, `CF_NO_ICU`):
- **Not built:** CFLocale and CFLocaleIdentifier (replaced by `src/nd_cflocale.c`, below), CFCalendar and its enumeration, CFDateComponents, CFDateFormatter, CFDateIntervalFormatter, CFNumberFormatter, CFListFormatter, CFRelativeDateTimeFormatter, CFRegularExpression, CFStringTransform, and the ICU string converters (`CFICUConverters.c`). The Windows-only sources aren't built either.
- **CFLocale is an identifier.** `nd_cflocale.c` implements the calls the rest of CF makes:
  - `CFLocaleCreate`, `CFLocaleCopyCurrent` (from `LC_ALL`, `LC_MESSAGES` or `LANG`; C and POSIX are `en_US_POSIX`) and `CFLocaleGetSystem`;
  - `CFLocaleGetIdentifier` and `CFLocaleGetValue` (the identifier, `.` and `,` as the separators, nothing else);
  - the canonicalization calls, which return the identifier as given.
- **CFString:**
  - grapheme clusters use CF's own `Grapheme_Extend` bitmap and the skin-tone modifiers. Without `Extended_Pictographic`, `Prepend` and `SpacingMark`, an emoji ZWJ sequence is several clusters;
  - localized number formatting (`%d` with a CFLocale as format options) falls back to the C locale's;
  - comparisons with `kCFCompareLocalized` use CF's code-point order, swift-corelibs' non-ICU path;
  - only the built-in encodings are available. `CFStringIsEncodingAvailable` is false for the ICU-backed ones: EUC, Shift-JIS, GB 18030, Big5, KOI8 and so on.
- **CFTimeZone:** `CFTimeZoneCopyLocalizedName` returns NULL, a named zone is valid if its TZif data loads, and `__CFTimeZoneCopyDataVersionString` is empty.
- **CFBundle:** localizations are matched by name, with no canonicalization and no language and region codes.

**Other patches and NeoDarwin sources:**

| | Why |
|---|---|
| 0001 pure C CF on Darwin | The non-Swift configuration had bit-rotted on Darwin. `DEPLOYMENT_RUNTIME_SWIFT` defaults to 0 in the installed headers. `CFInternal.h` always includes `ForSwiftFoundationOnly.h` (`_CFThreadRef`). The allocator type ID is named `_kCFRuntimeIDCFAllocator`. `OS_LOG_SUBSYSTEM_RUNTIME_ISSUES` (private) gets its value. The legacy stream thread is named with `pthread_setname_np`. `CFTargetConditionals.h` defines the non-Darwin `TARGET_OS_*` names, which clang warns about otherwise |
| 0003 no sysdir | `CFSystemDirectories` calls `sysdir_*`, in the unpublished `libsystem_coreservices`. The search paths are empty |
| `src/nd_cfmachport.c` | swift-corelibs doesn't carry `CFMachPort.c`, but CFRunLoop runs Mach port (version 1) sources on Darwin, and IOKitLib's `IONotificationPortGetRunLoopSource` wraps its port in a CFMachPort. This is the public interface on top of those sources: unique per port while valid, as in Apple's CF; no dead-name notifications. It also has `__CFMachMessageCheckForAndDestroyUnsentMessage` and `_CFGetCurrentDirectory`, which CFRunLoop and CFURL call and which aren't in the source drop |

**Tests.**
- `//base:CoreFoundation_exports_test`.
- `//base:corefoundation_host_test` builds `base/corefoundation/cftest.c` against the framework's headers and runs it on the build machine, against a renamed copy of the binary:
  - a CFDictionary of CFStrings, CFNumbers, an array, data, a date and a boolean, written as an XML and a binary property list and parsed back equal;
  - UTF-8 and ISO Latin 1 round trips, `CFStringCreateWithFormat`, a case-insensitive comparison.
- On QEMU, `ioreg -l` and `ioreg -a` exercise CF: CFDictionary, CFString, CFNumber and CFData from the kernel's properties, and CF's XML property list writer.

**ISA audit.** The framework is in `//base:base_isa_audit` with no new findings. CF's hand-written paths (`CFBasicHashFindBucket.inc`, CFUniChar's tables) are C; nothing in it needs more than Armv8.2.

## 3. IOKit.framework (IOKitLib's core)

| | |
|---|---|
| Source | IOKitUser-100222.0.4 (`MODULE.bazel`, `base/upstream.lock`) |
| Target | `//base:iokit_framework`, `base/iokit/build.sh` |
| Installs | `/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit` (128 KB), current version 275 (the SDK's) |
| Headers | build-only, `usr/local/frameworks/IOKit.framework/Headers`: xnu's IOKit headers, public then private, with IOKitUser's own on top |
| Exports | 138, all in the macOS 27 SDK's `IOKit.tbd` (`base/iokit/exports.txt`, `//base:IOKit_exports_test`). The SDK lists 2,377, mostly the families |

**Scope.**
- `IOKitLib.c`: the main port, the registry and its iterators, notifications (`IONotificationPort*`, `IOServiceAdd*Notification`), `IOServiceOpen` and the `IOConnect*` calls, the matching dictionaries, properties.
- `IOCFSerialize.c` and `IOCFUnserialize.tab.c`: the XML and binary forms the kernel speaks.
- `iokitmig.c` with the MIG user stubs of xnu's `device.defs`, generated at build time as `DeviceMIG.sh` does.
- `iokit_user_client_trap` (Mach trap −100): `src/nd_iotrap.c`. `IOTrap.s` uses a `kernel_trap` macro xnu publishes only for 32-bit Arm and i386.

**Left out:**
- the families: HID, power management, graphics, network, USB, kext, audio, display;
- `IOCFPlugIn` and `IOBundle`, which load CFPlugIns;
- `IOCFURLAccess`, which uses `getdirentries`;
- the data queues.

Missing core exports against Apple's list: `IOCFURLWriteDataAndPropertiesToResource` (IOCFURLAccess) and `IOServiceGetWaitInfo` (not in the published source).

**Patch 0001, no IOServiceAuthorizeAgent.** `IOServiceAuthorize` and `IOServiceOpenAsFileDescriptor` ask that agent over XPC. NeoDarwin has neither the agent nor XPC connections, so they return what they return when the agent can't be reached.

**Kernel side.** No kernel changes were needed. IOKitLib must build against xnu's *private* `<device/device_types.h>`, as Apple's build against the internal SDK does: it sets `IOKIT_SERVER_VERSION` (20210810), which selects the binary property calls (`io_registry_entry_get_properties_bin_buf`). Against the public header, IOKitLib falls back to the XML calls, the kernel refuses them, and every `IORegistryEntryCreateCFProperties` fails. The MIG stubs need the private `<mach/message.h>` too (`mach_msg2`, `MACH64_SEND_KOBJECT_CALL`). On QEMU, these all work:
- `IOServiceGetMatchingServices` (by class);
- `IORegistryEntryCreateCFProperties`, on every entry of the service plane;
- the plane iterators, names, locations and entry IDs;
- `IORegistryGetRootEntry` and the device tree plane.

Notifications and `IOServiceOpen` haven't been exercised yet.

## 4. ioreg

`//base:ioreg_command` (`base/iokittools/build.sh`): `/usr/sbin/ioreg` from IOKitTools-125, linked against both frameworks and ncurses, with `ioreg.8`. FreeBSD's `devinfo` is `equivalent` to it. `//kernel:sbsa_base_commands_test` checks:
- `ioreg -l`: the platform expert's three lines, and no "can't obtain properties";
- `ioreg -c IOMedia`: an empty answer, exit 0. QEMU's SBSA machine in this test has no IOMedia;
- `ioreg -r -c IOResources`: the `AppleFSCompression.Type1` property's value;
- `ioreg -p IODeviceTree`: the device tree plane;
- `ioreg -a`: an XML property list;
- `man -w ioreg`.

## 5. Next (6b, part 2)

`iostat`, `top`, `pciconf` and `acpidump`, and the CF smoke test on QEMU. The notes are in `docs/architecture/freebsd-parity.md` §2.1, "Left for the next agent".
