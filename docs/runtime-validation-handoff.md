# 运行验证交接（2026-11-16）

目标是 Linux 底座、Mac 风格桌面，像用户举例的 AeroAndroid 体验一样：下载应用包后安装、生成启动图标、以独立窗口打开，并支持输入、剪贴板和文件互通。该目标应覆盖 Linux、Windows、Android、OpenHarmony 和 macOS。**本 PR 没有完成五平台应用全兼容，也没有证明接近原生性能。**

## 本次可交付部分

- 修复空闲持续重绘：消费并轮转 wlroots damage；输入、窗口标题、客户端内容和定时系统状态变化触发重绘。相同云机、1280×800 headless/pixman、无客户端的五秒采样中，单核 CPU 从 25.59% 降至 0.80%。该数据不是实机 GPU 测试。
- 窗口解绑、隐藏或销毁时清理标题栏按下和拖动引用；补充标题变化的重绘通知及无 Xwayland 构建声明。原始 Wine 堆损坏没有复现，不能宣布根因已彻底解决。
- 原生 Wayland 会话可以不等 Xwayland；Wine 通过现有显示连接文件等待 Xwayland。移除首次设置窗口的三秒固定等待，将网络/驱动报告移出首帧阻塞链路，不再依赖全局 udev settle。驱动模块目录只索引一次。
- 新增渲染、启动顺序和驱动索引回归测试；新增固定提交的上游源码下载入口。
- 保存实验所用下载地址和校验值：`validation/runtime-artifacts.json`。它是完整性证据，不是应用执行通过证书。
- `validation/macos/hello-gui.c` 已交叉编译为真实 x86_64 Mach-O，链接 Darling 的 Cocoa/AppKit。**尚未验证其窗口实际打开。**

## 已执行的检查

Ubuntu 24.04 开发容器中：

| 检查 | 结果 |
| --- | --- |
| 桌面完整构建、`make -C desktop test` | 通过 |
| `make -C desktop test-render` | 空闲帧数、外壳变化重绘、12 轮 X 窗口隐藏/重映射/销毁通过 |
| AddressSanitizer/UBSan 构建的同类生命周期测试，40 轮 | 通过，无报告；不是原始 Wine 堆损坏的复现 |
| `make -C desktop test-startup` | 启动先后顺序、只启动一次会话、驱动索引通过 |
| `TEST_STARTUP_WINE=1 make -C desktop test-startup` | 无继承 DISPLAY 的原生会话仍可启动真实 Wine 窗口 |
| `make -C desktop test-wine` | 64 位 Wine、双向剪贴板、共享文件、26 轮重映射、无 Xwayland 路径通过；32 位未测 |
| `make -C desktop test-packages` | .deb/.rpm 导入、执行及对应检查通过 |
| `make -C desktop test-mac` | 整套失败：`nobody` 回退下两个 world-writable 目录隔离断言失败；安装器、搜索、Launchpad 等功能检查通过 |
| 源码下载器 | 新目录下载和重复执行通过，Git 提交核对通过 |

启动测试刻意让 Xwayland 延迟 1.5 秒：一次采样从 1.982 秒降至 0.243 秒；另一次为 1.857 秒和 0.263 秒。**不是整机冷启动成绩。没有为本轮修改重建或启动 ISO。**

## 未完成部分和最后检查点

### Android

- 官方 AOSP API 30 x86_64 首次启动不稳定，包管理请求出现 Broken pipe，未通过。
- 改用官方 API 28 x86 镜像，`sys.boot_completed` 返回 `1`。
- 下载并核验 Fossify Calculator 1.4.0 APK，ADB 安装返回 `Success`。
- 实际包名是 **`org.fossify.math`**。先前使用 `org.fossify.calculator/.activities.MainActivity` 的启动命令错误，返回 Activity 不存在。这不是兼容性失败的证明，也不是启动通过。
- 曾观察到 `com.android.systemui` 无响应对话框。尚无可确认的计算器前台界面、运算输入或独立桌面窗口结果。
- scrcpy 5.0.1 官方 Linux 包已下载、校验，并运行过 `--help`；尚未将 Android 应用接入桌面独立窗口。

下一步：用 `cmd package resolve-activity` 或 `monkey -p org.fossify.math` 解析正确入口，确认真实界面并执行 2+3=5、保存截图/UI 树；然后验证 scrcpy 的应用独立窗口方案及版本要求。API 28 的通过不能代表现代 Android API 或所有 APK。Waydroid 的原生 Binder 方案仍未安装/运行。

### OpenHarmony

- 官方 Eclipse Oniro 6.1 x86_64 镜像已下载并核验 SHA-256。
- 已用 QEMU TCG 启动，HDC 连通，读取到 `OpenHarmony-6.1.0.31` 和真实 bundle 列表。
- 计算器示例 bundle 为 `ohos.samples.distributedcalc`，入口 `MainAbility`，HAP 位于 `/system/app/com.example.distributedcalc/Calc_Demo.hap`。
- 启动返回 **10106102：设备处于锁屏状态，开发者模式下自动解锁失败**。尚未验证前台 GUI 或计算输入，也没有完成 HAP 重装验证。
- 日志还有 launcher `AbilityAttachTimeOut`、`THREAD_BLOCK_3S`。软件模拟很慢，不能当作近原生体验。
- 诊断时尝试上游提供的 `persist.sys.abilityms.timeout_unit_time_ratio=20`：设置回报错误 105，但读回值为 20。该值在服务中缓存，尚未重启服务验证效果；不得称为修复。VM 以 `-snapshot` 运行，这些实验写入没有修改原始镜像。

下一步：处理真实解锁/输入和图形启动；确认前台 ability、实际像素和计算结果；再导入一个 HAP、接入桌面启动器和窗口。OpenHarmony 的结果不能扩写为所有商业 HarmonyOS 应用兼容。

### macOS GUI

- 已取得官方 Darling GUI Debian 包，发布标签 `v0.1.20260608`，包内版本 `0.1.20260609~noble`；完整包 SHA-256 已核验。
- 现有源码 pin 的 CLI+GUI 构建仅部分完成；取得官方 GUI 包后暂停了可选完整构建，保留其输出。不能将其算作构建通过。
- 用已生成的 Darwin 链接器和官方运行库，把独立 Cocoa 测试源码编译成真正 Mach-O。没有使用 GTK 假装成 Mac 应用。
- 当前容器不能创建 mount namespace，所以另外启动了 Ubuntu 24.04 完整 VM，SSH 和只读 9p 源码挂载已经工作。
- **该 VM 的 Darling/桌面依赖安装未成功**：嵌套网络不能直接连接会话代理；尝试的 loopback TCP 转发也尚未验证成功。没有打开真实 Cocoa 窗口。

下一步：修复 VM 的代理路由，保持 TLS 和包签名验证；安装官方 Darling GUI 包和桌面依赖；把 smoke Mach-O 包装为 `.app`，通过 `dev-install` 与 `olympium-mac-run` 安装、启动，在桌面核对窗口与按钮回调。然后测试真实第三方 Mac GUI 应用。

### 共用桌面集成与其他遗留项

- Android/HAP 的统一安装记录、图标、启动器、独立窗口、退出处理、剪贴板和共享目录都没有完成。
- 未验证 Android/OpenHarmony/macOS 同时运行，未验证近原生性能。
- 32 位 Wine、原始 heap corruption 的确定性复现、隔离测试的两个失败仍待处理。
- 新 ISO 的构建、真实冷启动、物理 GPU 和硬件加速均未验证。

## 本工作区的续接资料

实验缓存位于 `/workspace/bbb/build/runtime-validation/`（被 Git 忽略，**不会随 PR 分发**）：

- `android-sdk/`：已校验的 emulator、ADB、API 28/30 镜像；`android-avd/`：测试 AVD。
- `android-calculator.apk`、`scrcpy/`：已校验的真实 APK 和显示工具。
- `oniro/images/`：已校验的官方 OH 镜像；`oh-sdk/sdk/toolchains/hdc`：HDC。
- `darling-release/`、`darling-stage/`：官方 GUI 包及解包结果；`darling-build/`：部分源码构建；`hello-gui`：已编译 Mach-O。
- `mac-guest/`：已校验 Ubuntu 基础镜像、实验 qcow2、NoCloud seed。它包含临时 SSH 私钥和环境网络配置，**不要提交、打包公开或复制到 PR**。
- `evidence/`：交接时保存的运行日志。原有主机日志还在 `/tmp/bbb-*.log`；容器内日志在 `bbb-dev:/tmp/bbb-*.log`。

交接会停止本次启动的测试 VM 和代理转发，避免继续占用 CPU。进程不会随环境快照恢复；后续应从已验证的镜像和保存的配置重新启动，不能假设保留运行中的 RAM 状态。
