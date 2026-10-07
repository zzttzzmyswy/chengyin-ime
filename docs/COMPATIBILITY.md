# 兼容性矩阵

更新：2026-10-04。状态定义：**已测**指在列出的环境实际运行对应测试；**构建检查**只证明源码对该 target 可编译检查；**待测**需要对应桌面/设备。CI 文件存在不等于 CI 已运行成功。

## 当前证据

| 范围 | 当前状态 | 证据与边界 |
| --- | --- | --- |
| Linux x86_64 Rust 核心 | 已测 | Debian 13、Rust 1.99，行为/索引参考对比/零分配测试 |
| C ABI 动态链接 | 已测 | 本地 GCC 14 调用 release `.so`；GitHub Linux/Windows MSVC C 程序调用动态库通过，UTF-8、空指针、缓冲区、句柄生命周期 |
| Fcitx 5 插件 | 本地构建及无头测试已测 | Debian Fcitx 5.1.12；事件/热切换测试及 C++ ASan/UBSan 通过；无头测试不代表 GUI/协议通过 |
| Debian amd64 测试包 | 隔离文件布局和生命周期已测 | dpkg 安装/重装/升级/回滚/移除/purge；依赖解析已生成，依赖安装和真实桌面待测 |
| Linux aarch64 共享核心 | 构建检查 | `cargo check`，未链接或运行 ARM 原生程序 |
| Android aarch64 共享核心 | 构建检查 | `cargo check`，无 JNI、APK、NDK 链接和设备测试 |
| Windows x64 Rust + TSF | 原生 CI 与 Wine 回归已测 | Windows 2022/MSVC 与 MinGW/Wine 10 六个 CTest 通过，含 100 次激活/输入/停用；真实 Windows 11 应用验收待测 |
| Windows 单 EXE 安装器 | 原生 CI 与隔离 Wine 生命周期已测 | NSIS 3.11 amd64 Unicode，完整 SHA-256 负载、安装/修复/升级/回滚/卸载、旧 ZIP 迁移、占用 DLL 和用户文件保留通过；Wine 另做真实旧 EXE 升级；原生 profile 名称 getter 已通过，Wine 的 E_NOTIMPL 仅在该环境用保存的 Description 核对 |
| Windows MSVC | 原生 CI 已测 | 提交 b14116e、GitHub Actions 37212769471：Rust/C ABI/TSF 构建、六个 CTest、安装生命周期与 MSVC EXE 通过；Windows 2022 不能代替 Win11 桌面 |
| 首拼/声母混输与连续组句 | 核心与模拟 TSF 已测 | 穷举音节别名对照、同音翻页、中间编辑、完整输入优先；真实语料质量及 Windows 端到端延迟待测 |
| 上屏联想与会话偏好 | 核心与模拟 TSF 已测 | Tab/鼠标确认、普通空格透传、焦点/插入点变化、陈旧宿主提交、密码作用域；上下文不跨会话；Windows 独立持久 Profile 另见下一行 |
| Windows 设置/候选与持久学习 | 原生 CI/Wine UI 与模拟 TSF 已测 | 六页经典设置/嵌入测试/滚动/96→144 DPI；配置通知、语言栏与中文标点；原生 GDI 150 次重绘零增长；Profile 重载、深页排序、并发/清除 epoch、失败保留与隐私；真实 Win11 外观/端到端仍待测 |
| 搜狗词库导入 | 核心与 Windows 设置 Wine 回归已测 | 经典 SCEL 0x44/0x45、Unicode/GBK 文本、追加/替换、损坏拒绝与原子保存；其他私有/加密格式不支持，见 SOGOU_COMPATIBILITY |

## Linux 桌面验收计划（以下全部待真实会话测试）

| 会话 | 应用路径 | 重点 |
| --- | --- | --- |
| X11 | GTK 3/4、Qt 5/6、XIM 客户端 | 预编辑、候选矩形、焦点切换、终端快捷键 |
| KDE Plasma Wayland | Qt 原生 Wayland、GTK、Firefox | 系统虚拟键盘配置、text-input、缩放和多屏 |
| GNOME Wayland | GTK、Qt、Firefox、Electron | Fcitx 接入与应用模块依赖，不能假设支持统一 input-method 协议 |
| wlroots（先 Sway） | GTK/Qt、终端、Chromium | compositor 的 input-method/text-input 版本与配置 |
| 任意 Wayland 桌面的 XWayland | X11 版 Electron、Qt/XCB | X11 前端路径、混合窗口焦点、光标定位 |
| 沙盒应用 | Flatpak、必要时 Snap | 输入法模块/门户/环境继承及发行版差异 |

Fcitx 5 的插件可复用其前端和候选 UI，但 compositor 是否实现协议、GTK/Qt 的输入模块、浏览器启动后端都会影响结果。不要全局盲设 `GTK_IM_MODULE`/`QT_IM_MODULE` 并将其当作 Wayland 万能修复；按发行版、桌面和应用分别配置并记录。

应用首批：GTK 编辑器、Qt 编辑器、Firefox、Chromium、VS Code/Electron、一个终端、LibreOffice。每项记录系统/桌面/应用/Fcitx 版本、图形后端、配置、复现输入序列与实际结果。

## 通用验收用例

| 场景 | 期望 |
| --- | --- |
| 输入 `nihao`、退格、数字或空格选词 | 组合/候选同步，只上屏一次 |
| `xian` 对比 `xi'an`，部分音节分隔 | 分隔约束正确，未匹配时可原文上屏 |
| Ctrl/Alt/Super、Shift、Caps Lock、AltGr、长按 | 不吞系统/应用快捷键；当前 Caps Lock 模式尚待设计验证 |
| Enter/Esc/Tab/左右/Home/End/鼠标改插入点 | 不把残留组合带到错误位置；核心和 Windows 已实现中间编辑及选段撤回 |
| 两窗口轮换、同应用多输入框、失焦/切换输入法 | 状态隔离，不重复提交或意外泄漏组合 |
| 候选鼠标点击、陈旧候选列表回调 | 只提交当前会话当前版本的候选 |
| 密码/敏感/数字/URL 字段 | 按平台类型处理；原型已绕过 Fcitx 密码/敏感标志，其他类型待测 |
| 多屏、分数缩放、全屏、远程桌面 | 候选位置与窗口一致，无越界或卡顿 |
| 连续输入/长输入/恶意词典/词典更新 | 延迟有界，错误保持旧状态，无宿主崩溃 |
| 长时间运行、应用重启、桌面重启 | 无泄漏、无僵尸会话，恢复行为明确 |

## Windows 实机验收（全部待测）

当前预览目标 Windows 10 2004+/Windows 11、Intel/AMD x64。先运行包内 `chengyin_testpad.exe` 的两个标准编辑框及密码框，再测：

| 应用/场景 | 重点 |
| --- | --- |
| 标准 Win32 编辑框 / 老版记事本 / Notepad3 x64 / Notepad++ x64 | 启用、组合范围、选词一次上屏、标点顺序、Esc/Enter |
| Chromium / Firefox / Electron x64 | 同步锁策略、排版后的候选定位、鼠标移动插入点、不同输入框与密码作用域 |
| Office x64 | 外部编辑通知、快捷键、文档范围、组合结束 |
| 系统切换器、安装/卸载、注销重登 | profile 可见、未改变默认输入法、反复卸载及 DLL 被加载时的清理 |
| 125%/150% 缩放、多屏、窗口关闭/重建 | 候选坐标、拥有者生命周期、候选不抢焦点 |

现代记事本、Windows Terminal、WinUI/UWP、游戏或触摸界面可能使用不同的输入/候选 UI 模式，不能凭“64 位”宣称兼容。UI-element-only 已实现宿主候选接口并通过模拟回归，实际 WinUI/UWP 待测；安全模式拒绝；32 位宿主和 ARM64 缺少对应 DLL。候选窗支持键盘与鼠标选词，鼠标选择在写锁中校验会话版本。

## 后续原生平台

Windows：先完成较完整的功能和自动化批次，再集中获取 Windows x64 应用反馈；验证 Win32 编辑器、Office、Chromium/Firefox、Electron、Windows Terminal。随后评估 ARM64、32 位应用宿主、UWP/WinUI、管理员权限差异、RDP、触摸键盘、安装签名。核心 C ABI 通过不能证明这些行为。

Android：先 API 26+ 设计基线，确认实际 SDK 要求后锁定；至少一台低/中端 arm64 真机和 x86_64 模拟器。验证系统编辑框、浏览器、WebView、聊天应用、Compose TextField、硬件键盘、横竖屏、进程重建、emoji/代理对删除、EditorInfo 的密码/数字/URL 类型和无个性化学习标志。

用户实际反馈（preview4）：ChatGPT 应用可触发候选；本项目输入测试与
Notepad3 无候选。本批针对输入测试补 TSF 激活/消息循环，对传统控件补
COM-less/input-mode 注册、输入模式同步、范围/Win32 光标/视图定位回退。
已有模拟回归，但没有真实 Windows 的版本、架构和复测证据，仍标待测。
