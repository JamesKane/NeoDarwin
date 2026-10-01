// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: expressibility: a compat header for Apple's C login(1), which includes it.
//
// Apple's private <SoftLinking/WeakLinking.h> (not published).
// WEAK_LINK_FORCE_IMPORT(sym) keeps a weak import of sym, so that a program
// can test whether its library was found. login_audit.c uses it for
// libEndpointSecuritySystem's login events (closed). NeoDarwin has no such
// library, and <EndpointSecuritySystem/ESSubmitSPI.h> (compat/) makes the
// symbols null, so there is nothing to import.
#ifndef ND_WEAKLINKING_H
#define ND_WEAKLINKING_H
#define WEAK_LINK_FORCE_IMPORT(sym)
#endif
