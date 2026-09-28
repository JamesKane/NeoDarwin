// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: checks that nd_audio.h, a C ABI header, also compiles as C++ for C++ clients (SDL, Qt, Dawn, engines)
/*
 * nd_audio_cxx.cpp - C++20 compile check for docs/audio/nd_audio.h
 * (AU-T-087 in audio-service.md; spec-conventions.md §5). The header must
 * compile as C++20 with no warnings, with C linkage, and give the same
 * layouts as in C. Its own static_asserts run here under C++ rules too.
 */
#include "docs/audio/nd_audio.h"

#include <type_traits>

static_assert(sizeof(AUcontract) == 256, "AUcontract is 256 bytes in C++ too");
static_assert(sizeof(AUevent) == 32, "AUevent is 32 bytes in C++ too");
static_assert(sizeof(AUdevinfo) == 256, "AUdevinfo is 256 bytes in C++ too");
static_assert(sizeof(AUring_hdr) == 512, "AUring_hdr is 512 bytes in C++ too");
static_assert(sizeof(AUsession_hdr) == 384, "AUsession_hdr is 384 bytes in C++ too");
static_assert(sizeof(AUstream_params) == 128, "AUstream_params is 128 bytes in C++ too");
static_assert(offsetof(AUring_hdr, client_pos) == 256, "AUring_hdr.client_pos at 256 in C++ too");
static_assert(offsetof(AUcontract, anchor_host) == 152, "AUcontract.anchor_host at 152 in C++ too");
static_assert(std::is_same_v<std::underlying_type_t<AUerror>, int32_t>, "AUerror is int32_t");
static_assert(std::is_same_v<std::underlying_type_t<AUevtype>, uint16_t>, "AUevtype is uint16_t");
static_assert(std::is_same_v<std::underlying_type_t<AUcap>, uint32_t>, "AUcap is uint32_t");
static_assert(std::is_standard_layout_v<AUring_hdr> && std::is_trivially_copyable_v<AUring_hdr>,
    "shared-memory records are plain data");
static_assert(std::is_standard_layout_v<AUcontract> && std::is_trivially_copyable_v<AUcontract>,
    "the contract is plain data");
static_assert(AU_HANDLE_KIND(AU_HANDLE_MAKE(AU_KIND_STREAM, 7, 3)) == AU_KIND_STREAM &&
    AU_HANDLE_GEN(AU_HANDLE_MAKE(AU_KIND_STREAM, 7, 3)) == 7u &&
    AU_HANDLE_INDEX(AU_HANDLE_MAKE(AU_KIND_STREAM, 7, 3)) == 3u, "handle accessors work in C++");

// Instantiates the inline helpers and takes the address of every function,
// which checks the declarations under C++ rules (no definitions are linked:
// this is a compile check).
extern "C++" uint64_t nd_audio_cxx_time(const AUcontract *c);
uint64_t nd_audio_cxx_time(const AUcontract *c)
{
	return au_frame_to_host(c, 48000) + au_host_to_ns(c, 24000000);
}

extern "C++" const void *nd_audio_cxx_check(void);
const void *nd_audio_cxx_check(void)
{
	static const void *const fns[] = {
		reinterpret_cast<const void *>(&au_open),
		reinterpret_cast<const void *>(&au_session_fd),
		reinterpret_cast<const void *>(&au_session_events),
		reinterpret_cast<const void *>(&au_query),
		reinterpret_cast<const void *>(&au_error_detail),
		reinterpret_cast<const void *>(&au_error_refusal),
		reinterpret_cast<const void *>(&au_devices),
		reinterpret_cast<const void *>(&au_device_info),
		reinterpret_cast<const void *>(&au_set_default),
		reinterpret_cast<const void *>(&au_stream_open),
		reinterpret_cast<const void *>(&au_stream_contract),
		reinterpret_cast<const void *>(&au_stream_rt_group),
		reinterpret_cast<const void *>(&au_stream_write),
		reinterpret_cast<const void *>(&au_stream_write_all),
		reinterpret_cast<const void *>(&au_stream_doorbell),
		reinterpret_cast<const void *>(&au_buffer_create),
		reinterpret_cast<const void *>(&au_voice_play),
		reinterpret_cast<const void *>(&au_voice_state),
		reinterpret_cast<const void *>(&au_mixer_contract),
	};
	return fns[0];
}
