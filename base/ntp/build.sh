#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# sntp from Apple's ntp-139 (ntp 4.2.8p10; docs/base/pf-ntp.md): replays
# ntp.xcodeproj's "ntp" (libntp.a) and "sntp" targets with their Release
# settings.
#   build.sh OUT NTP_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/bin/sntp (the target's INSTALL_PATH) and private/etc/ntp.conf.
#
# ntp-139 is the last drop Apple published (macOS 10.15); later releases set
# the clock with timed, which is closed. By the reuse order
# (docs/repository.md §3.1, an earlier Apple drop) NeoDarwin takes its
# sntp, the SNTP client macOS's ntpd-wrapper ran at boot, and not ntpd, which
# links Apple's closed CrashReporter and SMC libraries and Seatbelt
# (ntp.xcodeproj; sntp links libntp alone). The "ntp" target builds
# libntp.a: its sources less systime_s.c (systime.c built for ntpdsim, the
# simulator; both define the clock functions, and sntp wants systime.c's).
# The shipped include/config.h and sntp/config.h are configure's answers
# for macOS, which hold for NeoDarwin's libSystem. patches/: 0001 os_log in
# place of os_trace, and libisc's MD5 in place of CommonCrypto's
# (NEODARWIN_NO_COMMONCRYPTO); 0002 host:port server names.
# The job and its configuration reader are NeoDarwin's
# (//base/sntp_wrapper); /etc/ntp.conf is pool.ntp.org, as FreeBSD's.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; N="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
N="$(stage_src "$N" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$N"

# The project's settings: HAVE_CONFIG_H with the shipped include/config.h,
# gnu99, the libopts and ISC search paths; sntp adds HAVE_VSNPRINTF, its own
# directory (sntp/config.h) and libevent's.
base=("${TARGET_FLAGS[@]}" -Os -std=gnu99 -fno-common -DHAVE_CONFIG_H -DNEODARWIN_NO_COMMONCRYPTO '-D__RC__=\"139\"' $(cmd_sysroot_flags "$SYSROOT"))
inc=(-Isntp/libopts -Ilib/isc/include -Ilib/isc/pthreads/include -Ilib/isc/unix/include)
write_rsp "$B/libntp.rsp" "${base[@]}" -Iinclude "${inc[@]}"
write_rsp "$B/sntp.rsp" "${base[@]}" -DHAVE_VSNPRINTF -Isntp -Iinclude -Isntp/libevent/include "${inc[@]}" \
	-Isntp/libevent/compat

LIBNTP=(lib/isc/{assertions,backtrace-emptytbl,backtrace,buffer,bufferlist,error,fsaccess,hmacmd5,hmacsha,inet_aton,inet_pton,lfsr,lib,log,md5,netaddr,netscope,ondestroy,parseint,random,region,result,serial,sha1,sha2,strtoul,tsmemcmp}.c
	lib/isc/nls/msgcat.c lib/isc/pthreads/mutex.c
	lib/isc/unix/{dir,errno2result,file,fsaccess,interfaceiter,ipv6,keyboard,net,os,stdio,stdtime,strerror,syslog,time}.c
	libntp/{a_md5encrypt,adjtime,atoint,atolfp,atouint,audio,authkeys,authreadkeys,authusekey,bsd_strerror,buftvtots,caljulian,caltontp,calyearstart,clocktime,clocktypes,decodenetnum,dofptoa,dolfptoa,emalloc,findconfig,getopt,hextoint,hextolfp,humandate,icom,iosignal,is_ip_address,lib_strbuf,machines,mktime,modetoa,mstolfp,msyslog,netof,ntp_calendar,ntp_crypto_rnd,ntp_intres,ntp_libopts,ntp_lineedit,ntp_random,ntp_rfc2553,ntp_worker,numtoa,numtohost,octtoint,prettydate,recvbuff,refidsmear,refnumtoa,snprintf,socket,socktoa,socktohost,ssl_init,statestr,strdup,strl_obsd,syssignal,systime,timetoa,timevalops,uglydate,vint64ops,work_dispatch,work_fork,work_thread,ymd2yd}.c)
compile "$B/obj/libntp" "$B/libntp.rsp" "${LIBNTP[@]}"
xcrun libtool -static -no_warning_for_no_symbols -o "$B/libntp.a" "$B"/obj/libntp/*.o

SNTP=(sntp/{crypto,kod_management,log,main,networking,sntp-opts,sntp,utilities,version}.c sntp/libopts/libopts.c
	sntp/libevent/{buffer,bufferevent_filter,bufferevent_pair,bufferevent_ratelim,bufferevent_sock,bufferevent,devpoll,evdns,event_tagging,event,evmap,evport,evrpc,evthread_pthread,evthread,evutil_rand,evutil_time,evutil,http,kqueue,listener,log,poll,select,signal,strlcpy}.c)
tool "$B" "$ROOT" "$OUT/usr/bin/sntp" "$B/sntp.rsp" "${SNTP[@]}" -- "$B/libntp.a"
mkdir -p "$OUT/private/etc"
install -m 0644 "$PROJ/ntp.conf" "$OUT/private/etc/ntp.conf"
