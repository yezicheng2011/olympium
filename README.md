# Olympium Universal OS

Olympium Universal OS is a native Wayland operating system with a macOS-like desktop: a menu bar, left window controls, a dock, and Launchpad. This repository is the source for one multi-architecture ISO that boots on x86_64, i386, and ARM64 and runs 32-bit and 64-bit programs across those architectures.

The project homepage is https://olympium.net.

This tree does not contain a prebuilt bootable ISO. Building from a checkout writes `build/olympium-universal.iso` on your machine. That directory is gitignored. The full command list is [BUILD_ISO.txt](BUILD_ISO.txt). Known limits of the image are in [KNOWN_VULNERABILITIES.txt](KNOWN_VULNERABILITIES.txt).

First-party source is [Apache License 2.0](LICENSE) only. Copyright 2026 Daniel Ye (Ye Zicheng) and Olympium Contributors. Third-party components stay under their own licenses. See [NOTICE](NOTICE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Build from this repository

Host packages and the .NET 8 SDK are listed in [BUILD_ISO.txt](BUILD_ISO.txt). From the repository root:

```sh
make -C desktop
make -C vbnet build
make -C desktop test
sudo PATH="$HOME/.dotnet:$PATH" ./image/build-iso.sh universal
```

`image/build-iso.sh universal` calls `image/build-universal-iso.sh` and stages cross-architecture support. The ISO and its checksum are created at:

- `build/olympium-universal.iso`
- `build/olympium-universal.iso.sha256`

Optional switches, including Darling, are documented in [BUILD_ISO.txt](BUILD_ISO.txt). `./image/build-iso.sh amd64` and `./image/build-iso.sh arm64` still build a single-architecture image when you need one. Those outputs are also under `build/` and are not committed.

## Layout

- `branding/icons/` — gold Eastern dragon icons ([olympium-1024.png](branding/icons/olympium-1024.png), [olympium-transparent-1024.png](branding/icons/olympium-transparent-1024.png))
- `desktop/` — Wayland compositor (`compositor.c`), cross-architecture launcher (`run.c`), native bridge (`native_bridge.c`), and Mach-O helpers
- `vbnet/` — VB.NET (.NET 8) desktop and compatibility tools (`Olympium.dll`), calling `desktop/libolympium-native.so`
- `image/` — ISO build scripts (`build-iso.sh`, `build-universal-iso.sh`, `stage-crossarch.sh`, `stage-mobile-slim.sh`, `slim-rootfs.sh`, `install-wine.sh`, `install-darling.sh`) and the C++ sources they compile
- `docs/` and `validation/` — design notes and validation suites
- `BUILD_ISO.txt` — host dependencies, pack command, and QEMU boot checks
- `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md` — Apache-2.0 for first-party code, and the licenses of components the built ISO aggregates (Linux kernel GPL-2.0, GRUB GPL-3.0, QEMU GPL-2.0, Wine LGPL-2.1, Darling GPL-3.0 + APSL, and others)

## What the universal image builds in

The desktop tools are VB.NET on .NET 8. The Wayland compositor, the glass shader, and the ELF/Mach-O sandbox bridge stay in C.

- Pointer drawing is ordered after the menu bar, window controls, dock, Launchpad, and context menus, so the cursor is not covered or blurred by glass.
- One ISO carries x86_64, i386, and ARM64 boot plus userland. `qemu-user-static`, `binfmt_misc`, and a small cross-architecture libc sysroot run x86, x86_64, armhf, and ARM64 programs on any of those hosts.
- Windows `.exe` / `.msi` start through Wine and `binfmt_misc`. macOS `.dmg` / `.pkg` / `.app` go through Darling when `OLYMPIUM_DARLING=1`. Linux `.deb` / `.rpm` and cross-architecture ELF files are unpacked into a sandbox. Android `.apk` and OpenHarmony `.hap` use the slim `scrcpy`/`adb` and `hdc` tools. Full Android and OpenHarmony system images are not packed into the ISO.

## 官方说明

Olympium Universal OS 是一个采用类 macOS 桌面交互、全局单一多架构 ISO 的原生 Wayland 操作系统。所有核心桌面工具与跨平台运行时均已重构为 VB.NET (.NET 8)，仅将 Wayland 合成器、毛玻璃像素着色器与底层 ELF/Mach-O 沙箱桥接保留在极简 C 模块中。

本仓库是源代码，不包含已构建好的可启动 ISO。打包命令详见 [BUILD_ISO.txt](BUILD_ISO.txt)。项目主页是 https://olympium.net。

1. **修复指针 Z 轴层级与玻璃渲染**
   - `desktop/compositor.c` 将软件光标绘制严格延后到顶部菜单栏、窗口红黄绿控制按钮、Dock、Launchpad 与右键菜单之后，彻底解决鼠标指针被菜单栏、窗口按钮或 Dock 遮挡的问题，且光标不会被毛玻璃错误模糊。
2. **全局统一最新金色东方龙图标**
   - 统一采用最新图标资源 [olympium-1024.png](branding/icons/olympium-1024.png) 与 [olympium-transparent-1024.png](branding/icons/olympium-transparent-1024.png)，覆盖顶部菜单栏、Dock、Launchpad、Settings/About、启动画面及桌面快捷方式。
3. **VB.NET (.NET 8) 主体重构与 ISO 极限精简**
   - `vbnet/Olympium/` 实现了统一多入口程序 `Olympium.dll`（AnyCPU），完整承载 Settings、Files、Install、TestApp、Swatch、自动 Wine 运行器、Android/OpenHarmony 管理器、驱动与系统工具，并通过 P/Invoke 调用 `desktop/libolympium-native.so`。
   - Android 与 OpenHarmony 仅保留精简工具链（`scrcpy` + `adb`、`hdc` + 本地 `.hap` 解析/沙箱），移除冗余文档、调试符号与多 GB 系统完整镜像，并使用 `squashfs xz -b 1M` 高压缩率。
4. **单 ISO 全架构融合 + 跨架构无缝执行**
   - 通过 `image/build-iso.sh universal`（调用 `image/build-universal-iso.sh` 与 `image/stage-crossarch.sh`）产出全局唯一的 `build/olympium-universal.iso`，同时内置 x86_64、i386、ARM64 引导与用户态支持。该文件由构建生成，不随本仓库分发。
   - 借助 `qemu-user-static` + `binfmt_misc` + 最小跨架构 `libc` sysroot，可在任意架构电脑上无缝运行 32/64 位跨架构程序。
5. **全格式双击即原生安装与启动**
   - **Windows `.exe` / `.msi`**：通过 `binfmt_misc`（`PE`/`MZ` 魔数）+ `olympium-wine-exec` + 预建 Wine 前缀，双击即可后台自动调用 Wine 安装和启动。
   - **macOS `.dmg` / `.pkg` / `.app`（Intel + Universal2 + M 系列 ARM64 路由）**：接入 Darling GUI (`cli;gui`) 与可选 `darling-arm64` 桥接。
   - **Linux `.deb` / `.rpm` / 跨架构 ELF**：沙箱受限解包后自动配置入口与跨架构执行器。
   - **Android `.apk` & OpenHarmony `.hap`**：自动检测设备并拉起精简 `scrcpy` / 本地 OpenHarmony 窗口。
