// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the C interfaces ndsign's Swift imports (ndamfi's ndsign code, libzstd).
#ifndef NDSIGN_C_H
#define NDSIGN_C_H
#include <stddef.h>
#include "kernel/neodarwin/amfi/nd_ndsign.h"

// The libzstd functions ndsign uses, as zstd.h (1.5.7) declares them. The
// header itself isn't included: its directory's module map (libzstd) would
// make Swift import it as a module whose configuration macros clash.
#define ND_ZSTD_CONTENTSIZE_UNKNOWN (0ULL - 1)
#define ND_ZSTD_CONTENTSIZE_ERROR (0ULL - 2)
size_t ZSTD_compressBound(size_t srcSize);
size_t ZSTD_compress(void *dst, size_t dstCapacity, const void *src, size_t srcSize, int compressionLevel);
size_t ZSTD_decompress(void *dst, size_t dstCapacity, const void *src, size_t compressedSize);
unsigned long long ZSTD_getFrameContentSize(const void *src, size_t srcSize);
unsigned ZSTD_isError(size_t code);
const char *ZSTD_getErrorName(size_t code);
#endif
