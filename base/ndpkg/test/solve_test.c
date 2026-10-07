// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: exercises ndpkg's C driver of libsolv (src/nd_solve.c) on the host.
//
// ndpkg's solver on the host (P2-02): src/nd_solve.c over libsolv, given
// installed and available packages as ndpkg passes them, must produce the
// transactions ndpkg turns into plans, and refuse what can't be done.

#include "nd_pkg.h"

static int failures;

static void
expect(const char *what, const char *input, int status, const char *output)
{
	char *out = NULL;
	int r = nd_pkg_solve(input, &out);
	if (r != status || out == NULL || strcmp(out, output) != 0) {
		printf("FAIL: %s: status %d, output:\n%s\nwanted status %d, output:\n%s\n", what, r, out ? out : "(null)", status, output);
		failures++;
	} else {
		printf("ok: %s\n", what);
	}
	free(out);
}

#define ARCH "arch\taarch64\n"
#define AVAILABLE                                       \
	"repo\tavailable\n"                             \
	"pkg\thello\t1.0\taarch64\ta1\n"                \
	"pkg\thello\t2.0\taarch64\ta2\n"                \
	"pkg\tlibgreet\t1.0\taarch64\ta3\n"             \
	"pkg\tgreet\t1.0\taarch64\ta4\n"                \
	"dep\trequires\tlibgreet >= 1.0\n"              \
	"pkg\tclash\t1.0\tany\ta5\n"                    \
	"dep\tconflicts\thello\n"                       \
	"pkg\tnewgreet\t1.0\taarch64\ta6\n"             \
	"dep\trequires\tlibgreet >= 2.0\n"              \
	"pkg\tvirtual\t1.0\taarch64\ta7\n"              \
	"dep\tprovides\tgreeting\n"                     \
	"pkg\tneedsvirtual\t1.0\taarch64\ta8\n"         \
	"dep\trequires\tgreeting\n"
#define INSTALLED                                       \
	"repo\tinstalled\n"                             \
	"pkg\thello\t1.0\taarch64\ti1\n"                \
	"pkg\tlibgreet\t1.0\taarch64\ti2\n"             \
	"pkg\tgreet\t1.0\taarch64\ti3\n"                \
	"dep\trequires\tlibgreet >= 1.0\n"

int
main(void)
{
	expect("install picks the newest", ARCH AVAILABLE "job\tinstall\thello\n", 0, "install\ta2\n");
	expect("install a version", ARCH AVAILABLE "job\tinstall\thello = 1.0\n", 0, "install\ta1\n");
	expect("a dependency first", ARCH AVAILABLE "job\tinstall\tgreet\n", 0, "install\ta3\ninstall\ta4\n");
	expect("a provided name", ARCH AVAILABLE "job\tinstall\tneedsvirtual\n", 0, "install\ta7\ninstall\ta8\n");
	expect("installed already", ARCH INSTALLED AVAILABLE "job\tinstall\thello = 1.0\n", 0, "nothing\n");
	expect("upgrade one", ARCH INSTALLED AVAILABLE "job\tupgrade\thello\n", 0, "upgrade\ti1\ta2\n");
	expect("upgrade all", ARCH INSTALLED AVAILABLE "job\tupgrade\t*\n", 0, "upgrade\ti1\ta2\n");
	expect("downgrade", ARCH "repo\tinstalled\npkg\thello\t2.0\taarch64\ti1\n" AVAILABLE "job\tinstall\thello = 1.0\n", 0,
	    "downgrade\ti1\ta1\n");
	expect("remove", ARCH INSTALLED AVAILABLE "job\tremove\tgreet\n", 0, "remove\ti3\n");
	expect("remove a dependency", ARCH INSTALLED AVAILABLE "job\tremove\tlibgreet\n", 1,
	    "problem\tpackage greet-1.0.aarch64 requires libgreet >= 1.0, but none of the providers can be installed\n");
	expect("a conflict", ARCH INSTALLED AVAILABLE "job\tinstall\tclash\n", 1,
	    "problem\tpackage clash-1.0.noarch conflicts with hello provided by hello-1.0.aarch64\n");
	expect("an unsatisfiable version", ARCH AVAILABLE "job\tinstall\tnewgreet\n", 1,
	    "problem\tnothing provides libgreet >= 2.0 needed by newgreet-1.0.aarch64\n");
	expect("no such package", ARCH AVAILABLE "job\tinstall\tnope\n", 1, "problem\tno such package\tnope\n");
	expect("not installed", ARCH AVAILABLE "job\tremove\thello\n", 1, "problem\tnot installed\thello\n");
	expect("a bad dependency", ARCH "repo\tavailable\npkg\tx\t1\taarch64\tk\ndep\trequires\ta >> 1\n", 1,
	    "problem\tbad dependency\ta >> 1\n");
	printf("%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
