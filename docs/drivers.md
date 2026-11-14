# Drivers and firmware in the Olympium live image

Both the amd64 and arm64 images install the Ubuntu 24.04 generic kernel
modules for that architecture, the extra module package for the same
kernel version, Mesa's open graphics drivers, and the redistributable
`linux-firmware` collection. Firmware copyright files stay under
`/usr/share/doc/linux-firmware*` exactly as the packages ship them.
This tree does not relicense those files.

The packed image writes the package names and versions it actually
installed to `/usr/share/olympium/driver-manifest.txt`. A copy of that
file from the build is `build/driver-manifest-<arch>.txt`.

amd64 image, kernel 6.8.0-149-generic:

| Package | Version |
| --- | --- |
| linux-image-generic | 6.8.0-149.149 |
| linux-modules-6.8.0-149-generic | 6.8.0-149.149 |
| linux-modules-extra-6.8.0-149-generic | 6.8.0-149.149 |
| linux-modules-iwlwifi-6.8.0-149-generic | 6.8.0-149.149 |
| linux-modules-ipu6-6.8.0-149-generic | 6.8.0-149.149 |
| linux-firmware and the split firmware packages it depends on | 20240318.git3b128b60-0ubuntu3.1 (amd-graphics is 0ubuntu3.5) |
| intel-microcode | 3.20260210.0ubuntu0.24.04.1 |
| amd64-microcode | 3.20251202.1ubuntu0.24.04.1 |
| firmware-sof-signed | 2023.12.1-1ubuntu1.11 |
| libgl1-mesa-dri, mesa-vulkan-drivers, mesa-va-drivers, mesa-libgallium | 25.2.8-0ubuntu0.24.04.4 |
| wireless-regdb | 2026.05.30-0ubuntu1~24.04.1 |
| alsa-ucm-conf | 1.2.10-1ubuntu5.15 |
| pciutils | 1:3.10.0-2build1 |
| usbutils | 1:017-3build1 |
| iproute2 | 6.1.0-1ubuntu6.4 |

## Bundled

- Kernel modules from `linux-modules-<version>` and
  `linux-modules-extra-<version>`: NVMe, AHCI, USB storage, xHCI,
  HID and touchpad, Intel and AMD GPUs, common Ethernet (including
  e1000 and r8169), Wi-Fi and Bluetooth (including iwlwifi, ath9k,
  ath10k, brcmfmac, mt76), HDA and other ALSA modules, and the QEMU
  bochs DRM module.
- `virtio_net` and `virtio_blk` are built into this kernel. `virtio-gpu`
  is a module.
- On amd64 only: `intel-microcode`, `amd64-microcode`, and, when the
  archive still splits them out, `linux-modules-iwlwifi` and
  `linux-modules-ipu6` for that kernel.
- `linux-firmware` (and the split firmware packages it pulls): Intel
  i915, AMD amdgpu, common Wi-Fi, Bluetooth, Ethernet, and audio blobs
  whose upstream license allows Ubuntu to redistribute them.
- `firmware-sof-signed` for Intel audio DSP firmware.
- Mesa: `libgl1-mesa-dri`, `mesa-vulkan-drivers`, `mesa-va-drivers`.
- `wireless-regdb`, `alsa-ucm-conf`, `pciutils`, `usbutils`, `iproute2`.

## GPUs

Kernel 6.8.0-149. The module is in the image when this kernel builds it. Firmware files are the Ubuntu `linux-firmware` set (package 20240318.git3b128b60). A file on disk is not a test of that card. QEMU did not attach a real AMD, Intel, or NVIDIA GPU.

Intel, amd64: `i915.ko` and `xe.ko`. Mesa iris, crocus, ANV (`intel_icd.json`), and HasVK. 124 i915 firmware files, including Tiger Lake, Alder Lake, Meteor Lake, and DG2 (Arc Alchemist). Eight xe firmware files: Battlemage, Lunar Lake, and Panther Lake blobs. Whether 6.8's xe driver binds those newest chips was not tested. `intel-media-va-driver` is the open VAAPI driver and is installed on amd64 when the archive publishes it. `intel-media-va-driver-non-free` is multiverse and is not bundled.

Intel, arm64: this generic kernel has `xe.ko` and the same firmware files, plus iris and ANV userspace. It does not ship `i915.ko`. Integrated Intel graphics on an arm64 machine are not covered by that module.

AMD, both arches: `amdgpu.ko` and `radeon.ko`. Mesa radeonsi, r600, r300, and RADV (`radeon_icd.json`). amdgpu firmware includes GCN names (Bonaire, Hawaii, Tonga, Fiji, Polaris, Vega), RDNA names (Navi 10/12/14, Sienna Cichlid, Navy Flounder), and the newer IP blobs `gc_10_3`, `gc_11_0` (RDNA3), and `gc_12_0_0` (RDNA4). The radeon firmware set still has Southern Islands and Sea Islands names (Tahiti, Pitcairn, Verde, Oland, Bonaire, Hawaii, Kabini, Kaveri). Cards older than that, and any GPU this 6.8 amdgpu does not know, are out of support even if a blob happens to be present.

NVIDIA, both arches, bundled: `nouveau.ko`, Mesa's nouveau Gallium driver, NVK (`nouveau_icd.json`), and the redistributable firmware under `/lib/firmware/nvidia` (Kepler through Ada and Blackwell names the package ships: gk, gm, gp, gv, tu, ga, ad, gb, gh). That is a basic display stack. It is not the proprietary driver, and no NVIDIA card was attached in QEMU.

NVIDIA, not bundled. `driver-install` is the only path, and only after a device is present, the network works, restricted is enabled, and you type YES:

| Ask for | Package | What this archive actually provides |
| --- | --- | --- |
| `--install nvidia` or `nvidia:580` | nvidia-driver-580 580.178.04 | Prebuilt `linux-modules-nvidia-580-6.8.0-149-generic`. Widest current branch: Maxwell, Pascal, Volta, and newer cards the 580 series still lists. |
| `--install nvidia:580-open` | nvidia-driver-580-open | Open kernel module. Turing and newer. |
| `--install nvidia:595` | nvidia-driver-595 595.99.02 | Prebuilt for 6.8.0-149. Newer branch. Maxwell, Pascal, and Volta are outside it. |
| `--install nvidia:470` | nvidia-driver-470 470.256.02 | Kepler only. No prebuilt module for 6.8.0-149. DKMS needs `linux-headers` and a compiler and can fail. The script says so. |

`nvidia-driver-535` in noble-updates is a transitional package that depends on nvidia-driver-580. It is not a separate 535 driver. Fermi (GeForce 400/500) and anything older, including the 390 branch, have no package here. The script refuses those names. After a successful install the script runs `modprobe nvidia` and tells you to reboot; the live desktop stays on nouveau until that reboot. These proprietary packages are amd64. On arm64, apt failing to find them is the expected outcome, and nouveau stays.

Mesa also ships `asahi_dri.so` because the Ubuntu package builds it. That file does not make this image boot an Apple Silicon Mac.

## Not bundled

These stay out of the ISO. `driver-install` can fetch them later, and
only then:

- NVIDIA proprietary driver (`nvidia-driver-*` from the archive).
  The open nouveau module and the redistributable NVIDIA firmware
  files inside `linux-firmware` are already present. The proprietary
  kernel module is not.
- Broadcom STA (`broadcom-sta-dkms`). Cards that work with the
  in-tree `brcmfmac` driver use the bundled firmware. STA is the
  separate proprietary wl driver.

`driver-install --detect` prints PCI and USB devices and says whether
either package applies. `driver-install --install nvidia` or
`--install broadcom` refuses to continue when the matching device is
absent, when apt cannot see the package, when `archive.ubuntu.com` is
unreachable, or when the operator does not type YES. A YES install
still has to obey that package's own license.

## What was actually checked

On the amd64 image, the squashfs contains `e1000.ko.zst`, `nvme.ko.zst`, Mesa `iris_dri.so` and `radeonsi_dri.so`, `driver-install`, and the `linux-firmware` and `firmware-sof-signed` copyright files. A QEMU `pc` boot with KVM loaded e1000 (`e1000 0000:00:03.0`), virtio-net (`virtio_net virtio0`), and NVMe (`nvme nvme0: pci function`). The desktop came up on the bochs VGA device. `driver-install --detect` inside that root listed the host's virtio devices and refused `--install nvidia` because no NVIDIA device was present.

The arm64 kernel in this build has no `i915` module. The Intel GPU firmware files are still on disk because `linux-firmware` is architecture-independent. That is not a test of an Intel GPU.

## What a QEMU boot does not prove

An amd64 boot here is QEMU `pc` with std VGA (bochs), e1000, NVMe, and
USB tablet. An arm64 boot, when one is run, is QEMU `virt` with AAVMF.
Neither boot is a Raspberry Pi, a generic ARM server, an Apple Silicon
Mac, or a laptop with a real Wi-Fi card. A module file in the image
means the file is present. It does not mean that device was tested.
