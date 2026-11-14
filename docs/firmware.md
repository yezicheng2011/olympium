# Firmware and machines this image can boot

The desktop userspace is native to the ISO's architecture. A boot under QEMU's software emulator is evidence for that QEMU machine only.

## x86_64

The ISO is a hybrid image:

- BIOS boot through GRUB El Torito
- UEFI boot through GRUB for x86_64

The boot test in this environment uses QEMU's `pc` machine, the standard VGA device (bochs DRM), and KVM when `/dev/kvm` exists because the host is x86_64. That is a boot of the x86_64 image on x86_64. It is not a survey of every PC.

Kernel: the Ubuntu 24.04 generic kernel recorded in `sbom.tsv` (6.8.0 series at the time of the first build).

## aarch64

When the arm64 ISO is produced, GRUB is the arm64 EFI binary from the same package set. The only firmware this environment can boot it with is QEMU's `virt` machine plus AAVMF (EDK2). That boot is emulated. It does not show that the image boots a Raspberry Pi, a generic SBSA server, or Apple Silicon. Apple Silicon needs its own firmware path, which this image does not include.

## Writable disk

The live system keeps the squashfs read-only and puts changes on a tmpfs overlay. That overlay is discarded on power-off. `install-to-disk` can copy the running system to a disk the operator names twice. It selects a BIOS partition table or a UEFI ESP from the firmware it is actually running on.
