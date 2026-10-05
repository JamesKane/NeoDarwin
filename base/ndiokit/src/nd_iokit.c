// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: Embedded Swift can't import CoreFoundation's or IOKitLib's headers (CF objects are Unmanaged<AnyObject>, and CF's Dispatch import warns), so their calls are wrapped in plain C types.
//
// NDIOKit/nd_iokit.h's calls, over the base's CoreFoundation.framework and
// IOKit.framework (docs/base/corefoundation.md).
#include "nd_iokit.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

void
nd_cf_release(nd_cf_t object)
{
	if (object != NULL) {
		CFRelease(object);
	}
}

enum nd_cf_kind
nd_cf_kind(nd_cf_t object)
{
	CFTypeID t = CFGetTypeID(object);
	if (t == CFDictionaryGetTypeID()) {
		return ND_CF_DICTIONARY;
	}
	if (t == CFArrayGetTypeID()) {
		return ND_CF_ARRAY;
	}
	if (t == CFDataGetTypeID()) {
		return ND_CF_DATA;
	}
	if (t == CFStringGetTypeID()) {
		return ND_CF_STRING;
	}
	if (t == CFNumberGetTypeID()) {
		return ND_CF_NUMBER;
	}
	if (t == CFBooleanGetTypeID()) {
		return ND_CF_BOOLEAN;
	}
	return ND_CF_OTHER;
}

size_t
nd_cf_data_length(nd_cf_t data)
{
	return (size_t)CFDataGetLength((CFDataRef)data);
}

const uint8_t *
nd_cf_data_bytes(nd_cf_t data)
{
	return CFDataGetBytePtr((CFDataRef)data);
}

bool
nd_cf_number_value(nd_cf_t number, int64_t *value)
{
	long long v = 0;
	bool ok = CFNumberGetValue((CFNumberRef)number, kCFNumberLongLongType, &v);
	*value = v;
	return ok;
}

bool
nd_cf_string_copy(nd_cf_t string, char *buf, size_t size)
{
	return CFStringGetCString((CFStringRef)string, buf, (CFIndex)size, kCFStringEncodingUTF8);
}

size_t
nd_cf_dictionary_count(nd_cf_t dictionary)
{
	return (size_t)CFDictionaryGetCount((CFDictionaryRef)dictionary);
}

void
nd_cf_dictionary_entries(nd_cf_t dictionary, nd_cf_t *keys, nd_cf_t *values)
{
	CFDictionaryGetKeysAndValues((CFDictionaryRef)dictionary, keys, values);
}

static CFStringRef
cfstring(const char *s)
{
	return CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
}

nd_cf_t
nd_cf_dictionary_value(nd_cf_t dictionary, const char *key)
{
	CFStringRef k = cfstring(key);
	if (k == NULL) {
		return NULL;
	}
	const void *v = CFDictionaryGetValue((CFDictionaryRef)dictionary, k);
	CFRelease(k);
	return v;
}

nd_cf_t
nd_cf_property_list_read(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		return NULL;
	}
	CFMutableDataRef data = CFDataCreateMutable(kCFAllocatorDefault, 0);
	UInt8 buf[65536];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		CFDataAppendBytes(data, buf, n);
	}
	close(fd);
	if (n < 0) {
		CFRelease(data);
		return NULL;
	}
	CFPropertyListRef plist = CFPropertyListCreateWithData(kCFAllocatorDefault, data,
	    kCFPropertyListImmutable, NULL, NULL);
	CFRelease(data);
	return plist;
}

void
nd_io_release(nd_io_t object)
{
	if (object != IO_OBJECT_NULL) {
		IOObjectRelease(object);
	}
}

nd_io_t
nd_io_matching_services(const char *class_name)
{
	io_iterator_t it = IO_OBJECT_NULL;
	// IOServiceGetMatchingServices consumes the matching dictionary.
	if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(class_name), &it) != KERN_SUCCESS) {
		return IO_OBJECT_NULL;
	}
	return it;
}

nd_io_t
nd_io_iterator_next(nd_io_t iterator)
{
	return IOIteratorNext(iterator);
}

nd_io_t
nd_io_child_iterator(nd_io_t entry)
{
	io_iterator_t it = IO_OBJECT_NULL;
	if (IORegistryEntryGetChildIterator(entry, kIOServicePlane, &it) != KERN_SUCCESS) {
		return IO_OBJECT_NULL;
	}
	return it;
}

nd_io_t
nd_io_root(void)
{
	return IORegistryGetRootEntry(kIOMainPortDefault);
}

bool
nd_io_class_name(nd_io_t entry, char *buf, size_t size)
{
	io_name_t name;
	if (IOObjectGetClass(entry, name) != KERN_SUCCESS) {
		return false;
	}
	return strlcpy(buf, name, size) < size;
}

nd_cf_t
nd_io_property(nd_io_t entry, const char *key)
{
	CFStringRef k = cfstring(key);
	if (k == NULL) {
		return NULL;
	}
	CFTypeRef v = IORegistryEntryCreateCFProperty(entry, k, kCFAllocatorDefault, 0);
	CFRelease(k);
	return v;
}

nd_cf_t
nd_io_search_property(nd_io_t entry, const char *key, bool parents)
{
	CFStringRef k = cfstring(key);
	if (k == NULL) {
		return NULL;
	}
	IOOptionBits options = kIORegistryIterateRecursively | (parents ? kIORegistryIterateParents : 0);
	CFTypeRef v = IORegistryEntrySearchCFProperty(entry, kIOServicePlane, k, kCFAllocatorDefault, options);
	CFRelease(k);
	return v;
}

void
nd_warn(const char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
}

FILE *
nd_stdout(void)
{
	return stdout;
}
