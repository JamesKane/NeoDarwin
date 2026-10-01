#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# OpenSSH 10.0p2 from OpenSSH-354.0.3 (docs/base/session.md, "Loopback and
# sshd"): replays OpenSSH.xcodeproj's openbsd-compat, libssh and tool
# targets with base.xcconfig and openssh.xcconfig, and its configuration
# phases (make-config.zsh, install-launch-daemons.sh, the pam.d files).
#   build.sh OUT OPENSSH_SRC SYSROOT DEPROOT...
#   (DEPROOT: //base:root, //base:libcrypto_dylib, //base:libpam_dylib, //base:libresolv_dylib)
# OUT receives, where macOS has them:
#   usr/bin/{ssh,scp,sftp,ssh-add,ssh-agent,ssh-keygen,ssh-keyscan} and the
#     slogin link (slogin-symlinks.sh);
#   usr/sbin/sshd; usr/libexec/{sshd-session,sshd-auth,sftp-server};
#   private/etc/ssh: ssh_config and sshd_config with Apple's Include lines,
#     ssh_config.d/100-macos.conf, sshd_config.d/100-macos.conf (UsePAM yes),
#     crypto/{apple,fips}.conf, crypto.conf -> crypto/apple.conf, moduli;
#   private/etc/pam.d/sshd (patch 0002);
#   System/Library/LaunchDaemons/ssh.plist: com.openssh.sshd.plist as Apple
#     installs it for customers, Disabled (Remote Login off).
# The build doesn't run configure, as Apple's doesn't: openssh/config.h is
# the project's own, from configure-for-osx.sh. Patch 0001 turns off what
# needs closed code (GSSAPI and Kerberos, BSM audit, Seatbelt's
# sandbox_init) or a library the base doesn't have (zlib), and picks the
# rlimit sandbox for sshd-auth, the pre-authentication process.
# Apple's feature macros (openssh.xcconfig's AppleSshFeatures) are kept
# where their code needs only libSystem: clear_lv, display_var, membership,
# nohostauthproxy, tmpdir and basesystem. Left out, for closed code:
# keychain (Security.framework; keychain.m is not compiled), endpointsecurity
# (libEndpointSecuritySystem), managed_configuration
# (libManagedConfigurationFiles), nw_connection (Network.framework) and the
# two BSM audit fixes; and launchd (ssh-agent -l), which needs
# launch_activate_socket(), an XPC-era call launchd-842's liblaunch lacks.
# Not built: ssh-keysign (setuid, host-based authentication), the PKCS#11
# and security-key helpers (ssh-sk-helper needs libfido2),
# ssh-apple-pkcs11, sshd-fvunlock, remote-login-status and
# slapconfig-keygen (closed frameworks), and the regression tools.
# sshd-keygen-wrapper, the launchd job's program, is NeoDarwin's
# (base/sshd_keygen_wrapper): Apple's is Swift on Foundation.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
CRYPTO=""; PAM=""; RESOLV=""
for d in "${DEPS[@]}"; do
	[ -f "$d/usr/lib/libcrypto.46.dylib" ] && CRYPTO="$d"
	[ -f "$d/usr/lib/libpam.2.dylib" ] && PAM="$d"
	[ -f "$d/usr/lib/libresolv.9.dylib" ] && RESOLV="$d"
done
[ -n "$CRYPTO" ] || { echo "openssh: no DEPROOT holds usr/lib/libcrypto.46.dylib (pass //base:libcrypto_dylib)" >&2; exit 1; }
[ -n "$PAM" ] || { echo "openssh: no DEPROOT holds usr/lib/libpam.2.dylib (pass //base:libpam_dylib)" >&2; exit 1; }
[ -n "$RESOLV" ] || { echo "openssh: no DEPROOT holds usr/lib/libresolv.9.dylib (pass //base:libresolv_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S"

# openssh.xcconfig: GCC_PREPROCESSOR_DEFINITIONS (the kept AppleSshFeatures)
# and HEADER_SEARCH_PATHS (LibreSSL's headers, openssh/); base.xcconfig's
# GCC_NO_COMMON_BLOCKS and -Os. libpam's and libresolv's headers come from
# their build-only include directories. Warning flags are left out.
FEATURES=(-D__APPLE_CLEAR_LV__ -D__APPLE_DISPLAY_VAR__ -D__APPLE_MEMBERSHIP__ -D__APPLE_NOHOSTAUTHPROXY__
	-D__APPLE_TMPDIR__ -D__APPLE_BASESYSTEM__)
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fno-common "${FEATURES[@]}" \
	-I"$CRYPTO/usr/local/libressl/include" -I"$S/openssh" -I"$S/EndpointSecurity" -I"$PAM/usr/local/include" \
	-I"$RESOLV/usr/local/libresolv/include" $(cmd_sysroot_flags "$SYSROOT")
cc_objs() { local d="$B/obj/$1"; shift; compile "$d" "$B/cflags" "$@"; }

# libopenbsd-compat.a: the target's openbsd-compat/ sources. Its sources
# phase also lists eleven of libssh's (authfd.c, channels.c, ...); they are
# libssh's here only, so no object is in both archives.
COMPAT=(freezero recallocarray arc4random base64 basename bsd-getentropy bcrypt_pbkdf bindresvport blowfish
	bsd-asprintf bsd-closefrom bsd-cygwin_util bsd-getpeereid bsd-misc bsd-nextstep bsd-openpty arc4random_uniform
	bsd-poll bsd-pselect bsd-setres_id bsd-signal bsd-snprintf bsd-statvfs bsd-waitpid daemon dirname explicit_bzero
	fake-rfc2553 fmt_scaled fnmatch getcwd getgrouplist getopt_long getrrsetbyname-ldns getrrsetbyname glob inet_aton
	inet_ntoa inet_ntop kludge-fd_set libressl-api-compat md5 mktemp openssl-compat port-aix port-irix port-linux
	port-net port-prngd port-solaris port-uw readpassphrase reallocarray rresvport setenv setproctitle sha1 sha2
	sigact strlcat strlcpy strmode strnlen strptime strsep strtoll strtonum bsd-timegm strtoul strtoull
	timingsafe_bcmp vis xcrypt)
cc_objs compat $(printf 'openssh/openbsd-compat/%s.c\n' "${COMPAT[@]}")
libtool -static -no_warning_for_no_symbols -o "$B/libopenbsd-compat.a" "$B"/obj/compat/*.o
# libssh.a
LIBSSH=(platform-pledge platform-tracing addr addrmatch atomicio authfd authfile bitmap canohost chacha
	kexmlkem768x25519 channels cipher-aes cipher-chachapoly-libcrypto cipher-chachapoly cipher cleanup compat dh
	digest-openssl dispatch dns ed25519 entropy fatal gss-genr hash hmac hostfile kex kexc25519 kexdh kexecdh kexgen
	kexgex kexgexc kexgexs kexsntrup761x25519 krl log mac match misc moduli monitor_fdpass msg nchan packet
	platform-misc poly1305 readpass rijndael kex-names smult_curve25519_ref sntrup761 ssh-dss ssh-ecdsa-sk ssh-ecdsa
	ssh-ed25519-sk ssh-ed25519 ssh-pkcs11 ssh-rsa ssh-xmss sshbuf-getput-basic sshbuf-getput-crypto sshbuf-io
	sshbuf-misc sshbuf ssherr sshkey-xmss sshkey ssh_api ttymodes uidswap umac umac128 utf8 xmalloc xmss_commons
	xmss_fast xmss_hash xmss_hash_address xmss_wots)
cc_objs libssh dlopen_lv.c $(printf 'openssh/%s.c\n' "${LIBSSH[@]}")
libtool -static -no_warning_for_no_symbols -o "$B/libssh.a" "$B"/obj/libssh/*.o

# OTHER_LDFLAGS, less the frameworks and libraries for what's left out:
# -lbsm and -lz. -lresolv is libresolv-93 (getrrsetbyname's SSHFP lookups,
# VerifyHostKeyDNS, through res_9_query).
LINK=("$B/libssh.a" "$B/libopenbsd-compat.a" -L"$CRYPTO/usr/local/libressl/lib" -lcrypto -L"$PAM/usr/lib" -lpam
	-L"$RESOLV/usr/lib" -lresolv)
prog() {  # prog INSTALL_PATH SRC...
	local out="$OUT/$1"; shift
	tool "$B" "$ROOT" "$out" "$B/cflags" "$@" -- "${LINK[@]}"
}
o() { printf 'openssh/%s.c\n' "$@"; }
# sshd-session and sshd-auth's shared sources (sshd-session adds monitor.c,
# sshd-auth monitor's client side and the sandboxes).
SESSION_COMMON=(auth2-methods audit-bsm audit auth2-pubkeyfile auth-bsdauth auth-krb5 openbsd-compat/arc4random_uniform
	openbsd-compat/bsd-timegm auth-options auth-pam auth-passwd auth-rhosts auth-shadow auth-sia auth auth2-chall
	auth2-gss auth2-hostbased auth2-kbdint auth2-none auth2-passwd auth2-pubkey auth2 groupaccess gss-serv-krb5
	gss-serv kexgexs loginrec monitor_wrap platform progressmeter openbsd-compat/bsd-getentropy servconf serverloop
	session sftp-common sftp-realpath sftp-server ssh-sk-client sshlogin sshpty)

prog usr/bin/ssh $(o clientloop mux platform-pledge readconf ssh-sk-client ssh sshconnect sshconnect2 sshtty)
prog usr/sbin/sshd apple-what.c $(o ssh-sk-client canohost authfd utf8 compat fatal dns auth2-methods groupaccess \
	srclimit sshpty servconf sshd platform-listen)
prog usr/libexec/sshd-session $(o platform-listen sshd-session monitor "${SESSION_COMMON[@]}") apple-what.c \
	EndpointSecurity/submit-ess-event.c
prog usr/libexec/sshd-auth $(o sandbox-null sandbox-darwin sandbox-rlimit sshd-auth "${SESSION_COMMON[@]}") apple-what.c \
	EndpointSecurity/submit-ess-event.c
prog usr/bin/ssh-add $(o ssh-add ssh-sk-client)
prog usr/bin/ssh-keygen $(o ssh-keygen ssh-sk-client sshsig)
prog usr/bin/ssh-keyscan $(o ssh-keyscan ssh-sk-client)
prog usr/bin/ssh-agent $(o ssh-agent ssh-pkcs11-client ssh-sk-client)
prog usr/bin/scp $(o progressmeter scp sftp-client sftp-common sftp-glob)
prog usr/libexec/sftp-server $(o sftp-common sftp-realpath sftp-server-main sftp-server)
prog usr/bin/sftp $(o progressmeter sftp-usergroup sftp-client sftp-common sftp-glob sftp)
ln -s ssh "$OUT/usr/bin/slogin"

# make-config.zsh: ssh_config and sshd_config with the Include lines, and
# the mtree's files.
E="$OUT/private/etc/ssh"; mkdir -p "$E/crypto" "$E/ssh_config.d" "$E/sshd_config.d"
include() {  # include ssh|sshd: Apple's comment and Include line
	printf '\n# NOTE: The following Include directive is not part of the default\n'
	printf '# sshd_config shipped with OpenSSH. Options set in the included\n'
	printf '# configuration files generally override those that follow. The defaults\n'
	printf '# only apply to options that have not been explicitly set. Options that\n'
	printf '# appear multiple times keep the first value set, unless they are a\n'
	printf '# multivalue option such as HostKey or IdentityFile.\n'
	printf 'Include /etc/ssh/%s_config.d/*\n' "$1"
}
include ssh > "$B/include.ssh"; include sshd > "$B/include.sshd"
sed -e "/defaults at the end/r $B/include.ssh" openssh/ssh_config > "$E/ssh_config"
sed -e "/^# default value/r $B/include.sshd" openssh/sshd_config > "$E/sshd_config"
grep -q '^Include /etc/ssh/ssh_config.d/\*' "$E/ssh_config" && grep -q '^Include /etc/ssh/sshd_config.d/\*' "$E/sshd_config" ||
	{ echo "openssh: the Include lines weren't added" >&2; exit 1; }
install -m 0644 conf/apple.conf conf/fips.conf "$E/crypto/"
ln -s crypto/apple.conf "$E/crypto.conf"
install -m 0644 openssh/moduli "$E/moduli"
install -m 0644 conf/100-macos-ssh.conf "$E/ssh_config.d/100-macos.conf"
install -m 0644 conf/100-macos.conf "$E/sshd_config.d/100-macos.conf"
chmod 0644 "$E/ssh_config" "$E/sshd_config"
# The pam.d phase: sshd (patched; sshd-basesystem is the macOS Recovery
# system's and isn't installed).
mkdir -p "$OUT/private/etc/pam.d"
install -m 0444 pam.d/sshd "$OUT/private/etc/pam.d/sshd"
# install-launch-daemons.sh: the customer plist, unmodified (Disabled).
mkdir -p "$OUT/System/Library/LaunchDaemons"
install -m 0644 com.openssh.sshd.plist "$OUT/System/Library/LaunchDaemons/ssh.plist"
