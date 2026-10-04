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

六个 CTest 分别覆盖 live config/语言栏、UI 布局、设置与词库、键位/候选、COM、TSF 编辑。
TSF fixture 只在测试 DLL 编译私有配置接口，使用相同生产发布逻辑，验证中途更新保留原组合、
下一次在同一应用中使用新词库；测试不读取开发者词库或写入个人学习。
故障 DLL 与 fixture DLL 不进入安装包。UI 回归检查六页控件不重叠、说明完整高度、150% DPI、
滚动与测试框内容保留；截图可通过 `windows_ui_test.exe <截图目录>` 生成。

安装器测试会实际改变注册和文件，仅在隔离 Windows runner 或独立 Wine prefix 运行：

```powershell
python scripts/test_windows_installer.py --package build/packages/chengyin-windows-x64-0.1.0-preview7-msvc.exe --build-dir build/windows-msvc --makensis "C:\Program Files (x86)\NSIS\makensis.exe"
```

加 `--previous-package <旧版EXE>` 检查真实旧包升级。测试覆盖同版修复、外来注册保护、
故障注册回滚、占用 DLL、降级拒绝、用户数据/未知文件保留、旧独立测试程序移除和卸载。
安装包验证 PE64、完整 SHA-256 清单、静态运行库和许可，不把编译工具下载到用户机器。

## 提交与续作

提交源码、词库来源与文档；`build/`、`target/`、临时 SDK、日志和安装包均由 `.gitignore` 排除。
每次功能批次更新 STATUS、任务清单、已知兼容边界；先通过相关自动化，再集中交付一个安装包。
CI 定义不等于 CI 已执行，Wine/模拟宿主不等于实机桌面；分别报告证据。
后续优先项见 ROADMAP：Win11/Notepad3 实测、首拼性能与输入质量评估，然后双拼与其他架构/平台。
