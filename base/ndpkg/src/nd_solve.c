// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: libsolv (packaging.md §5) is a C library whose pool, solver and transaction API Embedded Swift can't import cleanly; this file drives it and speaks plain text to Swift.
//
// ndpkg's solver (P2-02, docs/architecture/packaging.md §5): libsolv over
// the installed set and the repositories' packages, as nd_pkg.h's
// nd_pkg_solve describes. Built for the host too, by
// //base/ndpkg:solve_test.

#include "nd_pkg.h"

// libsolv's inline functions leave parameters unused.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include <solv/pool.h>
#include <solv/poolarch.h>
#include <solv/problems.h>
#include <solv/repo.h>
#include <solv/selection.h>
#include <solv/solver.h>
#include <solv/transaction.h>
#pragma clang diagnostic pop

struct out {
	char *buf;
	size_t len, cap;
};

static void
put(struct out *o, const char *a, const char *b, const char *c)
{
	const char *parts[3] = {a, b, c};
	for (int i = 0; i < 3 && parts[i] != NULL; i++) {
		size_t n = strlen(parts[i]) + 1;
		if (o->len + n + 1 > o->cap) {
			size_t cap = (o->cap + n + 1) * 2;
			char *p = realloc(o->buf, cap);
			if (p == NULL) {
				abort();
			}
			o->buf = p;
			o->cap = cap;
		}
		memcpy(o->buf + o->len, parts[i], n - 1);
		o->len += n - 1;
		o->buf[o->len++] = i + 1 < 3 && parts[i + 1] != NULL ? '\t' : '\n';
	}
	o->buf[o->len] = 0;
}

// "name", or "name OP version" with OP one of = == != < <= > >=.
static Id
dep(Pool *pool, const char *text)
{
	char name[256], op[3], evr[128];
	int n = 0;
	if (sscanf(text, "%255s %2s %127s%n", name, op, evr, &n) == 3 && text[n] == 0) {
		int flags = 0;
		if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
			flags = REL_EQ;
		} else if (strcmp(op, "!=") == 0) {
			flags = REL_LT | REL_GT;
		} else if (strcmp(op, "<") == 0) {
			flags = REL_LT;
		} else if (strcmp(op, "<=") == 0) {
			flags = REL_LT | REL_EQ;
		} else if (strcmp(op, ">") == 0) {
			flags = REL_GT;
		} else if (strcmp(op, ">=") == 0) {
			flags = REL_GT | REL_EQ;
		} else {
			return 0;
		}
		return pool_rel2id(pool, pool_str2id(pool, name, 1), pool_str2id(pool, evr, 1), flags, 1);
	}
	if (sscanf(text, "%255s%n", name, &n) == 1 && text[n] == 0) {
		return pool_str2id(pool, name, 1);
	}
	return 0;
}

// The caller's key for each solvable, by solvable id.
static Queue keys;

static const char *
key(Pool *pool, Id p)
{
	return pool_id2str(pool, p < keys.count ? keys.elements[p] : 0);
}

int
nd_pkg_solve(const char *input, char **output)
{
	struct out o = {0};
	Pool *pool = pool_create();
	Repo *installed = repo_create(pool, "@System"), *available = repo_create(pool, "available");
	pool_set_installed(pool, installed);
	Repo *repo = available;
	Solvable *s = NULL;
	Queue jobs;
	queue_init(&jobs);
	queue_init(&keys);
	int error = 0;

	char *text = strdup(input), *save = NULL;
	if (text == NULL) {
		abort();
	}
	for (char *line = strtok_r(text, "\n", &save); line != NULL && error == 0; line = strtok_r(NULL, "\n", &save)) {
		char *f[5] = {0};
		int nf = 0;
		for (char *p = line; nf < 5;) {
			f[nf++] = p;
			char *tab = strchr(p, '\t');
			if (tab == NULL) {
				break;
			}
			*tab = 0;
			p = tab + 1;
		}
		if (strcmp(f[0], "arch") == 0 && nf == 2) {
			pool_setarch(pool, f[1]);
		} else if (strcmp(f[0], "repo") == 0 && nf == 2) {
			repo = strcmp(f[1], "installed") == 0 ? installed : available;
			s = NULL;
		} else if (strcmp(f[0], "pkg") == 0 && nf == 5) {
			Id p = repo_add_solvable(repo);
			s = pool_id2solvable(pool, p);
			s->name = pool_str2id(pool, f[1], 1);
			s->evr = pool_str2id(pool, f[2], 1);
			s->arch = pool_str2id(pool, strcmp(f[3], "any") == 0 ? "noarch" : f[3], 1);
			while (keys.count <= p) {
				queue_push(&keys, 0);
			}
			keys.elements[p] = pool_str2id(pool, f[4], 1);
			// Every package provides its own name at its version.
			s->provides = repo_addid_dep(repo, s->provides, pool_rel2id(pool, s->name, s->evr, REL_EQ, 1), 0);
		} else if (strcmp(f[0], "dep") == 0 && nf == 3 && s != NULL) {
			Id d = dep(pool, f[2]);
			if (d == 0) {
				put(&o, "problem", "bad dependency", f[2]);
				error = 1;
			} else if (strcmp(f[1], "provides") == 0) {
				s->provides = repo_addid_dep(repo, s->provides, d, 0);
			} else if (strcmp(f[1], "requires") == 0) {
				s->requires = repo_addid_dep(repo, s->requires, d, 0);
			} else if (strcmp(f[1], "conflicts") == 0) {
				s->conflicts = repo_addid_dep(repo, s->conflicts, d, 0);
			} else {
				put(&o, "problem", "bad dependency kind", f[1]);
				error = 1;
			}
		} else if (strcmp(f[0], "job") == 0 && nf == 3) {
			if (strcmp(f[1], "upgrade") == 0 && strcmp(f[2], "*") == 0) {
				queue_push2(&jobs, SOLVER_UPDATE | SOLVER_SOLVABLE_ALL, 0);
				continue;
			}
			Id what = dep(pool, f[2]);
			int how = strcmp(f[1], "install") == 0 ? SOLVER_INSTALL
			    : strcmp(f[1], "remove") == 0      ? SOLVER_ERASE
			    : strcmp(f[1], "upgrade") == 0     ? SOLVER_UPDATE
			                                       : -1;
			if (how < 0 || what == 0) {
				put(&o, "problem", "bad job", f[2]);
				error = 1;
			} else {
				queue_push2(&jobs, how | SOLVER_SOLVABLE_NAME, what);
			}
		} else {
			put(&o, "problem", "bad input line", f[0]);
			error = 1;
		}
	}
	free(text);

	if (error == 0) {
		repo_internalize(installed);
		repo_internalize(available);
		pool_addfileprovides(pool);
		pool_createwhatprovides(pool);
		// A job naming nothing the pool has is a problem, not a no-op.
		for (int i = 0; i < jobs.count; i += 2) {
			if ((jobs.elements[i] & SOLVER_SELECTMASK) != SOLVER_SOLVABLE_NAME) {
				continue;
			}
			Queue q;
			queue_init(&q);
			pool_job2solvables(pool, &q, jobs.elements[i], jobs.elements[i + 1]);
			bool any = false;
			for (int j = 0; j < q.count; j++) {
				if ((jobs.elements[i] & SOLVER_JOBMASK) != SOLVER_ERASE || pool->solvables[q.elements[j]].repo == installed) {
					any = true;
				}
			}
			queue_free(&q);
			if (!any) {
				put(&o, "problem", (jobs.elements[i] & SOLVER_JOBMASK) == SOLVER_ERASE ? "not installed" : "no such package",
				    pool_dep2str(pool, jobs.elements[i + 1]));
				error = 1;
			}
		}
	}
	if (error == 0) {
		Solver *solv = solver_create(pool);
		if (solver_solve(solv, &jobs) != 0) {
			unsigned int n = solver_problem_count(solv);
			for (Id problem = 1; problem <= (Id)n; problem++) {
				put(&o, "problem", solver_problem2str(solv, problem), NULL);
			}
			error = 1;
		} else {
			Transaction *trans = solver_create_transaction(solv);
			transaction_order(trans, 0);
			for (int i = 0; i < trans->steps.count; i++) {
				Id p = trans->steps.elements[i];
				int type = transaction_type(trans, p, SOLVER_TRANSACTION_SHOW_ACTIVE | SOLVER_TRANSACTION_SHOW_ALL | SOLVER_TRANSACTION_SHOW_OBSOLETES | SOLVER_TRANSACTION_CHANGE_IS_REINSTALL);
				switch (type) {
				case SOLVER_TRANSACTION_ERASE:
					put(&o, "remove", key(pool, p), NULL);
					break;
				case SOLVER_TRANSACTION_INSTALL:
					put(&o, "install", key(pool, p), NULL);
					break;
				case SOLVER_TRANSACTION_UPGRADE:
				case SOLVER_TRANSACTION_DOWNGRADE:
				case SOLVER_TRANSACTION_CHANGE:
				case SOLVER_TRANSACTION_REINSTALL:
				case SOLVER_TRANSACTION_OBSOLETES: {
					Id old = transaction_obs_pkg(trans, p);
					if (old != 0) {
						put(&o, type == SOLVER_TRANSACTION_DOWNGRADE ? "downgrade" : "upgrade", key(pool, old), key(pool, p));
					} else {
						put(&o, "install", key(pool, p), NULL);
					}
					break;
				}
				default:
					break;
				}
			}
			transaction_free(trans);
		}
		solver_free(solv);
	}
	queue_free(&jobs);
	queue_free(&keys);
	pool_free(pool);
	if (o.buf == NULL) {
		put(&o, "nothing", NULL, NULL);
	}
	*output = o.buf;
	return error;
}
