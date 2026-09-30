#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Build the TrustZone firmware for QEMU virt,secure=on (docs/kernel/qemu-secure.md):
# TF-A (BL1, BL2, BL31 at EL3, PLAT=qemu, GICv3) with EDK2's ArmVirtQemuKernel
# as BL33, packed as bl1.bin at 0 and fip.bin at 256 KiB of the secure flash.
#   build_secure_firmware.sh TOOLCHAIN TFA_SRC EDK2_SRC OPENSSL_SRC BROTLI_SRC LIBFDT_SRC OUT
# With --sbsa, the firmware for QEMU sbsa-ref (docs/kernel/qemu-sbsa-ref.md):
# TF-A PLAT=qemu_sbsa (BL1, and a FIP of BL2 and BL31) and EDK2's SbsaQemu from
# edk2-platforms, which runs in place from the Non-secure flash as BL33.
#   build_secure_firmware.sh --sbsa EDK2_PLATFORMS_SRC [--tfa-patch FILE]... TOOLCHAIN ... LIBFDT_SRC FLASH0 FLASH1
# Each --tfa-patch (an upstream fix, -p1) is applied to the TF-A copy first.
# FLASH0 (Secure: BL1 and the FIP) and FLASH1 (SbsaQemu and its variable
# store) are padded to QEMU's 256 MiB, as sparse files.
# TOOLCHAIN is the swift.org toolchain (clang, ld.lld, llvm-ar, llvm-objcopy)
# that also builds neoboot. The source trees are pristine upstream archives;
# the three submodules are the ones EDK2 compiles for this platform.
# Host tools: GNU make >= 4.3 and GNU sed (TF-A), iasl (EDK2's ASL), python3
# (EDK2's build), OpenSSL 3 headers (TF-A's fiptool), the host cc.
#   brew install make gnu-sed acpica openssl@3
set -euo pipefail
# Physical paths: Bazel passes external/<repo>, a symbolic link.
abs() { (cd "$1" && pwd -P); }
platforms=""; tfa_patches=()
if [ "${1:-}" = --sbsa ]; then platforms="$(abs "$2")"; shift 2; fi
while [ "${1:-}" = --tfa-patch ]; do
	case "$2" in /*) tfa_patches+=("$2") ;; *) tfa_patches+=("$PWD/$2") ;; esac; shift 2
done
tc="$(abs "$1")"; tfa="$(abs "$2")"; edk2="$(abs "$3")"; openssl_src="$(abs "$4")"
brotli="$(abs "$5")"; libfdt="$(abs "$6")"; out="$7"; out1="${8:-}"
case "$out" in /*) ;; *) out="$PWD/$out" ;; esac
case "$out1" in /*|"") ;; *) out1="$PWD/$out1" ;; esac
[ -z "$platforms" ] || [ -n "$out1" ] || { echo "--sbsa needs FLASH0 and FLASH1"; exit 1; }
tcbin="$tc/usr/bin"
[ -x "$tcbin/clang" ] && [ -x "$tcbin/ld.lld" ] || { echo "no clang/ld.lld in $tcbin"; exit 1; }

# Bazel's PATH is minimal; GNU make and sed go first as `make` and `sed`.
# EDK2's images record the absolute path of each module's .dll in their debug
# entry, so the build runs at a fixed path to be reproducible. A build that
# is still running owns it; one that was killed left it behind.
# Each platform has its own path, so the two can build at once.
work=/tmp/neodarwin-qemu-secure-fw
[ -z "$platforms" ] || work=/tmp/neodarwin-qemu-sbsa-fw
if [ -d "$work" ]; then
	owner="$(cat "$work/pid" 2>/dev/null || true)"
	if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then echo "$work: build $owner is running"; exit 1; fi
	chmod -R u+w "$work"; rm -rf "$work"
fi
mkdir "$work"; echo $$ > "$work/pid"
tools="$work/bin"; mkdir "$tools"
# ND_SECURE_FW_KEEP=DIR also keeps the build logs, even from a failed build.
trap '[ -z "${ND_SECURE_FW_KEEP:-}" ] || { mkdir -p "$ND_SECURE_FW_KEEP"; cp "$work"/*.log "$ND_SECURE_FW_KEEP/" 2>/dev/null; }
	chmod -R u+w "$work" 2>/dev/null; rm -rf "$work"' EXIT
trap 'exit 1' INT TERM
find_tool() {
	for p in "$@"; do [ -x "$p" ] && { echo "$p"; return; }; done
	command -v "$1" 2>/dev/null || true
}
gmake="$(find_tool gmake /opt/homebrew/bin/gmake /usr/local/bin/gmake)"
gsed="$(find_tool gsed /opt/homebrew/bin/gsed /usr/local/bin/gsed)"
iasl="$(find_tool iasl /opt/homebrew/bin/iasl /usr/local/bin/iasl)"
python="$(find_tool python3 /opt/homebrew/bin/python3 /usr/local/bin/python3 /usr/bin/python3)"
ossl=""
for d in "${ND_OPENSSL_DIR:-}" /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3 /usr; do
	[ -n "$d" ] && [ -f "$d/include/openssl/sha.h" ] && { ossl="$d"; break; }
done
[ -n "$gmake" ] && [ -n "$gsed" ] && [ -n "$iasl" ] && [ -n "$python" ] && [ -n "$ossl" ] || {
	echo "missing host tools (make=$gmake sed=$gsed iasl=$iasl python3=$python openssl=$ossl): brew install make gnu-sed acpica openssl@3"
	exit 1
}
ln -s "$gmake" "$tools/make"; ln -s "$gsed" "$tools/sed"; ln -s "$iasl" "$tools/iasl"; ln -s "$python" "$tools/python3"
export PATH="$tools:/usr/bin:/bin:/usr/sbin:/sbin"
# Fixed dates, so the image is the same bit for bit on every build.
export SOURCE_DATE_EPOCH=1767225600 # 2026-01-01T00:00:00Z
export ZERO_AR_DATE=1

# EDK2: copy the tree (the build writes into it), drop the submodules in,
# and create the include directories of submodules this platform never
# compiles (the .dec files name them and the parser requires them to exist).
# The sources are Bazel's repository cache: copies follow links (cp -RL), no
# copy may hold one (a write through it would reach the cache), and
# EmulatorPkg, whose one link dangles, isn't copied.
copy_tree() {
	mkdir -p "$2"
	for f in "$1"/* "$1"/.[!.]*; do
		[ -e "$f" ] || continue
		case "${f##*/}" in EmulatorPkg) continue ;; esac
		cp -RL "$f" "$2/"
	done
	[ -z "$(find "$2" -type l | head -1)" ] || { echo "$2: symbolic link in the copy"; exit 1; }
}
e="$work/edk2"; copy_tree "$edk2" "$e"
sub() { copy_tree "$1" "$e/$2"; }
sub "$openssl_src" CryptoPkg/Library/OpensslLib/openssl
sub "$brotli" BaseTools/Source/C/BrotliCompress/brotli
sub "$brotli" MdeModulePkg/Library/BrotliCustomDecompressLib/brotli
sub "$libfdt" MdePkg/Library/BaseFdtLib/libfdt
chmod -R u+w "$e"
mkdir -p "$e/SecurityPkg/DeviceSecurity/SpdmLib/libspdm/include" \
	"$e/CryptoPkg/Library/MbedTlsLib/mbedtls/include/mbedtls" \
	"$e/CryptoPkg/Library/MbedTlsLib/mbedtls/library" \
	"$e/MdePkg/Library/MipiSysTLib/mipisyst/library/include"
make -C "$e/BaseTools/Source/C" -j8 > "$work/basetools.log" 2>&1 ||
	{ tail -40 "$work/basetools.log"; exit 1; }
# edk2_build DSC NAME: build an EDK2 platform in $e (PACKAGES_PATH, if set,
# adds edk2-platforms).
edk2_build() (
	dsc="$1"; name="$2"; cd "$e"
	export WORKSPACE="$e" EDK_TOOLS_PATH="$e/BaseTools" PYTHON_COMMAND=python3
	export CLANGDWARF_BIN="$tcbin/"
	set +u; set --; . ./edksetup.sh > /dev/null; set -u
	# EDK2 draws each build's stack-cookie table from `secrets` and indexes it
	# with Python's per-process hash(); a fixed table and hash seed make the
	# cookies, and so the image, the same on every build. (Test firmware: a
	# known cookie is no loss here.)
	export PYTHONHASHSEED=0
	quiet=-q; [ -z "${ND_SECURE_FW_KEEP:-}" ] || quiet=-v
	mkdir -p "Build/$name/RELEASE_CLANGDWARF"
	python3 -c '
import hashlib, json, sys
for bits in (32, 64):
    vals = [int.from_bytes(hashlib.sha256(b"neodarwin-%d-%d" % (bits, i)).digest()[:bits // 8], "little") for i in range(100)]
    json.dump(vals, open(sys.argv[1] + "/StackCookieValues%d.json" % bits, "w"))
' "Build/$name/RELEASE_CLANGDWARF"
	build -a AARCH64 -t CLANGDWARF -b RELEASE -p "$dsc" -n 8 "$quiet" \
		> "$work/edk2.log" 2>&1 || { grep -B20 -m1 -i "error" "$work/edk2.log" | tail -40; exit 1; }
)
tfa_make() {
	make -C "$work/tfa" -j8 \
		CC="$tcbin/clang" LD="$tcbin/ld.lld" AR="$tcbin/llvm-ar" OC="$tcbin/llvm-objcopy" OD="$tcbin/llvm-objdump" \
		HOSTCC=/usr/bin/cc OPENSSL_DIR="$ossl" \
		BUILD_MESSAGE_TIMESTAMP='"(NeoDarwin pinned build)"' \
		"$@" all fip > "$work/tfa.log" 2>&1 || { tail -40 "$work/tfa.log"; exit 1; }
}
copy_tree "$tfa" "$work/tfa"; chmod -R u+w "$work/tfa"
for p in ${tfa_patches[@]+"${tfa_patches[@]}"}; do
	/usr/bin/patch -s -p1 -d "$work/tfa" --no-backup-if-mismatch < "$p" || { echo "$p: does not apply"; exit 1; }
done

if [ -z "$platforms" ]; then
	edk2_build ArmVirtPkg/ArmVirtQemuKernel.dsc ArmVirtQemuKernel-AArch64
	bl33="$e/Build/ArmVirtQemuKernel-AArch64/RELEASE_CLANGDWARF/FV/QEMU_EFI.fd"
	# TF-A: PLAT=qemu with the GICv3 driver (the default is GICv2).
	tfa_make PLAT=qemu QEMU_USE_GIC_DRIVER=QEMU_GICV3 BL33="$bl33"
	rel="$work/tfa/build/qemu/release"
	rm -f "$out"
	dd if="$rel/bl1.bin" of="$out" bs=4096 conv=notrunc status=none
	dd if="$rel/fip.bin" of="$out" bs=4096 seek=64 conv=notrunc status=none
else
	# TF-A first: SbsaQemu's flash description (SbsaQemu.fdf) places
	# Platform/Qemu/Sbsa/{bl1,fip}.bin, which edk2-non-osi would otherwise
	# supply prebuilt, in FLASH0. BL31 enters BL33 in place, at FLASH1's base.
	tfa_make PLAT=qemu_sbsa
	rel="$work/tfa/build/qemu_sbsa/release"
	nonosi="$work/non-osi"; mkdir -p "$nonosi/Platform/Qemu/Sbsa"
	cp "$rel/bl1.bin" "$rel/fip.bin" "$nonosi/Platform/Qemu/Sbsa/"
	# Not $work/edk2-platforms: EDK2 takes a path that starts with the
	# string $WORKSPACE ($work/edk2) as inside it.
	copy_tree "$platforms" "$work/platforms"; chmod -R u+w "$work/platforms"
	export PACKAGES_PATH="$e:$work/platforms:$nonosi"
	edk2_build Platform/Qemu/SbsaQemu/SbsaQemu.dsc SbsaQemu
	fv="$e/Build/SbsaQemu/RELEASE_CLANGDWARF/FV"
	rm -f "$out" "$out1"
	cp "$fv/SBSA_FLASH0.fd" "$out"; cp "$fv/SBSA_FLASH1.fd" "$out1"; chmod u+w "$out" "$out1"
	# QEMU requires each pflash image to be the whole 256 MiB device.
	perl -e 'for (@ARGV) { open(my $f, "+<", $_) or die "$_: $!"; truncate($f, 256 << 20) or die }' "$out" "$out1"
fi
# ND_SECURE_FW_KEEP=DIR keeps the build trees for comparing two builds.
if [ -n "${ND_SECURE_FW_KEEP:-}" ]; then mkdir -p "$ND_SECURE_FW_KEEP"; cp -R "$e/Build" "$rel" "$ND_SECURE_FW_KEEP/"; fi
