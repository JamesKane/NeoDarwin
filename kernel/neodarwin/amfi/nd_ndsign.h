// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the kernel's trust-cache grant verifier is C (it runs in ndamfi), and the host tools share it.
//
// ndsign documents and their verification (P2-01, docs/architecture/
// packaging.md §4). The same code runs in the kernel, where ndamfi checks a
// run-time trust cache's grant with it, and on the host, under //tools/ndsign.
//
// A document is canonical TOML: lines `key = value\n`, keys in strictly
// increasing byte order, values a decimal integer or a "string" of printable
// ASCII without `"` or `\`, then one last line `signature = "<128 hex>"`: an
// Ed25519 signature (RFC 8032) over ND_NDSIGN_DOMAIN followed by every byte
// before that line (the body). A bundle is three documents separated by one
// empty line: a channel certificate issued by a trusted root, a release
// certificate issued by that channel key, and a statement (`kind`
// "manifest" or "trust-cache") issued by the release key.

#ifndef ND_NDSIGN_H
#define ND_NDSIGN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ND_NDSIGN_DOMAIN "NeoDarwin ndsign 1\n"
#define ND_NDSIGN_DOCS 3
#define ND_NDSIGN_MAX_DOC 4096
#define ND_NDSIGN_MAX_BUNDLE (ND_NDSIGN_DOCS * (ND_NDSIGN_MAX_DOC + 1))

// Platform hooks: the kernel's crypto table or the host's libraries.
void nd_sha512(uint8_t out[64], const void *in, size_t len);
void nd_sha256(uint8_t out[32], const void *in, size_t len);
void nd_random_bytes(void *buf, size_t len);

// Ed25519 over a detached message. The secret key is the 32-byte seed
// followed by the public key, as SUPERCOP and OpenSSH keep it.
bool nd_ed25519_verify(const uint8_t sig[64], const uint8_t *msg, size_t len, const uint8_t pk[32]);
#ifndef KERNEL
bool nd_ed25519_keypair(uint8_t pk[32], uint8_t sk[64]);
bool nd_ed25519_keypair_from_seed(uint8_t pk[32], uint8_t sk[64], const uint8_t seed[32]);
bool nd_ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t len, const uint8_t sk[64]);
#endif

typedef enum {
	ND_NDSIGN_OK = 0,
	ND_NDSIGN_MALFORMED,        // not three canonical documents
	ND_NDSIGN_UNTRUSTED_ROOT,   // the channel certificate's issuer is no trusted root
	ND_NDSIGN_BAD_SIGNATURE,    // a document's signature doesn't verify under its issuer
	ND_NDSIGN_WRONG_ISSUER,     // a document isn't issued by the previous document's key
	ND_NDSIGN_WRONG_KIND,       // channel, release, statement out of order, or another statement kind
	ND_NDSIGN_EXPIRED,          // a certificate's `expires` is not after `now`
	ND_NDSIGN_NOT_AUTHORISED,   // a certificate's usage lacks the statement's kind
	ND_NDSIGN_OUT_OF_SCOPE,     // the statement's package is outside a certificate's scope
	ND_NDSIGN_ERRORS
} nd_ndsign_error_t;

const char *nd_ndsign_error_name(nd_ndsign_error_t error);

struct nd_ndsign_span {
	const char *p;
	size_t n;
};

// A verified bundle: each document's whole text and its body (the signed
// part); [0] the channel certificate, [1] the release one, [2] the statement.
struct nd_ndsign_bundle {
	struct nd_ndsign_span doc[ND_NDSIGN_DOCS];
	struct nd_ndsign_span body[ND_NDSIGN_DOCS];
};

// Checks one document's canonical form; sets its body (everything before the
// signature line) and the signature.
bool nd_ndsign_parse_doc(const char *doc, size_t len, struct nd_ndsign_span *body, uint8_t sig[64]);

// A field of a canonical body: a string's contents without quotes, or an
// integer's digits. False if absent.
bool nd_ndsign_field(struct nd_ndsign_span body, const char *key, struct nd_ndsign_span *value);

// Verifies a bundle against the trusted roots (nroots keys of 32 bytes, one
// after another) at time `now` (Unix seconds):
// signatures, the issuer chain, kinds, expiry, usage and scope. On success
// `out` points into `bundle`.
nd_ndsign_error_t nd_ndsign_verify_bundle(const uint8_t *bundle, size_t len, const uint8_t *roots, size_t nroots,
    uint64_t now, const char *statement_kind, struct nd_ndsign_bundle *out);

// A trust-cache grant: a bundle whose statement grants exactly `module`, as
// the trust-cache type named `tc_type` (`module-sha256`, `tc-type`).
nd_ndsign_error_t nd_ndsign_verify_tc_grant(const uint8_t *grant, size_t grant_len, const uint8_t *module, size_t module_len,
    const char *tc_type, const uint8_t *roots, size_t nroots, uint64_t now);

// Lower-case hex of `n` bytes into `out` (2n + 1 bytes, NUL-terminated); the
// reverse, false unless `hex` is exactly 2n lower-case hex digits.
void nd_ndsign_hex(char *out, const uint8_t *bytes, size_t n);
bool nd_ndsign_unhex(uint8_t *bytes, size_t n, const char *hex, size_t hex_len);

#endif
