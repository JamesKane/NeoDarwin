// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the kernel's code-signing path and ndamfi, which call it, are C.
//
// NeoDarwin's reader for CoreEntitlements' DER entitlements, the blob in a
// code signature's CSSLOT_DER_ENTITLEMENTS slot (magic 0xfade7172). Apple's
// CoreEntitlements is closed; its headers in xnu's EXTERNAL_HEADERS describe
// the interface, and codesign(1) writes the format:
//
//     [APPLICATION 16] {                -- 0x70
//         INTEGER 1,                    -- the format version
//         [CONTEXT 16] {                -- 0xb0, the dictionary
//             SEQUENCE { UTF8String key, value }, ...   -- keys sorted, unique
//         }
//     }
//
// where a value is a BOOLEAN, INTEGER, UTF8String, OCTET STRING, SEQUENCE
// of values (an array) or [CONTEXT 16] dictionary. The reader validates a
// whole blob before anything is read from it: DER lengths (definite,
// minimal), types, a nesting limit, strictly increasing keys, no NUL in a
// string. Anything else is rejected, and ndamfi then grants no entitlements.
// The host test is //kernel/neodarwin/amfi:entitlements_test.

#ifndef ND_ENTITLEMENTS_H
#define ND_ENTITLEMENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// DER tags of the format.
#define ND_ENT_TAG_BOOLEAN  0x01
#define ND_ENT_TAG_INTEGER  0x02
#define ND_ENT_TAG_DATA     0x04
#define ND_ENT_TAG_STRING   0x0c
#define ND_ENT_TAG_ARRAY    0x30  // SEQUENCE; also a dictionary entry
#define ND_ENT_TAG_DICT     0xb0  // [CONTEXT 16], constructed
#define ND_ENT_TAG_ROOT     0x70  // [APPLICATION 16], constructed

#define ND_ENT_MAX_DEPTH    16

typedef struct nd_ent_value {
	uint8_t tag;            // ND_ENT_TAG_*
	const uint8_t *body;    // the contents octets
	size_t length;
} nd_ent_value_t;

typedef struct nd_ent_iter {
	const uint8_t *next;
	const uint8_t *end;
	bool dict;
} nd_ent_iter_t;

// Validates a DER entitlements payload (the blob without its 8-byte
// CS_GenericBlob header) and returns its top-level dictionary.
bool nd_ent_parse(const uint8_t *der, size_t length, nd_ent_value_t *dict);

// Iterates over an array's values or a dictionary's entries (then with
// `key` set). Only for values from a validated blob.
void nd_ent_iter_init(const nd_ent_value_t *container, nd_ent_iter_t *it);
bool nd_ent_iter_next(nd_ent_iter_t *it, nd_ent_value_t *key, nd_ent_value_t *value);

// The value of `name` in a dictionary, if there is one.
bool nd_ent_lookup(const nd_ent_value_t *dict, const char *name, nd_ent_value_t *value);

bool nd_ent_bool_value(const nd_ent_value_t *value, bool *out);
bool nd_ent_integer_value(const nd_ent_value_t *value, int64_t *out);
bool nd_ent_string_equals(const nd_ent_value_t *value, const char *s);

// Queries with the semantics of XNU's entitlement checks (IOTaskHasEntitlement
// and IOTaskHasStringEntitlement): a boolean entitlement is granted only when
// its value is true; a string entitlement when its value is that string or
// an array holding it.
bool nd_ent_has_bool(const nd_ent_value_t *dict, const char *name);
bool nd_ent_has_string(const nd_ent_value_t *dict, const char *name, const char *value);

#endif
