/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: a C header OpenZFS's C userland includes (gettext's <libintl.h>). */
/*
 * NeoDarwin: OpenZFS's userland is built without NLS. The base system has
 * no gettext, so messages are not translated: these are the identity.
 */
#ifndef _ND_LIBINTL_H
#define	_ND_LIBINTL_H
#define	gettext(msgid)			((char *)(msgid))
#define	dgettext(domain, msgid)		((char *)(msgid))
#define	textdomain(domain)		((char *)(domain))
#define	bindtextdomain(domain, dir)	((char *)(dir))
#endif
