<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Driver strategy

## 1. Model

NeoDarwin keeps IOKit as the driver model because the kernel, its power management, and its device tree registry are built around it. Two execution tiers:

| Tier | Where | Use for | Basis in tree |
|---|---|---|---|
| **kext** | kernel | boot-critical and latency-critical: interrupt controller, PSCI, framebuffer console, storage (NVMe, AHCI, virtio-blk), filesystems (OpenZFS, HFS+, FUSE) | classic IOKit (`iokit/`) |
| **dext** | userland, DriverKit-style | USB devices, network cards, GPIO/I2C/SPI peripherals, audio | kernel side is open (`iokit/Kernel/IOUserServer.cpp`, `iokit/DriverKit/*.iig`); NeoDarwin writes the userland runtime (`NDDriverKit`) |

Dexts are the default for anything new after the base storage and display path exists (roadmap P3 onwards). They crash without taking the kernel down, they are packages like any other, and they are written in Swift: hosted Swift for the control plane, allocation-free Swift on the data path (language policy T1/T2). Kexts keep C++ class shells because IOKit requires them; FreeBSD-derived driver logic stays in C on the portability ground; new kext logic that is not an IOKit class may move to Embedded Swift once `kext_swift` is proven.

## 2. Families to build or adopt

| Family | Source | Status/plan | FreeBSD reference (BSD-2, may derive) | Linux (GPL, reference only) |
|---|---|---|---|---|
| ACPI platform (`ndacpi`) | new + ACPICA | P1-M5 | `sys/dev/acpica` for bus glue patterns | `drivers/acpi` |
| PCIe (`IOPCIFamily`) | Apple open source | adopt; ECAM/MSI glue from `ndacpi` | `sys/dev/pci` | `drivers/pci` |
| Storage stack (`IOStorageFamily`) | Apple open source | adopt | — | — |
| virtio (bus, blk, net, console, gpu, input, 9p) | new | P1-M5 (blk, console), P3 (net, gpu, input) | `sys/dev/virtio/*` (derive) | `drivers/virtio` |
| NVMe | new | P1-M5 | `sys/dev/nvme` (derive) | `drivers/nvme` |
| AHCI/SATA | new | P3 | `sys/dev/ahci` (derive) | `drivers/ata` |
| USB host (XHCI) + USB core | new (`NDUSBFamily`; modern IOUSBHostFamily is closed) | P3 | `sys/dev/usb/controller/xhci*.c`, `sys/dev/usb/usb_*.c` (derive) | `drivers/usb/host/xhci*` |
| USB HID, mass storage | new dexts | P3/P4 | `sys/dev/usb/input`, `sys/dev/usb/storage` | `drivers/hid` |
| Ethernet: virtio-net, e1000/igb, Realtek `re` | new dexts | P3 | `sys/dev/e1000`, `sys/dev/re` (derive) | `drivers/net/ethernet/{intel,realtek}` |
| Wi-Fi | deferred | — | `sys/dev/rtwn`, `sys/dev/iwlwifi` (firmware licensing per device) | — |
| SD/MMC (SBCs) | new | P3 | `sys/dev/sdhci`, `sys/dev/mmc` (derive) | `drivers/mmc` |
| GPIO / I2C / SPI via ACPI `_DSD` | new dexts | P4 | `sys/dev/gpio`, `sys/dev/iicbus`, `sys/dev/spibus` | `drivers/{gpio,i2c,spi}` |
| UART (PL011, 16550, DesignWare) | pexpert console + IOSerialFamily (Apple open source) | P1 (console), P3 (tty) | `sys/dev/uart` | `drivers/tty/serial` |
| Framebuffer (`IOGraphics` + `ndfb`) | Apple open source `IOGraphics` + new GOP/simple framebuffer driver | P4-01 | — | `drivers/gpu/drm/tiny/simpledrm.c` (reference) |
| GPU (Mali via Panthor-class, AMD, Intel) | long horizon | P7-01 | — | Mesa userland (MIT) may be adopted; kernel side clean-room |
| Audio (`NDAudioFamily`: virtio-snd, USB Audio Class, HD Audio) | new dexts | P7-08 | `sys/dev/sound` (derive) | `sound/` |
| Interrupt controller GICv3 / PSCI | new (in kernel) | P1-M2 | `sys/arm64/arm64/gic_v3.c`, `psci.c` (derive) | `drivers/irqchip/irq-gic-v3.c` |

## 3. Data-oriented driver rules

1. Descriptor rings are struct-of-arrays owned by the driver; completions are delivered in batches (`IOReturn complete(const Completion *items, size_t count)`), never one callback per I/O.
2. No allocation in the interrupt or completion path; pools are sized at start from the device's declared queue depth.
3. DMA buffers are pre-mapped `IODMACommand` pools; cache maintenance is explicit and per-batch on non-coherent boards.
4. Device state that a human or agent may need (link state, queue depths, error counters, power state) is published as IOKit registry properties and `sysctl` nodes by the family, readable with `ioreg` and `sysctl` and as `--libxo json`, not through ad-hoc `ioctl`s.
5. Interfaces are IOKit classes; data layouts are C structs with stated ABI versions shared with dexts through `.iig`.

## 4. Provenance policy

- **FreeBSD (BSD-2/3):** code may be ported and adapted; keep copyright headers and add a `PROVENANCE.md` per driver listing upstream path and revision.
- **Linux (GPL-2.0):** never copied. Engineers (or agents) may read it to learn register semantics, quirks and sequences, and must record what was consulted in `PROVENANCE.md` under "Referenced documentation" with file paths and the behaviour learned, so the clean-room boundary is auditable.
- **Vendor datasheets and specs** (NVMe, XHCI, virtio, SBSA, GIC, PCIe) are the primary sources; the references above are secondary.
- Firmware blobs are packaged separately with their own licences and never committed to the main repository.

## 5. Testing

Every family has: a QEMU model test (virtio, NVMe, XHCI, e1000, AHCI, SDHCI all exist in QEMU), a fault-injection harness (dexts can be killed and must recover), and a hardware-in-the-loop entry for the CD8180 board. Drivers publish counters as registry properties; tests assert on them rather than on logs.
