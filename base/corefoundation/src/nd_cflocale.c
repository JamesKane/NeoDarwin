// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: CoreFoundation's C interface, compiled inside swift-corelibs-foundation's C CoreFoundation.
// CFLocale without ICU (docs/base/corefoundation.md): a locale is its
// identifier and nothing else. swift-corelibs-foundation's CFLocale.c and
// CFLocaleIdentifier.c are built on ICU's locale data, which this build of
// CoreFoundation leaves out; CFString and CFBundle still call the handful of
// functions here. Values a locale would look up in ICU's data answer as the
// POSIX locale does: "." and "," as separators, no special case mapping.
#include "CFInternal.h"
#include "CFRuntime_Internal.h"
#include "CFLocale.h"
#include <stdlib.h>
#include <string.h>

struct __CFLocale {
	CFRuntimeBase _base;
	CFStringRef _identifier;
	_Atomic(Boolean) _noSpecialCaseHandling;
};

CONST_STRING_DECL(__kCFLocaleCollatorID, "locale:collator id");

// CFLocaleKeys.c's names; the public kCFLocale* names alias them
// (SymbolAliases, linked with -alias_list).
extern CFStringRef const kCFLocaleIdentifierKey;
extern CFStringRef const kCFLocaleDecimalSeparatorKey;
extern CFStringRef const kCFLocaleGroupingSeparatorKey;

static void __CFLocaleDeallocate(CFTypeRef cf) {
	CFRelease(((struct __CFLocale *)cf)->_identifier);
}

static Boolean __CFLocaleEqual(CFTypeRef cf1, CFTypeRef cf2) {
	return CFEqual(((struct __CFLocale *)cf1)->_identifier, ((struct __CFLocale *)cf2)->_identifier);
}

static CFHashCode __CFLocaleHash(CFTypeRef cf) {
	return CFHash(((struct __CFLocale *)cf)->_identifier);
}

static CFStringRef __CFLocaleCopyDescription(CFTypeRef cf) {
	return CFStringCreateWithFormat(kCFAllocatorSystemDefault, NULL, CFSTR("<CFLocale %p [%p]>{identifier = '%@'}"),
	    cf, CFGetAllocator(cf), ((struct __CFLocale *)cf)->_identifier);
}

const CFRuntimeClass __CFLocaleClass = {
	0, "CFLocale", NULL, NULL, __CFLocaleDeallocate, __CFLocaleEqual, __CFLocaleHash, NULL, __CFLocaleCopyDescription
};

CFTypeID CFLocaleGetTypeID(void) {
	return _kCFRuntimeIDCFLocale;
}

CFLocaleRef CFLocaleCreate(CFAllocatorRef allocator, CFLocaleIdentifier identifier) {
	if (identifier == NULL) return NULL;
	struct __CFLocale *locale = (struct __CFLocale *)_CFRuntimeCreateInstance(allocator, _kCFRuntimeIDCFLocale,
	    sizeof(struct __CFLocale) - sizeof(CFRuntimeBase), NULL);
	if (locale == NULL) return NULL;
	locale->_identifier = CFStringCreateCopy(kCFAllocatorSystemDefault, identifier);
	locale->_noSpecialCaseHandling = false;
	return locale;
}

CFLocaleRef CFLocaleCreateCopy(CFAllocatorRef allocator, CFLocaleRef locale) {
	return (CFLocaleRef)CFRetain(locale);
}

CFLocaleRef CFLocaleGetSystem(void) {
	static CFLocaleRef system;
	static dispatch_once_t once;
	dispatch_once(&once, ^{ system = CFLocaleCreate(kCFAllocatorSystemDefault, CFSTR("")); });
	return system;
}

// The current locale: the POSIX locale environment's language and region
// (LC_ALL, then LC_MESSAGES, then LANG; "en_US.UTF-8" is "en_US"). The C
// and POSIX locales are "en_US_POSIX", as on macOS.
CFLocaleRef CFLocaleCopyCurrent(void) {
	const char *env = NULL;
	const char *names[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]) && (env == NULL || *env == '\0'); i++) env = getenv(names[i]);
	char buf[64] = "en_US_POSIX";
	if (env != NULL && *env != '\0' && strcmp(env, "C") != 0 && strcmp(env, "POSIX") != 0) {
		size_t n = strcspn(env, ".@");
		if (n > 0 && n < sizeof(buf)) { memcpy(buf, env, n); buf[n] = '\0'; }
	}
	CFStringRef identifier = CFStringCreateWithCString(kCFAllocatorSystemDefault, buf, kCFStringEncodingASCII);
	if (identifier == NULL) identifier = (CFStringRef)CFRetain(CFSTR("en_US_POSIX"));
	CFLocaleRef locale = CFLocaleCreate(kCFAllocatorSystemDefault, identifier);
	CFRelease(identifier);
	return locale;
}

CFLocaleIdentifier CFLocaleGetIdentifier(CFLocaleRef locale) {
	__CFGenericValidateType(locale, CFLocaleGetTypeID());
	return ((struct __CFLocale *)locale)->_identifier;
}

CFTypeRef CFLocaleGetValue(CFLocaleRef locale, CFLocaleKey key) {
	__CFGenericValidateType(locale, CFLocaleGetTypeID());
	if (CFEqual(key, kCFLocaleIdentifierKey) || CFEqual(key, __kCFLocaleCollatorID)) return ((struct __CFLocale *)locale)->_identifier;
	if (CFEqual(key, kCFLocaleDecimalSeparatorKey)) return CFSTR(".");
	if (CFEqual(key, kCFLocaleGroupingSeparatorKey)) return CFSTR(",");
	return NULL;
}

CF_PRIVATE Boolean __CFLocaleGetDoesNotRequireSpecialCaseHandling(struct __CFLocale *locale) {
	return locale->_noSpecialCaseHandling;
}

CF_PRIVATE void __CFLocaleSetDoesNotRequireSpecialCaseHandling(struct __CFLocale *locale) {
	locale->_noSpecialCaseHandling = true;
}

// Canonicalization needs ICU's alias tables: identifiers are taken as given.
CFLocaleIdentifier CFLocaleCreateCanonicalLanguageIdentifierFromString(CFAllocatorRef allocator, CFStringRef string) {
	return string ? CFStringCreateCopy(allocator, string) : NULL;
}

CFLocaleIdentifier CFLocaleCreateCanonicalLocaleIdentifierFromString(CFAllocatorRef allocator, CFStringRef string) {
	return string ? CFStringCreateCopy(allocator, string) : NULL;
}

CFLocaleIdentifier CFLocaleCreateCanonicalLocaleIdentifierFromScriptManagerCodes(CFAllocatorRef allocator, LangCode lcode, RegionCode rcode) {
	return NULL;
}

Boolean CFLocaleGetLanguageRegionEncodingForLocaleIdentifier(CFStringRef localeIdentifier, LangCode *langCode, RegionCode *regCode, ScriptCode *scriptCode, CFStringEncoding *stringEncoding) {
	return false;
}
