// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: drives libsystem_c's crypt(3), whose interface is C, and the host's for comparison.
//
// Known answers for every scheme NeoDarwin's crypt(3) has, through crypt()
// itself (Libc's gen/crypt.c with patch 0001, and FreeBSD's libcrypt):
//   - SHA-256 and SHA-512: every test of Ulrich Drepper's "Unix crypt using
//     SHA-256 and SHA-512" (www.akkadia.org/drepper/SHA-crypt.txt), with its
//     rounds= settings, a salt cut at 16 characters and rounds held at 1000;
//   - bcrypt: Openwall crypt_blowfish's wrapper.c vectors, which Apache's
//     htpasswd -v accepts as well, for "$2a$", "$2b$" and "$2y$";
//   - MD5: FreeBSD's lib/libcrypt/tests vector, and ones `openssl passwd -1`
//     makes;
//   - DES, traditional and extended: the same code macOS's libc has, so the
//     host's crypt() is the reference for every password and setting, and
//     the guest test account's hash (tests/qemu/pam) is pinned, as is the
//     test account's SHA-512 one.

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

char *nd_libc_crypt(const char *key, const char *setting);

static int failures, passes;

static void
expect(const char *what, const char *got, const char *want)
{
	if (want == NULL ? got == NULL : got != NULL && strcmp(got, want) == 0) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL %s:\n  got  %s\n  want %s\n", what, got ? got : "(null)", want ? want : "(null)");
}

static const char long_text[] = "a very much longer text to encrypt.  This one even stretches over more"
				"than one line.";
static const char short_salt_text[] = "we have a short salt string but not a short password";
static const char rounds_text[] = "the minimum number is still observed";

static const struct {
	const char *key, *setting, *hash;
} vectors[] = {
	// Drepper, SHA-256.
	{ "Hello world!", "$5$saltstring", "$5$saltstring$5B8vYYiY.CVt1RlTTf8KbXBH3hsxY/GNooZaBBGWEc5" },
	{ "Hello world!", "$5$rounds=10000$saltstringsaltstring",
	  "$5$rounds=10000$saltstringsaltst$3xv.VbSHBb41AL9AvLeujZkZRBAwqFMz2.opqey6IcA" },
	{ "This is just a test", "$5$rounds=5000$toolongsaltstring",
	  "$5$rounds=5000$toolongsaltstrin$Un/5jzAHMgOGZ5.mWJpuVolil07guHPvOW8mGRcvxa5" },
	{ long_text, "$5$rounds=1400$anotherlongsaltstring",
	  "$5$rounds=1400$anotherlongsalts$Rx.j8H.h8HjEDGomFU8bDkXm3XIUnzyxf12oP84Bnq1" },
	{ short_salt_text, "$5$rounds=77777$short", "$5$rounds=77777$short$JiO1O3ZpDAxGJeaDIuqCoEFysAe1mZNJRs3pw0KQRd/" },
	{ "a short string", "$5$rounds=123456$asaltof16chars..",
	  "$5$rounds=123456$asaltof16chars..$gP3VQ/6X7UUEW3HkBn2w1/Ptq2jxPyzV/cZKmF/wJvD" },
	{ rounds_text, "$5$rounds=10$roundstoolow", "$5$rounds=1000$roundstoolow$yfvwcWrQ8l/K0DAWyuPMDNHpIVlTQebY9l/gL972bIC" },
	// Drepper, SHA-512.
	{ "Hello world!", "$6$saltstring",
	  "$6$saltstring$svn8UoSVapNtMuq1ukKS4tPQd8iKwSMHWjl/O817G3uBnIFNjnQJuesI68u4OTLiBFdcbYEdFCoEOfaS35inz1" },
	{ "Hello world!", "$6$rounds=10000$saltstringsaltstring",
	  "$6$rounds=10000$saltstringsaltst$OW1/O6BYHV6BcXZu8QVeXbDWra3Oeqh0sbHbbMCVNSnCM/UrjmM0Dp8vOuZeHBy/YTBmSK6H9qs/"
	  "y3RnOaw5v." },
	{ "This is just a test", "$6$rounds=5000$toolongsaltstring",
	  "$6$rounds=5000$toolongsaltstrin$lQ8jolhgVRVhY4b5pZKaysCLi0QBxGoNeKQzQ3glMhwllF7oGDZxUhx1yxdYcz/e1JSbq3y6JMxxl8audkUEm0" },
	{ long_text, "$6$rounds=1400$anotherlongsaltstring",
	  "$6$rounds=1400$anotherlongsalts$POfYwTEok97VWcjxIiSOjiykti.o/pQs.wPvMxQ6Fm7I6IoYN3CmLs66x9t0oSwbtEW7o7UmJEiDwGqd8p4ur1" },
	{ short_salt_text, "$6$rounds=77777$short",
	  "$6$rounds=77777$short$WuQyW2YR.hBNpjjRhpYD/ifIw05xdfeEyQoMxIXbkvr0gge1a1x3yRULJ5CCaUeOxFmtlcGZelFl5CxtgfiAc0" },
	{ "a short string", "$6$rounds=123456$asaltof16chars..",
	  "$6$rounds=123456$asaltof16chars..$BtCwjqMJGx5hrJhZywWvt0RLE8uZ4oPwcelCjmw2kSYu.Ec6ycULevoBK25fs2xXgMNrCzIMVcgEJAstJeonj1" },
	{ rounds_text, "$6$rounds=10$roundstoolow",
	  "$6$rounds=1000$roundstoolow$kUMsbe306n21p9R.FRkW3IGn.S9NPN0x50YhH1xhLsPuWGsUSklZt58jaTfF4ZEQpyUNGc0dqbpBYYBaHHrsX." },
	// Openwall crypt_blowfish (wrapper.c).
	{ "U*U", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW" },
	{ "U*U*", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.VGOzA784oUp/Z0DY336zx7pLYAy0lwK" },
	{ "U*U*U", "$2a$05$XXXXXXXXXXXXXXXXXXXXXO", "$2a$05$XXXXXXXXXXXXXXXXXXXXXOAcXxm9kjPGEMsLznoKqmqw7tc8WCx4a" },
	{ "", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.7uG0VCzI2bS7j6ymqJi9CdcdxiRTWNy" },
	{ "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789chars after 72 are ignored",
	  "$2a$05$abcdefghijklmnopqrstuu", "$2a$05$abcdefghijklmnopqrstuu5s2v8.iXieOjg/.AySBTTZIIVFJeBui" },
	{ "U*U", "$2b$05$CCCCCCCCCCCCCCCCCCCCC.", "$2b$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW" },
	{ "U*U", "$2y$05$CCCCCCCCCCCCCCCCCCCCC.", "$2y$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW" },
	// A full hash as the setting, as login and pam_unix pass it.
	{ "U*U", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW",
	  "$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW" },
	// MD5: FreeBSD's lib/libcrypt/tests, then `openssl passwd -1`.
	{ "0.s0.l33t", "$1$deadbeef$0Huu6KHrKLVWfqa4WljDE0", "$1$deadbeef$0Huu6KHrKLVWfqa4WljDE0" },
	{ "password", "$1$saltsalt$", "$1$saltsalt$qjXMvbEw8oaL.CzflDtaK/" },
	{ "a much longer password, over sixteen bytes", "$1$12345678$", "$1$12345678$PP/c4Kc.7EWyWL2a.RRb51" },
	{ "", "$1$$", "$1$$qRPK7m23GJusamGpoGLby/" },
	// The guest test account's traditional DES hash (tests/qemu/pam), kept
	// to show that hashes from before the change still match.
	{ "neodarwin", "nd", "ndic/XPiorxqM" },
	// The SHA-512 hash of tests/qemu/pam's test account.
	{ "neodarwin", "$6$K2DnKGs2OI7SVsY5",
	  "$6$K2DnKGs2OI7SVsY5$TWI4VPZYjNpKLP8DhaNm7pTdz6G7vHtXwRbQyjJPLTG6s7Mn3yQIfKTzfobAqRsF7SMUERPgdgJJEXdDJPuu50" },
	{ "neodarwin", "ndic/XPiorxqM", "ndic/XPiorxqM" },
	// Settings crypt() refuses: an unknown bcrypt minor version, and bcrypt
	// rounds below 2^4.
	{ "x", "$2c$05$CCCCCCCCCCCCCCCCCCCCC.", NULL },
	{ "x", "$2a$03$CCCCCCCCCCCCCCCCCCCCC.", NULL },
};

int
main(void)
{
	for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
		char what[160];
		snprintf(what, sizeof(what), "crypt(\"%.24s\", \"%.40s\")", vectors[i].key, vectors[i].setting);
		expect(what, nd_libc_crypt(vectors[i].key, vectors[i].setting), vectors[i].hash);
	}

	// FreeBSD's "invalid" test: a different salt doesn't reproduce the hash.
	const char *other = nd_libc_crypt("0.s0.l33t", "$1$cafebabe$0Huu6KHrKLVWfqa4WljDE0");
	if (other != NULL && strcmp(other, "$1$cafebabe$0Huu6KHrKLVWfqa4WljDE0") != 0)
		passes++;
	else
		expect("MD5 with another salt differs", other, "(a different hash)");

	// DES, traditional and BSDi extended, and settings no scheme claims
	// (they fall to traditional DES on both sides): the host's crypt() is
	// the same Libc code, unpatched.
	static const char *const keys[] = { "", "a", "neodarwin", "12345678", "123456789", "darwin26",
		"a password longer than eight characters, which extended DES uses in full", "\x80\xff" };
	static const char *const settings[] = { "nd", "..", "zz", "a", "", "_/.E.abcd", "_J9..CCCC",
		"_....salt", "_zz..zzzz", "$3$abcd", "$7$xyz", "$", "*" };
	for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
		for (size_t s = 0; s < sizeof(settings) / sizeof(settings[0]); s++) {
			char want[64], what[160];
			const char *host = crypt(keys[k], settings[s]);
			snprintf(want, sizeof(want), "%s", host ? host : "");
			snprintf(what, sizeof(what), "DES crypt(\"%.24s\", \"%s\") against the host's", keys[k], settings[s]);
			expect(what, nd_libc_crypt(keys[k], settings[s]), host ? want : NULL);
		}
	}

	printf("%d passed, %d failed\n", passes, failures);
	return failures != 0;
}
