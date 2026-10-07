// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: ndpkg checks signatures with ndamfi's C verifier (the kernel's code) and reaches libzstd, libmd and sysctl, which Embedded Swift can't import, through plain C.
//
// ndpkg's C shim: see NDPkgShim/nd_pkg.h.

#include "nd_pkg.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

// ndsign's documents and Ed25519: the kernel's grant verifier's own C,
// compiled into ndpkg, with the digests below (NeoDarwin has no
// CommonCrypto). OpenSSH's ed25519.c isn't written to these warning flags.
#define ND_NDSIGN_EXTERNAL_DIGESTS 1
#include "nd_ndsign.c"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "nd_ed25519.c"
#pragma clang diagnostic pop

void
nd_pkg_warn(const char *message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
}

int
nd_pkg_errno(void)
{
	return errno;
}

void
nd_pkg_free(void *p)
{
	free(p);
}

uint8_t *
nd_pkg_read_file(const char *path, size_t *len)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return NULL;
	}
	struct stat st;
	uint8_t *buf = NULL;
	if (fstat(fd, &st) == 0 && st.st_size >= 0) {
		size_t n = (size_t)st.st_size, at = 0;
		buf = malloc(n ? n : 1);
		while (buf != NULL && at < n) {
			ssize_t r = read(fd, buf + at, n - at);
			if (r <= 0) {
				int e = r == 0 ? EIO : errno;
				free(buf);
				buf = NULL;
				errno = e;
				break;
			}
			at += (size_t)r;
		}
		*len = n;
	}
	int e = errno;
	close(fd);
	errno = e;
	return buf;
}

// -- libzstd and libmd, bound at run time --------------------------------------------

static void *
sym(const char *lib, const char *name)
{
	void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
	return h == NULL ? NULL : dlsym(h, name);
}

uint8_t *
nd_pkg_zstd_decompress(const uint8_t *in, size_t len, size_t *out_len, char *error, size_t size)
{
	unsigned long long (*content_size)(const void *, size_t) = (unsigned long long (*)(const void *, size_t))
	    sym("/usr/lib/libzstd.1.dylib", "ZSTD_getFrameContentSize");
	size_t (*decompress)(void *, size_t, const void *, size_t) = (size_t (*)(void *, size_t, const void *, size_t))
	    sym("/usr/lib/libzstd.1.dylib", "ZSTD_decompress");
	unsigned (*is_error)(size_t) = (unsigned (*)(size_t))sym("/usr/lib/libzstd.1.dylib", "ZSTD_isError");
	if (content_size == NULL || decompress == NULL || is_error == NULL) {
		snprintf(error, size, "can't load libzstd: %s", dlerror());
		return NULL;
	}
	unsigned long long n = content_size(in, len);
	if (n >= (1ULL << 32)) {  // unknown (-1), an error (-2) or implausibly large
		snprintf(error, size, "not a zstd frame with its content size");
		return NULL;
	}
	uint8_t *out = malloc(n ? (size_t)n : 1);
	if (out == NULL) {
		snprintf(error, size, "out of memory");
		return NULL;
	}
	size_t r = decompress(out, (size_t)n, in, len);
	if (is_error(r) || r != n) {
		free(out);
		snprintf(error, size, "corrupt zstd frame");
		return NULL;
	}
	*out_len = (size_t)n;
	return out;
}

// FreeBSD's libmd: Init(ctx), Update(ctx, data, len), Final(digest, ctx).
// The context is opaque here; 512 bytes is more than SHA512_CTX needs.
static bool
md(const char *prefix, const void *in, size_t len, uint8_t *out)
{
	char name[32];
	snprintf(name, sizeof(name), "%s_Init", prefix);
	void (*init)(void *) = (void (*)(void *))sym("/usr/lib/libmd.dylib", name);
	snprintf(name, sizeof(name), "%s_Update", prefix);
	void (*update)(void *, const void *, size_t) = (void (*)(void *, const void *, size_t))sym("/usr/lib/libmd.dylib", name);
	snprintf(name, sizeof(name), "%s_Final", prefix);
	void (*final)(uint8_t *, void *) = (void (*)(uint8_t *, void *))sym("/usr/lib/libmd.dylib", name);
	if (init == NULL || update == NULL || final == NULL) {
		return false;
	}
	_Alignas(16) uint8_t ctx[512];
	init(ctx);
	update(ctx, in, len);
	final(out, ctx);
	return true;
}

void
nd_sha512(uint8_t out[64], const void *in, size_t len)
{
	if (!md("SHA512", in, len, out)) {
		memset(out, 0, 64);  // fails every signature check
	}
}

void
nd_sha256(uint8_t out[32], const void *in, size_t len)
{
	if (!md("SHA256", in, len, out)) {
		memset(out, 0, 32);
	}
}

void
nd_random_bytes(void *buf, size_t len)
{
	arc4random_buf(buf, len);
}

bool
nd_pkg_sha256(const uint8_t *in, size_t len, uint8_t *out)
{
	return md("SHA256", in == NULL ? (const void *)"" : in, len, out);
}

// -- verification and the kernel ---------------------------------------------------------

size_t
nd_pkg_kernel_roots(uint8_t *roots, size_t max)
{
	char hex[1024];
	size_t len = sizeof(hex) - 1;
	if (sysctlbyname("security.codesigning.neodarwin.pkg_roots", hex, &len, NULL, 0) != 0) {
		return 0;
	}
	hex[len < sizeof(hex) ? len : sizeof(hex) - 1] = '\0';
	size_t n = 0, at = 0, end = strlen(hex);
	while (at < end && n < max) {
		size_t stop = at;
		while (stop < end && hex[stop] != ',') {
			stop++;
		}
		if (!nd_ndsign_unhex(roots + 32 * n, 32, hex + at, stop - at)) {
			return 0;
		}
		n++;
		at = stop + 1;
	}
	return n;
}

int
nd_pkg_verify_bundle(const uint8_t *bundle, size_t len, const uint8_t *roots, size_t nroots, const char *kind,
    char *statement, size_t size)
{
	struct nd_ndsign_bundle b;
	nd_ndsign_error_t e = nd_ndsign_verify_bundle(bundle, len, roots, nroots, (uint64_t)time(NULL), kind, &b);
	if (e != ND_NDSIGN_OK) {
		return (int)e;
	}
	if (b.body[2].n >= size) {
		return ND_NDSIGN_MALFORMED;
	}
	memcpy(statement, b.body[2].p, b.body[2].n);
	statement[b.body[2].n] = '\0';
	return 0;
}

const char *
nd_pkg_error_name(int error)
{
	return nd_ndsign_error_name((nd_ndsign_error_t)error);
}

int
nd_pkg_load_trust_cache(const uint8_t *module, size_t module_len, const uint8_t *grant, size_t grant_len)
{
	if (module_len > UINT32_MAX || grant_len > UINT32_MAX) {
		return EINVAL;
	}
	size_t n = 8 + module_len + grant_len;
	uint8_t *req = malloc(n);
	if (req == NULL) {
		return ENOMEM;
	}
	for (int i = 0; i < 4; i++) {
		req[i] = (uint8_t)(module_len >> (8 * i));
		req[4 + i] = (uint8_t)(grant_len >> (8 * i));
	}
	memcpy(req + 8, module, module_len);
	memcpy(req + 8 + module_len, grant, grant_len);
	int r = sysctlbyname("security.codesigning.neodarwin.load_trust_cache", NULL, NULL, req, n) == 0 ? 0 : errno;
	free(req);
	return r;
}

// -- P2-02: the store and activation ------------------------------------------------------

static int
result(int r)
{
	return r == 0 ? 0 : errno;
}

int
nd_pkg_mkdir(const char *path, int mode)
{
	return result(mkdir(path, (mode_t)mode));
}

int
nd_pkg_mkdirs(const char *path, int mode)
{
	char buf[1024];
	if (strlcpy(buf, path, sizeof(buf)) >= sizeof(buf)) {
		return ENAMETOOLONG;
	}
	for (char *p = buf + 1; ; p++) {
		if (*p == '/' || *p == 0) {
			char c = *p;
			*p = 0;
			if (mkdir(buf, (mode_t)mode) != 0 && errno != EEXIST) {
				return errno;
			}
			*p = c;
			if (c == 0) {
				break;
			}
		}
	}
	return nd_pkg_kind(path) == 2 ? 0 : ENOTDIR;
}

int
nd_pkg_write_file(const char *path, const uint8_t *bytes, size_t len, int mode)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, (mode_t)mode);
	if (fd < 0) {
		return errno;
	}
	size_t at = 0;
	while (at < len) {
		ssize_t w = write(fd, bytes + at, len - at);
		if (w <= 0) {
			int e = w == 0 ? EIO : errno;
			close(fd);
			return e;
		}
		at += (size_t)w;
	}
	// open() applied the umask; the manifest's mode is the mode.
	if (fchmod(fd, (mode_t)mode) != 0 || fsync(fd) != 0) {
		int e = errno;
		close(fd);
		return e;
	}
	return result(close(fd));
}

int
nd_pkg_symlink(const char *target, const char *path)
{
	return result(symlink(target, path));
}

int
nd_pkg_rename(const char *from, const char *to)
{
	return result(rename(from, to));
}

int
nd_pkg_unlink(const char *path)
{
	return result(unlink(path));
}

int
nd_pkg_rmdir(const char *path)
{
	return result(rmdir(path));
}

int
nd_pkg_chmod(const char *path, int mode)
{
	return result(lchmod(path, (mode_t)mode));
}

int
nd_pkg_fsync_dir(const char *path)
{
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		return errno;
	}
	int r = result(fsync(fd));
	close(fd);
	return r;
}

int
nd_pkg_kind(const char *path)
{
	struct stat st;
	if (lstat(path, &st) != 0) {
		return 0;
	}
	return S_ISREG(st.st_mode) ? 1 : S_ISDIR(st.st_mode) ? 2 : S_ISLNK(st.st_mode) ? 3 : 4;
}

char *
nd_pkg_readlink(const char *path)
{
	char buf[1024];
	ssize_t n = readlink(path, buf, sizeof(buf) - 1);
	if (n < 0) {
		return NULL;
	}
	buf[n] = 0;
	return strdup(buf);
}

char *
nd_pkg_list_dir(const char *path)
{
	DIR *d = opendir(path);
	if (d == NULL) {
		return NULL;
	}
	size_t len = 0, cap = 256;
	char *out = malloc(cap);
	struct dirent *e;
	while (out != NULL && (e = readdir(d)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
			continue;
		}
		size_t n = strlen(e->d_name);
		if (len + n + 2 > cap) {
			cap = (len + n + 2) * 2;
			char *p = realloc(out, cap);
			if (p == NULL) {
				free(out);
				out = NULL;
				break;
			}
			out = p;
		}
		memcpy(out + len, e->d_name, n);
		len += n;
		out[len++] = '\n';
	}
	closedir(d);
	if (out == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	out[len] = 0;
	return out;
}

int
nd_pkg_remove_tree(const char *path)
{
	int kind = nd_pkg_kind(path);
	if (kind == 0) {
		return 0;
	}
	if (kind != 2) {
		return nd_pkg_unlink(path);
	}
	chmod(path, 0755);
	char *names = nd_pkg_list_dir(path);
	if (names == NULL) {
		return errno;
	}
	int r = 0;
	for (char *n = names, *nl; r == 0 && (nl = strchr(n, '\n')) != NULL; n = nl + 1) {
		*nl = 0;
		char child[1024];
		if ((size_t)snprintf(child, sizeof(child), "%s/%s", path, n) >= sizeof(child)) {
			r = ENAMETOOLONG;
		} else {
			r = nd_pkg_remove_tree(child);
		}
	}
	free(names);
	return r != 0 ? r : nd_pkg_rmdir(path);
}

int
nd_pkg_lock(const char *path)
{
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0) {
		return errno;
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		int e = errno;
		close(fd);
		return e;
	}
	return 0;  // the descriptor stays open, and the lock held, until exit
}

int
nd_pkg_pid(void)
{
	return (int)getpid();
}

bool
nd_pkg_is_root(void)
{
	return geteuid() == 0;
}

// nullfs_mount() copies in a struct null_mount_conf (a uint64_t of flags)
// and then the lower path, NUL-terminated, from the same address.
int
nd_pkg_nullfs_mount(const char *lower, const char *mountpoint)
{
	size_t n = strlen(lower) + 1;
	uint8_t *data = calloc(1, 8 + n);
	if (data == NULL) {
		return ENOMEM;
	}
	memcpy(data + 8, lower, n);
	int r = result(mount("nullfs", mountpoint, MNT_RDONLY | MNT_NOSUID | MNT_DONTBROWSE, data));
	free(data);
	return r;
}

int
nd_pkg_unmount(const char *mountpoint)
{
	return result(unmount(mountpoint, 0));
}

bool
nd_pkg_is_mountpoint(const char *path)
{
	char parent[1024];
	if (strlcpy(parent, path, sizeof(parent)) >= sizeof(parent)) {
		return false;
	}
	struct stat a, b;
	return stat(path, &a) == 0 && stat(dirname(parent), &b) == 0 && a.st_dev != b.st_dev;
}
