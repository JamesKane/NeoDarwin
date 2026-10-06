#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# FreeBSD's test framework (P4-21 checkpoint 7, docs/architecture/freebsd-parity.md
# §2.1, docs/base/kyua.md) from the source drop at 050683bb8e13, built as
# FreeBSD's Makefiles build it:
#   lib/atf/libatf-c, lib/atf/libatf-c++  ATF 0.26 (PRIVATELIBs on FreeBSD;
#                                         static archives here, build-only);
#   libexec/atf/atf-sh, atf-check         /usr/libexec/atf-sh, /usr/libexec/atf-check,
#                                         /usr/share/atf/libatf-sh.subr;
#   lib/liblutok                          lutok (an INTERNALLIB: static);
#   lib/libsqlite3                        SQLite 3.53.3 (a PRIVATELIB: static);
#   lib/liblua                            Lua, from @puc_rio_lua (5.4.9, base/vis's
#                                         pin; FreeBSD's contrib/lua is 5.4.8);
#   usr.bin/kyua                          kyua 0.13 with its pages, /etc/kyua/kyua.conf,
#                                         /usr/share/kyua/{misc,store}.
#   build.sh OUT FREEBSD_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# @puc_rio_lua is found next to FREEBSD_SRC in external/ (the rule's data).
# OUT also receives, build-only (usr/local, which base_root drops), the
# static libatf-c.a and libatf-c++.a with their headers, for the test
# programs of /usr/tests (base/freebsd_tests).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
LUA="$(ls -d "$(dirname "$F")"/*puc_rio_lua 2>/dev/null | head -1)/src"
[ -f "$LUA/lua.h" ] || { echo "kyua: no @puc_rio_lua next to $F" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
F="$(stage_src "$F" "$B/src" "$PROJ/patches")"   # patches/ (none) applied
cd "$F"
A="$F/contrib/atf"; K="$F/contrib/kyua"; LT="$F/contrib/lutok"; SQ="$F/contrib/sqlite3"

SYS=($(cmd_sysroot_flags "$SYSROOT"))
CXX=(-nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1")
# Warnings change no interface and are left out (FreeBSD's WARNS levels).
CF=("${TARGET_FLAGS[@]}" -O2 -w)
archive() { xcrun libtool -static -no_warning_for_no_symbols -o "$1" "$2"/*.o; }

# --- Lua: src/Makefile's CORE_O and LIB_O (lib/liblua's SRCS) with
# LUA_USE_POSIX, as base/vis builds it (no LUA_USE_DLOPEN: static).
write_rsp "$B/lua.rsp" "${CF[@]}" "${SYS[@]}" -std=gnu99 -DLUA_USE_POSIX
LUA_SRCS=(); for s in lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate \
	lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib \
	lstrlib ltablib lutf8lib linit; do LUA_SRCS+=("$LUA/$s.c"); done
compile "$B/obj/lua" "$B/lua.rsp" "${LUA_SRCS[@]}"
archive "$B/liblua.a" "$B/obj/lua"

# --- lutok (lib/liblutok): C++17, FreeBSD's default CXXSTD.
write_rsp "$B/lutok.rsp" "${CF[@]}" -std=c++17 "${CXX[@]}" "${SYS[@]}" -I"$LT/include" -I"$LUA"
compile "$B/obj/lutok" "$B/lutok.rsp" "$LT"/{c_gate,debug,exceptions,operations,stack_cleaner,state}.cpp
archive "$B/liblutok.a" "$B/obj/lutok"

# --- SQLite (lib/libsqlite3): the Makefile's CFLAGS, less
# HAVE_POSIX_FALLOCATE (Darwin has no posix_fallocate) and with
# SQLITE_ENABLE_LOCKING_STYLE=0, the plain POSIX advisory locking FreeBSD's
# build gets (sqlite3.c turns Apple's AFP and proxy locking on for __APPLE__).
write_rsp "$B/sqlite.rsp" "${CF[@]}" "${SYS[@]}" -I"$SQ" -DUSE_PREAD=1 -DSTDC_HEADERS=1 -DHAVE_SYS_TYPES_H=1 \
	-DHAVE_SYS_STAT_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_MEMORY_H=1 -DHAVE_STRINGS_H=1 \
	-DHAVE_INTTYPES_H=1 -DHAVE_STDINT_H=1 -DHAVE_UNISTD_H=1 -DHAVE_DLFCN_H=1 -DHAVE_USLEEP=1 \
	-DHAVE_LOCALTIME_R=1 -DHAVE_GMTIME_R=1 -DHAVE_DECL_STRERROR_R=1 -DHAVE_STRERROR_R=1 -D_REENTRANT=1 \
	-DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_LOCKING_STYLE=0
compile "$B/obj/sqlite" "$B/sqlite.rsp" "$SQ/sqlite3.c"
archive "$B/libsqlite3.a" "$B/obj/sqlite"

# --- libatf-c and libatf-c++ (lib/atf): Makefile.inc's -DHAVE_CONFIG_H and
# libatf-c's ATF_BUILD_* (atf-c/build.c's defaults); C++ is gnu++20.
# (Response files take quotes: a string macro is written -DNAME=\"value\".)
ATF_DEFS=(-DHAVE_CONFIG_H '-DATF_BUILD_CC=\"cc\"' '-DATF_BUILD_CFLAGS=\"-Wall\"' '-DATF_BUILD_CPP=\"cpp\"'
	'-DATF_BUILD_CPPFLAGS=\"\"' '-DATF_BUILD_CXX=\"c++\"' '-DATF_BUILD_CXXFLAGS=\"-Wall\"')
write_rsp "$B/atfc.rsp" "${CF[@]}" "${SYS[@]}" -std=gnu99 -I"$A" "${ATF_DEFS[@]}"
compile "$B/obj/atfc" "$B/atfc.rsp" "$A"/atf-c/{build,check,error,tc,tp,utils}.c \
	"$A"/atf-c/detail/{dynstr,env,fs,list,map,process,sanity,text,user,tp_main}.c
archive "$B/libatf-c.a" "$B/obj/atfc"
write_rsp "$B/atfcxx.rsp" "${CF[@]}" -std=gnu++20 "${CXX[@]}" "${SYS[@]}" -I"$A" -DHAVE_CONFIG_H
compile "$B/obj/atfcxx" "$B/atfcxx.rsp" "$A"/atf-c++/{build,check,tests,utils}.cpp \
	"$A"/atf-c++/detail/{application,env,exceptions,fs,process,text}.cpp
archive "$B/libatf-c++.a" "$B/obj/atfcxx"

# --- atf-sh and atf-check (libexec/atf): BINDIR /usr/libexec, the path
# every FreeBSD shell test's #! line names (atf.test.mk).
write_rsp "$B/atfsh.rsp" "${CF[@]}" -std=gnu++20 "${CXX[@]}" "${SYS[@]}" -I"$A" -DHAVE_CONFIG_H \
	'-DATF_LIBEXECDIR=\"/usr/libexec\"' '-DATF_PKGDATADIR=\"/usr/share/atf\"' '-DATF_SHELL=\"/bin/sh\"'
tool "$B" "$ROOT" "$OUT/usr/libexec/atf-sh" "$B/atfsh.rsp" "$A/atf-sh/atf-sh.cpp" -- "$B/libatf-c++.a" "$B/libatf-c.a" -lc++
tool "$B" "$ROOT" "$OUT/usr/libexec/atf-check" "$B/atfsh.rsp" "$A/atf-sh/atf-check.cpp" -- "$B/libatf-c++.a" "$B/libatf-c.a" -lc++
install -d "$OUT/usr/share/atf"
install -m 0444 "$A/atf-sh/libatf-sh.subr" "$OUT/usr/share/atf/"

# --- kyua (usr.bin/kyua): the Makefile's SRCS and CFLAGS, with its
# config.h for Darwin: LAST_SIGNO is 31 (xnu's NSIG is 32; configure probes
# it; FreeBSD's 128 makes kyua's signal setup fail), and the physical memory
# is hw.memsize (FreeBSD's hw.usermem). os/freebsd's kmods requirement
# checker and prepare handler are registered only #ifdef __FreeBSD__
# (os/freebsd/main.cpp), and need kld(2): they aren't built. MK_JAIL=no:
# execenv_jail_stub.cpp, as on a FreeBSD built without jails.
mkdir -p "$B/kyua/utils"
sed -e 's/^#define LAST_SIGNO 128$/#define LAST_SIGNO 31/' \
	-e 's/^#define MEMORY_QUERY_SYSCTL_MIB "hw.usermem"$/#define MEMORY_QUERY_SYSCTL_MIB "hw.memsize"/' \
	usr.bin/kyua/config.h > "$B/kyua/config.h"
grep -q '^#define LAST_SIGNO 31$' "$B/kyua/config.h" && grep -q 'hw.memsize' "$B/kyua/config.h" ||
	{ echo "kyua: config.h didn't take the Darwin values" >&2; exit 1; }
cp usr.bin/kyua/utils/defs.hpp "$B/kyua/utils/"
KYUA_VERSION=0.13
write_rsp "$B/kyua.rsp" "${CF[@]}" -std=c++17 "${CXX[@]}" -I"$B/kyua" -I"$K" -I"$LT/include" -I"$SQ" "${SYS[@]}" \
	-DHAVE_CONFIG_H '-DGDB=\"/usr/local/bin/gdb\"' '-DKYUA_ARCHITECTURE=\"aarch64\"' '-DKYUA_CONFDIR=\"/etc/kyua\"' \
	'-DKYUA_DOCDIR=\"/usr/share/doc/kyua\"' '-DKYUA_MISCDIR=\"/usr/share/kyua/misc\"' '-DKYUA_PLATFORM=\"arm64\"' \
	'-DKYUA_STOREDIR=\"/usr/share/kyua/store\"' '-DPACKAGE=\"kyua\"' '-DPACKAGE_NAME=\"Kyua\"' \
	"-DPACKAGE_VERSION=\\\"$KYUA_VERSION\\\"" "-DVERSION=\\\"$KYUA_VERSION\\\""
KS=(main.cpp)
for s in datetime env memory passwd sanity stacktrace stream units \
	cmdline/{base_command,exceptions,globals,options,parser,ui,ui_mock} \
	config/{exceptions,keys,lua_module,nodes,parser,tree} format/{exceptions,formatter} \
	fs/{auto_cleaners,directory,exceptions,lua_module,operations,path} logging/operations \
	process/{child,deadline_killer,exceptions,executor,fdstream,isolation,operations,status,system,systembuf} \
	signals/{exceptions,interrupts,misc,programmer,timer} \
	sqlite/{c_gate,database,exceptions,statement,transaction} \
	text/{exceptions,operations,regex,table,templates}; do KS+=("utils/$s.cpp"); done
KS+=(model/{context,exceptions,metadata,test_case,test_program,test_result}.cpp)
KS+=(engine/{atf,atf_list,atf_result,config,exceptions,filters,kyuafile,plain,requirements,scanner,tap,tap_parser,scheduler}.cpp
	engine/execenv/{execenv,execenv_host}.cpp engine/prepare/{prepare,prepare_all}.cpp)
KS+=(os/freebsd/{execenv_jail_manager,main,execenv_jail_stub}.cpp)
KS+=(store/{dbtypes,exceptions,layout,metadata,migrate,read_backend,read_transaction,write_backend,write_transaction}.cpp)
KS+=(drivers/{debug_test,list_tests,report_junit,run_tests,scan_results}.cpp)
KS+=(cli/{cmd_about,cmd_config,cmd_db_exec,cmd_db_migrate,cmd_debug,cmd_help,cmd_list,cmd_prepare,cmd_report,cmd_report_html,cmd_report_junit,cmd_test,common,config,main}.cpp)
KSRCS=(); for s in "${KS[@]}"; do KSRCS+=("$K/$s"); done
tool "$B" "$ROOT" "$OUT/usr/bin/kyua" "$B/kyua.rsp" "${KSRCS[@]}" -- "$B/liblutok.a" "$B/liblua.a" "$B/libsqlite3.a" -lc++

# kyua's files: CONFS (kyua.conf-default as /etc/kyua/kyua.conf), DOCS,
# EXAMPLES, MISC and STORE. The configuration's unprivileged_user is
# nobody: FreeBSD's names its `tests` account, which NeoDarwin's
# /etc/master.passwd doesn't have yet (accounts are P4-22), and kyua refuses
# a configuration naming a user that doesn't exist.
install -d "$OUT/private/etc/kyua" "$OUT/usr/share/doc/kyua" "$OUT/usr/share/examples/kyua" \
	"$OUT/usr/share/kyua/misc" "$OUT/usr/share/kyua/store"
sed "s/^unprivileged_user = 'tests'$/unprivileged_user = 'nobody'/" usr.bin/kyua/kyua.conf-default > "$B/kyua.conf"
grep -q "^unprivileged_user = 'nobody'$" "$B/kyua.conf" || { echo "kyua: kyua.conf-default changed" >&2; exit 1; }
install -m 0644 "$B/kyua.conf" "$OUT/private/etc/kyua/kyua.conf"
install -m 0444 "$K"/{AUTHORS,CONTRIBUTORS,LICENSE} "$OUT/usr/share/doc/kyua/"
install -m 0444 "$K"/examples/{Kyuafile.top,kyua.conf} "$OUT/usr/share/examples/kyua/"
install -m 0444 "$K"/misc/{context.html,index.html,report.css,test_result.html} "$OUT/usr/share/kyua/misc/"
install -m 0444 "$K"/store/{migrate_v1_v2.sql,migrate_v2_v3.sql,schema_v3.sql} "$OUT/usr/share/kyua/store/"

# The pages: kyua's from doc/*.in through doc/manbuild.sh, as the Makefile
# makes them; ATF's pages (atf-check.1 is installed by base_library's
# man_pages; atf-sh.1 is installed here, since atf-sh.3 would satisfy it).
man="$OUT/usr/share/man"; install -d "$man/man1" "$man/man3" "$man/man5" "$man/man7"
for m in kyua-about.1 kyua-config.1 kyua-db-exec.1 kyua-db-migrate.1 kyua-debug.1 kyua-help.1 kyua-list.1 \
	kyua-report-html.1 kyua-report-junit.1 kyua-report.1 kyua-test.1 kyua.1 kyua.conf.5 kyuafile.5; do
	(cd "$K/doc" && sh ./manbuild.sh -v "CONFDIR=/etc/kyua" -v "DOCDIR=/usr/share/doc/kyua" \
		-v "EGDIR=/usr/share/examples/kyua" -v "MISCDIR=/usr/share/kyua/misc" -v "PACKAGE=kyua" \
		-v "STOREDIR=/usr/share/kyua/store" -v "TESTSDIR=/usr/tests" -v "VERSION=$KYUA_VERSION" \
		"$m.in" "$B/$m")
	install -m 0444 "$B/$m" "$man/man${m##*.}/$m"
done
install -m 0444 "$A/atf-c/atf-c.3" "$A/atf-c++/atf-c++.3" "$A/atf-sh/atf-sh.3" "$man/man3/"
install -m 0444 "$A/doc/atf-test-program.1" "$A/atf-sh/atf-sh.1" "$man/man1/"
install -m 0444 "$A/doc/atf-test-case.7" "$man/man7/"

# Build-only: the ATF libraries and headers for the test programs.
L="$OUT/usr/local"; install -d "$L/lib" "$L/include/atf-c" "$L/include/atf-c++"
install -m 0444 "$B/libatf-c.a" "$B/libatf-c++.a" "$L/lib/"
install -m 0444 "$A/atf-c.h" "$L/include/"; install -m 0444 "$A/atf-c++.hpp" "$L/include/"
install -m 0444 "$A"/atf-c/{build,check,defs,error,error_fwd,macros,tc,tp,utils}.h "$L/include/atf-c/"
install -m 0444 "$A"/atf-c++/{build,check,macros,tests,utils}.hpp "$L/include/atf-c++/"
