// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: compile check that nd_audio.h is freestanding C23 (spec-conventions.md §1, §5).
#include "nd_audio.h"

// Every record type is used once so that -Wunused and -Wpedantic see the full header.
size_t nd_audio_check_sizes(void);
size_t nd_audio_check_sizes(void)
{
	return sizeof(AUcontract) + sizeof(AUevent) + sizeof(AUdevinfo) + sizeof(AUring_hdr) +
	       sizeof(AUdoorbell) + sizeof(AUsession_hdr) + sizeof(AUvcmd) + sizeof(AUvstate) +
	       sizeof(AUrender_info) + sizeof(AUstream_params) + sizeof(AUstream_stats) +
	       sizeof(AUbuffer_desc) + sizeof(AUvoice_params) + sizeof(AUvoice_state);
}
