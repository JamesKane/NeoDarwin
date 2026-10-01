// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the C interface of OpenBSD pfctl's queueing functions, which its upstream C sources call.
//
// pfctl without ALTQ (docs/base/pf-ntp.md). xnu has no ALTQ: it answers
// ALTQ's requests ENODEV, so pfctl finds no queueing support at start
// ("No ALTQ support in kernel") and never loads or shows queues, as on
// macOS. OpenBSD's pfctl_altq.c and pfctl_qstats.c, which implement
// queueing, aren't built; these take their place for the parser and the
// rest of pfctl: a queueing rule is an error, and there is nothing to show
// or commit.
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <net/pfvar.h>

#include <err.h>
#include <stdio.h>

#include "pfctl_parser.h"
#include "pfctl.h"

static int
nd_no_queueing(void)
{
	warnx("queueing (ALTQ) is not supported: xnu has no ALTQ");
	return (1);
}

int
eval_pfaltq(struct pfctl *pf, struct pf_altq *pa, struct node_queue_bw *bw,
    struct node_queue_opt *opts)
{
	(void)pf; (void)pa; (void)bw; (void)opts;
	return (nd_no_queueing());
}

int
eval_pfqueue(struct pfctl *pf, struct pf_altq *pa, struct node_queue_bw *bw,
    struct node_queue_opt *opts)
{
	(void)pf; (void)pa; (void)bw; (void)opts;
	return (nd_no_queueing());
}

void
print_altq(const struct pf_altq *a, unsigned level, struct node_queue_bw *bw,
    struct node_queue_opt *qopts)
{
	(void)a; (void)level; (void)bw; (void)qopts;
}

void
print_queue(const struct pf_altq *a, unsigned level, struct node_queue_bw *bw,
    int print_interface, struct node_queue_opt *qopts)
{
	(void)a; (void)level; (void)bw; (void)print_interface; (void)qopts;
}

int
check_commit_altq(int dev, int opts)
{
	(void)dev; (void)opts;
	return (0);
}

void
pfaltq_store(struct pf_altq *a)
{
	(void)a;
}

struct pf_altq *
pfaltq_lookup(const char *ifname)
{
	(void)ifname;
	return (NULL);
}

char *
rate2str(double rate)
{
	static char buf[32];

	snprintf(buf, sizeof(buf), "%.0f", rate);
	return (buf);
}

int
pfctl_show_altq(int dev, const char *iface, int opts, int verbose2)
{
	(void)dev; (void)iface; (void)opts; (void)verbose2;
	return (0);
}
