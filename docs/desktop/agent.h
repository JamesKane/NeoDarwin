/* Provenance: distilled from the plan-neo pilot (commit b0a35a1). Reference copy; the build consumes the mirror. */
/*
 * agent.h - the agent endpoint protocol for NeoDarwin applications.
 *
 * Every scriptable application serves a small 9P tree, mounted at
 * /n/agent/APPID:
 *
 *   schema    r    JSON: the verbs the app accepts and the shape of its state
 *   state     r    JSON snapshot of the app's semantic state; consistent per open
 *   actions   rw   write one command line, read the JSON reply on the same fd
 *   log       r    append-only stream of JSON event lines; reads block for more
 *
 * libagent implements the tree and the 9P2000 server on top of an
 * application's three callbacks, so an app adds the endpoint in about a
 * hundred lines. The normative text is docs/agent-protocol.md.
 */
#ifndef PLANNEO_AGENT_H
#define PLANNEO_AGENT_H

#include <stdint.h>
#include <stddef.h>

#define AGENT_VERSION   1
#define AGENT_MOUNT     "/n/agent"
#define AGENT_SRVFMT    "agent.%s"          /* nsd registry name */
#define AGENT_MSIZE     32768               /* 9P message size offered */
#define AGENT_LOGRING   256                 /* events retained for slow readers */
#define AGENT_LINEMAX   1024                /* longest event or command line */
#define AGENT_NFID      128

typedef struct Agent Agent;

typedef struct Agentops {
	const char *app;                        /* APPID: [a-z0-9_-]+ */
	const char *version;
	/* Write the JSON schema into buf; return the length it needs (may exceed n). */
	size_t (*schema)(void *ctx, char *buf, size_t n);
	/* Write the JSON state snapshot into buf; return the length it needs. */
	size_t (*state)(void *ctx, char *buf, size_t n);
	/* Run one command line; write exactly one JSON reply line. Return 0 or -1. */
	int    (*action)(void *ctx, const char *line, char *reply, size_t n);
} Agentops;

Agent   *agent_new(const Agentops *ops, void *ctx);
void     agent_free(Agent *a);

/* Append one JSON event line to the log; wakes blocked log readers. */
void     agent_log(Agent *a, const char *jsonline);
uint64_t agent_seq(const Agent *a);         /* number of events logged so far */

/* Serve one 9P2000 connection on the pair of fds until EOF or error.
 * Returns 0 on clean EOF, -1 on a protocol or I/O error. */
int      agent_serve(Agent *a, int infd, int outfd);

/* Hosted helpers: a listening AF_UNIX socket (returns the fd), and a loop
 * that accepts connections one at a time and serves each. */
int      agent_listen_unix(const char *path);
int      agent_accept_loop(Agent *a, int lfd);

/* JSON string encoder shared by apps: writes "..." with escapes; returns length needed. */
size_t   agent_jsonstr(char *out, size_t n, const char *s, size_t len);

#endif /* PLANNEO_AGENT_H */
