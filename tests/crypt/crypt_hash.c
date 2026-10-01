// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a host driver for libsystem_c's crypt(3), whose interface is C.
//
// crypt_hash PASSWORD SETTING: prints the hash NeoDarwin's crypt(3) makes,
// built for the host from the same sources (//tests/crypt:libc_crypt). The
// host's own crypt can't do this: macOS's has only DES.

#include <stdio.h>

char *nd_libc_crypt(const char *key, const char *setting);

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "usage: crypt_hash PASSWORD SETTING\n");
		return 2;
	}
	const char *hash = nd_libc_crypt(argv[1], argv[2]);
	if (hash == NULL) {
		fprintf(stderr, "crypt_hash: crypt failed for setting %s\n", argv[2]);
		return 1;
	}
	printf("%s\n", hash);
	return 0;
}
