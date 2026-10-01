// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: OpenBSD interfaces OpenBSD's pfctl assumes, supplied in C so the upstream sources compile against Darwin's libc and xnu's pfvar.h.
//
// Force-included (-include) ahead of every pfctl source (base/pfctl/build.sh).
#ifndef ND_PFCTL_COMPAT_H
#define ND_PFCTL_COMPAT_H

#include <sys/types.h>
#include <sys/queue.h>
#include <limits.h>

// OpenBSD's <sys/queue.h> SIMPLEQ is Darwin's STAILQ.
#ifndef SIMPLEQ_HEAD
#define SIMPLEQ_HEAD(name, type)        STAILQ_HEAD(name, type)
#define SIMPLEQ_HEAD_INITIALIZER(head)  STAILQ_HEAD_INITIALIZER(head)
#define SIMPLEQ_ENTRY(type)             STAILQ_ENTRY(type)
#define SIMPLEQ_FIRST(head)             STAILQ_FIRST(head)
#define SIMPLEQ_END(head)               NULL
#define SIMPLEQ_EMPTY(head)             STAILQ_EMPTY(head)
#define SIMPLEQ_NEXT(elm, field)        STAILQ_NEXT(elm, field)
#define SIMPLEQ_FOREACH(var, head, field) STAILQ_FOREACH(var, head, field)
#define SIMPLEQ_INIT(head)              STAILQ_INIT(head)
#define SIMPLEQ_INSERT_HEAD(head, elm, field) STAILQ_INSERT_HEAD(head, elm, field)
#define SIMPLEQ_INSERT_TAIL(head, elm, field) STAILQ_INSERT_TAIL(head, elm, field)
#define SIMPLEQ_INSERT_AFTER(head, listelm, elm, field) STAILQ_INSERT_AFTER(head, listelm, elm, field)
#define SIMPLEQ_REMOVE_HEAD(head, field) STAILQ_REMOVE_HEAD(head, field)
#endif
#ifndef TAILQ_END
#define TAILQ_END(head)                 NULL
#endif

// xnu reads a rule's rtableid as an interface scope, an interface index
// (pf.c, PF_RTABLEID_IS_VALID), where OpenBSD names a routing table.
#ifndef RT_TABLEID_MAX
#define RT_TABLEID_MAX INT_MAX
#endif

// pfctl sizes its default table-entry limit by physical memory; Darwin's
// 64-bit sysctl for it is hw.memsize.
#define HW_PHYSMEM64 HW_MEMSIZE

// OpenBSD's struct pf_addr names its address members v4 and v6; xnu's
// names them v4addr and v6addr. pfctl uses neither word otherwise.
#define v4 v4addr
#define v6 v6addr

// The ruleset functions pfctl links from pf_ruleset.c. OpenBSD's pfvar.h
// declares them for userland; xnu's declares them for the kernel only
// (KERNEL_PRIVATE), with a bounds-checked pf_anchor_setup().
struct pf_anchor;
struct pf_rule;
struct pf_ruleset;
struct pfioc_rule;
int                     pf_get_ruleset_number(u_int8_t);
void                    pf_init_ruleset(struct pf_ruleset *);
int                     pf_anchor_setup(struct pf_rule *, const struct pf_ruleset *, const char *);
int                     pf_anchor_copyout(const struct pf_ruleset *, const struct pf_rule *, struct pfioc_rule *);
void                    pf_anchor_remove(struct pf_rule *);
void                    pf_remove_if_empty_ruleset(struct pf_ruleset *);
struct pf_anchor        *pf_find_anchor(const char *);
struct pf_ruleset       *pf_find_ruleset(const char *);
struct pf_ruleset       *pf_find_or_create_ruleset(const char *);

#endif
