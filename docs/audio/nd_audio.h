// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the C ABI of the audio service is the cross-language boundary (language-policy.md §3) shared by audiod, libndaudio, the toolkit's Swift layer, the PulseAudio door and out-of-tree bindings.
/*
 * nd_audio.h - the NeoDarwin audio service: records, shared-memory layouts
 * and the client library (libndaudio) C ABI.
 *
 * Normative text: docs/audio/audio-service.md (requirement ids AU-*). Where
 * this header and the text differ, the text is normative and this header is
 * a defect (spec-conventions.md §5).
 *
 *   /n/sys/audio            audiod's 9P tree (control, contract, events)
 *   ring  (per stream)      shared memory: samples, positions, anchor
 *   doorbell (per stream)   Mach port, queue limit 1: "a period is due"
 *   session region          shared memory: voice command ring, voice states
 *
 * Every record is little-endian, fixed-width, explicitly padded and
 * size-asserted. Fields of the shared regions that are written by one side
 * and read by the other are accessed with 32- or 64-bit atomic loads and
 * stores (acquire/release as stated per field); they are declared as plain
 * integers so that Swift and other FFIs import them.
 *
 * Freestanding C23 that also compiles as C++20 (spec-conventions.md §5):
 * the declarations sit inside extern "C", and no C-only declaration syntax
 * is used. Checked by nd_audio_check.c (C23) and nd_audio_cxx.cpp (C++20).
 *
 * Time: *_host values are mach_absolute_time() ticks and *_ns values are
 * nanoseconds, both on the SC clock of docs/kernel/scheduling-contract.md
 * (SC-TIME-001, SC-TIME-002). au_host_to_ns() gives the value nd_wait and
 * nd_rt_set_deadline take, with no further conversion.
 *
 * LP64 only: NeoDarwin is a 64-bit system (multi-arch.md).
 */
#ifndef ND_AUDIO_H
#define ND_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The header is C23. Importers that parse it in an older mode (the Swift
 * ClangImporter uses Objective-C/gnu11) get the C11 spelling of the keyword. */
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L) && !defined(static_assert)
#define static_assert _Static_assert
#endif

#ifdef __cplusplus
extern "C" {
#endif

static_assert(sizeof(void *) == 8, "nd_audio.h is LP64 only");

#define AU_API_VERSION      1
#define AU_MOUNT            "/n/sys/audio"   /* where nsd mounts audiod's tree   */
#define AU_SRVNAME          "audio"          /* nsd registry name (/n/sys/srv)  */
#define AU_CACHELINE        128              /* shared-region field separation  */

/* ---------------------------------------------------------------------------
 * Handles (charter P5): kind in bits 56-63, generation in bits 32-55 (never
 * 0), slot index in bits 0-31. AU_HANDLE_NONE is never a valid handle.
 * ------------------------------------------------------------------------- */
typedef uint64_t AUhandle;

#define AU_HANDLE_NONE                  ((AUhandle)0)
#define AU_HANDLE_KIND(h)               ((uint32_t)(((uint64_t)(h)) >> 56))
#define AU_HANDLE_GEN(h)                ((uint32_t)((((uint64_t)(h)) >> 32) & 0xffffffu))
#define AU_HANDLE_INDEX(h)              ((uint32_t)(((uint64_t)(h)) & 0xffffffffu))
#define AU_HANDLE_MAKE(kind, gen, idx)  ((AUhandle)((((uint64_t)(kind) & 0xffu) << 56) | \
                                        (((uint64_t)(gen) & 0xffffffu) << 32) | (uint64_t)(uint32_t)(idx)))

enum AUkind : uint8_t {
	AU_KIND_SESSION = 1,
	AU_KIND_STREAM  = 2,
	AU_KIND_VOICE   = 3,
	AU_KIND_BUFFER  = 4,
	AU_KIND_DEVICE  = 5,
};

/* ---------------------------------------------------------------------------
 * Errors (charter P12). Functions return AUerror; au_error_detail() gives a
 * thread-local UTF-8 detail for the last error on the calling thread.
 * ------------------------------------------------------------------------- */
typedef enum AUerror : int32_t {
	AU_OK                  = 0,
	AU_ERR_INVALID_HANDLE  = 1,   /* stale, wrong kind or NONE: a diagnosed no-op */
	AU_ERR_INVALID_ARG     = 2,
	AU_ERR_FORMAT          = 3,   /* format, rate or channel layout not convertible */
	AU_ERR_PERIOD          = 4,   /* AU_OPEN_EXACT_PERIOD and the period is unavailable */
	AU_ERR_ADMISSION       = 5,   /* nd_rt_admit refused; detail carries its reason */
	AU_ERR_PERMISSION      = 6,   /* capability missing or revoked */
	AU_ERR_NO_DEVICE       = 7,
	AU_ERR_DEVICE_LOST     = 8,
	AU_ERR_LIMIT           = 9,   /* stream, voice, buffer or ring-size limit */
	AU_ERR_BUSY            = 10,  /* non-blocking call would block (command ring full) */
	AU_ERR_STATE           = 11,  /* call not valid in the object's state or mode */
	AU_ERR_SERVICE         = 12,  /* audiod unreachable or protocol error */
	AU_ERR_VERSION         = 13,  /* record size or version not understood */
	AU_ERR_NOMEM           = 14,  /* client-side allocation failed (never on the RT path) */
	AU_ERR_TIMEOUT         = 15,
	AU_ERR_UNSUPPORTED     = 16,  /* capability not present in this version */
} AUerror;

/* ---------------------------------------------------------------------------
 * Enumerations stored in records (records hold them as fixed-width ints).
 * ------------------------------------------------------------------------- */
enum AUdirection : uint8_t { AU_DIR_OUTPUT = 1, AU_DIR_INPUT = 2 };

enum AUmode : uint8_t {
	AU_MODE_CALLBACK = 1,   /* pull: the library's admitted RT thread calls render */
	AU_MODE_QUEUE    = 2,   /* push (output) or read (capture) from any thread     */
	AU_MODE_FILE     = 3,   /* 9P data file only (scripts, remote clients); no ring */
};

enum AUformat : uint16_t {  /* interleaved PCM */
	AU_FMT_F32 = 1,         /* ring format; IEEE-754 binary32 */
	AU_FMT_S16 = 2,
	AU_FMT_S32 = 3,
	AU_FMT_S24_32 = 4,      /* 24-bit in the low bits of a 32-bit container */
	AU_FMT_U8  = 5,
};

/* Stream and ring states. */
enum AUstate : uint32_t {
	AU_STATE_STOPPED   = 1,
	AU_STATE_RUNNING   = 2,
	AU_STATE_DRAINING  = 3,
	AU_STATE_DETACHED  = 4,   /* pinned device absent; resumes if it returns */
	AU_STATE_RETIRED   = 5,   /* ring replaced by a layout-changing transition */
	AU_STATE_CLOSED    = 6,
};

/* AUcontract.flags */
#define AU_CF_CAPTURE          0x0001u
#define AU_CF_FOLLOWS_DEFAULT  0x0002u   /* opened on "default": migrates with it */
#define AU_CF_CONVERTING       0x0004u   /* rate conversion active: frames per callback vary by +/-1 */
#define AU_CF_MONITOR          0x0008u   /* capture of an output mix */
#define AU_CF_DETACHED         0x0010u

/* AUcontract.change and AU_EV_CONTRACT arg32: what changed in this generation. */
#define AU_CHG_DEVICE    0x0001u
#define AU_CHG_RATE      0x0002u
#define AU_CHG_PERIOD    0x0004u
#define AU_CHG_LATENCY   0x0008u
#define AU_CHG_CHANNELS  0x0010u
#define AU_CHG_ROUTE     0x0020u   /* port change on the same device, e.g. jack */
#define AU_CHG_STATE     0x0040u   /* detached, reattached */
#define AU_CHG_LAYOUT    0x0080u   /* the ring was replaced (period, rate or channels) */
#define AU_CHG_SERVICE   0x0100u   /* audiod restarted; the library re-established the stream */
#define AU_CHG_ADMISSION 0x0200u   /* the stream thread's admission changed: re-admitted, demoted or revoked (AU-EVENT-006) */

/* ---------------------------------------------------------------------------
 * The contract record (AU-CONTRACT). 256 bytes. Read from streams/N/contract,
 * from the ring's contract copy, or with au_stream_contract().
 * Time values: *_ns in nanoseconds; *_host in mach_absolute_time() ticks.
 * ------------------------------------------------------------------------- */
typedef struct AUcontract {
	uint32_t size;                 /*   0 sizeof(AUcontract) for this version */
	uint16_t version;              /*   4 1 */
	uint16_t flags;                /*   6 AU_CF_* */
	AUhandle stream;               /*   8 */
	uint64_t generation;           /*  16 increments on every change; never 0 */
	AUhandle device;               /*  24 device handle (volatile across reboots) */
	uint8_t  device_uid[16];       /*  32 stable device identity */
	uint32_t rate;                 /*  48 client-facing frames per second */
	uint32_t device_rate;          /*  52 mixer rate of the device */
	uint32_t period_frames;        /*  56 client frames per callback (nominal) */
	uint32_t mixer_period_frames;  /*  60 device frames per mixer period */
	uint64_t period_ns;            /*  64 callback period = admitted period */
	uint64_t channel_mask;         /*  72 AU_CH_* speaker positions */
	uint16_t channels;             /*  80 */
	uint16_t format;               /*  82 AUformat (client-facing) */
	uint8_t  direction;            /*  84 AUdirection */
	uint8_t  mode;                 /*  85 AUmode */
	uint16_t lead_periods;         /*  86 */
	uint32_t ring_frames;          /*  88 ring capacity, device frames */
	uint32_t change;               /*  92 AU_CHG_* since the previous generation */
	uint64_t latency_ns;           /*  96 total = sum of the five components */
	uint64_t lat_client_ns;        /* 104 ring lead: lead_periods x period */
	uint64_t lat_convert_ns;       /* 112 resampler group delay (0 if none) */
	uint64_t lat_mixer_ns;         /* 120 mixer period (output) or capture tick (input) */
	uint64_t lat_device_ns;        /* 128 driver buffering and safety offset */
	uint64_t lat_hw_ns;            /* 136 codec, converter, transport (e.g. Bluetooth) */
	uint64_t anchor_pos;           /* 144 client frame position ...                */
	uint64_t anchor_host;          /* 152 ... presented at the terminal at this time */
	uint64_t ticks_per_frame_q32;  /* 160 host ticks per client frame, Q32.32, drift-corrected */
	uint32_t timebase_numer;       /* 168 mach_timebase_info, copied (no lazy globals on RT) */
	uint32_t timebase_denom;       /* 172 */
	uint64_t rt_period_ns;         /* 176 as admitted by nd_rt_admit (callback mode) */
	uint64_t rt_computation_ns;    /* 184 */
	uint64_t rt_constraint_ns;     /* 192 */
	uint32_t route;                /* 200 AU_ROUTE_* */
	uint32_t state;                /* 204 AUstate */
	uint8_t  reserved[48];         /* 208 zero */
} AUcontract;

enum AUroute : uint32_t {
	AU_ROUTE_DEFAULT  = 1,   /* following the default device */
	AU_ROUTE_PINNED   = 2,   /* on the device the client named */
	AU_ROUTE_FALLBACK = 3,   /* default fell back after the preferred device left */
};

/* Channel positions (AUcontract.channel_mask), WAVE_FORMAT_EXTENSIBLE order. */
#define AU_CH_FL   (1ull << 0)
#define AU_CH_FR   (1ull << 1)
#define AU_CH_FC   (1ull << 2)
#define AU_CH_LFE  (1ull << 3)
#define AU_CH_BL   (1ull << 4)
#define AU_CH_BR   (1ull << 5)
#define AU_CH_FLC  (1ull << 6)
#define AU_CH_FRC  (1ull << 7)
#define AU_CH_BC   (1ull << 8)
#define AU_CH_SL   (1ull << 9)
#define AU_CH_SR   (1ull << 10)
#define AU_CH_MONO AU_CH_FC
#define AU_CH_STEREO (AU_CH_FL | AU_CH_FR)
#define AU_MAX_CHANNELS 8

/* Presentation (output) or capture (input) time of client frame pos, in host
 * ticks. Valid while |pos - anchor_pos| < 2^32 frames. RT-safe. */
static inline uint64_t au_frame_to_host(const AUcontract *c, uint64_t pos)
{
	uint64_t q = c->ticks_per_frame_q32;
	if (pos >= c->anchor_pos) {
		uint64_t d = pos - c->anchor_pos;
		return c->anchor_host + (d >> 32) * q + (((d & 0xffffffffu) * (q >> 32)) +
		       (((d & 0xffffffffu) * (q & 0xffffffffu)) >> 32));
	}
	uint64_t d = c->anchor_pos - pos;
	return c->anchor_host - ((d >> 32) * q + (((d & 0xffffffffu) * (q >> 32)) +
	       (((d & 0xffffffffu) * (q & 0xffffffffu)) >> 32)));
}

/* Host ticks to nanoseconds with the contract's copied timebase. RT-safe. */
static inline uint64_t au_host_to_ns(const AUcontract *c, uint64_t ticks)
{
	return (ticks / c->timebase_denom) * c->timebase_numer +
	       ((ticks % c->timebase_denom) * c->timebase_numer) / c->timebase_denom;
}

/* ---------------------------------------------------------------------------
 * Events (AU-EVENT): 32-byte records read from the session events file.
 * The session descriptor (au_session_fd) is readable when one is pending.
 * ------------------------------------------------------------------------- */
enum AUevtype : uint16_t {
	AU_EV_CONTRACT        = 1,   /* object=stream value=new generation arg32=AU_CHG_* */
	AU_EV_UNDERRUN        = 2,   /* object=stream value=cumulative underruns arg32=new since last event */
	AU_EV_OVERRUN         = 3,   /* capture: object=stream value=cumulative arg32=new */
	AU_EV_DEVICE_ADDED    = 4,   /* object=device */
	AU_EV_DEVICE_REMOVED  = 5,   /* object=device */
	AU_EV_DEFAULT_CHANGED = 6,   /* object=device arg32=AUdirection */
	AU_EV_STREAM_LOST     = 7,   /* object=stream arg32=AUerror: cannot continue */
	AU_EV_DRAINED         = 8,   /* object=stream */
	AU_EV_VOICE_END       = 9,   /* object=voice arg32=AU_VEND_* */
	AU_EV_BUFFER_LOST     = 10,  /* object=buffer (service restart) */
	AU_EV_REVOKED         = 11,  /* object=stream or session; arg32=0 in v1 */
	AU_EV_SERVICE         = 12,  /* object=session arg32=0 lost, 1 re-established */
	AU_EV_RT_NOTICE       = 13,  /* object=stream arg32=sc_rt_notice_kind value=count (AU-EVENT-006, SC-OVR-005) */
	AU_EV_MAX
};

typedef struct AUevent {
	uint16_t type;       /*  0 AUevtype */
	uint16_t flags;      /*  2 zero in v1 */
	uint32_t arg32;      /*  4 */
	AUhandle object;     /*  8 */
	uint64_t value;      /* 16 */
	uint64_t host;       /* 24 host ticks when audiod observed it */
} AUevent;

/* ---------------------------------------------------------------------------
 * Devices (AU-DEV).
 * ------------------------------------------------------------------------- */
#define AU_DF_OUTPUT       0x0001u
#define AU_DF_INPUT        0x0002u
#define AU_DF_DEFAULT_OUT  0x0004u
#define AU_DF_DEFAULT_IN   0x0008u
#define AU_DF_PRESENT      0x0010u
#define AU_DF_MONITOR      0x0020u   /* capture-only view of an output's mix */

enum AUdevkind : uint32_t {
	AU_DEVKIND_BUILTIN   = 1,
	AU_DEVKIND_USB       = 2,
	AU_DEVKIND_BLUETOOTH = 3,
	AU_DEVKIND_HDMI      = 4,   /* HDMI and DisplayPort */
	AU_DEVKIND_VIRTIO    = 5,
	AU_DEVKIND_VIRTUAL   = 6,   /* null sink, loopback test device */
};

enum AUport : uint32_t {
	AU_PORT_UNKNOWN = 0, AU_PORT_SPEAKER = 1, AU_PORT_HEADPHONES = 2, AU_PORT_LINE = 3,
	AU_PORT_DIGITAL = 4, AU_PORT_MIC = 5, AU_PORT_HEADSET = 6,
};

/* AUdevinfo.rates bits */
#define AU_RATE_8000   (1u << 0)
#define AU_RATE_11025  (1u << 1)
#define AU_RATE_16000  (1u << 2)
#define AU_RATE_22050  (1u << 3)
#define AU_RATE_32000  (1u << 4)
#define AU_RATE_44100  (1u << 5)
#define AU_RATE_48000  (1u << 6)
#define AU_RATE_88200  (1u << 7)
#define AU_RATE_96000  (1u << 8)
#define AU_RATE_176400 (1u << 9)
#define AU_RATE_192000 (1u << 10)

typedef struct AUdevinfo {
	uint32_t size;                 /*   0 */
	uint16_t version;              /*   4 1 */
	uint16_t flags;                /*   6 AU_DF_* */
	AUhandle device;               /*   8 */
	uint8_t  uid[16];              /*  16 stable identity */
	uint32_t kind;                 /*  32 AUdevkind */
	uint32_t rate;                 /*  36 current mixer rate */
	uint32_t rates;                /*  40 AU_RATE_* supported */
	uint16_t max_channels_out;     /*  44 */
	uint16_t max_channels_in;      /*  46 */
	uint32_t min_period_frames;    /*  48 */
	uint32_t max_period_frames;    /*  52 */
	uint32_t mixer_period_frames;  /*  56 current */
	uint32_t port;                 /*  60 AUport */
	uint64_t lat_device_ns;        /*  64 */
	uint64_t lat_hw_ns;            /*  72 */
	uint64_t channel_mask_out;     /*  80 */
	uint64_t channel_mask_in;      /*  88 */
	uint64_t generation;           /*  96 increments when any field changes */
	char     name[64];             /* 104 UTF-8, NUL-padded */
	uint8_t  reserved[88];         /* 168 zero */
} AUdevinfo;

/* ---------------------------------------------------------------------------
 * The stream ring (AU-RING): one shared-memory region per stream.
 *
 *   0     AUring_hdr        512 bytes, four 128-byte lines
 *   512   AUcontract        contract copy, guarded by hdr.contract_seq
 *   1024  data              capacity_frames x channels x float, interleaved
 *
 * Output: the client produces (client_pos), audiod consumes (server_pos).
 * Capture: audiod produces (server_pos), the client consumes (client_pos).
 * Positions are monotonic 64-bit frame counts in device frames; the sample
 * slot of position p is (p % capacity_frames).
 * ------------------------------------------------------------------------- */
#define AU_RING_MAGIC          0x31525541u   /* "AUR1" little-endian */
#define AU_RING_CONTRACT_OFF   512u
#define AU_RING_DATA_OFF       1024u

/* AUring_hdr.client_flags */
#define AU_RCF_WANT_DOORBELL   0x0001u   /* queue mode: ring when free >= watermark */

typedef struct AUring_hdr {
	/* line 0: written by audiod before the port is handed out; immutable after */
	uint32_t magic;                /*   0 AU_RING_MAGIC */
	uint16_t version;              /*   4 1 */
	uint16_t hdr_size;             /*   6 512 */
	uint64_t region_size;          /*   8 */
	AUhandle stream;               /*  16 */
	uint32_t contract_offset;      /*  24 AU_RING_CONTRACT_OFF */
	uint32_t data_offset;          /*  28 AU_RING_DATA_OFF */
	uint32_t capacity_frames;      /*  32 multiple of period_frames */
	uint32_t period_frames;        /*  36 device frames per callback period */
	uint16_t channels;             /*  40 */
	uint16_t sample_bytes;         /*  42 4 (AU_FMT_F32) */
	uint8_t  direction;            /*  44 AUdirection */
	uint8_t  mode;                 /*  45 AUmode */
	uint16_t lead_periods;         /*  46 */
	uint64_t ring_id;              /*  48 increments per transition */
	uint8_t  reserved0[72];        /*  56 */

	/* line 1: written by audiod only (release); read by the client (acquire) */
	uint64_t server_pos;           /* 128 */
	uint32_t state;                /* 136 AUstate */
	uint32_t doorbell_seq;         /* 140 increments with every doorbell message */
	uint32_t contract_seq;         /* 144 seqlock: odd while the copy is written */
	uint32_t anchor_seq;           /* 148 seqlock over anchor_* */
	uint64_t contract_gen;         /* 152 generation of the contract copy */
	uint64_t anchor_pos;           /* 160 device frame position ...        */
	uint64_t anchor_host;          /* 168 ... at the terminal at this time */
	uint64_t ticks_per_frame_q32;  /* 176 per device frame */
	uint64_t tick_host;            /* 184 mixer tick that last moved server_pos */
	uint64_t deadline_host;        /* 192 next period must be complete by then */
	uint64_t underruns;            /* 200 */
	uint64_t underrun_frames;      /* 208 */
	uint64_t overruns;             /* 216 capture */
	uint8_t  reserved1[32];        /* 224 */

	/* line 2: written by the client only; audiod treats it as untrusted */
	uint64_t client_pos;           /* 256 */
	uint32_t watermark_frames;     /* 264 */
	uint32_t client_flags;         /* 268 AU_RCF_* */
	uint64_t client_callbacks;     /* 272 */
	uint64_t client_late;          /* 280 callbacks completed after deadline_host */
	uint64_t client_overbudget;    /* 288 callbacks longer than rt_computation_ns */
	uint8_t  reserved2[88];        /* 296 */

	/* line 3: reserved for later versions */
	uint8_t  reserved3[128];       /* 384 */
} AUring_hdr;

/* The doorbell message audiod sends (MACH_SEND_TIMEOUT 0 on a qlimit-1
 * port; a full queue means a doorbell is already pending). Payload after the
 * mach_msg_header_t; the header itself is not part of this ABI. */
typedef struct AUdoorbell {
	uint32_t doorbell_seq;         /*  0 */
	uint32_t state;                /*  4 AUstate */
	uint64_t server_pos;           /*  8 */
	uint64_t deadline_host;        /* 16 */
	uint64_t contract_gen;         /* 24 */
} AUdoorbell;

/* ---------------------------------------------------------------------------
 * The session region (AU-VOICE): one per session, for Tier 2 voices.
 *
 *   0      AUsession_hdr      384 bytes
 *   384    AUcontract         the mixer's contract copy (hdr.mixer_seq)
 *   1024   AUvcmd[cmd_capacity]      client -> audiod, multi-producer
 *   ...    AUvstate[voice_slots]     audiod -> client
 * ------------------------------------------------------------------------- */
#define AU_SESSION_MAGIC        0x31535541u   /* "AUS1" */
#define AU_SESSION_CONTRACT_OFF 384u
#define AU_SESSION_CMD_OFF      1024u

typedef struct AUsession_hdr {
	/* line 0: immutable */
	uint32_t magic;                /*   0 */
	uint16_t version;              /*   4 */
	uint16_t hdr_size;             /*   6 384 */
	uint64_t region_size;          /*   8 */
	AUhandle session;              /*  16 */
	uint32_t cmd_offset;           /*  24 */
	uint32_t cmd_capacity;         /*  28 power of two */
	uint32_t voice_offset;         /*  32 */
	uint32_t voice_slots;          /*  36 */
	uint32_t contract_offset;      /*  40 */
	uint8_t  reserved0[84];        /*  44 */
	/* line 1: client (atomic fetch-add to claim a command slot) */
	uint64_t cmd_tail;             /* 128 */
	uint8_t  reserved1[120];       /* 136 */
	/* line 2: audiod */
	uint64_t cmd_head;             /* 256 next slot the mixer consumes */
	uint32_t mixer_seq;            /* 264 seqlock over the mixer contract copy */
	uint32_t reserved2a;           /* 268 */
	uint64_t cmd_rejected;         /* 272 malformed commands dropped */
	uint8_t  reserved2[104];       /* 280 */
} AUsession_hdr;

enum AUvop : uint16_t {
	AU_VOP_PLAY = 1, AU_VOP_STOP = 2, AU_VOP_GAIN = 3, AU_VOP_LOOP = 4,
};

/* AUvcmd.flags and AUvoice_params.flags */
#define AU_VF_LOOP   0x0001u   /* loop [loop_start, loop_end) until stopped or unset */
#define AU_VF_AT     0x0002u   /* start at at_host (sample-accurate) */

typedef struct AUvcmd {
	uint64_t seq;                  /*  0 slot sequence: published when == claim + 1 */
	uint16_t op;                   /*  8 AUvop */
	uint16_t flags;                /* 10 AU_VF_* */
	uint32_t slot;                 /* 12 voice slot index */
	uint32_t voice_gen;            /* 16 */
	float    gain;                 /* 20 linear */
	AUhandle buffer;               /* 24 */
	uint64_t at_host;              /* 32 */
	uint32_t loop_start;           /* 40 frames */
	uint32_t loop_end;             /* 44 0 = end of buffer */
	uint8_t  reserved[16];         /* 48 */
} AUvcmd;

enum AUvstate_kind : uint32_t {
	AU_VOICE_IDLE = 0, AU_VOICE_PENDING = 1, AU_VOICE_PLAYING = 2, AU_VOICE_ENDED = 3,
};

/* AU_EV_VOICE_END arg32 and AUvstate.end_reason */
enum AUvend : uint32_t {
	AU_VEND_NONE = 0, AU_VEND_FINISHED = 1, AU_VEND_STOPPED = 2, AU_VEND_BUFFER = 3,
	AU_VEND_SERVICE = 4, AU_VEND_LIMIT = 5,   /* LIMIT: system voice limit reached */
};

typedef struct AUvstate {
	uint32_t gen;                  /*  0 read first and last: equal means consistent */
	uint32_t state;                /*  4 AUvstate_kind */
	uint64_t pos;                  /*  8 frames of the buffer played */
	AUhandle buffer;               /* 16 */
	uint32_t end_reason;           /* 24 AUvend */
	uint32_t gen_end;              /* 28 copy of gen written last */
} AUvstate;

/* ---------------------------------------------------------------------------
 * Client library records (in-process C ABI).
 * ------------------------------------------------------------------------- */
typedef struct AUrender_info {
	AUhandle stream;               /*  0 */
	uint64_t pos;                  /*  8 client frame position of buf[0] */
	uint64_t present_host;         /* 16 output: at the terminal; input: captured */
	uint64_t deadline_host;        /* 24 return before this */
	uint64_t generation;           /* 32 contract generation in force */
	uint32_t frames;               /* 40 */
	uint32_t flags;                /* 44 AU_RF_* */
	uint64_t underruns;            /* 48 cumulative */
	uint64_t reserved;             /* 56 */
} AUrender_info;

#define AU_RF_PRIME        0x0001u   /* warm-up call before start; output discarded */
#define AU_RF_DISCONTINUITY 0x0002u  /* frames were lost before this buffer */
#define AU_RF_CONTRACT     0x0004u   /* first call under a new contract generation */

/* The render callback. Output: fill frames x channels samples of the stream
 * format into buf. Capture: buf holds the input. T2 rules: AU-T2. */
typedef void (*AUrender_fn)(void *ctx, const AUrender_info *info, void *buf);

/* AUstream_params.flags */
#define AU_OPEN_PIN           0x0001u   /* stay on device_uid; do not follow default */
#define AU_OPEN_EXACT_PERIOD  0x0002u   /* fail with AU_ERR_PERIOD instead of adjusting */
#define AU_OPEN_NO_CONVERT    0x0004u   /* fail with AU_ERR_FORMAT instead of converting rate */
#define AU_OPEN_MONITOR       0x0008u   /* capture the mix of the output device_uid */
#define AU_OPEN_NO_PRIME      0x0010u   /* skip the AU_RF_PRIME warm-up call */

typedef struct AUstream_params {
	uint32_t    size;              /*   0 sizeof(AUstream_params) */
	uint16_t    version;           /*   4 1 */
	uint8_t     direction;         /*   6 AUdirection */
	uint8_t     mode;              /*   7 AUmode (CALLBACK or QUEUE) */
	uint16_t    format;            /*   8 AUformat */
	uint16_t    channels;          /*  10 1..AU_MAX_CHANNELS */
	uint32_t    rate;              /*  12 */
	uint32_t    period_frames;     /*  16 requested, client frames; 0 = service default */
	uint16_t    lead_periods;      /*  20 0 = 1 (callback) or 8 (queue) */
	uint16_t    reserved0;         /*  22 */
	uint64_t    channel_mask;      /*  24 0 = default for channels */
	uint64_t    compute_ns;        /*  32 declared callback computation; 0 = period/2 */
	uint8_t     device_uid[16];    /*  40 all zero = default */
	uint32_t    flags;             /*  56 AU_OPEN_* */
	uint32_t    reserved1;         /*  60 */
	AUrender_fn render;            /*  64 CALLBACK mode */
	void       *ctx;               /*  72 */
	const char *name;              /*  80 UTF-8, shown in state and the Pulse door; may be NULL */
	uint8_t     reserved[40];      /*  88 zero */
} AUstream_params;

typedef struct AUstream_stats {
	uint32_t size;                 /*   0 */
	uint32_t reserved0;            /*   4 */
	uint64_t callbacks;            /*   8 */
	uint64_t frames;               /*  16 client frames transferred */
	uint64_t underruns;            /*  24 */
	uint64_t underrun_frames;      /*  32 */
	uint64_t overruns;             /*  40 */
	uint64_t late;                 /*  48 */
	uint64_t overbudget;           /*  56 */
	uint64_t max_callback_ns;      /*  64 */
	uint64_t max_wake_ns;          /*  72 doorbell sent -> callback entered */
	uint64_t rt_violations;        /*  80 debug builds: locks/allocations seen on the stream thread */
	uint8_t  reserved[40];         /*  88 */
} AUstream_stats;

typedef struct AUbuffer_desc {
	uint32_t size;                 /*  0 */
	uint16_t format;               /*  4 AUformat */
	uint16_t channels;             /*  6 */
	uint32_t rate;                 /*  8 */
	uint32_t frames;               /* 12 */
	uint64_t channel_mask;         /* 16 */
	uint8_t  reserved[8];          /* 24 */
} AUbuffer_desc;

typedef struct AUvoice_params {
	uint32_t size;                 /*  0 */
	uint32_t flags;                /*  4 AU_VF_* */
	AUhandle buffer;               /*  8 */
	float    gain;                 /* 16 linear, 0..AU_GAIN_MAX */
	uint32_t reserved0;            /* 20 */
	uint64_t at_host;              /* 24 AU_VF_AT */
	uint32_t loop_start;           /* 32 */
	uint32_t loop_end;             /* 36 0 = end */
	uint8_t  reserved[24];         /* 40 */
} AUvoice_params;

#define AU_GAIN_MAX 8.0f

typedef struct AUvoice_state {
	AUhandle voice;                /*  0 */
	uint32_t state;                /*  8 AUvstate_kind */
	uint32_t end_reason;           /* 12 AUvend */
	uint64_t pos;                  /* 16 */
	uint64_t reserved;             /* 24 */
} AUvoice_state;

/* ---------------------------------------------------------------------------
 * Capability queries (AU-VER). au_query returns false for unknown ids.
 * ------------------------------------------------------------------------- */
enum AUcap : uint32_t {
	AU_CAP_API_VERSION     = 0,   /* value: AU_API_VERSION of the library */
	AU_CAP_SERVICE_VERSION = 1,   /* value: audiod protocol version */
	AU_CAP_STREAM_CALLBACK = 2,
	AU_CAP_STREAM_QUEUE    = 3,
	AU_CAP_CAPTURE         = 4,   /* value: 1 if this session may capture */
	AU_CAP_CONVERT_RATE    = 5,
	AU_CAP_VOICES          = 6,   /* value: voice slots per session */
	AU_CAP_VOICE_AT        = 7,
	AU_CAP_HOTPLUG         = 8,
	AU_CAP_FOLLOW_DEFAULT  = 9,
	AU_CAP_MONITOR         = 10,  /* value: 1 if this session may capture a mix */
	AU_CAP_RT_GROUP        = 11,  /* helper threads may join the stream's deadline */
	AU_CAP_MIN_PERIOD      = 12,  /* value: smallest period in frames at 48 kHz */
	AU_CAP_MAX_STREAMS     = 13,  /* value: per session */
	AU_CAP_DUPLEX          = 14,  /* reserved: false in v1 */
	AU_CAP_VOICE_PITCH     = 15,  /* reserved: false in v1 */
	AU_CAP_DEVICE_CONTROL  = 16,  /* value: 1 if this session holds cap:audio:device */
};

/* ---------------------------------------------------------------------------
 * The client library, libndaudio. Thread-safety per AU-LIB-006; the calls
 * marked [RT] take no lock, make no allocation and no system call, and may
 * be called from a render callback.
 * ------------------------------------------------------------------------- */

/* Sessions: one 9P attach to audiod. mount may be NULL for AU_MOUNT. */
AUerror     au_open(const char *mount, AUhandle *out_session);
void        au_close(AUhandle session);
int         au_session_fd(AUhandle session);   /* readable when events are pending; -1 on error */
AUerror     au_session_events(AUhandle session, AUevent *out, uint32_t cap, uint32_t *out_n);
bool        au_query(AUhandle session, uint32_t cap, uint64_t *out_value);

const char *au_error_detail(void);   /* for AU_ERR_ADMISSION: contains nd_sched_error_detail() verbatim */
const char *au_error_name(AUerror e);

/* After AU_ERR_ADMISSION on the calling thread: copies SC's refusal record
 * (sc_rt_refusal, nd_sched.h; the caller sets out->size) and returns true.
 * Returns false when the thread's last error was not an admission refusal
 * (AU-ABI-006). The type is declared in nd_sched.h; it is only named here. */
struct sc_rt_refusal;
bool        au_error_refusal(struct sc_rt_refusal *out);

/* Devices. */
AUerror     au_devices(AUhandle session, AUdevinfo *out, uint32_t cap, uint32_t *out_n);
AUerror     au_device_info(AUhandle session, const uint8_t uid[16], AUdevinfo *out);
AUerror     au_set_default(AUhandle session, uint8_t direction, const uint8_t uid[16]);

/* Tier 1: streams. */
AUerror     au_stream_open(AUhandle session, const AUstream_params *p, AUhandle *out_stream);
AUerror     au_stream_start(AUhandle stream);
AUerror     au_stream_stop(AUhandle stream);
AUerror     au_stream_drain(AUhandle stream);   /* output: AU_EV_DRAINED when played out */
void        au_stream_close(AUhandle stream);
AUerror     au_stream_contract(AUhandle stream, AUcontract *out);            /* [RT] */
AUerror     au_stream_stats(AUhandle stream, AUstream_stats *out);           /* [RT] */
AUerror     au_stream_set_gain(AUhandle stream, float gain);
AUerror     au_stream_pin(AUhandle stream, const uint8_t uid[16]);         /* NULL: follow default */
AUerror     au_stream_rt_group(AUhandle stream, uint64_t *out_group);    /* out: the stream thread's sc_ticket, not an AUhandle (AU-STREAM-010) */

/* Queue mode. write/read are non-blocking and return frames moved. */
uint32_t    au_stream_write(AUhandle stream, const void *frames, uint32_t n);  /* [RT] */
uint32_t    au_stream_read(AUhandle stream, void *frames, uint32_t n);         /* [RT] */
uint32_t    au_stream_available(AUhandle stream);                              /* [RT] */
AUerror     au_stream_write_all(AUhandle stream, const void *frames, uint32_t n, uint64_t deadline_host);
AUerror     au_stream_read_all(AUhandle stream, void *frames, uint32_t n, uint64_t deadline_host);
AUerror     au_stream_set_watermark(AUhandle stream, uint32_t frames);
AUerror     au_stream_doorbell(AUhandle stream, uint32_t *out_port);            /* EVFILT_MACHPORT */

/* Tier 2: buffers and voices on the system mixer. */
AUerror     au_buffer_create(AUhandle session, const AUbuffer_desc *d, const void *samples, AUhandle *out_buffer);
void        au_buffer_release(AUhandle buffer);
AUerror     au_voice_play(AUhandle session, const AUvoice_params *p, AUhandle *out_voice);  /* [RT] */
AUerror     au_voice_stop(AUhandle voice);                                                  /* [RT] */
AUerror     au_voice_set_gain(AUhandle voice, float gain);                                  /* [RT] */
AUerror     au_voice_set_loop(AUhandle voice, bool loop);                                   /* [RT] */
AUerror     au_voice_state(AUhandle voice, AUvoice_state *out);                             /* [RT] */
AUerror     au_mixer_contract(AUhandle session, AUcontract *out);                           /* [RT] */

#ifdef __cplusplus
}
#endif

/* ---------------------------------------------------------------------------
 * Layout assertions (spec-conventions.md §5): every record's size and the
 * offset of every field that crosses a process or ABI boundary.
 * ------------------------------------------------------------------------- */
static_assert(sizeof(AUcontract) == 256, "AUcontract is 256 bytes");
static_assert(offsetof(AUcontract, size) == 0, "AUcontract.size");
static_assert(offsetof(AUcontract, version) == 4, "AUcontract.version");
static_assert(offsetof(AUcontract, flags) == 6, "AUcontract.flags");
static_assert(offsetof(AUcontract, stream) == 8, "AUcontract.stream");
static_assert(offsetof(AUcontract, generation) == 16, "AUcontract.generation");
static_assert(offsetof(AUcontract, device) == 24, "AUcontract.device");
static_assert(offsetof(AUcontract, device_uid) == 32, "AUcontract.device_uid");
static_assert(offsetof(AUcontract, rate) == 48, "AUcontract.rate");
static_assert(offsetof(AUcontract, device_rate) == 52, "AUcontract.device_rate");
static_assert(offsetof(AUcontract, period_frames) == 56, "AUcontract.period_frames");
static_assert(offsetof(AUcontract, mixer_period_frames) == 60, "AUcontract.mixer_period_frames");
static_assert(offsetof(AUcontract, period_ns) == 64, "AUcontract.period_ns");
static_assert(offsetof(AUcontract, channel_mask) == 72, "AUcontract.channel_mask");
static_assert(offsetof(AUcontract, channels) == 80, "AUcontract.channels");
static_assert(offsetof(AUcontract, format) == 82, "AUcontract.format");
static_assert(offsetof(AUcontract, direction) == 84, "AUcontract.direction");
static_assert(offsetof(AUcontract, mode) == 85, "AUcontract.mode");
static_assert(offsetof(AUcontract, lead_periods) == 86, "AUcontract.lead_periods");
static_assert(offsetof(AUcontract, ring_frames) == 88, "AUcontract.ring_frames");
static_assert(offsetof(AUcontract, change) == 92, "AUcontract.change");
static_assert(offsetof(AUcontract, latency_ns) == 96, "AUcontract.latency_ns");
static_assert(offsetof(AUcontract, lat_client_ns) == 104, "AUcontract.lat_client_ns");
static_assert(offsetof(AUcontract, lat_convert_ns) == 112, "AUcontract.lat_convert_ns");
static_assert(offsetof(AUcontract, lat_mixer_ns) == 120, "AUcontract.lat_mixer_ns");
static_assert(offsetof(AUcontract, lat_device_ns) == 128, "AUcontract.lat_device_ns");
static_assert(offsetof(AUcontract, lat_hw_ns) == 136, "AUcontract.lat_hw_ns");
static_assert(offsetof(AUcontract, anchor_pos) == 144, "AUcontract.anchor_pos");
static_assert(offsetof(AUcontract, anchor_host) == 152, "AUcontract.anchor_host");
static_assert(offsetof(AUcontract, ticks_per_frame_q32) == 160, "AUcontract.ticks_per_frame_q32");
static_assert(offsetof(AUcontract, timebase_numer) == 168, "AUcontract.timebase_numer");
static_assert(offsetof(AUcontract, timebase_denom) == 172, "AUcontract.timebase_denom");
static_assert(offsetof(AUcontract, rt_period_ns) == 176, "AUcontract.rt_period_ns");
static_assert(offsetof(AUcontract, rt_computation_ns) == 184, "AUcontract.rt_computation_ns");
static_assert(offsetof(AUcontract, rt_constraint_ns) == 192, "AUcontract.rt_constraint_ns");
static_assert(offsetof(AUcontract, route) == 200, "AUcontract.route");
static_assert(offsetof(AUcontract, state) == 204, "AUcontract.state");
static_assert(offsetof(AUcontract, reserved) == 208, "AUcontract.reserved");

static_assert(sizeof(AUevent) == 32, "AUevent is 32 bytes");
static_assert(offsetof(AUevent, type) == 0, "AUevent.type");
static_assert(offsetof(AUevent, flags) == 2, "AUevent.flags");
static_assert(offsetof(AUevent, arg32) == 4, "AUevent.arg32");
static_assert(offsetof(AUevent, object) == 8, "AUevent.object");
static_assert(offsetof(AUevent, value) == 16, "AUevent.value");
static_assert(offsetof(AUevent, host) == 24, "AUevent.host");
static_assert(AU_EV_MAX <= 64, "event types fit a 64-bit mask");

static_assert(sizeof(AUdevinfo) == 256, "AUdevinfo is 256 bytes");
static_assert(offsetof(AUdevinfo, flags) == 6, "AUdevinfo.flags");
static_assert(offsetof(AUdevinfo, device) == 8, "AUdevinfo.device");
static_assert(offsetof(AUdevinfo, uid) == 16, "AUdevinfo.uid");
static_assert(offsetof(AUdevinfo, kind) == 32, "AUdevinfo.kind");
static_assert(offsetof(AUdevinfo, rate) == 36, "AUdevinfo.rate");
static_assert(offsetof(AUdevinfo, rates) == 40, "AUdevinfo.rates");
static_assert(offsetof(AUdevinfo, max_channels_out) == 44, "AUdevinfo.max_channels_out");
static_assert(offsetof(AUdevinfo, max_channels_in) == 46, "AUdevinfo.max_channels_in");
static_assert(offsetof(AUdevinfo, min_period_frames) == 48, "AUdevinfo.min_period_frames");
static_assert(offsetof(AUdevinfo, max_period_frames) == 52, "AUdevinfo.max_period_frames");
static_assert(offsetof(AUdevinfo, mixer_period_frames) == 56, "AUdevinfo.mixer_period_frames");
static_assert(offsetof(AUdevinfo, port) == 60, "AUdevinfo.port");
static_assert(offsetof(AUdevinfo, lat_device_ns) == 64, "AUdevinfo.lat_device_ns");
static_assert(offsetof(AUdevinfo, lat_hw_ns) == 72, "AUdevinfo.lat_hw_ns");
static_assert(offsetof(AUdevinfo, channel_mask_out) == 80, "AUdevinfo.channel_mask_out");
static_assert(offsetof(AUdevinfo, channel_mask_in) == 88, "AUdevinfo.channel_mask_in");
static_assert(offsetof(AUdevinfo, generation) == 96, "AUdevinfo.generation");
static_assert(offsetof(AUdevinfo, name) == 104, "AUdevinfo.name");
static_assert(offsetof(AUdevinfo, reserved) == 168, "AUdevinfo.reserved");

static_assert(sizeof(AUring_hdr) == 512, "AUring_hdr is 512 bytes");
static_assert(offsetof(AUring_hdr, magic) == 0, "AUring_hdr.magic");
static_assert(offsetof(AUring_hdr, version) == 4, "AUring_hdr.version");
static_assert(offsetof(AUring_hdr, hdr_size) == 6, "AUring_hdr.hdr_size");
static_assert(offsetof(AUring_hdr, region_size) == 8, "AUring_hdr.region_size");
static_assert(offsetof(AUring_hdr, stream) == 16, "AUring_hdr.stream");
static_assert(offsetof(AUring_hdr, contract_offset) == 24, "AUring_hdr.contract_offset");
static_assert(offsetof(AUring_hdr, data_offset) == 28, "AUring_hdr.data_offset");
static_assert(offsetof(AUring_hdr, capacity_frames) == 32, "AUring_hdr.capacity_frames");
static_assert(offsetof(AUring_hdr, period_frames) == 36, "AUring_hdr.period_frames");
static_assert(offsetof(AUring_hdr, channels) == 40, "AUring_hdr.channels");
static_assert(offsetof(AUring_hdr, sample_bytes) == 42, "AUring_hdr.sample_bytes");
static_assert(offsetof(AUring_hdr, direction) == 44, "AUring_hdr.direction");
static_assert(offsetof(AUring_hdr, mode) == 45, "AUring_hdr.mode");
static_assert(offsetof(AUring_hdr, lead_periods) == 46, "AUring_hdr.lead_periods");
static_assert(offsetof(AUring_hdr, ring_id) == 48, "AUring_hdr.ring_id");
static_assert(offsetof(AUring_hdr, server_pos) == 128, "AUring_hdr.server_pos starts line 1");
static_assert(offsetof(AUring_hdr, state) == 136, "AUring_hdr.state");
static_assert(offsetof(AUring_hdr, doorbell_seq) == 140, "AUring_hdr.doorbell_seq");
static_assert(offsetof(AUring_hdr, contract_seq) == 144, "AUring_hdr.contract_seq");
static_assert(offsetof(AUring_hdr, anchor_seq) == 148, "AUring_hdr.anchor_seq");
static_assert(offsetof(AUring_hdr, contract_gen) == 152, "AUring_hdr.contract_gen");
static_assert(offsetof(AUring_hdr, anchor_pos) == 160, "AUring_hdr.anchor_pos");
static_assert(offsetof(AUring_hdr, anchor_host) == 168, "AUring_hdr.anchor_host");
static_assert(offsetof(AUring_hdr, ticks_per_frame_q32) == 176, "AUring_hdr.ticks_per_frame_q32");
static_assert(offsetof(AUring_hdr, tick_host) == 184, "AUring_hdr.tick_host");
static_assert(offsetof(AUring_hdr, deadline_host) == 192, "AUring_hdr.deadline_host");
static_assert(offsetof(AUring_hdr, underruns) == 200, "AUring_hdr.underruns");
static_assert(offsetof(AUring_hdr, underrun_frames) == 208, "AUring_hdr.underrun_frames");
static_assert(offsetof(AUring_hdr, overruns) == 216, "AUring_hdr.overruns");
static_assert(offsetof(AUring_hdr, client_pos) == 256, "AUring_hdr.client_pos starts line 2");
static_assert(offsetof(AUring_hdr, watermark_frames) == 264, "AUring_hdr.watermark_frames");
static_assert(offsetof(AUring_hdr, client_flags) == 268, "AUring_hdr.client_flags");
static_assert(offsetof(AUring_hdr, client_callbacks) == 272, "AUring_hdr.client_callbacks");
static_assert(offsetof(AUring_hdr, client_late) == 280, "AUring_hdr.client_late");
static_assert(offsetof(AUring_hdr, client_overbudget) == 288, "AUring_hdr.client_overbudget");
static_assert(offsetof(AUring_hdr, reserved3) == 384, "AUring_hdr line 3");
static_assert(AU_RING_CONTRACT_OFF >= sizeof(AUring_hdr), "contract copy after the header");
static_assert(AU_RING_DATA_OFF >= AU_RING_CONTRACT_OFF + sizeof(AUcontract), "data after the contract copy");
static_assert(AU_RING_DATA_OFF % AU_CACHELINE == 0, "data is line-aligned");

static_assert(sizeof(AUdoorbell) == 32, "AUdoorbell is 32 bytes");
static_assert(offsetof(AUdoorbell, doorbell_seq) == 0, "AUdoorbell.doorbell_seq");
static_assert(offsetof(AUdoorbell, state) == 4, "AUdoorbell.state");
static_assert(offsetof(AUdoorbell, server_pos) == 8, "AUdoorbell.server_pos");
static_assert(offsetof(AUdoorbell, deadline_host) == 16, "AUdoorbell.deadline_host");
static_assert(offsetof(AUdoorbell, contract_gen) == 24, "AUdoorbell.contract_gen");

static_assert(sizeof(AUsession_hdr) == 384, "AUsession_hdr is 384 bytes");
static_assert(offsetof(AUsession_hdr, magic) == 0, "AUsession_hdr.magic");
static_assert(offsetof(AUsession_hdr, region_size) == 8, "AUsession_hdr.region_size");
static_assert(offsetof(AUsession_hdr, session) == 16, "AUsession_hdr.session");
static_assert(offsetof(AUsession_hdr, cmd_offset) == 24, "AUsession_hdr.cmd_offset");
static_assert(offsetof(AUsession_hdr, cmd_capacity) == 28, "AUsession_hdr.cmd_capacity");
static_assert(offsetof(AUsession_hdr, voice_offset) == 32, "AUsession_hdr.voice_offset");
static_assert(offsetof(AUsession_hdr, voice_slots) == 36, "AUsession_hdr.voice_slots");
static_assert(offsetof(AUsession_hdr, contract_offset) == 40, "AUsession_hdr.contract_offset");
static_assert(offsetof(AUsession_hdr, cmd_tail) == 128, "AUsession_hdr.cmd_tail starts line 1");
static_assert(offsetof(AUsession_hdr, cmd_head) == 256, "AUsession_hdr.cmd_head starts line 2");
static_assert(offsetof(AUsession_hdr, mixer_seq) == 264, "AUsession_hdr.mixer_seq");
static_assert(offsetof(AUsession_hdr, cmd_rejected) == 272, "AUsession_hdr.cmd_rejected");
static_assert(AU_SESSION_CONTRACT_OFF >= sizeof(AUsession_hdr), "mixer contract after the header");
static_assert(AU_SESSION_CMD_OFF >= AU_SESSION_CONTRACT_OFF + sizeof(AUcontract), "commands after the contract");

static_assert(sizeof(AUvcmd) == 64, "AUvcmd is 64 bytes");
static_assert(offsetof(AUvcmd, seq) == 0, "AUvcmd.seq");
static_assert(offsetof(AUvcmd, op) == 8, "AUvcmd.op");
static_assert(offsetof(AUvcmd, flags) == 10, "AUvcmd.flags");
static_assert(offsetof(AUvcmd, slot) == 12, "AUvcmd.slot");
static_assert(offsetof(AUvcmd, voice_gen) == 16, "AUvcmd.voice_gen");
static_assert(offsetof(AUvcmd, gain) == 20, "AUvcmd.gain");
static_assert(offsetof(AUvcmd, buffer) == 24, "AUvcmd.buffer");
static_assert(offsetof(AUvcmd, at_host) == 32, "AUvcmd.at_host");
static_assert(offsetof(AUvcmd, loop_start) == 40, "AUvcmd.loop_start");
static_assert(offsetof(AUvcmd, loop_end) == 44, "AUvcmd.loop_end");

static_assert(sizeof(AUvstate) == 32, "AUvstate is 32 bytes");
static_assert(offsetof(AUvstate, gen) == 0, "AUvstate.gen");
static_assert(offsetof(AUvstate, state) == 4, "AUvstate.state");
static_assert(offsetof(AUvstate, pos) == 8, "AUvstate.pos");
static_assert(offsetof(AUvstate, buffer) == 16, "AUvstate.buffer");
static_assert(offsetof(AUvstate, end_reason) == 24, "AUvstate.end_reason");
static_assert(offsetof(AUvstate, gen_end) == 28, "AUvstate.gen_end");

static_assert(sizeof(AUrender_info) == 64, "AUrender_info is 64 bytes");
static_assert(offsetof(AUrender_info, pos) == 8, "AUrender_info.pos");
static_assert(offsetof(AUrender_info, present_host) == 16, "AUrender_info.present_host");
static_assert(offsetof(AUrender_info, deadline_host) == 24, "AUrender_info.deadline_host");
static_assert(offsetof(AUrender_info, generation) == 32, "AUrender_info.generation");
static_assert(offsetof(AUrender_info, frames) == 40, "AUrender_info.frames");
static_assert(offsetof(AUrender_info, flags) == 44, "AUrender_info.flags");
static_assert(offsetof(AUrender_info, underruns) == 48, "AUrender_info.underruns");

static_assert(sizeof(AUstream_params) == 128, "AUstream_params is 128 bytes");
static_assert(offsetof(AUstream_params, direction) == 6, "AUstream_params.direction");
static_assert(offsetof(AUstream_params, mode) == 7, "AUstream_params.mode");
static_assert(offsetof(AUstream_params, format) == 8, "AUstream_params.format");
static_assert(offsetof(AUstream_params, channels) == 10, "AUstream_params.channels");
static_assert(offsetof(AUstream_params, rate) == 12, "AUstream_params.rate");
static_assert(offsetof(AUstream_params, period_frames) == 16, "AUstream_params.period_frames");
static_assert(offsetof(AUstream_params, lead_periods) == 20, "AUstream_params.lead_periods");
static_assert(offsetof(AUstream_params, channel_mask) == 24, "AUstream_params.channel_mask");
static_assert(offsetof(AUstream_params, compute_ns) == 32, "AUstream_params.compute_ns");
static_assert(offsetof(AUstream_params, device_uid) == 40, "AUstream_params.device_uid");
static_assert(offsetof(AUstream_params, flags) == 56, "AUstream_params.flags");
static_assert(offsetof(AUstream_params, render) == 64, "AUstream_params.render");
static_assert(offsetof(AUstream_params, ctx) == 72, "AUstream_params.ctx");
static_assert(offsetof(AUstream_params, name) == 80, "AUstream_params.name");

static_assert(sizeof(AUstream_stats) == 128, "AUstream_stats is 128 bytes");
static_assert(offsetof(AUstream_stats, callbacks) == 8, "AUstream_stats.callbacks");
static_assert(offsetof(AUstream_stats, underruns) == 24, "AUstream_stats.underruns");
static_assert(offsetof(AUstream_stats, max_wake_ns) == 72, "AUstream_stats.max_wake_ns");
static_assert(offsetof(AUstream_stats, rt_violations) == 80, "AUstream_stats.rt_violations");

static_assert(sizeof(AUbuffer_desc) == 32, "AUbuffer_desc is 32 bytes");
static_assert(offsetof(AUbuffer_desc, format) == 4, "AUbuffer_desc.format");
static_assert(offsetof(AUbuffer_desc, rate) == 8, "AUbuffer_desc.rate");
static_assert(offsetof(AUbuffer_desc, frames) == 12, "AUbuffer_desc.frames");
static_assert(offsetof(AUbuffer_desc, channel_mask) == 16, "AUbuffer_desc.channel_mask");

static_assert(sizeof(AUvoice_params) == 64, "AUvoice_params is 64 bytes");
static_assert(offsetof(AUvoice_params, flags) == 4, "AUvoice_params.flags");
static_assert(offsetof(AUvoice_params, buffer) == 8, "AUvoice_params.buffer");
static_assert(offsetof(AUvoice_params, gain) == 16, "AUvoice_params.gain");
static_assert(offsetof(AUvoice_params, at_host) == 24, "AUvoice_params.at_host");
static_assert(offsetof(AUvoice_params, loop_start) == 32, "AUvoice_params.loop_start");
static_assert(offsetof(AUvoice_params, loop_end) == 36, "AUvoice_params.loop_end");

static_assert(sizeof(AUvoice_state) == 32, "AUvoice_state is 32 bytes");
static_assert(offsetof(AUvoice_state, state) == 8, "AUvoice_state.state");
static_assert(offsetof(AUvoice_state, pos) == 16, "AUvoice_state.pos");

static_assert(sizeof(float) == 4, "binary32 gain fields");

#endif /* ND_AUDIO_H */
