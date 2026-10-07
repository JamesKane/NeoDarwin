// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the kernel's trust-cache grant verifier is C (it runs in ndamfi), and the host tools share it.
//
// ndsign documents and bundles: see nd_ndsign.h and docs/architecture/
// packaging.md §4. Nothing here allocates except one message buffer per
// signature check (Ed25519's open() works in place over sig || message).

#include "nd_ndsign.h"

#include <string.h>

#if KERNEL
#include <kern/kalloc.h>
#include <libkern/crypto/sha2.h>
#include <sys/random.h>
#define nd_alloc(n) kalloc_data((n), Z_WAITOK)
#define nd_free(p, n) kfree_data((p), (n))
#elif defined(ND_NDSIGN_EXTERNAL_DIGESTS)
// The program supplies nd_sha512, nd_sha256 and nd_random_bytes (ndpkg on
// NeoDarwin, which has no CommonCrypto: base/ndpkg).
#include <stdlib.h>
#define nd_alloc(n) malloc(n)
#define nd_free(p, n) free(p)
#else
#include <CommonCrypto/CommonDigest.h>
#include <stdlib.h>
#define nd_alloc(n) malloc(n)
#define nd_free(p, n) free(p)
#endif

int nd_ed25519_keypair_raw(unsigned char *pk, unsigned char *sk);
int nd_ed25519_sign_raw(unsigned char *sm, unsigned long long *smlen, const unsigned char *m,
    unsigned long long mlen, const unsigned char *sk);
int nd_ed25519_open_raw(unsigned char *m, unsigned long long *mlen, const unsigned char *sm,
    unsigned long long smlen, const unsigned char *pk);

// -- platform -----------------------------------------------------------------------

#if KERNEL
void
nd_sha512(uint8_t out[64], const void *in, size_t len)
{
	SHA512_CTX ctx;
	SHA512_Init(&ctx);
	SHA512_Update(&ctx, in, len);
	SHA512_Final(out, &ctx);
}

void
nd_sha256(uint8_t out[32], const void *in, size_t len)
{
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, in, len);
	SHA256_Final(out, &ctx);
}

void
nd_random_bytes(void *buf, size_t len)
{
	read_random(buf, (u_int)len);
}
#elif !defined(ND_NDSIGN_EXTERNAL_DIGESTS)
void
nd_sha512(uint8_t out[64], const void *in, size_t len)
{
	CC_SHA512(in, (CC_LONG)len, out);
}

void
nd_sha256(uint8_t out[32], const void *in, size_t len)
{
	CC_SHA256(in, (CC_LONG)len, out);
}

void
nd_random_bytes(void *buf, size_t len)
{
	arc4random_buf(buf, len);
}
#endif

// -- Ed25519 ---------------------------------------------------------------------------

#define DOMAIN_LEN (sizeof(ND_NDSIGN_DOMAIN) - 1)

bool
nd_ed25519_verify(const uint8_t sig[64], const uint8_t *msg, size_t len, const uint8_t pk[32])
{
	if (len > ND_NDSIGN_MAX_BUNDLE + DOMAIN_LEN) {
		return false;
	}
	size_t n = 64 + len;
	uint8_t *sm = nd_alloc(n), *m = nd_alloc(n);
	if (sm == NULL || m == NULL) {
		if (sm != NULL) {
			nd_free(sm, n);
		}
		if (m != NULL) {
			nd_free(m, n);
		}
		return false;
	}
	memcpy(sm, sig, 64);
	memcpy(sm + 64, msg, len);
	unsigned long long mlen = 0;
	bool ok = nd_ed25519_open_raw(m, &mlen, sm, n, pk) == 0 && mlen == len;
	nd_free(sm, n);
	nd_free(m, n);
	return ok;
}

#ifndef KERNEL
bool
nd_ed25519_keypair(uint8_t pk[32], uint8_t sk[64])
{
	return nd_ed25519_keypair_raw(pk, sk) == 0;
}

// The keypair function hashes a random seed; this one takes the seed, so a
// key file need only hold the seed (and tests can use RFC 8032's vectors).
bool
nd_ed25519_keypair_from_seed(uint8_t pk[32], uint8_t sk[64], const uint8_t seed[32])
{
	// keypair_raw takes its 32-byte seed from randombytes (nd_ed25519.c).
	extern const uint8_t *nd_ed25519_fixed_seed;
	nd_ed25519_fixed_seed = seed;
	bool ok = nd_ed25519_keypair_raw(pk, sk) == 0;
	nd_ed25519_fixed_seed = NULL;
	return ok;
}

bool
nd_ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t len, const uint8_t sk[64])
{
	size_t n = 64 + len;
	uint8_t *sm = malloc(n);
	if (sm == NULL) {
		return false;
	}
	unsigned long long smlen = 0;
	bool ok = nd_ed25519_sign_raw(sm, &smlen, msg, len, sk) == 0 && smlen == n;
	if (ok) {
		memcpy(sig, sm, 64);
	}
	free(sm);
	return ok;
}
#endif

// The kernel's libkern has no memchr.
static const char *
find_newline(const char *p, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (p[i] == '\n') {
			return p + i;
		}
	}
	return NULL;
}

// -- hex ---------------------------------------------------------------------------------

void
nd_ndsign_hex(char *out, const uint8_t *bytes, size_t n)
{
	static const char digits[] = "0123456789abcdef";
	for (size_t i = 0; i < n; i++) {
		out[2 * i] = digits[bytes[i] >> 4];
		out[2 * i + 1] = digits[bytes[i] & 15];
	}
	out[2 * n] = '\0';
}

static int
nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return -1;
}

bool
nd_ndsign_unhex(uint8_t *bytes, size_t n, const char *hex, size_t hex_len)
{
	if (hex_len != 2 * n) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
		if (hi < 0 || lo < 0) {
			return false;
		}
		bytes[i] = (uint8_t)(hi << 4 | lo);
	}
	return true;
}

// -- documents ---------------------------------------------------------------------------

static bool
key_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

// One line `key = value` (without its newline): the key and the value's
// raw text (quotes included). False unless canonical.
static bool
parse_line(const char *line, size_t len, struct nd_ndsign_span *key, struct nd_ndsign_span *value)
{
	size_t k = 0;
	while (k < len && key_char(line[k])) {
		k++;
	}
	if (k == 0 || len < k + 4 || memcmp(line + k, " = ", 3) != 0) {
		return false;
	}
	const char *v = line + k + 3;
	size_t vn = len - k - 3;
	if (v[0] == '"') {
		if (vn < 2 || v[vn - 1] != '"') {
			return false;
		}
		for (size_t i = 1; i + 1 < vn; i++) {
			if (v[i] < 0x20 || v[i] > 0x7e || v[i] == '"' || v[i] == '\\') {
				return false;
			}
		}
	} else {
		if (vn > 19 || (vn > 1 && v[0] == '0')) {
			return false;
		}
		for (size_t i = 0; i < vn; i++) {
			if (v[i] < '0' || v[i] > '9') {
				return false;
			}
		}
	}
	key->p = line;
	key->n = k;
	value->p = v;
	value->n = vn;
	return true;
}

static int
span_cmp(struct nd_ndsign_span a, struct nd_ndsign_span b)
{
	int c = memcmp(a.p, b.p, a.n < b.n ? a.n : b.n);
	return c != 0 ? c : (a.n < b.n ? -1 : a.n > b.n ? 1 : 0);
}

static bool
span_is(struct nd_ndsign_span s, const char *lit)
{
	size_t n = strlen(lit);
	return s.n == n && memcmp(s.p, lit, n) == 0;
}

bool
nd_ndsign_parse_doc(const char *doc, size_t len, struct nd_ndsign_span *body, uint8_t sig[64])
{
	if (len == 0 || len > ND_NDSIGN_MAX_DOC || doc[len - 1] != '\n') {
		return false;
	}
	struct nd_ndsign_span prev = { NULL, 0 }, key, value;
	size_t at = 0, lines = 0;
	while (at < len) {
		const char *nl = find_newline(doc + at, len - at);
		size_t n = (size_t)(nl - (doc + at));
		if (!parse_line(doc + at, n, &key, &value)) {
			return false;
		}
		bool last = at + n + 1 == len;
		if (last) {
			// The envelope: the signature, outside the body's order.
			if (!span_is(key, "signature") || value.p[0] != '"' || lines == 0 ||
			    !nd_ndsign_unhex(sig, 64, value.p + 1, value.n - 2)) {
				return false;
			}
			body->p = doc;
			body->n = at;
			return true;
		}
		if ((prev.p != NULL && span_cmp(prev, key) >= 0) || span_is(key, "signature")) {
			return false;
		}
		prev = key;
		lines++;
		at += n + 1;
	}
	return false;
}

bool
nd_ndsign_field(struct nd_ndsign_span body, const char *name, struct nd_ndsign_span *value)
{
	size_t at = 0;
	while (at < body.n) {
		const char *nl = find_newline(body.p + at, body.n - at);
		if (nl == NULL) {
			return false;
		}
		size_t n = (size_t)(nl - (body.p + at));
		struct nd_ndsign_span key, raw;
		if (parse_line(body.p + at, n, &key, &raw) && span_is(key, name)) {
			if (raw.p[0] == '"') {
				raw.p++;
				raw.n -= 2;
			}
			*value = raw;
			return true;
		}
		at += n + 1;
	}
	return false;
}

static bool
field_is(struct nd_ndsign_span body, const char *name, const char *want)
{
	struct nd_ndsign_span v;
	return nd_ndsign_field(body, name, &v) && span_is(v, want);
}

static bool
field_u64(struct nd_ndsign_span body, const char *name, uint64_t *out)
{
	struct nd_ndsign_span v;
	if (!nd_ndsign_field(body, name, &v) || v.n == 0 || v.p[-1] == '"') {
		return false;
	}
	uint64_t x = 0;
	for (size_t i = 0; i < v.n; i++) {
		if (x > (UINT64_MAX - 9) / 10) {
			return false;
		}
		x = x * 10 + (uint64_t)(v.p[i] - '0');
	}
	*out = x;
	return true;
}

static bool
field_key(struct nd_ndsign_span body, const char *name, uint8_t key[32])
{
	struct nd_ndsign_span v;
	return nd_ndsign_field(body, name, &v) && nd_ndsign_unhex(key, 32, v.p, v.n);
}

// `usage` is a space-separated list of statement kinds.
static bool
usage_allows(struct nd_ndsign_span body, const char *kind)
{
	struct nd_ndsign_span v;
	if (!nd_ndsign_field(body, "usage", &v)) {
		return false;
	}
	size_t n = strlen(kind), at = 0;
	while (at < v.n) {
		size_t end = at;
		while (end < v.n && v.p[end] != ' ') {
			end++;
		}
		if (end - at == n && memcmp(v.p + at, kind, n) == 0) {
			return true;
		}
		at = end + 1;
	}
	return false;
}

// `scope` is "*", "PREFIX*" or an exact package name.
static bool
scope_allows(struct nd_ndsign_span body, struct nd_ndsign_span package)
{
	struct nd_ndsign_span s;
	if (!nd_ndsign_field(body, "scope", &s) || s.n == 0 || package.n == 0) {
		return false;
	}
	if (s.p[s.n - 1] == '*') {
		return package.n >= s.n - 1 && memcmp(package.p, s.p, s.n - 1) == 0;
	}
	return span_cmp(s, package) == 0;
}

static bool
signed_by(struct nd_ndsign_span body, const uint8_t sig[64], const uint8_t key[32])
{
	size_t n = (sizeof(ND_NDSIGN_DOMAIN) - 1) + body.n;
	uint8_t *msg = nd_alloc(n);
	if (msg == NULL) {
		return false;
	}
	memcpy(msg, ND_NDSIGN_DOMAIN, sizeof(ND_NDSIGN_DOMAIN) - 1);
	memcpy(msg + sizeof(ND_NDSIGN_DOMAIN) - 1, body.p, body.n);
	bool ok = nd_ed25519_verify(sig, msg, n, key);
	nd_free(msg, n);
	return ok;
}

nd_ndsign_error_t
nd_ndsign_verify_bundle(const uint8_t *bundle, size_t len, const uint8_t *roots, size_t nroots, uint64_t now,
    const char *statement_kind, struct nd_ndsign_bundle *out)
{
	static const char *const kinds[ND_NDSIGN_DOCS] = { "channel", "release", NULL };
	const char *text = (const char *)bundle;
	uint8_t sig[ND_NDSIGN_DOCS][64];
	if (bundle == NULL || len > ND_NDSIGN_MAX_BUNDLE) {
		return ND_NDSIGN_MALFORMED;
	}

	// Split on the empty lines; check each document's form.
	size_t at = 0;
	for (size_t d = 0; d < ND_NDSIGN_DOCS; d++) {
		size_t end = at;
		while (end < len && !(text[end] == '\n' && (end + 1 == len || text[end + 1] == '\n'))) {
			end++;
		}
		if (end >= len) {
			return ND_NDSIGN_MALFORMED;
		}
		end++;  // the document's last newline
		out->doc[d].p = text + at;
		out->doc[d].n = end - at;
		if (!nd_ndsign_parse_doc(out->doc[d].p, out->doc[d].n, &out->body[d], sig[d]) ||
		    !field_is(out->body[d], "schema", "1")) {
			return ND_NDSIGN_MALFORMED;
		}
		bool last = d + 1 == ND_NDSIGN_DOCS;
		if (last ? end != len : (end >= len || text[end] != '\n')) {
			return ND_NDSIGN_MALFORMED;
		}
		at = end + 1;
	}

	// Kinds in order.
	for (size_t d = 0; d < ND_NDSIGN_DOCS; d++) {
		if (!field_is(out->body[d], "kind", kinds[d] != NULL ? kinds[d] : statement_kind)) {
			return ND_NDSIGN_WRONG_KIND;
		}
	}

	// The channel certificate's issuer is a trusted root; each later
	// document's issuer is the previous one's key.
	uint8_t issuer[32], key[32];
	if (!field_key(out->body[0], "issuer", issuer)) {
		return ND_NDSIGN_MALFORMED;
	}
	bool trusted = false;
	for (size_t r = 0; r < nroots; r++) {
		trusted = trusted || memcmp(roots + 32 * r, issuer, 32) == 0;
	}
	if (!trusted) {
		return ND_NDSIGN_UNTRUSTED_ROOT;
	}
	for (size_t d = 0; d < ND_NDSIGN_DOCS; d++) {
		uint8_t named[32];
		if (!field_key(out->body[d], "issuer", named)) {
			return ND_NDSIGN_MALFORMED;
		}
		if (memcmp(named, issuer, 32) != 0) {
			return ND_NDSIGN_WRONG_ISSUER;
		}
		if (!signed_by(out->body[d], sig[d], issuer)) {
			return ND_NDSIGN_BAD_SIGNATURE;
		}
		if (d + 1 < ND_NDSIGN_DOCS) {
			uint64_t expires = 0, seq = 0;
			struct nd_ndsign_span name;
			if (!field_key(out->body[d], "key", key) || !field_u64(out->body[d], "expires", &expires) ||
			    !field_u64(out->body[d], "seq", &seq) || seq == 0 || !nd_ndsign_field(out->body[d], "name", &name) ||
			    name.n == 0) {
				return ND_NDSIGN_MALFORMED;
			}
			memcpy(issuer, key, 32);
		}
	}

	// Usage, scope and expiry of both certificates.
	struct nd_ndsign_span package;
	if (!nd_ndsign_field(out->body[2], "package", &package)) {
		return ND_NDSIGN_MALFORMED;
	}
	for (size_t d = 0; d + 1 < ND_NDSIGN_DOCS; d++) {
		uint64_t expires = 0;
		(void)field_u64(out->body[d], "expires", &expires);
		if (now >= expires) {
			return ND_NDSIGN_EXPIRED;
		}
		if (!usage_allows(out->body[d], statement_kind)) {
			return ND_NDSIGN_NOT_AUTHORISED;
		}
		if (!scope_allows(out->body[d], package)) {
			return ND_NDSIGN_OUT_OF_SCOPE;
		}
	}
	return ND_NDSIGN_OK;
}

nd_ndsign_error_t
nd_ndsign_verify_tc_grant(const uint8_t *grant, size_t grant_len, const uint8_t *module, size_t module_len,
    const char *tc_type, const uint8_t *roots, size_t nroots, uint64_t now)
{
	struct nd_ndsign_bundle b;
	nd_ndsign_error_t e = nd_ndsign_verify_bundle(grant, grant_len, roots, nroots, now, "trust-cache", &b);
	if (e != ND_NDSIGN_OK) {
		return e;
	}
	uint8_t digest[32], granted[32];
	nd_sha256(digest, module, module_len);
	if (!field_key(b.body[2], "module-sha256", granted) || memcmp(digest, granted, 32) != 0 ||
	    !field_is(b.body[2], "tc-type", tc_type)) {
		return ND_NDSIGN_WRONG_KIND;
	}
	return ND_NDSIGN_OK;
}

const char *
nd_ndsign_error_name(nd_ndsign_error_t error)
{
	static const char *const names[ND_NDSIGN_ERRORS] = {
		"ok", "malformed", "untrusted root", "bad signature", "wrong issuer", "wrong kind or grant",
		"expired", "not authorised for this kind", "package out of scope",
	};
	return (unsigned)error < ND_NDSIGN_ERRORS ? names[error] : "unknown";
}
