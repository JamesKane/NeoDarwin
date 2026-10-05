/*
 * SPDX-License-Identifier: BSD-2-Clause
 * <network/conninfo.h> for netcat-56 (base/netcat/build.sh). macOS's comes
 * with libnetwork, which is closed; nc uses only copyconninfo() and
 * freeconninfo(), for -v's connection report after connectx(2). These are
 * the same calls over xnu's SIOCGCONNINFO (sys/sockio_private.h), the ioctl
 * libnetwork's own copyconninfo() issues: once to learn the address and aux
 * lengths, then again into buffers of that size.
 */
#ifndef ND_NETWORK_CONNINFO_H
#define ND_NETWORK_CONNINFO_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct conninfo {
	__uint32_t      ci_flags;
	__uint32_t      ci_ifindex;
	__int32_t       ci_error;
	struct sockaddr *ci_src;
	struct sockaddr *ci_dst;
	__uint32_t      ci_aux_type;
	void            *ci_aux_data;
} conninfo_t;

static inline void
freeconninfo(conninfo_t *cfo)
{
	if (cfo == NULL)
		return;
	free(cfo->ci_src);
	free(cfo->ci_dst);
	free(cfo->ci_aux_data);
	free(cfo);
}

static inline int
copyconninfo(int s, sae_connid_t cid, conninfo_t **cfop)
{
	struct so_cinforeq scir;
	conninfo_t *cfo;

	memset(&scir, 0, sizeof(scir));
	scir.scir_cid = cid;
	if (ioctl(s, SIOCGCONNINFO, &scir) != 0)
		return (-1);
	if ((cfo = calloc(1, sizeof(*cfo))) == NULL)
		return (-1);
	if (scir.scir_src_len != 0 && (cfo->ci_src = calloc(1, scir.scir_src_len)) == NULL)
		goto fail;
	if (scir.scir_dst_len != 0 && (cfo->ci_dst = calloc(1, scir.scir_dst_len)) == NULL)
		goto fail;
	if (scir.scir_aux_len != 0 && (cfo->ci_aux_data = calloc(1, scir.scir_aux_len)) == NULL)
		goto fail;
	scir.scir_src = cfo->ci_src;
	scir.scir_dst = cfo->ci_dst;
	scir.scir_aux_data = cfo->ci_aux_data;
	if (ioctl(s, SIOCGCONNINFO, &scir) != 0)
		goto fail;
	cfo->ci_flags = scir.scir_flags;
	cfo->ci_ifindex = scir.scir_ifindex;
	cfo->ci_error = scir.scir_error;
	cfo->ci_aux_type = scir.scir_aux_type;
	*cfop = cfo;
	return (0);
fail:
	freeconninfo(cfo);
	return (-1);
}

#endif
