// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the kernel's code-signing path and ndamfi, which call it, are C.
//
// NeoDarwin's reader for CoreEntitlements' DER entitlements (nd_entitlements.h).
// No allocation and no recursion beyond ND_ENT_MAX_DEPTH, so it runs in the
// kernel as it does in the host test.

#include <string.h>
#include "nd_entitlements.h"

// One DER element at p: its tag, contents and the byte after it. Single-byte
// tags, definite minimal lengths up to 32 bits.
static bool
element(const uint8_t *p, const uint8_t *end, nd_ent_value_t *v, const uint8_t **after)
{
	if (p >= end || end - p < 2) {
		return false;
	}
	uint8_t tag = p[0];
	if ((tag & 0x1f) == 0x1f) {
		return false;  // high tag number form
	}
	uint8_t l0 = p[1];
	const uint8_t *q = p + 2;
	size_t length;
	if (l0 < 0x80) {
		length = l0;
	} else {
		size_t n = l0 & 0x7f;
		if (n == 0 || n > 4 || (size_t)(end - q) < n || q[0] == 0) {
			return false;  // indefinite, too long, truncated or not minimal
		}
		length = 0;
		for (size_t i = 0; i < n; i++) {
			length = (length << 8) | q[i];
		}
		if (length < 0x80) {
			return false;  // not minimal
		}
		q += n;
	}
	if (length > (size_t)(end - q)) {
		return false;
	}
	v->tag = tag;
	v->body = q;
	v->length = length;
	*after = q + length;
	return true;
}

static bool
string_valid(const nd_ent_value_t *v)
{
	for (size_t i = 0; i < v->length; i++) {
		if (v->body[i] == 0) {
			return false;
		}
	}
	return true;
}

// Key order: bytewise, a prefix first.
static int
key_compare(const nd_ent_value_t *a, const nd_ent_value_t *b)
{
	size_t n = a->length < b->length ? a->length : b->length;
	int c = memcmp(a->body, b->body, n);
	if (c != 0) {
		return c;
	}
	return a->length < b->length ? -1 : a->length > b->length ? 1 : 0;
}

static bool value_valid(const nd_ent_value_t *v, int depth);

static bool
dict_valid(const nd_ent_value_t *d, int depth)
{
	const uint8_t *p = d->body, *end = d->body + d->length;
	nd_ent_value_t previous = { 0 };
	bool first = true;
	while (p < end) {
		nd_ent_value_t entry, key, value;
		const uint8_t *after, *q;
		if (!element(p, end, &entry, &after) || entry.tag != ND_ENT_TAG_ARRAY) {
			return false;
		}
		const uint8_t *eend = entry.body + entry.length;
		if (!element(entry.body, eend, &key, &q) || key.tag != ND_ENT_TAG_STRING || !string_valid(&key)) {
			return false;
		}
		const uint8_t *r;
		if (!element(q, eend, &value, &r) || r != eend || !value_valid(&value, depth + 1)) {
			return false;
		}
		if (!first && key_compare(&previous, &key) >= 0) {
			return false;  // unsorted or duplicate
		}
		previous = key;
		first = false;
		p = after;
	}
	return true;
}

static bool
value_valid(const nd_ent_value_t *v, int depth)
{
	if (depth > ND_ENT_MAX_DEPTH) {
		return false;
	}
	switch (v->tag) {
	case ND_ENT_TAG_BOOLEAN:
		return v->length == 1 && (v->body[0] == 0x00 || v->body[0] == 0xff);
	case ND_ENT_TAG_INTEGER:
		if (v->length == 0 || v->length > 8) {
			return false;
		}
		// Minimal two's complement.
		if (v->length > 1 && ((v->body[0] == 0x00 && (v->body[1] & 0x80) == 0) ||
		    (v->body[0] == 0xff && (v->body[1] & 0x80) != 0))) {
			return false;
		}
		return true;
	case ND_ENT_TAG_STRING:
		return string_valid(v);
	case ND_ENT_TAG_DATA:
		return true;
	case ND_ENT_TAG_ARRAY: {
		const uint8_t *p = v->body, *end = v->body + v->length;
		while (p < end) {
			nd_ent_value_t item;
			if (!element(p, end, &item, &p) || !value_valid(&item, depth + 1)) {
				return false;
			}
		}
		return true;
	}
	case ND_ENT_TAG_DICT:
		return dict_valid(v, depth);
	default:
		return false;
	}
}

bool
nd_ent_parse(const uint8_t *der, size_t length, nd_ent_value_t *dict)
{
	if (der == NULL || dict == NULL) {
		return false;
	}
	const uint8_t *end = der + length, *after;
	nd_ent_value_t root, version, d;
	if (!element(der, end, &root, &after) || after != end || root.tag != ND_ENT_TAG_ROOT) {
		return false;
	}
	const uint8_t *rend = root.body + root.length, *p;
	if (!element(root.body, rend, &version, &p) || version.tag != ND_ENT_TAG_INTEGER ||
	    version.length != 1 || version.body[0] != 1) {
		return false;
	}
	if (!element(p, rend, &d, &after) || after != rend || d.tag != ND_ENT_TAG_DICT || !dict_valid(&d, 1)) {
		return false;
	}
	*dict = d;
	return true;
}

void
nd_ent_iter_init(const nd_ent_value_t *container, nd_ent_iter_t *it)
{
	it->next = container->body;
	it->end = container->body + container->length;
	it->dict = container->tag == ND_ENT_TAG_DICT;
}

bool
nd_ent_iter_next(nd_ent_iter_t *it, nd_ent_value_t *key, nd_ent_value_t *value)
{
	if (it->next >= it->end) {
		return false;
	}
	nd_ent_value_t v;
	const uint8_t *after;
	if (!element(it->next, it->end, &v, &after)) {
		return false;
	}
	it->next = after;
	if (!it->dict) {
		*value = v;
		return true;
	}
	const uint8_t *q, *eend = v.body + v.length;
	nd_ent_value_t k;
	if (!element(v.body, eend, &k, &q) || !element(q, eend, value, &after)) {
		return false;
	}
	if (key) {
		*key = k;
	}
	return true;
}

bool
nd_ent_string_equals(const nd_ent_value_t *value, const char *s)
{
	size_t n = strlen(s);
	return value->tag == ND_ENT_TAG_STRING && value->length == n && memcmp(value->body, s, n) == 0;
}

bool
nd_ent_lookup(const nd_ent_value_t *dict, const char *name, nd_ent_value_t *value)
{
	if (dict == NULL || name == NULL || dict->tag != ND_ENT_TAG_DICT) {
		return false;
	}
	nd_ent_iter_t it;
	nd_ent_value_t k, v;
	nd_ent_iter_init(dict, &it);
	while (nd_ent_iter_next(&it, &k, &v)) {
		if (nd_ent_string_equals(&k, name)) {
			*value = v;
			return true;
		}
	}
	return false;
}

bool
nd_ent_bool_value(const nd_ent_value_t *value, bool *out)
{
	if (value->tag != ND_ENT_TAG_BOOLEAN || value->length != 1) {
		return false;
	}
	*out = value->body[0] != 0;
	return true;
}

bool
nd_ent_integer_value(const nd_ent_value_t *value, int64_t *out)
{
	if (value->tag != ND_ENT_TAG_INTEGER || value->length == 0 || value->length > 8) {
		return false;
	}
	uint64_t u = (value->body[0] & 0x80) ? UINT64_MAX : 0;
	for (size_t i = 0; i < value->length; i++) {
		u = (u << 8) | value->body[i];
	}
	*out = (int64_t)u;
	return true;
}

bool
nd_ent_has_bool(const nd_ent_value_t *dict, const char *name)
{
	nd_ent_value_t v;
	bool b = false;
	return nd_ent_lookup(dict, name, &v) && nd_ent_bool_value(&v, &b) && b;
}

bool
nd_ent_has_string(const nd_ent_value_t *dict, const char *name, const char *value)
{
	nd_ent_value_t v;
	if (value == NULL || !nd_ent_lookup(dict, name, &v)) {
		return false;
	}
	if (v.tag == ND_ENT_TAG_STRING) {
		return nd_ent_string_equals(&v, value);
	}
	if (v.tag == ND_ENT_TAG_ARRAY) {
		nd_ent_iter_t it;
		nd_ent_value_t item;
		nd_ent_iter_init(&v, &it);
		while (nd_ent_iter_next(&it, NULL, &item)) {
			if (nd_ent_string_equals(&item, value)) {
				return true;
			}
		}
	}
	return false;
}
