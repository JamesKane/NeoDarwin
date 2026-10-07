// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: exercises the C grant verifier ndamfi runs in the kernel.
//
// ndamfi's ndsign code (nd_ndsign.c, nd_ed25519.c) on the host: Ed25519
// against RFC 8032's test vectors 1 and 2, then a trust-cache grant that
// //tools/ndsign wrote for a package (//tools/ndsign:pciconf_pkg, the test
// key chain) checked as the kernel checks it: accepted for its module and
// type against the test root; refused for another root, after expiry, for
// another module or type, edited, truncated or padded.
//     ndsign_test MODULE GRANT ROOT.pub OTHER-ROOT.pub

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../nd_ndsign.h"

static int passes, failures;

static void
expect(const char *what, int ok)
{
	if (ok) { passes++; printf("ok   %s\n", what); } else { failures++; printf("FAIL %s\n", what); }
}

static uint8_t *
slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) { perror(path); exit(2); }
	fseek(f, 0, SEEK_END);
	*size = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = malloc(*size + 1);
	if (fread(b, 1, *size, f) != *size) { perror(path); exit(2); }
	fclose(f);
	return b;
}

static void
key_file(const char *path, uint8_t key[32])
{
	size_t n;
	uint8_t *t = slurp(path, &n);
	if (n < 64 || !nd_ndsign_unhex(key, 32, (const char *)t, 64)) { fprintf(stderr, "%s: not a key\n", path); exit(2); }
	free(t);
}

static void
unhex_or_die(uint8_t *out, size_t n, const char *hex)
{
	if (!nd_ndsign_unhex(out, n, hex, strlen(hex))) { fprintf(stderr, "bad vector\n"); exit(2); }
}

static void
rfc8032(void)
{
	// RFC 8032 §7.1, TEST 1 (empty message) and TEST 2 (0x72).
	static const struct { const char *seed, *pk, *msg, *sig; } v[] = {
		{ "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
		  "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
		  "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
		{ "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
		  "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
		  "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
	};
	for (size_t i = 0; i < 2; i++) {
		uint8_t seed[32], pk[32], sig[64], msg[1], mypk[32], sk[64], mysig[64];
		size_t mlen = strlen(v[i].msg) / 2;
		unhex_or_die(seed, 32, v[i].seed);
		unhex_or_die(pk, 32, v[i].pk);
		unhex_or_die(sig, 64, v[i].sig);
		if (mlen) { unhex_or_die(msg, mlen, v[i].msg); }
		char what[64];
		snprintf(what, sizeof(what), "RFC 8032 test %zu: public key from the seed", i + 1);
		expect(what, nd_ed25519_keypair_from_seed(mypk, sk, seed) && memcmp(mypk, pk, 32) == 0);
		snprintf(what, sizeof(what), "RFC 8032 test %zu: signature", i + 1);
		expect(what, nd_ed25519_sign(mysig, msg, mlen, sk) && memcmp(mysig, sig, 64) == 0);
		snprintf(what, sizeof(what), "RFC 8032 test %zu: verifies", i + 1);
		expect(what, nd_ed25519_verify(sig, msg, mlen, pk));
		sig[10] ^= 1;
		snprintf(what, sizeof(what), "RFC 8032 test %zu: a flipped bit fails", i + 1);
		expect(what, !nd_ed25519_verify(sig, msg, mlen, pk));
	}
}

int
main(int argc, char **argv)
{
	rfc8032();
	if (argc != 5) {
		fprintf(stderr, "usage: ndsign_test MODULE GRANT ROOT.pub OTHER-ROOT.pub\n");
		return 2;
	}
	size_t mlen, glen;
	uint8_t *module = slurp(argv[1], &mlen), *grant = slurp(argv[2], &glen);
	uint8_t roots[2 * 32], other[32];
	key_file(argv[4], roots);      // an untrusted key first: roots are a list
	key_file(argv[3], roots + 32);
	memcpy(other, roots, 32);
	const uint64_t now = 1790000000, expiry = 4102444800;

#define VERIFY(g, gl, m, ml, type, r, nr, t) nd_ndsign_verify_tc_grant((g), (gl), (m), (ml), (type), (r), (nr), (t))
	expect("the grant verifies for its module as ltrs", VERIFY(grant, glen, module, mlen, "ltrs", roots, 2, now) == ND_NDSIGN_OK);
	expect("valid one second before expiry", VERIFY(grant, glen, module, mlen, "ltrs", roots, 2, expiry - 1) == ND_NDSIGN_OK);
	expect("expired at the certificates' expiry", VERIFY(grant, glen, module, mlen, "ltrs", roots, 2, expiry) == ND_NDSIGN_EXPIRED);
	expect("another root alone: untrusted", VERIFY(grant, glen, module, mlen, "ltrs", other, 1, now) == ND_NDSIGN_UNTRUSTED_ROOT);
	expect("no roots: untrusted", VERIFY(grant, glen, module, mlen, "ltrs", roots, 0, now) == ND_NDSIGN_UNTRUSTED_ROOT);
	expect("another type (dtrs): refused", VERIFY(grant, glen, module, mlen, "dtrs", roots, 2, now) == ND_NDSIGN_WRONG_KIND);
	module[mlen - 1] ^= 1;
	expect("another module: refused", VERIFY(grant, glen, module, mlen, "ltrs", roots, 2, now) == ND_NDSIGN_WRONG_KIND);
	module[mlen - 1] ^= 1;

	struct nd_ndsign_bundle b;
	expect("a grant is not a manifest signature", nd_ndsign_verify_bundle(grant, glen, roots, 2, now, "manifest", &b) == ND_NDSIGN_WRONG_KIND);
	expect("the bundle's statement names the package",
	    nd_ndsign_verify_bundle(grant, glen, roots, 2, now, "trust-cache", &b) == ND_NDSIGN_OK &&
	    b.body[2].n > 0 && memmem(b.body[2].p, b.body[2].n, "package = \"pciconf\"\n", 19) != NULL);

	// Edit the statement's package name (same length): its signature fails.
	uint8_t *edited = malloc(glen);
	memcpy(edited, grant, glen);
	char *p = memmem(edited, glen, "package = \"pciconf\"", 19);
	if (p != NULL) { p[11] = 'q'; }
	expect("an edited statement: bad signature", p != NULL && VERIFY(edited, glen, module, mlen, "ltrs", roots, 2, now) == ND_NDSIGN_BAD_SIGNATURE);
	memcpy(edited, grant, glen);
	p = memmem(edited, glen, "usage = \"manifest trust-cache\"", 30);
	if (p != NULL) { memcpy(p + 9, "manifest manifest   ", 20); }
	expect("an edited certificate: bad signature", p != NULL && VERIFY(edited, glen, module, mlen, "ltrs", roots, 2, now) == ND_NDSIGN_BAD_SIGNATURE);
	expect("truncated: malformed", VERIFY(grant, glen - 1, module, mlen, "ltrs", roots, 2, now) == ND_NDSIGN_MALFORMED);
	uint8_t *padded = malloc(glen + 1);
	memcpy(padded, grant, glen);
	padded[glen] = '\n';
	expect("a trailing empty line: malformed", VERIFY(padded, glen + 1, module, mlen, "ltrs", roots, 2, now) == ND_NDSIGN_MALFORMED);
	expect("empty: malformed", VERIFY(grant, 0, module, mlen, "ltrs", roots, 2, now) == ND_NDSIGN_MALFORMED);

	// Documents' canonical form.
	struct nd_ndsign_span body;
	uint8_t sig[64];
	const char *sig_line = "signature = \"00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"\n";
	char doc[512];
	snprintf(doc, sizeof(doc), "a = 1\nb = \"x\"\n%s", sig_line);
	expect("a canonical document parses", nd_ndsign_parse_doc(doc, strlen(doc), &body, sig) && body.n == 14);
	snprintf(doc, sizeof(doc), "b = 1\na = \"x\"\n%s", sig_line);
	expect("unsorted keys: refused", !nd_ndsign_parse_doc(doc, strlen(doc), &body, sig));
	snprintf(doc, sizeof(doc), "a = 01\n%s", sig_line);
	expect("a non-canonical integer: refused", !nd_ndsign_parse_doc(doc, strlen(doc), &body, sig));
	snprintf(doc, sizeof(doc), "a = \"x\\\"\"\n%s", sig_line);
	expect("a backslash or quote in a string: refused", !nd_ndsign_parse_doc(doc, strlen(doc), &body, sig));
	snprintf(doc, sizeof(doc), "a  = 1\n%s", sig_line);
	expect("extra spaces: refused", !nd_ndsign_parse_doc(doc, strlen(doc), &body, sig));
	snprintf(doc, sizeof(doc), "a = 1\na = 2\n%s", sig_line);
	expect("a duplicate key: refused", !nd_ndsign_parse_doc(doc, strlen(doc), &body, sig));
	snprintf(doc, sizeof(doc), "%s", sig_line);
	expect("a signature alone: refused", !nd_ndsign_parse_doc(doc, strlen(doc), &body, sig));

	printf("%d passed, %d failed\n", passes, failures);
	return failures == 0 ? 0 : 1;
}
