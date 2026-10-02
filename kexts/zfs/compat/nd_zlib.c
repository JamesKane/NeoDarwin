// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: two functions of zlib's C API, which OpenZFS's C userland calls.
/*
 * NeoDarwin: the two zlib functions OpenZFS's userland calls, until the base
 * system carries zlib (P3-02). crc32() is the standard CRC-32 (ISO 3309,
 * polynomial 0xedb88320 reflected) that libefi uses for GPT headers.
 * uncompress() serves only zfs_send_resume_token_to_nvlist(), which decodes
 * the zlib-compressed resume token of an interrupted receive (`zfs send -t`,
 * `zfs receive -A`); it reports a data error, so resuming is unavailable.
 */

#include <stddef.h>
#include <stdint.h>

#define	Z_DATA_ERROR	(-3)

unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);
int uncompress(unsigned char *dest, unsigned long *destLen, const unsigned char *source,
    unsigned long sourceLen);

unsigned long
crc32(unsigned long crc, const unsigned char *buf, unsigned int len)
{
	uint32_t c = (uint32_t)crc ^ 0xffffffffu;

	if (buf == NULL)
		return (0);
	while (len--) {
		c ^= *buf++;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
	}
	return (c ^ 0xffffffffu);
}

int
uncompress(unsigned char *dest, unsigned long *destLen, const unsigned char *source,
    unsigned long sourceLen)
{
	(void) dest, (void) destLen, (void) source, (void) sourceLen;
	return (Z_DATA_ERROR);
}
