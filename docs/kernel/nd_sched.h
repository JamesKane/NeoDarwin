// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the C ABI is the cross-language boundary (language-policy.md §3); reference copy of the scheduling contract ABI
/*
 * nd_sched.h - NeoDarwin kernel scheduling contract, API version 1.
 *
 * Companion to docs/kernel/scheduling-contract.md, which is normative.
 * Where this header and the text differ, the text wins and this header
 * is a defect (spec-conventions.md §5).
 *
 * Freestanding C23 that also compiles as C++20: only <stdint.h>,
 * <stddef.h> and <stdbool.h>. struct kevent64_s is declared incomplete;
 * callers of nd_wait include <sys/event.h> themselves.
 *
 * Units: every *_ns value is nanoseconds. Deadlines and timestamps are on
 * the SC clock: mach_absolute_time() scaled to ns. It does not advance
 * while the system sleeps. See [SC-TIME-001]..[SC-TIME-003].
 *
 * Records that cross the user/kernel boundary are little-endian,
 * fixed-width, explicitly padded, and begin with a `size` field that
 * the caller sets to sizeof(record); later versions only grow records
 * (charter P13).
 */
#ifndef ND_SCHED_H
#define ND_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_API_VERSION 1u

/* ---------------------------------------------------------------------
 * Errors (charter P12): every call returns an sc_error or a value whose
 * failure form is documented; nd_sched_last_error() gives the calling
 * thread's last error.
 */
enum sc_error : int32_t {
	SC_OK         = 0,
	SC_E_INVAL    = 1,   /* malformed argument or record size */
	SC_E_HANDLE   = 2,   /* stale or foreign ticket (generation mismatch) */
	SC_E_REFUSED  = 3,   /* admission refused; see sc_rt_refusal */
	SC_E_PERM     = 4,   /* flag needs an entitlement the caller lacks */
	SC_E_BUSY     = 5,   /* thread already joined to another ticket */
	SC_E_AGAIN    = 6,   /* nothing to take (nd_rt_notice_take) */
	SC_E_NOTSUP   = 7,   /* capability absent on this kernel */
	SC_E_BADF     = 8,   /* kq is not a kqueue descriptor */
	SC_E_INTR     = 9,   /* wait interrupted by a signal */
	SC_E_NOMEM    = 10,
};

/* ---------------------------------------------------------------------
 * Capabilities [SC-CAP-001]. Bits are only ever added.
 */
enum sc_cap : uint64_t {
	SC_CAP_WAIT           = 1ull << 0,  /* nd_wait */
	SC_CAP_9P_READY       = 1ull << 1,  /* EVFILT_READ/WRITE on nd9p stream files */
	SC_CAP_TIMER_PRECISE  = 1ull << 2,  /* intent-derived slop, leeway replaces slop */
	SC_CAP_TIMER_FIRETIME = 1ull << 3,  /* EVFILT_TIMER ext[0] = expiry time */
	SC_CAP_INTENT         = 1ull << 4,  /* nd_thread_set_intent */
	SC_CAP_PLACEMENT      = 1ull << 5,  /* heterogeneous placement active (AMP) */
	SC_CAP_RT_ADMIT       = 1ull << 6,  /* nd_rt_admit and friends */
	SC_CAP_RT_GROUP       = 1ull << 7,  /* several threads may join one ticket */
	SC_CAP_RT_NOTICE      = 1ull << 8,  /* nd_rt_watch / nd_rt_notice_take */
	SC_CAP_RT_DEADLINE    = 1ull << 9,  /* nd_rt_set_deadline */
};

/* ---------------------------------------------------------------------
 * The one wait [SC-WAIT], [SC-TIMER], [SC-LIB].
 */
#define SC_DEADLINE_POLL   0ull          /* return at once */
#define SC_DEADLINE_NONE   UINT64_MAX    /* no deadline */

/* Reserved kevent idents: EVFILT_USER wake and nd_wait's own deadline
 * timer. Applications MUST NOT register these idents on those filters. */
#define SC_WAKE_IDENT      0x4e44574bull /* 'NDWK', EVFILT_USER */
#define SC_DEADLINE_IDENT  0x4e44444cull /* 'NDDL', EVFILT_TIMER */

/* XNU EVFILT_TIMER fflags (bsd/sys/event.h:310-326), repeated so this
 * header stays freestanding; the layout check compares them. */
#define SC_NOTE_ABSOLUTE              0x00000008u
#define SC_NOTE_LEEWAY                0x00000010u
#define SC_NOTE_CRITICAL              0x00000020u
#define SC_NOTE_BACKGROUND            0x00000040u
#define SC_NOTE_MACH_CONTINUOUS_TIME  0x00000080u
#define SC_NOTE_MACHTIME              0x00000100u
/* The contract timer: absolute, mach-time units, explicit leeway. */
#define SC_TIMER_FFLAGS (SC_NOTE_ABSOLUTE | SC_NOTE_MACHTIME | SC_NOTE_LEEWAY)

struct kevent64_s;

/* Wait on kq until an event or deadline_ns (+ at most leeway_ns).
 * Returns the number of caller events stored in events[0..n) (0 when
 * the deadline passed first), or -1 with nd_sched_last_error() set. */
int32_t nd_wait(int32_t kq, uint64_t deadline_ns, uint64_t leeway_ns,
    struct kevent64_s *events, int32_t n);

uint32_t       nd_sched_api_version(void);   /* SC_API_VERSION of the running system */
uint64_t       nd_sched_capabilities(void);  /* OR of enum sc_cap */
uint64_t       nd_sched_now_ns(void);        /* the SC clock */
enum sc_error  nd_sched_last_error(void);    /* thread-local */
/* Thread-local, human-readable reason for the last failure on this thread
 * (UTF-8, NUL-terminated, never null; "" after success). Valid until the
 * thread's next nd_* call. The codes and records are authoritative. */
const char    *nd_sched_error_detail(void);

/* ---------------------------------------------------------------------
 * The 9P readiness request [SC-9P-004]..[SC-9P-007]: an extension in
 * NeoDarwin's 9P2000.L profile. Wire form (little-endian, 9P framing):
 *   size[4] Tready(160) tag[2] fid[4] mask[4]
 *   size[4] Rready(161) tag[2] mask[4] count[4]
 */
#define SC_9P_VERSION       "9P2000.L.nd1"
#define SC_9P_TREADY        160u
#define SC_9P_RREADY        161u
#define SC_9P_TREADY_SIZE   15u   /* 4 + 1 + 2 + 4 + 4 */
#define SC_9P_RREADY_SIZE   15u   /* 4 + 1 + 2 + 4 + 4 */
#define SC_9P_READY_READ    0x1u
#define SC_9P_READY_WRITE   0x2u
#define SC_9P_READY_END     0x4u

/* ---------------------------------------------------------------------
 * Thread intent [SC-INT], [SC-PLACE].
 */
enum sc_intent : uint32_t {
	SC_INTENT_NONE        = 0,  /* never set: XNU QoS rules apply unchanged */
	SC_INTENT_INTERACTIVE = 1,  /* UI, per-frame work: QoS USER_INTERACTIVE */
	SC_INTENT_THROUGHPUT  = 2,  /* lockstep compute: QoS USER_INITIATED, one core type */
	SC_INTENT_BACKGROUND  = 3,  /* deferrable: QoS BACKGROUND, maximal coalescing */
	SC_INTENT_AUDIO       = 4,  /* real-time when joined to a ticket, else as interactive */
};

enum sc_cluster : uint32_t {
	SC_CLUSTER_ANY = 0,         /* homogeneous system, or no preference */
	SC_CLUSTER_P   = 1,         /* performance cores */
	SC_CLUSTER_E   = 2,         /* efficiency cores */
};

typedef struct sc_intent_info {
	uint32_t size;              /*  0 caller sets sizeof(sc_intent_info) */
	uint32_t intent;            /*  4 enum sc_intent in effect */
	uint32_t qos;               /*  8 XNU THREAD_QOS_* applied */
	uint32_t cluster;           /* 12 enum sc_cluster preferred now */
	uint64_t timer_slop_ns;     /* 16 default slop bound for this thread's timers */
	uint64_t rt_ticket;         /* 24 sc_ticket joined, or 0 */
	uint32_t rt_state;          /* 32 enum sc_rt_state if joined, else 0 */
	uint32_t reserved0;         /* 36 zero */
	uint64_t reserved[2];       /* 40 zero */
} sc_intent_info;               /* 56 bytes */

enum sc_error nd_thread_set_intent(uint32_t intent);   /* calling thread only */
uint32_t      nd_thread_get_intent(void);              /* enum sc_intent */
enum sc_error nd_thread_intent_info(sc_intent_info *out);

/* ---------------------------------------------------------------------
 * Real-time admission [SC-RT], [SC-OVR].
 *
 * A ticket is a per-process handle: index in the low 32 bits, generation
 * in the high 32 (charter P5). Generation 0 is never issued, so 0 is
 * never a valid ticket.
 */
typedef uint64_t sc_ticket;

#define SC_TICKET_NONE          0ull
#define SC_TICKET_MAKE(idx, gen) ((((uint64_t)(uint32_t)(gen)) << 32) | (uint64_t)(uint32_t)(idx))
#define SC_TICKET_INDEX(t)      ((uint32_t)((uint64_t)(t) & 0xffffffffull))
#define SC_TICKET_GEN(t)        ((uint32_t)((uint64_t)(t) >> 32))
#define SC_TICKET_VALID(t)      (SC_TICKET_GEN(t) != 0u)

/* Default admission policy; the live values are in /n/sys/sched/rt/policy. */
#define SC_RT_MIN_COMPUTATION_NS   50000ull        /* XNU min_rt_quantum */
#define SC_RT_MAX_COMPUTATION_NS   50000000ull     /* XNU max_rt_quantum */
#define SC_RT_MAX_PERIOD_NS        1000000000ull
#define SC_RT_TICKET_DENSITY_PPM   750000u         /* one ticket: C/D <= 0.75 */
#define SC_RT_DOMAIN_CAP_PPM       750000u         /* all tickets: sum C/D <= 0.75 m */
#define SC_RT_SYSTEM_SHARE_PPM     250000u         /* reserved for SC_RT_F_SYSTEM */
#define SC_RT_DEMOTE_WINDOW        64u             /* periods */
#define SC_RT_DEMOTE_OVERRUNS      8u              /* overruns within the window */
#define SC_RT_TICKETS_PER_PROCESS  16u

enum sc_rt_flags : uint32_t {
	SC_RT_F_SYSTEM       = 1u << 0,  /* draw on the system share; needs the sched.rt.system entitlement */
	SC_RT_F_RESERVE_ONLY = 1u << 1,  /* reserve without joining the calling thread */
};

typedef struct sc_rt_request {
	uint32_t size;              /*  0 caller sets sizeof(sc_rt_request) */
	uint32_t flags;             /*  4 enum sc_rt_flags */
	uint64_t period_ns;         /*  8 T: minimum time between releases */
	uint64_t computation_ns;    /* 16 C: CPU time per release, on the domain's cores */
	uint64_t constraint_ns;     /* 24 D: relative deadline; 0 means D = T */
	uint64_t udata;             /* 32 returned in every notice */
	uint64_t reserved[3];       /* 40 zero */
} sc_rt_request;                /* 64 bytes */

enum sc_rt_reason : uint32_t {
	SC_RT_R_NONE          = 0,
	SC_RT_R_PARAMS        = 1,  /* T or C zero, C > D, or D > T */
	SC_RT_R_COMPUTATION   = 2,  /* C outside [SC_RT_MIN_COMPUTATION_NS, SC_RT_MAX_COMPUTATION_NS] */
	SC_RT_R_PERIOD        = 3,  /* T > SC_RT_MAX_PERIOD_NS */
	SC_RT_R_DENSITY       = 4,  /* C/D above the per-ticket bound */
	SC_RT_R_BUDGET        = 5,  /* the principal's budget is spent */
	SC_RT_R_UNSCHEDULABLE = 6,  /* the domain test fails */
	SC_RT_R_PERMISSION    = 7,  /* SC_RT_F_SYSTEM without the entitlement */
	SC_RT_R_LIMIT         = 8,  /* SC_RT_TICKETS_PER_PROCESS reached */
	SC_RT_R_NO_DOMAIN     = 9,  /* no real-time domain (all RT cores offline) */
};

typedef struct sc_rt_refusal {
	uint32_t size;              /*  0 caller sets sizeof(sc_rt_refusal) */
	uint32_t reason;            /*  4 enum sc_rt_reason */
	uint64_t max_computation_ns;/*  8 largest C admissible now for the same T and D; 0 if none */
	uint32_t available_ppm;     /* 16 density still admissible in the domain */
	uint32_t budget_ppm;        /* 20 density left in the caller's budget */
	uint64_t reserved[3];       /* 24 zero */
} sc_rt_refusal;                /* 48 bytes */

enum sc_rt_state : uint32_t {
	SC_RT_NONE     = 0,
	SC_RT_ADMITTED = 1,         /* reservation held; joined threads are real-time */
	SC_RT_DEMOTED  = 2,         /* persistent overrun: reservation dropped, threads at fallback */
	SC_RT_REVOKED  = 3,         /* domain shrank or administrator revoked */
};

typedef struct sc_rt_status {
	uint32_t size;              /*  0 caller sets sizeof(sc_rt_status) */
	uint32_t state;             /*  4 enum sc_rt_state */
	uint64_t period_ns;         /*  8 as admitted */
	uint64_t computation_ns;    /* 16 */
	uint64_t constraint_ns;     /* 24 */
	uint64_t releases;          /* 32 since admission */
	uint64_t overruns;          /* 40 releases whose budget ran out */
	uint64_t misses;            /* 48 jobs that ended after their deadline */
	uint64_t max_used_ns;       /* 56 largest CPU time used in one release */
	uint32_t threads;           /* 64 threads joined now */
	uint32_t density_ppm;       /* 68 C/D as reserved */
	uint64_t reserved[3];       /* 72 zero */
} sc_rt_status;                 /* 96 bytes */

enum sc_rt_notice_kind : uint32_t {
	SC_RT_N_OVERRUN = 1,        /* budget exhausted in a release; threads ran at fallback until the next */
	SC_RT_N_MISS    = 2,        /* a job ended after its deadline */
	SC_RT_N_DEMOTED = 3,        /* ticket entered SC_RT_DEMOTED */
	SC_RT_N_REVOKED = 4,        /* ticket entered SC_RT_REVOKED */
};

typedef struct sc_rt_notice {
	uint32_t size;              /*  0 sizeof(sc_rt_notice) as written by the kernel */
	uint32_t kind;              /*  4 enum sc_rt_notice_kind */
	uint64_t ticket;            /*  8 sc_ticket */
	uint64_t udata;             /* 16 from sc_rt_request */
	uint64_t first_ns;          /* 24 SC clock of the first occurrence coalesced here */
	uint64_t count;             /* 32 occurrences coalesced into this notice */
	uint64_t reserved[3];       /* 40 zero */
} sc_rt_notice;                 /* 64 bytes */

/* Admit (T, C, D; D 0 means T) and join the calling thread. If the caller
 * is already joined to a ticket, re-admit that ticket with the new values.
 * Returns the ticket, or SC_TICKET_NONE with *refusal filled (if non-null),
 * nd_sched_last_error() == SC_E_REFUSED and a reason in
 * nd_sched_error_detail(). */
sc_ticket     nd_rt_admit(uint64_t period_ns, uint64_t computation_ns,
                  uint64_t constraint_ns, sc_rt_refusal *refusal);
enum sc_error nd_rt_admit_ex(const sc_rt_request *req, sc_ticket *out,
                  sc_rt_refusal *refusal);
enum sc_error nd_rt_join(sc_ticket ticket);    /* calling thread joins */
enum sc_error nd_rt_leave(void);               /* calling thread leaves its ticket */
enum sc_error nd_rt_release(sc_ticket ticket); /* ends the reservation */
enum sc_error nd_rt_status(sc_ticket ticket, sc_rt_status *out);
/* Absolute deadline (SC clock) of the calling thread's current or next job. */
enum sc_error nd_rt_set_deadline(uint64_t deadline_ns);
enum sc_error nd_rt_watch(int32_t kq, sc_ticket ticket, uint64_t udata);
enum sc_error nd_rt_notice_take(sc_ticket ticket, sc_rt_notice *out);

#ifdef __cplusplus
}
#endif

#endif /* ND_SCHED_H */
