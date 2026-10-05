// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: Embedded Swift can't import CoreFoundation's or IOKitLib's headers (CF objects are Unmanaged<AnyObject>, and CF's Dispatch import warns), so their calls are wrapped in plain C types.
//
// The CoreFoundation and IOKitLib calls NeoDarwin's Swift administration
// tools make (pciconf, acpidump), and the libSystem headers they use. A CF
// object is an opaque nd_cf_t: the calls that say "retained" return one the
// caller releases with nd_cf_release; the rest return one owned by their
// argument. An I/O Registry object is its Mach port name (io_object_t),
// released with nd_io_release. src/nd_iokit.c implements them.
#ifndef ND_IOKIT_H
#define ND_IOKIT_H
#include <errno.h>
#include <spawn.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef const void *nd_cf_t;
typedef uint32_t nd_io_t;

enum nd_cf_kind {
	ND_CF_OTHER = 0,
	ND_CF_DICTIONARY,
	ND_CF_ARRAY,
	ND_CF_DATA,
	ND_CF_STRING,
	ND_CF_NUMBER,
	ND_CF_BOOLEAN,
};

// CoreFoundation.
void nd_cf_release(nd_cf_t _Nullable object);
enum nd_cf_kind nd_cf_kind(nd_cf_t _Nonnull object);
size_t nd_cf_data_length(nd_cf_t _Nonnull data);
const uint8_t *_Nullable nd_cf_data_bytes(nd_cf_t _Nonnull data);
bool nd_cf_number_value(nd_cf_t _Nonnull number, int64_t *_Nonnull value);
// The string's UTF-8 into buf (NUL-terminated); false if it doesn't fit.
bool nd_cf_string_copy(nd_cf_t _Nonnull string, char *_Nonnull buf, size_t size);
size_t nd_cf_dictionary_count(nd_cf_t _Nonnull dictionary);
// count entries each into keys and values, owned by the dictionary.
void nd_cf_dictionary_entries(nd_cf_t _Nonnull dictionary, nd_cf_t _Nullable *_Nonnull keys,
    nd_cf_t _Nullable *_Nonnull values);
// The value under a string key, owned by the dictionary; NULL if none.
nd_cf_t _Nullable nd_cf_dictionary_value(nd_cf_t _Nonnull dictionary, const char *_Nonnull key);
// A property list file (XML or binary), retained; NULL if it can't be read.
nd_cf_t _Nullable nd_cf_property_list_read(const char *_Nonnull path);

// IOKitLib: the I/O Registry's service plane.
void nd_io_release(nd_io_t object);
// An iterator over the services of a class (IOServiceGetMatchingServices); 0 on failure.
nd_io_t nd_io_matching_services(const char *_Nonnull class_name);
nd_io_t nd_io_iterator_next(nd_io_t iterator);
// An iterator over an entry's children in the service plane; 0 on failure.
nd_io_t nd_io_child_iterator(nd_io_t entry);
nd_io_t nd_io_root(void);
// The entry's class name (IOObjectGetClass) into buf; false on failure.
bool nd_io_class_name(nd_io_t entry, char *_Nonnull buf, size_t size);
// A property of the entry, retained; NULL if it has none.
nd_cf_t _Nullable nd_io_property(nd_io_t entry, const char *_Nonnull key);
// The property on the entry or, searching recursively in the service plane,
// its children (or, with parents, its ancestors), retained; NULL if none has it.
nd_cf_t _Nullable nd_io_search_property(nd_io_t entry, const char *_Nonnull key, bool parents);

// stdio's stderr and stdout, macros Swift doesn't import: a line to stderr,
// and stdout itself.
void nd_warn(const char *_Nonnull message);
FILE *_Nonnull nd_stdout(void);
#endif
