<!-- SPDX-License-Identifier: BSD-2-Clause -->
# QEMU with a Secure world: TF-A + EDK2 on `virt,secure=on`

**P1-05.** The default QEMU tests boot `virt` without EL3, using the EDK2 that ships with QEMU. That machine has one GIC security state (`GICD_CTLR.DS = 1`), so Group 0 interrupts reach Non-secure EL1 as FIQs. SBSA boards run TF-A at EL3, and their GIC has two security states (`DS = 0`): Group 0 is Secure, and Non-secure writes to its group and enable bits are ignored. This configuration reproduces that on QEMU, which is why `arm64-sbsa-bringup.md` §5.2 asks for it.

## Configuration

| | `--machine virt` (default) | `--machine virt-secure` |
|---|---|---|
| QEMU machine | `virt,gic-version=3` | `virt,secure=on,gic-version=3` (no `virtualization=on`) |
| Firmware | QEMU's `edk2-aarch64-code.fd` in pflash0 | `-bios` secure flash: TF-A BL1 at 0, FIP (BL2, BL31, BL33 = EDK2 ArmVirtQemuKernel) at 256 KiB |
| neoboot and the kernel run at | Non-secure EL1 | Non-secure EL1 (BL31 enters BL33 at EL1, since there is no EL2) |
| GIC | `has-security-extensions = false` | `has-security-extensions = true` |
| `GICD_CTLR` read by neoboot (Non-secure) | `0x52`: DS, ARE, EnableGrp1 | `0x12`: ARE_NS, EnableGrp1A; DS reads 0 |
| FADT PSCI | HVC | **absent** (see below); neoboot uses SMC |
| Minimum RAM | 512M | 1G: TF-A loads BL33 at `0x60000000` |

The GIC's state comes from QEMU's monitor (`info qtree`) and its `gicv3_dist_*` trace. TF-A's Secure write to `GICD_CTLR` is `0x34` (ARE_S, ARE_NS, EnableGrp1S), so EnableGrp0 stays clear. After that, BL31 sets every SGI and PPI to Group 1 Non-secure.

**PSCI absent.** When firmware owns EL3, QEMU turns its own PSCI emulation off, and its ACPI FADT then advertises no PSCI at all, although TF-A's BL31 implements PSCI over SMC. BL2 adds a PSCI node only to the device tree, which neoboot doesn't read. A real board's FADT says SMC. neoboot therefore takes SMC when the FADT reports no PSCI but the CPU implements EL3 (`ID_AA64PFR0_EL1.EL3`, readable from EL1), and logs `neoboot: PSCI conduit: SMC: the FADT reports none, but the CPU implements EL3` (P1-06, `dt-abi.md`). `//kernel:sbsa_secure_smp_boot_test` starts four CPUs through TF-A this way.

**CPU.** QEMU's `cortex-a76`, `neoverse-n2`, `cortex-a57` and `max` all boot. TF-A logs "workaround ... missing" warnings for the CPUs it has errata code for, because QEMU doesn't implement the IMPDEF registers.

## Firmware: built from pinned source

`//third_party/qemu_firmware:virt_secure_flash` builds the image with `tools/efi/build_secure_firmware.sh`, from these archives (pinned in `MODULE.bazel` by SHA-256):

| Repository | Upstream | Licence |
|---|---|---|
| `@arm_tf_a` | TF-A `v2.15.0` | BSD-3-Clause |
| `@tianocore_edk2` | EDK2 `edk2-stable202608` | BSD-2-Clause-Patent |
| `@edk2_openssl` | openssl `8cf17aae` (the edk2 submodule commit) | Apache-2.0 |
| `@edk2_brotli` | brotli `e230f474` (submodule) | MIT |
| `@edk2_libfdt` | pylibfdt `cfff8054` (submodule) | BSD-2-Clause / GPL-2.0 |

ArmVirtQemuKernel is the position-independent ArmVirt build that TF-A's QEMU port documents as BL33. QEMU's bundled EDK2 can't take that role: it runs in place from flash0, which is Secure-only when `secure=on`. EDK2 names four more submodules in its `.dec` include paths (libspdm, mbedtls, mipisyst, and the TPM reference code). This platform compiles none of them, so the script creates their include directories empty.

**Toolchain.** The compiler is the swift.org toolchain's clang 21 and `ld.lld`, the same toolchain neoboot uses (`toolchains/README.md`): TF-A builds with `CC=clang`, and EDK2 builds with its `CLANGDWARF` toolchain. The host needs `brew install make gnu-sed acpica openssl@3`: GNU make 4.3 or later and GNU sed for TF-A, `iasl` for EDK2's ASL, and OpenSSL headers for `fiptool`. The build takes about a minute.

**Reproducible.** The image is the same bit for bit on every build, and a manual build gives the same image as a Bazel one: SHA-256 `e9e0f4b9578ef4482fb1debf7c23491c1367d42b26a72e8648c6b99e8d3b6e92` with the pins above and swift-6.3.2's clang. The script does four things to get there:
- It sets `SOURCE_DATE_EPOCH`, and TF-A's `BUILD_MESSAGE_TIMESTAMP`, to fixed values.
- It builds at the fixed path `/tmp/neodarwin-qemu-secure-fw`, because EDK2's PE debug entries record the absolute `.dll` path. The action is therefore unsandboxed, and only one build can run at a time.
- It supplies EDK2's stack-cookie tables from a fixed seed. EDK2 otherwise draws them from `secrets` on every build.
- It sets `PYTHONHASHSEED=0`, because EDK2 picks each module's cookie with Python's `hash()`.

This is test firmware, so a known cookie costs nothing. `ND_SECURE_FW_KEEP=DIR` keeps the build trees, to compare two builds.

## Running

```sh
bazel test //boot/neoboot:neoboot_secure_qemu_test   # TF-A → EDK2 → neoboot; DS=0, timer on Group 1
bazel test //kernel:sbsa_secure_boot_test             # manual: the sbsa_boot_test boot under DS=0
bazel test //kernel:sbsa_secure_smp_boot_test         # manual: four CPUs started by TF-A (PSCI over SMC)
```

`tools/efi/qemu_efi_test.sh --machine virt-secure --firmware FILE` selects the configuration for any other test. To run it by hand:

```sh
qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu neoverse-n2 -m 2G -nographic \
  -bios bazel-bin/third_party/qemu_firmware/qemu_virt_secure_flash.bin \
  -drive format=raw,file=fat:rw:ESPDIR -serial stdio
```

TF-A's `NOTICE:` lines (BL1, BL2, BL31) come first on the same PL011, then EDK2, then neoboot.

## What DS=0 does to the kernel (measured)

- **Group 0 timer** (neoboot without DS detection, kernel before patch 0016): the timer never ticks as a FIQ. `pe_init_fiq` writes `GICR_IGROUPR0 = 0x81ffffff` to move PPI 27 to Group 0, and the GIC ignores the write, so PPI 27 stays Group 1 Non-secure and pending. The QEMU trace shows the HPPI as `irq 27 group 2`. Boot gets as far as `no external trust caches found`. Then `NeoDarwinGICv3::initWithNode` sets `ICC_IGRPEN1_EL1 = 1`, the pending timer arrives as an **IRQ**, and `sleh_irq → PE_handle_ext_interrupt` dereferences NULL, because nothing handles IRQs yet (`Kernel data abort`, FAR 0).
- **Group 1 timer** (neoboot reads DS = 0 and sets `timer-group = 1`; patch 0016): `//kernel:sbsa_secure_boot_test` passes. It logs `NeoDarwinGICv3: timer PPI 27 on Group 1 (IRQ); 5 timer interrupts taken as IRQs so far`, reaches BSD, mounts the HFS+ root, and PID 1 passes its checks.

## Alternatives considered

- **`sbsa-ref`** (TF-A `qemu_sbsa` + EDK2 SbsaQemu from edk2-platforms). It is the SBSA reference machine, but the firmware needs edk2-platforms on top of EDK2. It would also test more than the GIC's security state: RAM at 1 TiB (`0x100_0000_0000`), GICD at `0x40060000`, GICR at `0x40080000` (a 64 MiB range), an ITS at `0x44081000`, the PL011 at `0x60000000`, and four `neoverse-n2` CPUs by default. The ACPI tables come from SbsaQemu's own ASL rather than from QEMU's generator, and they advertise PSCI over SMC. It's a good second matrix entry for A7, but neoboot and the kernel haven't been tried on its memory map.
- **Prebuilt images.** Linaro's sbsa-ref CI keeps dated builds (artifacts.codelinaro.org `linaro-419-sbsa-ref`, the latest from 2024-11), with no source pin that can be checked. Distribution packages ship ArmVirtQemu rather than the Kernel variant, and no TF-A for `PLAT=qemu`. Building from source takes a minute and is reproducible, so it wins.
