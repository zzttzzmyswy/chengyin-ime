# preview11 纠错示例说明

2026-10-05：见 [WINDOWS_PREVIEW11_TASKLIST](WINDOWS_PREVIEW11_TASKLIST.md)。四类纠错为通用 trie/字母对齐规则，
不限定 zhang。设置每项标注“示例”，分别展示 zhang、ping、hao、shi，并在组内说明适用于词库拼音。
扩展既有键盘失误回归到不同声母、韵母和 ni'hao 多音节，核对逐字母标记和提交；算法范围不变。

# preview10 匹配与主题补充

2026-10-05：见 [WINDOWS_PREVIEW10_TASKLIST](WINDOWS_PREVIEW10_TASKLIST.md)。三个角色主题及绘制代码删除，保留通用系统/白/黑绘制器。
共享核心 fuzzy.rs 定义声母/韵母规则、QWERTY 键位和固定栈字母对齐；dictionary.rs 在只读 trie 上有界遍历，
每音节一次、每词两次键盘纠错，4096 状态预算。Decoder 在原始输入位置构词图，准确拼写路径优先。
纠错句子保留标准拼音，候选每 ASCII 字母的标记经 C ABI 提供；不存在按键期堆分配和平台 I/O。
内部 ResultRef 压成 32 位以容纳句子标准拼音，候选结果上限仍 4096，会话仍低于 64 KiB。
拼音超过 255 字节的纠错句子显式受限，未纠错路径保留原语义。候选字体和字母坐标同步 DPI 缩放。
个人配置 MatchingOptions 默认 0，旧文件兼容；现有后台通知从下一段输入应用匹配设置。
学习错拼后，已有词库/解码句子的标准读音仍用于标记。新增接口 configure_matching/candidate_marks 不改 ABI v1 生命周期。
原生构建只列生产与七项回归 target，build-only desktop helper 的杀毒提示未绕过，详见 STATUS。

以下为历史批次记录：

# preview9 主题开发补充

2026-10-05：批次见 [WINDOWS_PREVIEW9_TASKLIST](WINDOWS_PREVIEW9_TASKLIST.md)。
theme_art.cpp 为候选窗口与设置预览共享的原创矢量绘制器，GDI+ 由系统提供。
主题 0 保留经典 GDI；其他主题绘制圆角、插画、纹样、选中态和序号徽章。
不在共享 Rust 核心加入 GUI 依赖；插画不下载第三方资产。
Shift 测试回调只允许取消切换，不能触发切换；空闲透传按键不谎报 eaten。
真实安装的 Notepad3 与模拟编辑对象的结果分别记录，见 STATUS。

# preview8 本机 Windows 开发补充

2026-10-05：批次见 [WINDOWS_PREVIEW8_TASKLIST](WINDOWS_PREVIEW8_TASKLIST.md)。
Windows-only 空格映射到原文提交；共享核心/C ABI 不改语义。
设置 UI 字体在所有控件换用新句柄后才释放旧句柄；UI/桌面测试使用隔离路径。
词库列表原子封装于 dictionary.custom，内置词库与各启用项在后台合并为只读快照。
系统亮暗颜色读取 AppsUseLightTheme，响应系统设置/颜色通知；高对比度仍优先。
build-only windows_desktop_test 用进程内 COM 工厂测试实际新 DLL，不安装独立测试应用。
自动更新通过 WinHTTP 只读查询 Releases，在后台下载、验证 SHA-256/PE64 后由用户启动安装。

原生构建和七项 CTest 通过；日志在 build/native-build-preview8.txt、
build/native-test-preview8.txt。工具链和复现见 ENVIRONMENT，桌面边界见 STATUS。

以下保留已有开发指南：

# 开发指南

## 工程边界

| 目录 | 职责 |
| --- | --- |
| `crates/ime-core` | 词典、音节图、有界解码、会话、联想与选词偏好；不访问平台文件或 GUI |
| `crates/ime-ffi`、`include/myswy_ime.h` | C ABI、句柄所有权、UTF-8 缓冲区、panic 边界 |
| `crates/ime-cli` | 查询、词库转换、交互演示 |
| `platforms/windows` | TSF、原生设置、候选、学习保存、语言栏、安装器 |
| `platforms/fcitx5` | Fcitx 5 适配、后台词库切换与配置 |
| `scripts`、`.github/workflows` | 检查、词库编译、打包、安装生命周期与 CI |
| `data` | 有许可证和固定来源的常用词库；不得混入用户个人词库 |

每个输入上下文拥有独立 Session；词典不可变并共享，用户偏好使用快照。
公共 ABI 保留内部 Myswy 标识以兼容旧配置；对外品牌为澄音。
不要将平台消息循环、磁盘读写或字体逻辑加入共享核心。

## 构建和检查

Rust 1.82+：`cargo test --workspace --locked`；Linux 完整检查为 `bash scripts/check.sh`。
性能用 `cargo bench -p myswy-core --bench latency`，记录工具链、硬件、词库、场景和百分位。
按键处理的零分配保证不包含初始化、加载、GUI、后台持久化与系统 API。

Windows 使用 [平台指南](../platforms/windows/README.md) 的 `Build.ps1`，同时运行六个 CTest
并输出单个安装 EXE。需要 Rust MSVC target、VS 2022 C++/Windows SDK、CMake 3.20+、
Python 3.11+、NSIS 3.11+；生产 DLL 与设置 EXE 静态链接运行库。
官方 NSIS 3.11 Windows 安装缺少 AMD64 stubs/plugins。Build.ps1 自动在
`build/nsis-amd64` 复制私有工具链，并用固定 SHA-256 的 Debian `nsis-common 3.11-1`
补齐 Windows PE 组件，不修改系统 NSIS。来源与组件哈希随包保存在 RUNTIME_LICENSES.zip。
该下载只发生在构建机；离线准备可给 `scripts/prepare_windows_nsis.py` 传 `--package`。
MinGW 交叉构建可指定 `MYSWY_CORE_LIBRARY` 与标准 CMake toolchain，注意 Rust target 必须匹配。
跨编译的 CTest 通过 `CMAKE_CROSSCOMPILING_EMULATOR` 启动 Wine；这不代替 Windows 实机。

Linux/Fcitx 5 的 SDK、构建、暂存安装、Debian 包和 ASan/UBSan 命令见
[平台文档](../platforms/fcitx5/README.md)。Android 当前只检查共享核心的 ARM64 编译，没有 IME APK。

## Windows 配置发布

`preferences.cpp` 使用有界、严格校验的 UTF-16LE INI。写入经临时文件、刷盘和原子替换，
失败保留旧文件；新的 `ChinesePunctuation` 键缺失时默认启用，兼容旧 Version=1 配置。
只有保存成功才增加会话范围共享映射 `Local\MyswyIME.ConfigurationGeneration` 的版本号。
词库导入/恢复、学习导入/清除也发布通知；普通选词保存不发布，避免每次上屏引发全应用重载。

每个已激活 Service 的 `ConfigurationWatcher` 在后台每 200 ms 检查共享整数，有变化才读取配置、
词典与 Profile。读取完成后向本线程的 message-only window 投递消息，最新快照覆盖待发布快照。
UI/Session 只在 TSF 所在 STA 线程修改；输入事件中没有逐键文件读取。
连续多次保存会合并，损坏配置不会覆盖可用设置；大型词库加载的延迟另计。
手工编辑 INI 不发布共享通知，需要通过设置保存或重新激活服务。

外观和标点选项随快照发布生效；已有组合保留原词典和核心候选策略，完成后下一次输入切换。
不要为了换配置删除正在显示的拼音。`defaultEnglish` 仅控制服务激活时的初始模式。
学习关闭后立即停止提交确认；核心排序策略在下一次组合配置时更新。
用户清除/导入学习使用独立的 LearningGeneration，让旧排队写入无法复活已清除记录。
Service 停用先停止/join watcher，再释放词典与 Profile；快照和后台对象计入 DLL 模块生命周期。

## Windows 按键、标点与语言栏

`OnTestKeyDown/Up` 只规划行为，不写入文档、不切换模式、不推进引号状态。
实际输入仅在同步 TSF 写锁成功后修改 Session；一次 `SetText` 同时提交候选和标点，防止重放。
中文模式默认转换常用标点，引号只在宿主写入成功后推进；英文、密码和快捷键透传。
活动拼音中的 `'` 始终是音节约束，`-/=` 继续翻页；空闲状态的引号才走中文标点。

`language_bar.cpp` 实现 SDK ABI 兼容的 ITfLangBarItemButton/ITfSource，使用系统 INPUTMODE item GUID，
注册 SYSTRAY 类别；模式改变同步 open/conversion compartments、状态、图标、提示和文本通知。
每次 GetIcon 返回宿主独占的副本；停用时移除 item 并断开回调，避免宿主保留按钮造成悬空指针。
`chengyin.ico` 是项目自制 C/水波标识，通过 `scripts/generate_windows_icon.py` 可重新生成；
嵌入 TSF 与设置程序，注册 profile 使用 DLL 的图标索引 0。Win11 最终显示形式由系统外壳决定，需实测。

## 原生界面与回归

设置采用六个 WC_TABCONTROL 页面和标准控件，文字按 DrawText 实际高度排列。
Edit/RichEdit/密码测试框保持 HWND，不因切换页、窗口大小或 DPI 改变而丢失内容；设置主程序激活
TSF，并先投递真实键事件，再处理对话框 Tab 导航。测试框没有自动保存功能。
候选用矩形边框、系统高亮色、被动 HWND 与复用绘图缓冲区；不可逐键重建窗口或抢输入焦点。
候选的离屏 DC/bitmap 在首次绘制时创建。GDI 回归以预热并刷新绘制批次后的对象数为基准，
连续 150 次重绘不允许对象数增长；Wine 的 GetGuiResources 返回 0，资源计数结论以原生 Windows 为准。

六个 CTest 分别覆盖 live config/语言栏、UI 布局、设置与词库、键位/候选、COM、TSF 编辑。
TSF fixture 只在测试 DLL 编译私有配置接口，使用相同生产发布逻辑，验证中途更新保留原组合、
下一次在同一应用中使用新词库；测试不读取开发者词库或写入个人学习。
故障 DLL 与 fixture DLL 不进入安装包。UI 回归检查六页控件不重叠、说明完整高度、150% DPI、
滚动与测试框内容保留；截图可通过 `windows_ui_test.exe <截图目录>` 生成。

安装器测试会实际改变注册和文件，仅在隔离 Windows runner 或独立 Wine prefix 运行：

```powershell
python scripts/test_windows_installer.py --package build/packages/chengyin-windows-x64-0.1.0-preview8-msvc.exe --build-dir build/windows-msvc --makensis (Get-Content -LiteralPath build/nsis-compiler-path.txt -Raw).Trim()
```

加 `--previous-package <旧版EXE>` 检查真实旧包升级。测试覆盖同版修复、外来注册保护、
故障注册回滚、占用 DLL、降级拒绝、用户数据/未知文件保留、旧独立测试程序移除和卸载。
安装包验证 PE64、完整 SHA-256 清单、静态运行库和许可，不把编译工具下载到用户机器。

## 提交与续作

提交源码、词库来源与文档；`build/`、`target/`、临时 SDK、日志和安装包均由 `.gitignore` 排除。
每次功能批次更新 STATUS、任务清单、已知兼容边界；先通过相关自动化，再集中交付一个安装包。
CI 定义不等于 CI 已执行，Wine/模拟宿主不等于实机桌面；分别报告证据。
后续优先项见 ROADMAP：Win11/Notepad3 实测、首拼性能与输入质量评估，然后双拼与其他架构/平台。
