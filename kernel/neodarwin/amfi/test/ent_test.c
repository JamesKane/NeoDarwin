// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: exercises the C entitlements reader behind XNU's AMFI table.
//
// ndamfi's DER entitlements reader (nd_entitlements.c) against a blob that
// codesign(1) wrote (`codesign -s - --entitlements` of the plist below, slot
// 7 of the signature, without its 8-byte blob header), and against damaged
// copies of it: every defect must reject the whole blob.
//
//   com.apple.private.iopol.case_sensitivity  true
//   neodarwin.test.array                      [ "a", "b" ]
//   neodarwin.test.dict                       { k = "v" }
//   neodarwin.test.false                      false
//   neodarwin.test.int                        42
//   neodarwin.test.string                     "hello"

#include <stdio.h>
#include <string.h>
#include "../nd_entitlements.h"

static int passes, failures;

static void
expect(const char *what, int ok)
{
	if (ok) { passes++; } else { failures++; printf("FAIL %s\n", what); }
}

static const uint8_t codesign_der[] = {
	0x70, 0x81, 0xca, 0x02, 0x01, 0x01, 0xb0, 0x81, 0xc4, 0x30, 0x2d, 0x0c,
	0x28, 0x63, 0x6f, 0x6d, 0x2e, 0x61, 0x70, 0x70, 0x6c, 0x65, 0x2e, 0x70,
	0x72, 0x69, 0x76, 0x61, 0x74, 0x65, 0x2e, 0x69, 0x6f, 0x70, 0x6f, 0x6c,
	0x2e, 0x63, 0x61, 0x73, 0x65, 0x5f, 0x73, 0x65, 0x6e, 0x73, 0x69, 0x74,
	0x69, 0x76, 0x69, 0x74, 0x79, 0x01, 0x01, 0xff, 0x30, 0x1e, 0x0c, 0x14,
	0x6e, 0x65, 0x6f, 0x64, 0x61, 0x72, 0x77, 0x69, 0x6e, 0x2e, 0x74, 0x65,
	0x73, 0x74, 0x2e, 0x61, 0x72, 0x72, 0x61, 0x79, 0x30, 0x06, 0x0c, 0x01,
	0x61, 0x0c, 0x01, 0x62, 0x30, 0x1f, 0x0c, 0x13, 0x6e, 0x65, 0x6f, 0x64,
	0x61, 0x72, 0x77, 0x69, 0x6e, 0x2e, 0x74, 0x65, 0x73, 0x74, 0x2e, 0x64,
	0x69, 0x63, 0x74, 0xb0, 0x08, 0x30, 0x06, 0x0c, 0x01, 0x6b, 0x0c, 0x01,
	0x76, 0x30, 0x19, 0x0c, 0x14, 0x6e, 0x65, 0x6f, 0x64, 0x61, 0x72, 0x77,
	0x69, 0x6e, 0x2e, 0x74, 0x65, 0x73, 0x74, 0x2e, 0x66, 0x61, 0x6c, 0x73,
	0x65, 0x01, 0x01, 0x00, 0x30, 0x17, 0x0c, 0x12, 0x6e, 0x65, 0x6f, 0x64,
	0x61, 0x72, 0x77, 0x69, 0x6e, 0x2e, 0x74, 0x65, 0x73, 0x74, 0x2e, 0x69,
	0x6e, 0x74, 0x02, 0x01, 0x2a, 0x30, 0x1e, 0x0c, 0x15, 0x6e, 0x65, 0x6f,
	0x64, 0x61, 0x72, 0x77, 0x69, 0x6e, 0x2e, 0x74, 0x65, 0x73, 0x74, 0x2e,
	0x73, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x0c, 0x05, 0x68, 0x65, 0x6c, 0x6c,
	0x6f,
};

// Offsets into codesign_der.
enum {
	OFF_VERSION = 5,        // INTEGER 1's value
	OFF_TRUE = 55,          // case_sensitivity's BOOLEAN value
	OFF_ARRAY_KEY_A = 64,   // the first 'a' of "neodarwin.test.array"
	OFF_ARRAY_TAG = 80,     // the array's SEQUENCE tag
};

static bool
parse_copy(void (*damage)(uint8_t *, size_t *), nd_ent_value_t *dict)
{
	uint8_t copy[sizeof(codesign_der) + 8];
	size_t n = sizeof(codesign_der);
	memcpy(copy, codesign_der, n);
	damage(copy, &n);
	return nd_ent_parse(copy, n, dict);
}

static void d_version(uint8_t *b, size_t *n) { (void)n; b[OFF_VERSION] = 2; }
static void d_truncate(uint8_t *b, size_t *n) { (void)b; *n -= 1; }
static void d_trailing(uint8_t *b, size_t *n) { b[(*n)++] = 0; }
static void d_bool(uint8_t *b, size_t *n) { (void)n; b[OFF_TRUE] = 0x01; }  // not DER's 0xff
static void d_nul(uint8_t *b, size_t *n) { (void)n; b[OFF_ARRAY_KEY_A] = 0; }
static void d_type(uint8_t *b, size_t *n) { (void)n; b[OFF_ARRAY_TAG] = 0x31; }  // SET
static void d_order(uint8_t *b, size_t *n) { (void)n; b[OFF_ARRAY_KEY_A] = 'z'; }  // "neodzrwin...", before "neodarwin.test.dict"
static void d_long_length(uint8_t *b, size_t *n)
{
	// Re-encode the root's length 0xca as 0x82 0x00 0xca: not minimal.
	memmove(b + 3, b + 2, *n - 2);
	b[1] = 0x82;
	b[2] = 0x00;
	*n += 1;
}

int
main(void)
{
	nd_ent_value_t dict, v;
	expect("codesign's DER parses", nd_ent_parse(codesign_der, sizeof(codesign_der), &dict));

	// Queries.
	expect("boolean true is granted", nd_ent_has_bool(&dict, "com.apple.private.iopol.case_sensitivity"));
	expect("boolean false is not", !nd_ent_has_bool(&dict, "neodarwin.test.false"));
	expect("absent is not", !nd_ent_has_bool(&dict, "neodarwin.test.absent"));
	expect("a string is not a boolean", !nd_ent_has_bool(&dict, "neodarwin.test.string"));
	expect("a key's prefix is not the key", !nd_ent_has_bool(&dict, "com.apple.private.iopol"));
	expect("string value matches", nd_ent_has_string(&dict, "neodarwin.test.string", "hello"));
	expect("other string does not", !nd_ent_has_string(&dict, "neodarwin.test.string", "hell"));
	expect("array holding the string matches", nd_ent_has_string(&dict, "neodarwin.test.array", "b"));
	expect("array not holding it does not", !nd_ent_has_string(&dict, "neodarwin.test.array", "c"));
	expect("boolean is not a string", !nd_ent_has_string(&dict, "com.apple.private.iopol.case_sensitivity", "true"));
	int64_t i = 0;
	expect("integer", nd_ent_lookup(&dict, "neodarwin.test.int", &v) && nd_ent_integer_value(&v, &i) && i == 42);
	nd_ent_value_t inner;
	expect("nested dictionary", nd_ent_lookup(&dict, "neodarwin.test.dict", &v) && v.tag == ND_ENT_TAG_DICT &&
	    nd_ent_lookup(&v, "k", &inner) && nd_ent_string_equals(&inner, "v"));

	// Iteration sees all six entries in order.
	nd_ent_iter_t it;
	nd_ent_value_t k;
	int count = 0;
	nd_ent_iter_init(&dict, &it);
	while (nd_ent_iter_next(&it, &k, &v)) {
		count++;
	}
	expect("six entries", count == 6);

	// Every defect rejects the whole blob.
	expect("version 2 rejected", !parse_copy(d_version, &dict));
	expect("truncated rejected", !parse_copy(d_truncate, &dict));
	expect("trailing bytes rejected", !parse_copy(d_trailing, &dict));
	expect("non-DER boolean rejected", !parse_copy(d_bool, &dict));
	expect("NUL in a key rejected", !parse_copy(d_nul, &dict));
	expect("unknown value type rejected", !parse_copy(d_type, &dict));
	expect("unsorted keys rejected", !parse_copy(d_order, &dict));
	expect("non-minimal length rejected", !parse_copy(d_long_length, &dict));
	expect("empty rejected", !nd_ent_parse(codesign_der, 0, &dict));

	// Deep nesting past the limit: 0x70 { 1, 0xb0 { "k": [[[...]]] } }.
	uint8_t deep[128];
	size_t depth = ND_ENT_MAX_DEPTH + 2, n = 0;
	deep[n++] = 0x70; deep[n++] = 0;  // lengths patched below
	deep[n++] = 0x02; deep[n++] = 0x01; deep[n++] = 0x01;
	deep[n++] = 0xb0; deep[n++] = 0;
	deep[n++] = 0x30; deep[n++] = 0;
	deep[n++] = 0x0c; deep[n++] = 0x01; deep[n++] = 'k';
	for (size_t d = 0; d < depth; d++) {
		deep[n++] = 0x30;
		deep[n++] = (uint8_t)(2 * (depth - d - 1));
	}
	deep[1] = (uint8_t)(n - 2);
	deep[6] = (uint8_t)(n - 7);
	deep[8] = (uint8_t)(n - 9);
	expect("nesting past the limit rejected", !nd_ent_parse(deep, n, &dict));

	printf("%d passed, %d failed\n", passes, failures);
	return failures ? 1 : 0;
}
