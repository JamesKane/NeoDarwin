// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: layout and helper test for the nd_audio.h C ABI (spec-conventions.md §1).
#include "nd_audio.h"

#include <stdio.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(void)
{
	// Handles: kind, generation and index round-trip; NONE is kind 0 (AU-ABI-001).
	AUhandle h = AU_HANDLE_MAKE(AU_KIND_STREAM, 0xabcdefu, 0x12345678u);
	CHECK(AU_HANDLE_KIND(h) == AU_KIND_STREAM);
	CHECK(AU_HANDLE_GEN(h) == 0xabcdefu);
	CHECK(AU_HANDLE_INDEX(h) == 0x12345678u);
	CHECK(AU_HANDLE_KIND(AU_HANDLE_NONE) == 0 && AU_HANDLE_GEN(AU_HANDLE_NONE) == 0);
	CHECK(AU_HANDLE_GEN(AU_HANDLE_MAKE(AU_KIND_VOICE, 0x1000001u, 1)) == 1);   // generation is 24 bits

	// Region offsets (AU-RING-003 layout table).
	CHECK(AU_RING_CONTRACT_OFF == 512 && AU_RING_DATA_OFF == 1024);
	CHECK(offsetof(AUring_hdr, server_pos) / AU_CACHELINE == 1);
	CHECK(offsetof(AUring_hdr, client_pos) / AU_CACHELINE == 2);
	CHECK(offsetof(AUsession_hdr, cmd_head) / AU_CACHELINE == 2);
	CHECK(AU_RING_MAGIC == ((uint32_t)'A' | (uint32_t)'U' << 8 | (uint32_t)'R' << 16 | (uint32_t)'1' << 24));
	CHECK(AU_SESSION_MAGIC == ((uint32_t)'A' | (uint32_t)'U' << 8 | (uint32_t)'S' << 16 | (uint32_t)'1' << 24));

	// au_frame_to_host (AU-CONTRACT-002): 24 MHz timebase at 48 kHz = 500 ticks per frame.
	AUcontract c = {0};
	c.size = sizeof c; c.version = 1;
	c.anchor_pos = 1000000; c.anchor_host = 5000000000ull;
	c.ticks_per_frame_q32 = 500ull << 32;
	c.timebase_numer = 125; c.timebase_denom = 3;          // 24 MHz: 41.666 ns per tick
	CHECK(au_frame_to_host(&c, 1000000) == 5000000000ull);
	CHECK(au_frame_to_host(&c, 1000128) == 5000000000ull + 64000);
	CHECK(au_frame_to_host(&c, 999872) == 5000000000ull - 64000);
	CHECK(au_frame_to_host(&c, 1000000 + (1ull << 33)) == 5000000000ull + 500ull * (1ull << 33));
	// Fractional rate: 44.1 kHz on a 24 MHz timebase = 544.2177 ticks per frame.
	c.ticks_per_frame_q32 = (uint64_t)((24000000.0 / 44100.0) * 4294967296.0);
	CHECK(au_frame_to_host(&c, 1000000 + 44100) - 5000000000ull >= 23999999ull);
	CHECK(au_frame_to_host(&c, 1000000 + 44100) - 5000000000ull <= 24000000ull);
	// 128 frames at 48 kHz = 64000 ticks = 2 666 666 ns.
	CHECK(au_host_to_ns(&c, 64000) == 2666666);
	CHECK(au_host_to_ns(&c, 24000000ull * 3600) == 3600000000000ull);

	// Version 1 record sizes (AU-ABI-004).
	CHECK(sizeof(AUcontract) == 256 && sizeof(AUdevinfo) == 256 && sizeof(AUevent) == 32);
	CHECK(sizeof(AUstream_params) == 128 && sizeof(AUvcmd) == 64 && sizeof(AUvstate) == 32);

	if (failures == 0) printf("nd_audio layout: ok\n");
	return failures == 0 ? 0 : 1;
}
