#!/bin/ksh -p
# SPDX-License-Identifier: BSD-2-Clause
#
# nd-zfs-tests: run groups of the OpenZFS test suite on NeoDarwin (P3-01
# checkpoint 2, docs/architecture/filesystems.md §7), in place of
# zfs-tests.sh and test-runner.py (Python isn't in the base). It keeps
# test-runner.py's semantics: per group, the pre script (setup) runs first
# and, if it doesn't pass, the group's tests are SKIP; then each test, then
# the post script (cleanup), whatever happened. A test's result is its exit
# status as logapi.shlib sets it: 0 PASS, 4 SKIP (log_unsupported), any
# other FAIL; a test still running at its timeout is sent SIGTERM, is
# KILLED, and the failsafe callback (zinject -c all) runs. A killed test's
# own cleanup (log_onexit) never runs, so the runner then ends what the
# test left: every process it started (test-runner.py signals the script
# alone, and zpool_create_024_pos's 128 background workers went on for
# ten minutes, KILLing the next tests), the pools that weren't there
# before it (unless destroying one hangs), and the files it added to
# FILEDIR. If zpool list then hangs, ZFS is wedged: the run prints where
# processes sleep and reports every test still to run as SKIP.
#
#   nd-zfs-tests [-l RUNLIST] [-t SECONDS] GROUP...
#
# RUNLIST (default $STF_SUITE/neodarwin.run, generated from the suite's
# common.run by kexts/zfs/tests/suite.sh) has one line per group:
#   DIR TIMEOUT PRE POST TEST...
# DIR relative to $STF_SUITE, PRE/POST "-" for none. GROUP is DIR's last
# component (zpool_create). -t caps every timeout (TCG is slow, and a hung
# test should cost minutes, not the suite's 600 s).
#
# The disks: DISKS (three whole disks for the tests), FILEDIR (an HFS+
# volume for file vdevs and temporary files) come from the environment, or
# nd-zfs-tests picks them: of the whole disks without slices, the one over
# 8 GB is formatted HFS+ and mounted at /private/var/tmp/zts for FILEDIR (the real path:
# mount(2) records it, and a dataset mounted under /var/tmp would not match
# its mountpoint property in the mount table), the next three
# are DISKS.
#
# Output, on stdout (the serial console in //kernel:sbsa_zfs_suite_test):
#   ZTS: tests/functional/cli_root/zpool_create/zpool_create_001_pos [PASS] 12s
# then, for anything but PASS, the test's last lines ("ZTS|  ..."), and at
# the end a block that repeats every result, which the host's ratchet reads:
#   ZTS-RESULTS-BEGIN / ZTS-R <path> <RESULT> / ZTS-RESULTS-END pass=N fail=N skip=N killed=N
# and "zts-done-<N>" where N is the number of results.

export STF_SUITE=${STF_SUITE:-/usr/share/zfs/zfs-tests}
export STF_TOOLS=${STF_TOOLS:-/usr/share/zfs/test-runner}
export STF_PATH=${STF_PATH:-$STF_SUITE/bin}
runlist=$STF_SUITE/neodarwin.run
cap=0
while getopts "l:t:" opt; do
	case $opt in
	l) runlist=$OPTARG ;;
	t) cap=$OPTARG ;;
	*) print -u2 "usage: nd-zfs-tests [-l RUNLIST] [-t SECONDS] GROUP..."; exit 2 ;;
	esac
done
shift $((OPTIND - 1))
(( $# > 0 )) || { print -u2 "nd-zfs-tests: no group given"; exit 2; }

# The suite's PATH: the helpers (STF_PATH, //kexts/zfs:zfs_test_commands),
# then the system's. zfs-tests.sh builds STF_PATH of links to a fixed set
# of commands instead; here a missing command fails where it is used.
export PATH=$STF_PATH:/sbin:/usr/sbin:/bin:/usr/bin
export STF_PATH=$PATH
export UNAME=$(uname)
export SHELL=/bin/ksh
export LC_ALL=C LANG=C
export KEEP=${KEEP:-}
export __ZFS_POOL_EXCLUDE=$KEEP

# dyld finds no shared cache on NeoDarwin, and the kernel reports it at
# every exec (vm_unix.c's shared_region_check_np trace): quiet it for the
# run, as it would interleave with the results on the console.
sysctl -w vm.shared_region_trace_level=0 >/dev/null 2>&1

# The disks.
if [[ -z "$DISKS" || -z "$FILEDIR" ]]; then
	typeset -a whole=()
	for d in /dev/disk+([0-9]); do
		[[ -e ${d}s1 ]] && continue
		whole+=(${d#/dev/})
	done
	if [[ -z "$FILEDIR" ]]; then
		# The largest disk: the only one with a block past 8 GB.
		f=
		typeset -a rest=()
		for d in "${whole[@]}"; do
			if [[ -z $f ]] && dd if=/dev/$d of=/dev/null bs=1048576 skip=8192 count=1 2>&1 |
			    grep -q '^1+0 records in'; then
				f=$d
			else
				rest+=($d)
			fi
		done
		[[ -n $f ]] || { print "nd-zfs-tests: no disk over 8 GB for FILEDIR"; exit 1; }
		whole=("${rest[@]}")
		mkdir -p /private/var/tmp/zts
		newfs_hfs -v ZTS /dev/$f >/dev/null && mount -t hfs /dev/$f /private/var/tmp/zts ||
		    { print "nd-zfs-tests: can't make FILEDIR on /dev/$f"; exit 1; }
		export FILEDIR=/private/var/tmp/zts
	fi
	[[ -z "$DISKS" ]] && DISKS="${whole[*]:0:3}"
fi
export DISKS FILEDIR
export TMPDIR=$FILEDIR
# The suite's configuration, exported to every script, as zfs-tests.sh
# sources it (TESTPOOL, TESTDIR under FILEDIR, ...).
. $STF_SUITE/include/default.cfg
logdir=/private/var/tmp/zts-logs
mkdir -p $logdir
print "nd-zfs-tests: DISKS='$DISKS' FILEDIR=$FILEDIR STF_SUITE=$STF_SUITE"

typeset -A result
typeset -a order
typeset -i npass=0 nfail=0 nskip=0 nkilled=0 wedged=0

# bounded SECONDS COMMAND...: run COMMAND, output discarded, for at most
# SECONDS; 124 if it was still running. timeout(1) would wait for a
# process asleep in the kernel, which SIGKILL doesn't end: this leaves it.
function bounded
{
	typeset -i to=$1 t0=$SECONDS; shift
	"$@" > /dev/null 2>&1 &
	typeset pid=$!
	while kill -0 $pid 2>/dev/null; do
		if (( SECONDS - t0 >= to )); then
			kill -KILL $pid 2>/dev/null
			return 124
		fi
		sleep 0.2
	done
	wait $pid 2>/dev/null
}

# descendants PID...: the processes below the PIDs, from ps's parent links.
function descendants
{
	ps -axo pid=,ppid= | awk -v roots=" $* " '
		{ par[$1] = $2 }
		END {
			for (c in par) {
				x = par[c]
				for (n = 0; n < 64 && x > 1; n++) {
					if (index(roots, " " x " ")) { print c; break }
					x = par[x]
				}
			}
		}'
}

# killtree PID...: stop the PIDs and everything below them (so nothing
# forks meanwhile), then kill them all.
function killtree
{
	typeset all="$*" more
	kill -STOP $all 2>/dev/null
	more=$(descendants $all)
	while [[ -n "$more" ]]; do
		kill -STOP $more 2>/dev/null
		all="$all $more"
		more=$(descendants $all | grep -vxF -f <(print -r -- "${all// /$'\n'}"))
	done
	kill -KILL $all 2>/dev/null
}

# destroy_pool POOL: zpool destroy -f, errors to leftovers.err.
function destroy_pool
{
	zpool destroy -f $1 2>>$logdir/leftovers.err
}

# leftovers BEFORE_POOLS BEFORE_FILES: after a killed test, destroy the pools
# and remove the FILEDIR entries that weren't there before it (what its
# cleanup would have removed). A pool whose destroy takes over two minutes
# (a frozen pool's unmount waits for a txg that never syncs) is left as it
# is, as is the rest after two such.
function leftovers
{
	typeset bpools=$1 bfiles=$2 p f
	typeset -a pools=() files=() stuck=()
	for p in $(zpool list -H -o name 2>/dev/null); do
		[[ $'\n'$bpools$'\n' == *$'\n'$p$'\n'* ]] || pools+=($p)
	done
	for f in $(ls -A $FILEDIR); do
		[[ $'\n'$bfiles$'\n' == *$'\n'$f$'\n'* ]] || files+=($f)
	done
	(( ${#pools[@]} + ${#files[@]} > 0 )) || return 0
	print "ZTS|  nd-zfs-tests: the killed test left ${#pools[@]} pools and ${#files[@]} files in FILEDIR: removing them"
	for p in "${pools[@]}"; do
		bounded 120 destroy_pool $p
		if (( $? == 124 )); then
			stuck+=($p)
			(( ${#stuck[@]} < 2 )) || break
		fi
	done
	sed 's/.*: //' $logdir/leftovers.err 2>/dev/null | sort | uniq -c | sed 's/^/ZTS|  nd-zfs-tests: zpool destroy: /'
	rm -f $logdir/leftovers.err
	(( ${#stuck[@]} == 0 )) || print "ZTS|  nd-zfs-tests: zpool destroy hangs on ${stuck[*]}: left as it is"
	(cd $FILEDIR && rm -rf -- "${files[@]}")
	return 0
}

# run PATH TIMEOUT: run the script PATH (without .ksh) with a timeout;
# sets $res and prints its line.
function run
{
	typeset path=$1 to=$2 script=$STF_SUITE/$1.ksh
	typeset log=$logdir/${path//\//_}.log
	typeset -i t0=$SECONDS killed=0 st
	if [[ ! -x $script ]]; then
		print "ZTS: $path [FAIL] 0s"; print "ZTS|  no executable $script"; res=FAIL; return
	fi
	# What there is before the test, for a killed test's leftovers.
	typeset bpools=$(zpool list -H -o name 2>/dev/null) bfiles=$(ls -A $FILEDIR)
	t0=$SECONDS
	(cd $logdir && exec $script) > $log 2>&1 &	# test-runner.py runs in its output directory
	typeset pid=$!
	while kill -0 $pid 2>/dev/null; do
		if (( SECONDS - t0 >= to )); then
			killed=1
			typeset kids=$(descendants $pid)
			kill -TERM $pid 2>/dev/null
			typeset -i w=0
			while kill -0 $pid 2>/dev/null && (( w < 10 )); do sleep 1; w+=1; done
			# The test and every process it started (they outlive
			# its shell, and a worker would go on forking).
			killtree $pid $kids
			break
		fi
		sleep 0.2
	done
	# A process asleep in the kernel (a suspended pool) outlives SIGKILL:
	# don't wait for it.
	if (( killed )) && kill -0 $pid 2>/dev/null; then st=137
	else wait $pid 2>/dev/null; st=$?
	fi
	if (( killed )); then res=KILLED
	elif (( st == 0 )); then res=PASS
	elif (( st == 4 )); then res=SKIP
	else res=FAIL
	fi
	typeset -i dt=SECONDS-t0
	print "ZTS: $path [$res] ${dt}s"
	if [[ $res != PASS ]]; then
		tail -n 25 $log | sed 's/^/ZTS|  /'
	fi
	if [[ $res == KILLED ]]; then
		bounded 60 $STF_SUITE/callbacks/zfs_failsafe.ksh
		# A test killed in the kernel can leave ZFS wedged (a zpool
		# command asleep holding the namespace lock, or a suspended
		# pool): then every later test would hang to its timeout too.
		# Show where processes sleep, and stop the run.
		bounded 60 zpool list
		st=$?
		if (( st != 124 )); then
			leftovers "$bpools" "$bfiles"
			bounded 60 zpool list
			st=$?
		fi
		if (( st == 124 )); then
			print "ZTS-WEDGED: zpool list hangs after $path was killed"
			ps -axo pid,stat,wchan,command | grep -v ' ps -axo' | sed 's/^/ZTS|  /'
			wedged=1
		fi
	fi
}

# record PATH RESULT
function record
{
	result[$1]=$2; order+=("$1")
	case $2 in
	PASS) npass+=1 ;; SKIP) nskip+=1 ;; KILLED) nkilled+=1 ;; *) nfail+=1 ;;
	esac
}

for group in "$@"; do
	line=$(grep -E "^[^ ]*/$group " $runlist)
	[[ -n "$line" ]] || { print "nd-zfs-tests: no group $group in $runlist"; exit 2; }
	set -- $line
	dir=$1 to=$2 pre=$3 post=$4; shift 4
	(( cap > 0 && to > cap )) && to=$cap
	cont=1
	if [[ $pre != - ]]; then
		if (( wedged )); then
			res=SKIP; print "ZTS: $dir/$pre [SKIP] 0s (wedged)"
		else
			run $dir/$pre $to
		fi
		record $dir/$pre $res
		[[ $res == PASS ]] || cont=0
	fi
	for t in "$@"; do
		if (( wedged )); then
			res=SKIP; print "ZTS: $dir/$t [SKIP] 0s (wedged)"
		elif (( cont )); then
			run $dir/$t $to
		else
			res=SKIP; print "ZTS: $dir/$t [SKIP] 0s (the group's $pre didn't pass)"
		fi
		record $dir/$t $res
	done
	if [[ $post != - ]]; then
		if (( wedged )); then
			res=SKIP; print "ZTS: $dir/$post [SKIP] 0s (wedged)"
		else
			run $dir/$post $to
		fi
		record $dir/$post $res
	fi
done

print "ZTS-RESULTS-BEGIN"
for p in "${order[@]}"; do print "ZTS-R $p ${result[$p]}"; done
print "ZTS-RESULTS-END pass=$npass fail=$nfail skip=$nskip killed=$nkilled"
print "zts-done-${#order[@]}"
