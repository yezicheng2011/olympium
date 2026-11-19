# Platforms and hardware: what is true today

This page answers a list of requirements as they stand. It says what was run, and it does not claim what was not.

## Programs from other systems

"Near-native performance and full compatibility" for OpenHarmony, Android, macOS, Windows and Linux cannot be confirmed, and no system can honestly confirm it. This is where each one stands.

| Programs for | How they run here | State |
| --- | --- | --- |
| Linux | Natively. `.deb` and `.rpm` are unpacked per user, confined. | Works. Native speed, because it is native. |
| Windows | Wine 9.0, 64-bit and 32-bit x86. | Works for what Wine runs. Same CPU instructions, so CPU speed is native; graphics depend on the driver. Not every Windows program runs under Wine. No x86 programs on arm64. |
| macOS | Darling, when the image is built with it. `.dmg`, `.pkg` and `.app` install. | Command-line Mach-O programs ran, including on a booted image. No graphical Mac application has been run. Darling says its own graphical support is in development. |
| Android | Nothing. | Not started. It needs a container runtime (Waydroid is the usual one) and a kernel with binder, and an Android system image. |
| OpenHarmony | Nothing. | Not started. There is no established compatibility layer for OpenHarmony programs on a desktop Linux system to build on. |

Guest system images (Android and the like) are neither in the image nor downloaded at first launch, because nothing here can run them yet. When a runtime exists, first launch is where the download would be offered.

## Accounts

First launch asks for an account and for the administrator (root) password, and does not finish until both are set. Settings, Users & Passwords, changes either one and asks for the current password first. Passwords go to the system's `chpasswd` on its standard input. `make -C desktop test-accounts` runs the real `useradd` and `chpasswd` in a throwaway root: the first-launch window, filled in the way a boot test fills it, makes the account and sets the root password, right passwords verify, wrong ones do not, nothing is written in clear, and a change takes effect while a short one is refused. It was also run on a booted image (QEMU without KVM, the install ISO built from this tree): the welcome window appeared over the desktop 9.7 s into the session, filling it created `dana` (uid 1001, full name, home folder 0750) with yescrypt hashes for `dana` and `root`, both showing as usable in `passwd -S`, and neither password was in the compositor log. Settings, Users & Passwords, opened in that session and showed both accounts. Changing a password through the window was not clicked through on the booted image; the same code is what `test-accounts` runs.

On a live boot nothing persists, so the window comes back on every boot. Boot options a person picks (such as the contrast entry) still show it; boot-test flags that open or import something do not.

The desktop session itself still runs as root. Running it as the account that was created is not done.

## Radios

The image installs NetworkManager with wpa_supplicant (Wi-Fi), BlueZ (Bluetooth) and ModemManager with usb-modeswitch (LTE and 5G modems that speak QMI, MBIM or AT). The kernel in the built amd64 image has, among others, `iwlwifi`, `ath11k`, `mt7921e`, `rtw89`, `btusb`, `qmi_wwan`, `cdc_mbim`, `mhi_wwan_mbim`, `iosm` and `mtk_t7xx`, with `linux-firmware`.

Booted under QEMU (no KVM), with no radio hardware: NetworkManager, ModemManager and wpa_supplicant are active and no unit failed. `nmcli` reports Wi-Fi and mobile-broadband hardware as missing, `bluetooth.service` stays inactive because there is no adapter, and `mmcli -L` finds no modem. So **no radio was exercised**: no Wi-Fi scan, no Bluetooth pairing, no cellular attach. There are no Wi-Fi, Bluetooth or cellular controls in Settings or the menu bar yet; `nmcli`, `bluetoothctl` and `mmcli` in the terminal are what there is.

What that boot did show, and was fixed:

- Wired Ethernet never had an address. Ubuntu's NetworkManager package leaves Ethernet to netplan, and the image has no netplan. `image/NetworkManager-olympium.conf` lets NetworkManager manage every device. After that, the wired port got `10.0.2.15` over DHCP, a default route, working DNS and connectivity `full`.
- `run-session` raised every link at start, before NetworkManager had looked at them. A raised link has an address from router advertisements by then, and NetworkManager took that over as it was ("connected (externally)") and never ran DHCP. The loop now runs only when there is no NetworkManager.
- The desktop waited for NetworkManager: `dev-desktop.service` came after `systemd-user-sessions.service`, which comes after `network.target`, which comes after NetworkManager. The service no longer orders after it, because the session is root and does not need the network. Under emulation the desktop service became active 12.9 s into userspace instead of 43.0 s (`systemd-analyze critical-chain`).
- `ldconfig.service` ran again on every boot of the live image, 13.7 s on the critical path under emulation, because `/usr` was newer than `/etc/.updated`. `repack-install.sh` now writes the stamps after the last change to `/usr`, and the unit's start condition is unmet on boot.

## Processors and graphics

- x86_64: one generic Ubuntu kernel. That covers consumer Intel and AMD CPUs and server ones (Xeon, EPYC) alike; nothing extra is needed for them. Only QEMU was booted.
- arm64: the image uses Ubuntu's generic arm64 kernel, which boots machines with UEFI. Phones and tablets on MediaTek Dimensity or Qualcomm Snapdragon do not boot a generic image: each needs its vendor's boot chain, device tree and, mostly, drivers that are not in the mainline kernel. That is a port per device, and none was done.
- AMD GPUs: `amdgpu` with Mesa, in the image. Data-center AMD cards use the same driver.
- NVIDIA GPUs: `nouveau` is in the image. NVIDIA's own driver is not bundled, because its license does not allow it to be shipped this way; `driver-install` is where it would be fetched on the user's machine. Not run.
- Hardware rendering: the compositor draws with pixman, in software, unless `olympium.gles=1`. Forcing hardware-accelerated rendering per application is not done.
- Game controllers: the kernel has `xpad`, `hid-playstation` and `hid-nintendo`. There is no controller mode in the desktop.
