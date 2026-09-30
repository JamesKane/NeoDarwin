// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: XNU's OSEntitlements objects are libkern C++ OSObjects, retained and released by the kernel.
//
// ndamfi's entitlements (P1-15, docs/kernel/amfi-provider.md): the AMFI
// table's OSEntitlements members. A code signature's entitlement object is
// an NDEntitlements, which nd_amfi_policy.c attaches only to binaries a
// trust cache lists; any other binary has none, and every query on it is
// denied. Its dictionary holds exactly the entitlements the signature
// carries: from the DER blob (CSSLOT_DER_ENTITLEMENTS, nd_entitlements.c)
// when there is one, else from the XML plist (CSSLOT_ENTITLEMENTS, through
// libkern's OSUnserializeXML). Both blobs are hash-checked against the code
// directory by XNU (csblob_get_der_entitlements, csblob_get_entitlements)
// before they are read. A blob that fails to parse grants nothing.

#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSObject.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSUnserialize.h>
#include <libkern/OSAtomic.h>

extern "C" {
#include <kern/kalloc.h>
#include <libkern/libkern.h>
#include <libkern/amfi/amfi.h>
#include <sys/codesign.h>
#include <sys/proc.h>
#include "nd_amfi_internal.h"
#include "nd_entitlements.h"
}

extern "C" uint64_t proc_getcsflags(proc_t);

class NDEntitlements : public OSObject {
	OSDeclareDefaultStructors(NDEntitlements);

public:
	OSDictionary *dict;       // NULL: no entitlements
	uint8_t *xml;             // a copy of the XML blob, with its header
	size_t xmlLength;
	volatile SInt32 invalid;  // set when the signature stops being valid

	static NDEntitlements *make();
	void free() APPLE_KEXT_OVERRIDE;
	OSDictionary *entitlements() const;
};

OSDefineMetaClassAndStructors(NDEntitlements, OSObject);

NDEntitlements *
NDEntitlements::make()
{
	NDEntitlements *e = OSTypeAlloc(NDEntitlements);
	if (e != nullptr && !e->init()) {
		e->release();
		return nullptr;
	}
	return e;
}

void
NDEntitlements::free()
{
	OSSafeReleaseNULL(dict);
	if (xml != nullptr) {
		kfree_data(xml, xmlLength);
		xml = nullptr;
	}
	OSObject::free();
}

OSDictionary *
NDEntitlements::entitlements() const
{
	return invalid ? nullptr : dict;
}

// -- building the dictionary from DER ---------------------------------------

static char *
cstring(const nd_ent_value_t *v, size_t *size)
{
	*size = v->length + 1;
	char *s = (char *)kalloc_data(*size, Z_WAITOK_ZERO);
	if (s != nullptr) {
		memcpy(s, v->body, v->length);
	}
	return s;
}

static OSObject *object_from_der(const nd_ent_value_t *v);

static OSObject *
string_from_der(const nd_ent_value_t *v)
{
	size_t size;
	char *s = cstring(v, &size);
	if (s == nullptr) {
		return nullptr;
	}
	OSString *o = OSString::withCString(s);
	kfree_data(s, size);
	return o;
}

static OSObject *
object_from_der(const nd_ent_value_t *v)
{
	switch (v->tag) {
	case ND_ENT_TAG_BOOLEAN: {
		bool b = false;
		nd_ent_bool_value(v, &b);
		OSBoolean *o = b ? kOSBooleanTrue : kOSBooleanFalse;
		o->retain();
		return o;
	}
	case ND_ENT_TAG_INTEGER: {
		int64_t i = 0;
		nd_ent_integer_value(v, &i);
		return OSNumber::withNumber((unsigned long long)i, 64);
	}
	case ND_ENT_TAG_STRING:
		return string_from_der(v);
	case ND_ENT_TAG_DATA:
		return OSData::withBytes(v->body, (unsigned int)v->length);
	case ND_ENT_TAG_ARRAY: {
		OSArray *a = OSArray::withCapacity(4);
		nd_ent_iter_t it;
		nd_ent_value_t item;
		nd_ent_iter_init(v, &it);
		while (a != nullptr && nd_ent_iter_next(&it, nullptr, &item)) {
			OSObject *o = object_from_der(&item);
			if (o == nullptr || !a->setObject(o)) {
				OSSafeReleaseNULL(o);
				OSSafeReleaseNULL(a);
				break;
			}
			o->release();
		}
		return a;
	}
	case ND_ENT_TAG_DICT: {
		OSDictionary *d = OSDictionary::withCapacity(8);
		nd_ent_iter_t it;
		nd_ent_value_t key, value;
		nd_ent_iter_init(v, &it);
		while (d != nullptr && nd_ent_iter_next(&it, &key, &value)) {
			size_t size;
			char *k = cstring(&key, &size);
			OSObject *o = k != nullptr ? object_from_der(&value) : nullptr;
			bool ok = o != nullptr && d->setObject(k, o);
			OSSafeReleaseNULL(o);
			if (k != nullptr) {
				kfree_data(k, size);
			}
			if (!ok) {
				OSSafeReleaseNULL(d);
			}
		}
		return d;
	}
	default:
		return nullptr;
	}
}

static OSDictionary *
dict_from_der(const CS_GenericBlob *blob, size_t length)
{
	nd_ent_value_t root;
	if (length < sizeof(CS_GenericBlob) || !nd_ent_parse((const uint8_t *)blob->data, length - sizeof(CS_GenericBlob), &root)) {
		return nullptr;
	}
	return OSDynamicCast(OSDictionary, object_from_der(&root));
}

static OSDictionary *
dict_from_xml(const uint8_t *xml, size_t length)
{
	size_t n = length - sizeof(CS_GenericBlob);
	char *text = (char *)kalloc_data(n + 1, Z_WAITOK_ZERO);
	if (text == nullptr) {
		return nullptr;
	}
	memcpy(text, xml + sizeof(CS_GenericBlob), n);
	OSObject *o = OSUnserializeXML(text, n + 1);
	kfree_data(text, n + 1);
	OSDictionary *d = OSDynamicCast(OSDictionary, o);
	if (d == nullptr) {
		OSSafeReleaseNULL(o);
	}
	return d;
}

// -- ndamfi's interface (nd_amfi_internal.h) ------------------------------------

extern "C" void *
nd_osent_create(void)
{
	return NDEntitlements::make();
}

extern "C" void
nd_osent_release(void *osent)
{
	if (osent != nullptr) {
		((OSObject *)osent)->release();
	}
}

// Called once the signature's storage is final (accelerate_entitlement_queries).
extern "C" kern_return_t
nd_osent_adopt(void *osent, struct cs_blob *blob, unsigned *count)
{
	*count = 0;
	NDEntitlements *e = OSDynamicCast(NDEntitlements, (OSObject *)osent);
	if (e == nullptr || blob == nullptr) {
		return KERN_SUCCESS;  // an untrusted binary: nothing to adopt
	}
	const CS_GenericBlob *der = nullptr;
	size_t der_length = 0;
	void *xml = nullptr;
	size_t xml_length = 0;
	bool der_ok = csblob_get_der_entitlements(blob, &der, &der_length) == 0;
	bool xml_ok = csblob_get_entitlements(blob, &xml, &xml_length) == 0;
	if (!der_ok || !xml_ok) {
		return KERN_DENIED;  // a blob that doesn't match its code directory hash
	}
	if (xml != nullptr && xml_length >= sizeof(CS_GenericBlob)) {
		e->xml = (uint8_t *)kalloc_data(xml_length, Z_WAITOK);
		if (e->xml != nullptr) {
			memcpy(e->xml, xml, xml_length);
			e->xmlLength = xml_length;
		}
	}
	if (der != nullptr) {
		e->dict = dict_from_der(der, der_length);
	} else if (xml != nullptr && xml_length > sizeof(CS_GenericBlob)) {
		e->dict = dict_from_xml((const uint8_t *)xml, xml_length);
	}
	if ((der != nullptr || xml != nullptr) && e->dict == nullptr) {
		printf("ndamfi: %s: entitlements unreadable; none granted\n", csblob_get_identity(blob) ?: "?");
	}
	*count = e->dict != nullptr ? e->dict->getCount() : 0;
	return KERN_SUCCESS;
}

extern "C" void
nd_osent_invalidate(void *osent)
{
	NDEntitlements *e = OSDynamicCast(NDEntitlements, (OSObject *)osent);
	if (e != nullptr) {
		OSCompareAndSwap(0, 1, &e->invalid);
	}
}

extern "C" void *
nd_osent_as_dict(void *osent)
{
	NDEntitlements *e = OSDynamicCast(NDEntitlements, (OSObject *)osent);
	OSDictionary *d = e ? e->entitlements() : nullptr;
	if (d != nullptr) {
		d->retain();
	}
	return d;
}

extern "C" bool
nd_osent_get_xml(void *osent, CS_GenericBlob **blob)
{
	*blob = nullptr;
	NDEntitlements *e = OSDynamicCast(NDEntitlements, (OSObject *)osent);
	if (e == nullptr || e->entitlements() == nullptr || e->xml == nullptr) {
		return false;
	}
	// The caller (csops) frees it with kfree_data(blob, ntohl(blob->length)).
	CS_GenericBlob *copy = (CS_GenericBlob *)kalloc_data(e->xmlLength, Z_WAITOK);
	if (copy == nullptr) {
		return false;
	}
	memcpy(copy, e->xml, e->xmlLength);
	*blob = copy;
	return true;
}

static OSDictionary *
entitlements_of(const void *osent)
{
	NDEntitlements *e = OSDynamicCast(NDEntitlements, static_cast<OSObject *>(const_cast<void *>(osent)));
	return e ? e->entitlements() : nullptr;
}

static OSDictionary *
entitlements_of_proc(proc_t proc)
{
	if (proc == nullptr || (proc_getcsflags(proc) & CS_VALID) == 0) {
		return nullptr;
	}
	struct cs_blob *blob = csproc_get_blob(proc);
	return blob ? entitlements_of(csblob_os_entitlements_get(blob)) : nullptr;
}

static kern_return_t
query_bool(OSDictionary *d, const char *name)
{
	return d != nullptr && name != nullptr && d->getObject(name) == kOSBooleanTrue ? KERN_SUCCESS : KERN_DENIED;
}

static kern_return_t
query_string(OSDictionary *d, const char *name, const char *value)
{
	if (d == nullptr || name == nullptr || value == nullptr) {
		return KERN_DENIED;
	}
	OSObject *o = d->getObject(name);
	if (OSString *s = OSDynamicCast(OSString, o)) {
		return s->isEqualTo(value) ? KERN_SUCCESS : KERN_DENIED;
	}
	if (OSArray *a = OSDynamicCast(OSArray, o)) {
		for (unsigned i = 0; i < a->getCount(); i++) {
			OSString *s = OSDynamicCast(OSString, a->getObject(i));
			if (s != nullptr && s->isEqualTo(value)) {
				return KERN_SUCCESS;
			}
		}
	}
	return KERN_DENIED;
}

static kern_return_t
copy_object(OSDictionary *d, const char *name, void **object)
{
	*object = nullptr;
	OSObject *o = d != nullptr && name != nullptr ? d->getObject(name) : nullptr;
	if (o == nullptr) {
		return KERN_NOT_FOUND;
	}
	o->retain();
	*object = o;
	return KERN_SUCCESS;
}

extern "C" kern_return_t
nd_osent_query_bool(const void *osent, const char *name)
{
	return query_bool(entitlements_of(osent), name);
}

extern "C" kern_return_t
nd_osent_query_bool_proc(const proc_t proc, const char *name)
{
	return query_bool(entitlements_of_proc(proc), name);
}

extern "C" kern_return_t
nd_osent_query_string(const void *osent, const char *name, const char *value)
{
	return query_string(entitlements_of(osent), name, value);
}

extern "C" kern_return_t
nd_osent_query_string_proc(const proc_t proc, const char *name, const char *value)
{
	return query_string(entitlements_of_proc(proc), name, value);
}

extern "C" kern_return_t
nd_osent_copy_object(const void *osent, const char *name, void **object)
{
	return copy_object(entitlements_of(osent), name, object);
}

extern "C" kern_return_t
nd_osent_copy_object_proc(const proc_t proc, const char *name, void **object)
{
	return copy_object(entitlements_of_proc(proc), name, object);
}
