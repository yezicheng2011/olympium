# Olympium Universal OS (ISO)

Olympium Universal OS 是一个采用类 macOS 桌面交互、**全局单一多架构 ISO** 的原生 Wayland 操作系统。所有核心桌面工具与跨平台运行时均已重构为 **VB.NET (.NET 8)**，仅将 Wayland 合成器、毛玻璃像素着色器与底层 ELF/Mach-O 沙箱桥接保留在极简 C 模块中。

打包命令详见 [BUILD_ISO.txt](BUILD_ISO.txt)。

## 核心特性

1. **修复指针 Z 轴层级与玻璃渲染**
   - `desktop/compositor.c` 将软件光标绘制严格延后到顶部菜单栏、窗口红黄绿控制按钮、Dock、Launchpad 与右键菜单之后，彻底解决鼠标指针被菜单栏、窗口按钮或 Dock 遮挡的问题，且光标不会被毛玻璃错误模糊。
2. **全局统一最新金色东方龙图标**
   - 统一采用最新图标资源 [olympium-1024.png](branding/icons/olympium-1024.png) 与 [olympium-transparent-1024.png](branding/icons/olympium-transparent-1024.png)，覆盖顶部菜单栏、Dock、Launchpad、Settings/About、启动画面及桌面快捷方式。
3. **VB.NET (.NET 8) 主体重构与 ISO 极限精简**
   - `vbnet/Olympium/` 实现了统一多入口程序 `Olympium.dll`（157 KB AnyCPU），完整承载 Settings、Files、Install、TestApp、Swatch、自动 Wine 运行器、Android/OpenHarmony 管理器、驱动与系统工具，并通过 P/Invoke 调用 `desktop/libolympium-native.so`。
   - Android 与 OpenHarmony 仅保留精简工具链（`scrcpy` + `adb`、`hdc` + 本地 `.hap` 解析/沙箱），移除冗余文档、调试符号与多 GB 系统完整镜像，并使用 `squashfs xz -b 1M` 高压缩率。
4. **单 ISO 全架构融合 + 跨架构无缝执行**
   - 通过 `image/build-iso.sh universal`（调用 `image/build-universal-iso.sh` 与 `image/stage-crossarch.sh`）产出全局唯一的 `build/olympium-universal.iso`，同时内置 x86_64、i386、ARM64 引导与用户态支持。
   - 借助 `qemu-user-static` + `binfmt_misc` + 最小跨架构 `libc` sysroot，可在任意架构电脑上（如 x86_64 电脑上直接启动 ARM64 或 ARM32 软件，或 ARM64 上启动 x86/x86_64 软件）无缝运行 32/64 位跨架构程序。
5. **全格式双击即原生安装与启动**
   - **Windows `.exe` / `.msi`**：通过 `binfmt_misc`（`PE`/`MZ` 魔数）+ `olympium-wine-exec` + 预建 Wine 前缀，双击即可后台自动调用 Wine 安装和启动（如 Chrome Windows 版），不再报 `Exec format error`，也无需手动开启 Wine。
   - **macOS `.dmg` / `.pkg` / `.app`（Intel + Universal2 + M 系列 ARM64 路由）**：接入 Darling GUI (`cli;gui`) 与可选 `darling-arm64` 桥接，双击即解包、注册到 Applications 与 Launchpad 并直接运行。
   - **Linux `.deb` / `.rpm` / 跨架构 ELF**：沙箱受限解包后自动配置入口与跨架构执行器。
   - **Android `.apk` & OpenHarmony `.hap`**：自动检测设备并拉起精简 `scrcpy` / 本地 OpenHarmony 窗口，无需手工配置 SDK 会话。

## 目录结构

- `branding/icons/`：统一金色东方龙图标资源
- `desktop/`：极简 C Wayland 合成器 (`compositor.c`)、跨架构启动器 (`run.c`)、原生桥接库 (`native_bridge.c`)、Mach-O 解析 (`macbundle.c`)
- `vbnet/`：VB.NET (.NET 8) 桌面应用与跨平台兼容管理套件
- `image/`：通用单 ISO 打包脚本 (`build-iso.sh`、`build-universal-iso.sh`、`stage-crossarch.sh`、`stage-mobile-slim.sh`、`slim-rootfs.sh`、`install-wine.sh`、`install-darling.sh`)
- `docs/` & `validation/`：技术文档与校验套件
- `BUILD_ISO.txt`：完整打包命令手册
- `LICENSE` / `NOTICE` / `THIRD_PARTY_NOTICES.md`：第一方 Apache-2.0 许可证与 ISO 内全部第三方组件许可清单（Linux 内核 GPL-2.0、GRUB GPL-3.0、QEMU GPL-2.0、Wine LGPL-2.1、Darling GPL-3.0 + APSL 等）
