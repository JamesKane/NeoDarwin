// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD <sys/queue.h> macros Darwin's lacks (TAILQ_FOREACH_FROM and kin).
#ifndef ND_SYS_QUEUE_H
#define ND_SYS_QUEUE_H
#include_next <sys/queue.h>

#ifndef TAILQ_FOREACH_FROM
#define TAILQ_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : TAILQ_FIRST((head)));		\
	    (var);							\
	    (var) = TAILQ_NEXT((var), field))
#endif
#ifndef STAILQ_FOREACH_FROM
#define STAILQ_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : STAILQ_FIRST((head)));		\
	    (var);							\
	    (var) = STAILQ_NEXT((var), field))
#endif
#ifndef LIST_FOREACH_FROM
#define LIST_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : LIST_FIRST((head)));		\
	    (var);							\
	    (var) = LIST_NEXT((var), field))
#endif
#endif
