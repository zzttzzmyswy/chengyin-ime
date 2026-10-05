# 开发指南

preview8–16 阶段已结项；开发与交付定位见 [结项记录](WINDOWS_PHASE_WRAPUP.md)。
preview8–15 的实现细节见 [历史归档](history/DEVELOPMENT_PREVIEW8_15.md)。

## preview19 旧学习可信度

见 [本批清单](WINDOWS_PREVIEW19_TASKLIST.md)。Profile.has_repeated_evidence 同时检查累计次数 >=3
和衰减后的近期 hits >=3；Session 在缺少这项证据时查询 dictionary.attests，缓存仍随
profile/dictionary 快照、匹配 flags 和输入预算失效。未知组合的 v1 高计数不能取代近期选择，
可证明的词语保留旧偏好，查询不修改学习数据。两种匹配模式及模糊历史共用门槛，核心没有词语特例。

## preview18 审查修复与后续批次

详细问题、复现和边界见 [审查报告](CODE_REVIEW_2026-10-05.md)，顺序见 [修复计划](REVIEW_REPAIR_PLAN.md)。
声母索引尊重显式分段，只把逐字单头交给同字数优先；a/e/o 零声母头允许进入索引，
已可完整解析的含元音全拼保留既有流程。未分隔的规范读音初始化时按字数约束 DP，
多个同字数合法解析仍无法仅凭拼音确定实际读音，需后续来源证据/规范化。

末尾分隔只有已完成 terminal 且没有真实下一音节边时才能被 cursor 吸收；学习键去尾部分隔。
学习破坏性 generation 由规范路径 SHA-256 隔离，不能与将来的普通 profile revision 合并。
配置 fixture 通过编译定义切换到 PID 隔离通道，生产 DLL/设置仍使用原通道。
更新器以 release 标签构造唯一文件名和完整 URL，解析和下载后 image 验证共用绑定检查。
文件/平台操作保留在 Windows 适配层，C ABI 和词库/profile 格式保持兼容。

## preview17 完整声母缩写优先

2026-10-05，任务见 [WINDOWS_PREVIEW17_TASKLIST](WINDOWS_PREVIEW17_TASKLIST.md)。
对至少两个纯声母字母（允许撇号）、且不是单个完整音节的输入，先查共享不可变的首字母索引。
索引只收录合法完整音节解析且音节数等于 Unicode 字数的整词，按输入每字一个首字母精确查询；
`zh/ch/sh` 的组合声母解释不挤占同长度逐字首字母词条。含元音的全拼、混输和错拼走原匹配流程。
词库初始化建立排序 key/group/word-id 三表，不改二进制词库格式；运行时二分查询，不扫描全词库。
完整组保留所有词条 ID，避免旧 reset_fast 的 256 状态预算把稀有四字词提前裁掉。

完整组存在时跳过纠错组句阶段，只输出对应字数的整词及最多两项准确历史，历史按同样字数过滤；
缩写学习可通过词库读音见证一次提权，显示标准读音但没有错拼标记。无完整组则保留原回退。
分页复用懒加载结果缓冲；会话增加 8 bytes 至 64,902 bytes，初始化后按键/候选路径仍零分配。
清空、编辑及词库切换重新计算模式，历史缓存随词库变化失效。C ABI 与 Windows 宿主合约未改。
本机 Rust 73 项、C ABI /W4 /WX、Windows 七项 CTest、真实词库候选窗口/选择提交检查通过。

## preview16 立即分段上屏

2026-10-05：见 [WINDOWS_PREVIEW16_TASKLIST](WINDOWS_PREVIEW16_TASKLIST.md)。
Session 增加默认 false 的 incremental 模式；Windows bind 时显式开启。
默认 C ABI/核心保留过去的可撤销暂存段行为；新 configure_incremental 只在 idle 接受 0/1。
模式 1 的部分选择同时提供 commit（仅所选文字）和 preedit（剩余 raw），宿主必须将 commit 排除在 composition 外。

前缀结束点为 u64 位图，由共享词库 DAG 的准确边生成，排除完整输入和单个完整音节。
同音候选复用一个游标逐结束点懒加载，不复制词库或增加第二个游标；保留不同消耗范围的同字候选。
完整匹配/历史仍在原两线内排序，前缀为独立末级，按消耗长度降序和词库顺序排列。
选择前缀后 raw 删除 consumed（包含末尾音节分隔符），从新起点 refresh，学习键只保存本次选择拼音。
未获得宿主确认前不修改 Profile；成功 learn_commit 后使缓存失效并刷新剩余候选。
所有 prefix 缓冲预分配，会话 inline + capacity 64,894 bytes < 64 KiB；没有按键分配。

Windows 一次 SetText 写 commit+preedit，再将 composition 起点 ShiftStart 到 commit 后的 UTF-16 边界。
已有选择用剩余 range 对照 core preedit_cursor，OnEndEdit 仅核对剩余 raw；Escape 删除剩余 range。
若文本接受而 ShiftStart/GetRange 失败，用有效范围末端恢复光标，吞掉选择并结束状态，不记录学习。
测试发现短输出后旧光标超界，现已补上恢复并回归下一段输入；不会重新派发已经改动宿主的选择键。
数字、鼠标及宿主候选 Finalize 统一 editKey 路径；异步去重、代际检查和敏感字段透传保留。


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

七个 CTest 分别覆盖 live config/语言栏、更新与主题、UI 布局、设置与词库、键位/候选、COM、TSF 编辑。
TSF fixture 只在测试 DLL 编译私有配置接口，使用相同生产发布逻辑，验证中途更新保留原组合、
下一次在同一应用中使用新词库；测试不读取开发者词库或写入个人学习。
故障 DLL 与 fixture DLL 不进入安装包。UI 回归检查八页控件不重叠、说明完整高度、150% DPI、
滚动与测试框内容保留；截图可通过 `windows_ui_test.exe <截图目录>` 生成。

安装器测试会实际改变注册和文件，仅在隔离 Windows runner 或独立 Wine prefix 运行：

```powershell
python scripts/test_windows_installer.py --package build/packages/chengyin-windows-x64-0.1.0-preview16-msvc.exe --build-dir build/windows-msvc --makensis (Get-Content -LiteralPath build/nsis-compiler-path.txt -Raw).Trim()
```

加 `--previous-package <旧版EXE>` 检查真实旧包升级。测试覆盖同版修复、外来注册保护、
故障注册回滚、占用 DLL、降级拒绝、用户数据/未知文件保留、旧独立测试程序移除和卸载。
安装包验证 PE64、完整 SHA-256 清单、静态运行库和许可，不把编译工具下载到用户机器。

## 提交与续作

提交源码、词库来源与文档；`build/`、`target/`、临时 SDK、日志和安装包均由 `.gitignore` 排除。
每次功能批次更新 STATUS、任务清单、已知兼容边界；先通过相关自动化，再集中交付一个安装包。
CI 定义不等于 CI 已执行，Wine/模拟宿主不等于实机桌面；分别报告证据。
后续优先项见 ROADMAP：Win11/Notepad3 实测、首拼性能与输入质量评估，然后双拼与其他架构/平台。
