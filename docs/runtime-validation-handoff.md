# 运行验证交接（2026-11-17）

目标是 Linux 底座、Mac 风格桌面，像用户举例的 AeroAndroid 体验一样：下载应用包后安装、生成启动图标、以独立窗口打开，并支持输入、剪贴板和文件互通。该目标应覆盖 Linux、Windows、Android、OpenHarmony 和 macOS。**本 PR 没有完成五平台应用全兼容，也没有证明接近原生性能。**

## 最新续接状态

- **Cocoa GUI 冒烟通过**：正式 `.app` 导入、桌面搜索启动、真实 Mach-O/Cocoa 窗口、普通桌面指针触发 NSButton 回调，窗口标题变为 `Olympium Mac GUI - verified`。随后经桌面指针关闭、确认窗口不可见，用当前安装器在 Landlock 下重新导入并从搜索打开，第二个窗口再次响应按钮；生命周期记录在 `cocoa-lifecycle.log`。使用原始官方 Darling 二进制、Mesa softpipe 和关闭 MIT-SHM 的 Xwayland；没有应用二进制补丁或关闭 Landlock。证据在 `validation/macos/evidence/`。截帧中标签文字未更新，文字重绘不计通过；第三方 GUI 应用和近原生性能未验证。
- **Android 17 正式 APK 链路已实跑通过**：在官方 AOSP API 37.0 rev6 x86_64 上，经正式安装器安装并核验 Fossify Calculator 字节；桌面搜索打开独立 Wayland 窗口，普通桌面指针计算 **2+3=5**；关闭后从 Launchpad 原始 APK 图标重开，再次计算通过。截图、两份新 UI 树及结果在 `validation/android/evidence/api37-*`。
- **修复首次窗口尺寸和 SVG 图标依赖**：竖屏客户端初始尺寸适配面板与 Dock 之间的空间，底部按键可点；补装 `librsvg2-common` 后，自适应 APK 图标正确显示。构建、渲染和启动回归通过，四秒空闲 0 帧；受控延迟下原生映射 0.222 秒、兼容路径 1.887 秒。
- **Android 已补充正常时钟 KVM 验收**：CI 44208210089 在两个独立模拟器进程中通过自动配置、microG 普通 UID 验证和真实计算器输入；没有启用 TCG 或 icount。首次受控安装用时 158.018 秒，第二轮冷启动安装链路 77.499 秒，均不是桌面首帧或整机开机成绩。原始 APK 图标、桌面指针和独立窗口仍对应前述诊断 TCG 记录，KVM 作业没有验收宿主桌面窗口。
- **microG 限定签名兼容原型和基础 RPC 已验证**：Services 与 Companion 已安装且已核对实际 base APK 的 SHA256。新增独立普通 UID 的 Android 验证应用：Common 服务 broker、getService 请求、成功回调和返回服务 Binder 均通过；原始系统的证书查询失败保留在 `api37-microg-probe.json`；限定兼容层下同一普通 UID 客户端的旧/新 GMS、Companion 证书查询及 Common RPC 均通过，见 `api37-microg-compatible-probe.json`。原签名 Companion 更新成功，异签名 APK 被拒绝且已安装 APK 哈希不变，见 `api37-microg-update-signatures.json`。自动启动接入与两轮正常时钟 KVM 冷启动已通过，见 `api37-kvm-managed-roundtrip.json` 和 CI 44208210089；完整 GMS API、FCM、账号、地图和认证均未验收。
- **启动器路径回归已修复**：用 GIO 实际启动生成的桌面入口，验证空格、引号、`%`、反斜杠、美元符号和反引号路径；DEB/RPM 整套回归通过。完整 `test-mac` 仍仅有此前的两个 `nobody` 回退隔离断言失败，不能称整套通过。
- **OpenHarmony 正在进行 KVM GUI 验收**：新增固定源码版本的 SDK 测试客户端，可通过正常账号 API 激活用户 100，以自身的原生服务身份调用锁屏和电源接口；测试镜像如配置了凭证会直接拒绝解锁。TCG 曾观察到解锁回调成功，但仍未通过计算器前台界面和输入，不计为 GUI 修复完成。`validation/openharmony/` 现可重现构建；CI 要求真实 KVM、应用所属新 UI 树、物理截帧、点击计算和关闭重开，等待实际结果。
- **Android 更新和数据回退已有实际验证，后续更新开发暂停**：CI 44212303251 验证同签名 v1→v2 保留 UID 和私有数据、异签名 v3 被系统拒绝、原生 `pm rollback-app` 恢复 v1 APK 与更新前私有数据。正式桌面命令保留同一图标记录，新增 8 项事务检查及前端安装记录测试；不是所有软件的统一滚动更新。按用户要求，当前优先完善应用运行。

下方包含此前检查点，冲突处以本节为准。尚未重建 ISO、验证所有平台同时运行或任意商业应用。

## 本次可交付部分

- 修复空闲持续重绘：消费并轮转 wlroots damage；输入、窗口标题、客户端内容和定时系统状态变化触发重绘。相同云机、1280×800 headless/pixman、无客户端的五秒采样中，单核 CPU 从 25.59% 降至 0.80%。该数据不是实机 GPU 测试。
- 窗口解绑、隐藏或销毁时清理标题栏按下和拖动引用；补充标题变化的重绘通知及无 Xwayland 构建声明。原始 Wine 堆损坏没有复现，不能宣布根因已彻底解决。
- 原生 Wayland 会话可以不等 Xwayland；Wine 通过现有显示连接文件等待 Xwayland。移除首次设置窗口的三秒固定等待，将网络/驱动报告移出首帧阻塞链路，不再依赖全局 udev settle。驱动模块目录只索引一次。
- 新增渲染、启动顺序和驱动索引回归测试；新增固定提交的上游源码下载入口。
- 保存实验所用下载地址和校验值：`validation/runtime-artifacts.json`。它是完整性证据，不是应用执行通过证书。
- `validation/macos/hello-gui.c` 已交叉编译为真实 x86_64 Mach-O，链接 Darling 的 Cocoa/AppKit。独立窗口和按钮回调已在完整 VM 中通过；见下方更新。

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
- 实际包名是 **`org.fossify.math`**。续接验证已通过包管理器解析出启动别名 `.activities.SplashActivity.Green`，实际前台为 `.activities.MainActivity`。已安装 base APK 的 SHA256 与下载记录一致。
- 首次开机曾观察到 `com.android.systemui` 无响应对话框，选择 Wait 后恢复。之后自动验证已核对前台窗口、UI 树、实际渲染截图，并按资源 ID 定位按钮输入 **2+3=5**，检查独立的 formula/result 字段。结果与截图见 `validation/android/evidence/`；这不证明任意 APK 或近原生性能。
- 后续严格冷启动复测失败：黑屏、UiAutomator 连续三次取不到根节点，已保存失败记录；先前成功不能代表稳定冷启动。
- scrcpy 5.0.1 已实测拒绝 Android 9 的 `--new-display`。改用官方 API 29 x86 后，已核对同一 APK 字节，并在桌面的独立 Wayland 窗口显示真实计算器。应用的启动页会把主界面放回 display 0；将该应用独占的任务栈移到 scrcpy 显示后出图。
- 使用明确单独编译的 `dev-compositor-input-test`，经标准 Wayland 虚拟指针、桌面普通 cursor/seat 处理和 scrcpy 输入，点击 C、2、+、3、=。实际桌面截图和新导出的 Android UI 树均为 **2+3=5**，见 `validation/android/evidence/api29-desktop-calculator-*`。正式 compositor 不提供该测试输入协议。
- `validation/android/open-window.py` 已在 API 29 重新启动验证显示就绪和任务归位；再次经桌面指针计算通过。通过桌面 File → Close Window 关闭后 scrcpy 正常退出，脚本记录 `closed`。位置就绪状态本身不自动等于输入通过。
- 续接后在停止其他实验 compositor 的隔离条件下重跑 `test`、`test-render`、`test-startup` 通过：四秒空闲 1 帧，渲染/生命周期通过；受控延迟下原生窗口 0.286 秒、兼容路径 1.951 秒。额外验证正式二进制不暴露测试虚拟指针协议。

下一步：验证 ARM 应用、剪贴板与文件共享。Android 签名更新和原生数据回退已实测；进一步更新工作按用户要求暂停。正常时钟 KVM 的自动接入和第二次冷启动恢复已通过。Android 17 的正式安装、图标、桌面鼠标和关闭重开结果见本页最新状态。API 29 的单个 x86 APK 通过不能代表所有 APK。Waydroid 的原生 Binder 方案仍未安装/运行。

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
- 续接已修复嵌套代理转发，TLS 和 Ubuntu 包签名校验保持开启；官方 Darling GUI 包、桌面依赖已安装并配置，`darling shell true` 返回成功。
- `.app` 从只读 9p 导入时受访问限制而失败；把同一测试文件校验并复制到 VM 本地 ext4 后，`dev-install import --consent` 在 Landlock 隔离下成功生成安装记录和桌面文件。没有禁用隔离。
- 初版测试 Mach-O 的 ld64 SDK 元数据默认为 10.6，触发 Objective-C raw-isa 断言。新增 `validation/macos/build-smoke.sh` 明确设定目标/SDK 版本后断言不再出现。
- 当前程序能进入 `main`、创建非空 `NSApplication`，但在窗口初始化期间退出，尚未进入事件循环。软件渲染测试也未得到持续显示的窗口；窗口搜索曾找到短暂对象，随后已失效。**窗口和按钮仍未通过验证。**

下一步：测试真实第三方 Mac GUI 应用、标签重绘及重复冷启动。独立 Cocoa 窗口、按钮和关闭重开结果见本页最新状态。构建成功、导入成功和退出码 0 均不能代替 GUI 验证。

### 共用桌面集成与其他遗留项

- Android 17 的正式 APK 安装记录、图标、启动器、窗口关闭重开和指针计算已在诊断配置验证。HAP 统一安装和窗口管理、跨平台剪贴板与共享目录仍未完成。
- 未验证 Android/OpenHarmony/macOS 同时运行，未验证近原生性能。
- 32 位 Wine、原始 heap corruption 的确定性复现、隔离测试的两个失败仍待处理。
- 新 ISO 的构建、真实冷启动、物理 GPU 和硬件加速均未验证。

## 本工作区的续接资料

实验缓存位于 `/workspace/bbb/build/runtime-validation/`（被 Git 忽略，**不会随 PR 分发**）：

- `android-sdk/`：已校验的 emulator、ADB、API 28/29/30 镜像；`android-avd/`：测试 AVD。
- `android-calculator.apk`、`scrcpy/`：已校验的真实 APK 和显示工具。
- `oniro/images/`：已校验的官方 OH 镜像；`oh-sdk/sdk/toolchains/hdc`：HDC。
- `darling-release/`、`darling-stage/`：官方 GUI 包及解包结果；`darling-build/`：部分源码构建；`hello-gui`：已编译 Mach-O。
- `mac-guest/`：已校验 Ubuntu 基础镜像、实验 qcow2、NoCloud seed。它包含临时 SSH 私钥和环境网络配置，**不要提交、打包公开或复制到 PR**。
- `evidence/`：交接时保存的运行日志。原有主机日志还在 `/tmp/bbb-*.log`；容器内日志在 `bbb-dev:/tmp/bbb-*.log`。

交接会停止本次启动的测试 VM 和代理转发，避免继续占用 CPU。进程不会随环境快照恢复；后续应从已验证的镜像和保存的配置重新启动，不能假设保留运行中的 RAM 状态。
