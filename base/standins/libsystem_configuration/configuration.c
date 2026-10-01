// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: a stand-in with the C interface of a closed Apple library.
//
// Stand-in for libsystem_configuration.dylib, the client library of configd's
// DNS configuration and network-state (nwi) services. Apple publishes it in
// configd (libSystemConfiguration/), but NeoDarwin doesn't build configd yet.
// libSystem's fork handlers call _libSC_info_fork_prepare/parent/child(),
// which in Apple's library hold and reset the client's connections to
// configd across fork. No NeoDarwin library uses the client yet, so there
// is no connection and the three do nothing. The stand-in gives way to
// configd's library when NeoDarwin builds configd (docs/base/libsystem.md).
//
// The DNS configuration (configd's dnsinfo.h): libresolv's dns.c asks for
// it before reading /etc/resolv.conf and /etc/resolver/ itself. Without
// configd there is none: dns_configuration_copy() answers NULL, as
// Apple's does when configd isn't running, and the resolver reads the
// files (docs/kernel/network.md, "Name resolution").

// Declared by Libsystem's init.c, which calls them; configd's
// libSystemConfiguration_client.h declares them for its own build.
void _libSC_info_fork_prepare(void);
void _libSC_info_fork_parent(void);
void _libSC_info_fork_child(void);

void
_libSC_info_fork_prepare(void)
{
}

void
_libSC_info_fork_parent(void)
{
}

void
_libSC_info_fork_child(void)
{
}

// dnsinfo.h's dns_config_t is a structure this library returns by pointer
// and never builds here: an incomplete type stands in for it.
typedef struct nd_dns_config dns_config_t;

const char *dns_configuration_notify_key(void);
dns_config_t *dns_configuration_copy(void);
void dns_configuration_free(dns_config_t *config);

// configd's key (dnsinfo_copy.c), posted
// when the configuration changes; with no configd it never is.
const char *
dns_configuration_notify_key(void)
{
	return "com.apple.system.SystemConfiguration.dns_configuration";
}

dns_config_t *
dns_configuration_copy(void)
{
	return (dns_config_t *)0;
}

void
dns_configuration_free(dns_config_t *config)
{
	(void)config;
}
