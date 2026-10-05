// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a test of CoreFoundation's C interface, as C programs call it.
// CoreFoundation's smoke test (docs/base/corefoundation.md): a dictionary of
// CFStrings, CFNumbers, an array, data, a date and a boolean, written as an
// XML property list, parsed back and compared; the same through the binary
// format; and a UTF-8 round trip. Prints "cftest: ok" and exits 0, or
// prints what failed and exits 1.
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what) {
	if (!ok) { printf("cftest: FAIL %s\n", what); failures++; }
}

static CFPropertyListRef roundtrip(CFPropertyListRef plist, CFPropertyListFormat format, const char *name) {
	CFErrorRef error = NULL;
	CFDataRef data = CFPropertyListCreateData(NULL, plist, format, 0, &error);
	check(data != NULL, name);
	if (data == NULL) return NULL;
	if (format == kCFPropertyListXMLFormat_v1_0) {
		check(CFDataGetLength(data) > 6 && memcmp(CFDataGetBytePtr(data), "<?xml ", 6) == 0, "XML header");
		check(strstr((const char *)CFDataGetBytePtr(data), "<key>name</key>") != NULL, "XML key");
	}
	CFPropertyListFormat got = 0;
	CFPropertyListRef back = CFPropertyListCreateWithData(NULL, data, kCFPropertyListImmutable, &got, &error);
	check(back != NULL && got == format, name);
	CFRelease(data);
	return back;
}

int main(void) {
	int32_t answer = 42;
	double pi = 3.25;
	CFNumberRef n1 = CFNumberCreate(NULL, kCFNumberSInt32Type, &answer);
	CFNumberRef n2 = CFNumberCreate(NULL, kCFNumberDoubleType, &pi);
	CFStringRef s = CFStringCreateWithCString(NULL, "caf\xc3\xa9 \xe2\x98\x95", kCFStringEncodingUTF8);
	const void *items[] = {CFSTR("one"), n1, s};
	CFArrayRef array = CFArrayCreate(NULL, items, 3, &kCFTypeArrayCallBacks);
	CFDataRef bytes = CFDataCreate(NULL, (const UInt8 *)"\x00\x01\xfe\xff", 4);
	CFDateRef date = CFDateCreate(NULL, 800000000.0);
	CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(dict, CFSTR("name"), CFSTR("NeoDarwin"));
	CFDictionarySetValue(dict, CFSTR("answer"), n1);
	CFDictionarySetValue(dict, CFSTR("pi"), n2);
	CFDictionarySetValue(dict, CFSTR("unicode"), s);
	CFDictionarySetValue(dict, CFSTR("list"), array);
	CFDictionarySetValue(dict, CFSTR("bytes"), bytes);
	CFDictionarySetValue(dict, CFSTR("date"), date);
	CFDictionarySetValue(dict, CFSTR("flag"), kCFBooleanTrue);

	CFPropertyListRef xml = roundtrip(dict, kCFPropertyListXMLFormat_v1_0, "XML property list");
	check(xml != NULL && CFEqual(xml, dict), "XML round trip equal");
	CFPropertyListRef bin = roundtrip(dict, kCFPropertyListBinaryFormat_v1_0, "binary property list");
	check(bin != NULL && CFEqual(bin, dict), "binary round trip equal");
	if (xml) {
		int32_t v = 0;
		CFNumberRef n = CFDictionaryGetValue(xml, CFSTR("answer"));
		check(n && CFGetTypeID(n) == CFNumberGetTypeID() && CFNumberGetValue(n, kCFNumberSInt32Type, &v) && v == 42, "number value");
		CFStringRef u = CFDictionaryGetValue(xml, CFSTR("unicode"));
		char buf[32];
		check(u && CFStringGetCString(u, buf, sizeof(buf), kCFStringEncodingUTF8) && strcmp(buf, "caf\xc3\xa9 \xe2\x98\x95") == 0, "UTF-8 round trip");
		check(u && CFStringGetLength(u) == 6, "UTF-16 length");
	}
	CFStringRef f = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@-%d-%.2f"), CFSTR("fmt"), 7, 1.5);
	check(CFEqual(f, CFSTR("fmt-7-1.50")), "CFStringCreateWithFormat");
	check(CFStringCompare(CFSTR("abc"), CFSTR("ABC"), kCFCompareCaseInsensitive) == kCFCompareEqualTo, "case-insensitive compare");
	CFStringRef latin = CFStringCreateWithBytes(NULL, (const UInt8 *)"\xe9t\xe9", 3, kCFStringEncodingISOLatin1, false);
	check(latin && CFStringGetLength(latin) == 3 && CFStringGetCharacterAtIndex(latin, 0) == 0xe9, "ISO Latin 1");
	if (failures == 0) printf("cftest: ok\n");
	return failures != 0;
}
