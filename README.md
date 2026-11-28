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


## 已知安全风险与残余限制

Olympium Universal OS (ISO) — 已知安全风险与无法单靠代码根除的限制
(KNOWN_VULNERABILITIES.txt)

### 一、已在本次审计中直接修复的代码漏洞

1. [已修复] 供应链未校验下载 (CWE-494):

   iso/image/stage-mobile-slim.sh 原本直接 curl 下载 microG APK 无哈希校验；已改为强制要求 OLYMPIUM_MICROG_GMS_SHA256 校验（不匹配直接拒绝并删除），且强制 HTTPS + TLS 1.2+。

2. [已修复] .desktop / library.tsv 换行注入 (CWE-74):

   iso/vbnet/Olympium/InstallApp.vb 已对所有写入 .desktop 与 TSV 的应用名、ID、图标路径过滤控制字符与换行符，防止恶意包名注入额外 Exec= 指令。

3. [已修复] 跨平台剪切板/URI 注入 (CWE-78 / CWE-20):

   iso/vbnet/Olympium/InteropHub.vb 已对 Android ADB 剪切板参数采用严格 POSIX 单引号转义并限制 64 KiB + 剥离 NUL，仅放行 http/https/mailto URI，并对共享文件名做字符白名单过滤。

### 二、受架构或上游限制、无法单靠本项目代码根除的已知风险

1. [高] 双击即装即运行 / binfmt_misc 自动执行攻击面 (Architectural)

   - 风险：开启 binfmt_misc 自动执行 MZ/PE 与跨架构 ELF 后，用户在终端或文件管理器双击来源不明的 .exe/.deb/.rpm/ELF 会直接进入 Wine 或 QEMU 用户态执行，Wine 默认共享当前用户主目录与剪贴板，恶意程序可读写用户文件。
   - 无法根除原因：属于“双击即原生运行”产品交互与 Wine/Linux 用户态权限模型的固有权衡。
   - 缓解建议：仅安装可信来源软件；必要时对高风险 Windows 程序使用独立 OLYMPIUM_WINE_PREFIX 并在设置中关闭自动剪贴板共享。

2. [中] 非特权 User Namespace 放宽 (kernel.apparmor_restrict_unprivileged_userns=0)

   - 风险：iso/image/build-iso.sh 为兼容 Firefox 沙箱关闭了 Ubuntu 24.04 的 AppArmor userns 限制，这会扩大 Linux 内核用户命名空间相关提权漏洞的本地利用面。
   - 无法根除原因：Live ISO 下 Firefox 内容沙箱依赖非特权 userns。
   - 缓解建议：及时通过 Ubuntu 安全仓库更新发行版内核与浏览器。

3. [中] 上游组件固有漏洞 (Linux Kernel / Wine / QEMU / Darling / Firefox / .NET 8)

   - 风险：ISO 打包了静态快照版本的发行版内核、QEMU-user-static、Wine、Darling 与 Firefox；上游后续披露的 CVE 无法在旧 ISO 镜像中自动消失。
   - 无法根除原因：取决于上游安全更新，且未修改内核。
   - 缓解建议：装机后定期运行 apt upgrade 或重新使用 build-iso.sh 构建新镜像。

4. [中] microG 签名伪装 (Signature Spoofing) 风险

   - 风险：Android 端启用 microG 所需的 Signature Spoofing 权限若被恶意 APK 滥用，可能冒充系统或 GMS 签名。
   - 无法根除原因：开源 microG 替代闭源 GMS 的必要机制。
   - 缓解建议：仅授予受信任的官方 microG GmsCore 签名伪装权限，切勿全局放开。

5. [中] 跨生态共享剪贴板信息泄露 (Cross-Boundary Clipboard Leakage)

   - 风险：双向剪贴板同步开启时，宿主机复制的密码/令牌会同步至 Wine、Android 或鸿蒙侧，可能被侧方应用读取。
   - 缓解建议：复制敏感凭据前可在设置页临时关闭“Shared Clipboard”开关。

6. [低] Live ISO 明文写入内存 overlay / 无默认全盘加密 (LUKS)

   - 风险：Live 模式与 install-to-disk 默认未启用全盘 LUKS 加密。
   - 缓解建议：生产环境建议在装盘后配置 LUKS 加密分区或加密主目录。
