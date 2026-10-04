# 开发记录 / 下次从这里继续

更新：2026-10-05。当前批次：**preview9 候选主题整体美化已实现；本机原生七项 CTest 通过，六主题纵排/横排/大字号与设置预览截图已检查。用户安装的 preview8 注册及完整负载一致性通过；新 Notepad3 确认加载 preview8。**

交付：`build/packages/chengyin-windows-x64-0.1.0-preview9-msvc.exe`，4,314,737 bytes；
SHA-256 `c0b70b5f03fcde949492484a9742b4c2190fb8673d50f48c8888a00a5b031608`。
原生打包预检查、7-Zip、解包全负载哈希/PE64/COM、许可 ZIP 与最终三二进制逐字节一致性通过。
完整摘要 build/delivery-preview9.json；fixture DLL 和开发测试 EXE 不在包中。
本机卡巴斯基对 build-only windows_desktop_test.exe 提示 VHO:Trojan-Dropper.Win32.Convagent.gen；
该文件的源码未改（进程内 COM 工厂），本批只新增共享主题库链接。七项 CTest 仍全部通过，
产物完整性核对通过；检测分类尚未独立排除，未改杀毒配置，不将校验结果当成安全扫描结论。

## preview9 主题造型与验证

任务见 [WINDOWS_PREVIEW9_TASKLIST](WINDOWS_PREVIEW9_TASKLIST.md)。新增共享 GDI+ 矢量绘制器，
同时供候选窗和设置预览使用。系统保留经典矩形和动态亮暗；白为浅色卡片，黑为暗色金边高亮。
Deepseek 大肥鱼有原创胖鲸、海浪与气泡；初音未来有双马尾头像、耳机、音符与电子线路；
洛天依有灰发绿眼头像、云纹与飘带。角色主题各有背景、边框、选中态与序号徽章，
并非只有配色。无第三方人物图片，原创代码采用项目 MIT 许可。

保留候选字号、紧凑间距、纵向右侧/横向下方的回退拼音，内嵌可编辑时仍隐藏候选拼音。
主题装饰区紧凑 30px/标准 36px，按 DPI 缩放；窄候选只显示居中图案，不强制加宽。
原生回归覆盖全部主题的横排/纵排、18/36px 字体、点击区、非激活焦点、圆角切换及每主题 300 次重绘。
96/120/144/192/288 DPI 为布局通知与离屏矢量绘制模拟；不是物理混合 DPI 多屏验收。
主题预览切换只改测试窗口草稿，未保存个人设置、未写个人学习数据。

安装后的 preview8：COM 注册指向 0.1.0-preview8；安装文件清单、COM 探测和三个二进制
逐字节一致性通过。原有两个 Notepad3 进程仍加载 preview7，未关闭用户窗口。
新建独立 Notepad3（独立 INI/测试文档）加载 preview8 并显示候选；Space 未转中文，
保存文本为 `ni `（含尾随空格）；空闲 Shift ^/J 在该传统宿主未输出，不能把以前的
Edit/RichEdit 结果泛化为 Notepad3 验收。preview9 修正 OnTestKeyDown 因 Shift 待释放
状态误报拦截的问题：直接透传空闲组合，并仅保守取消切换。新增只有测试回调、没有
OnKeyDown 的 IMM/TSF 宿主回归，^&J 三种组合通过。完整新 Notepad3/安装升级实测另验。

原生构建与测试日志：build/native-build-preview9.txt、native-test-preview9.txt；
截图 build/theme-preview9-captures/；安装负载检查 build/installed-payload-preview8.json。
Windows 11 build 26200、Ryzen 9 9950X、MSVC 19.44.35229、SDK 10.0.26100、CMake 4.4.3。
共享 Rust 核心/C ABI 未改，沿用上一批的 38 项集成测试、fmt/clippy 和原生 C ABI 通过证据。
本轮没有新远程 CI，不能复用 preview7 的安装生命周期当作新版本已验证。

下一步：新安装 preview9 后复测 Notepad3 Shift、空格尾随和整段编辑；在干净隔离 runner
验证安装/修复/升级/卸载；物理混合 DPI 多屏、浏览器/WinUI/UWP/管理员/RDP 应用矩阵；
发布可校验 Release 后验证更新下载/安装。继续独立核心性能与语料质量，随后双拼及其他平台。

以下 preview8 记录是该批交付时的历史，安装状态以本节的新观察为准。

更新：2026-10-05。当前阶段：**preview8 十一项反馈已实现；本机 Win11/MSVC 七项 CTest、Rust 38 项集成测试、fmt/clippy 与原生 C ABI 检查通过。真实设置 Edit/RichEdit 的输入、原文空格、Shift ^&J、内嵌时隐藏候选拼音及启动焦点通过。单 EXE 打包和解包负载/COM 校验通过。不能把模拟 DPI 和本机两类控件写成任意应用/多屏 100% 兼容。**

## 最新批次：preview8

任务见 [WINDOWS_PREVIEW8_TASKLIST](WINDOWS_PREVIEW8_TASKLIST.md)，发行说明见
[CHANGELOG](../platforms/windows/CHANGELOG.md)。候选按字宽/字体指标布局，取消最小 240px
宽度和累计增长；紧凑边距 3px，标准 7px，实际按 DPI 缩放，候选字号保持。
候选拼音在纵向右侧/横向下方，至少 16px；能内嵌编辑时隐藏重复显示。
TSF 验证同一组合原文和选择范围后支持鼠标移动光标继续编辑；浮动上下文保留回退拼音。

七页原生设置新增主题：系统、白、黑、Deepseek 大肥鱼、初音未来、洛天依，
后者为原创配色。系统主题动态读取 AppsUseLightTheme，响应设置/颜色通知；高对比度优先。
所有控件切换新字体后再销毁旧句柄，避免鼠标悬停触发悬空句柄导致字体变化。
多页长文本按实际高度布局，窄窗口纵横滚动；候选按光标所在屏幕 DPI 排版并限制工作区，
极小工作区可滚动候选。主题预览也按字体指标排版。

删除“替换词库”界面：列表可添加、启停、删除独立导入项，内置词库保留；
相同内容去重、最多 64 项/64 MiB、旧文件原子迁移、陈旧窗口变更拒绝，损坏文件保留。
各上下文仍独立会话并引用不可变词库。共享 Rust 核心和 C ABI 事件/所有权不变。
Windows Space 仅改平台映射为原文提交；数字/鼠标选中文。Shift 组合保持键盘布局符号/大写，
单独 Shift 仍切换。关于新增 MIT 全文、仓库、离线发行说明、自动检查更新开关与按钮。
只查询本仓库 Releases，下载前验证更高版本及本仓库 URL，启动前验证 SHA-256/PE64 EXE。
真实 WinHTTP 查询成功，当前没有发布可校验的新安装包，不能冒称远程升级安装已测。

交付：`build/packages/chengyin-windows-x64-0.1.0-preview8-msvc.exe`，4,299,339 bytes；
SHA-256 `d1141b206f8f82a44f21bb4e208b931f3b33db1e9a061e39f91392421abc888b`。
打包器原生预检查、7-Zip 完整性、解包后的全负载哈希/PE64/COM 探测通过；三个运行二进制与最终构建逐字节一致，许可 ZIP 通过。
交付摘要 `build/delivery-preview8.json`，安装器未在当前用户系统安装，本轮正常系统升级/安装生命周期仍待专门执行。

## preview8 验证证据

- Windows 11 build 26200，Ryzen 9 9950X 16C/32T；MSVC 19.44.35229，SDK 10.0.26100，Rust 1.99，CMake 4.4.3。
- 七个 CTest：配置通知/语言栏、更新解析校验、七页 UI、词库/学习、按键/候选、COM、TSF 编辑故障与状态回归全部通过。
- UI 回归：96/120/144/192/288 DPI，所有页不重叠，窄视口纵横滚动，Edit/RichEdit/密码框保留，字体重建与悬停一致；六主题与紧凑候选截图人工查看。
- 当前真实桌面：build-only 进程内工厂加载新 fixture DLL（同一服务代码、人工词库/无个人历史），实测 Edit/RichEdit；不改系统 DLL 注册、不写个人学习数据；fixture 不进包。
- 核心：38 项测试，零分配回归、fmt/clippy、release，VS 原生 ffi_smoke；日志 build/rust-test-preview8.txt、rust-clippy-preview8.txt、native-core-check-preview8.txt。
- 原生构建/测试日志 build/native-build-preview8.txt、native-test-preview8.txt；截图 build/ui-preview8/；桌面工厂激活 HRESULT 0，日志 build/desktop-stderr.txt。
- Windows 核心独立基准：日常逐键 P50/P95/P99 70.3/280.3/308.2µs；首拼混输 213.9/1276.7/1774.7µs；连续上下文 1037.2/2714.0/3574.4µs。不含 TSF/显示或准确率，完整范围与样本见 PERFORMANCE。
- 新远程 CI 未运行；preview7 GitHub CI 的六个 job/安装生命周期记录在下方历史。

下一步：在干净隔离 runner 验证 preview8 安装/修复/升级/卸载；物理混合 DPI 多屏，
Notepad3/浏览器/WinUI/UWP/管理员/RDP 的完整应用矩阵；发布带 digest 的 Release 后
验证新版本下载和安装全链路。继续独立核心性能、语料质量，随后双拼与 ARM64/x86/Android。

以下为 preview7 及以前历史，不代表本轮新执行证据。

更新：2026-10-04。当前阶段：**澄音 preview7 的七项任务已实现并推送指定 GitHub main。核心检查、六个 Windows/Wine CTest、六页/DPI 截图检查和旧 preview6 升级通过；GitHub CI 六个 job 全部通过，包含 Windows 2022/MSVC 原生六项 CTest 和完整安装生命周期。交付通过该 CI 的单个 MSVC 安装 EXE。真实 Win11 任务栏与 Notepad3 桌面验收仍待完成。**

## 最新批次：preview7

本轮七项任务见 [WINDOWS_TASKLIST](WINDOWS_TASKLIST.md)。设置改为六个经典原生选项卡，
按文字实际高度布局；修复字号标签重叠、Tab 背景盖住内容面板、分组框黑底和跨页残影。
候选保留稳定 HWND/双缓冲，改为矩形边框与系统高亮色。
输入测试移入设置，保留标准 Edit/RichEdit/密码框和真正的 TSF 消息循环；升级删除旧独立 EXE/入口。

配置成功保存后发布共享版本号，后台读取后回 TSF 主线程应用；旧应用不需要重开。
当前拼音保留原词库/候选策略完成；下一次输入使用新词库。默认模式不覆盖当前状态。
增加语言栏 INPUTMODE 按钮、SYSTRAY 注册类别、澄音图标和中/英状态通知；
中文标点只在宿主成功写入后推进引号配对，保留手工音节分隔及翻页。

交付包：`build/packages/chengyin-windows-x64-0.1.0-preview7-msvc.exe`，4,312,794 bytes，
SHA-256 `9c8f4d22e324808a5a378b93856ceda0cee8d1cd6931cb222e67835e0a5faa3d`。
源码提交 `b14116e`；[通过的原生 CI](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37212769471)。
已取回 artifact 并验证 ZIP/EXE 哈希、大小与 PE64；原 GNU 内部 preview7 包保留。
验证日志位于忽略的 `build/core-check-preview7.txt`、`build/windows-test-preview7-final.txt`、
`build/ui-preview7.txt`、`build/installer-preview7-final.txt`；原生验证摘要
`build/native-ci-preview7.json`、同一交付 EXE 的旧版升级日志
`build/installer-preview7-msvc-from6.txt`；截图位于 `build/ui-preview7/`。
原有 preview3/4/5/6 安装包保留。README、DEVELOPMENT、ENVIRONMENT、AGENTS 与路线图已更新。

后续优先：Win11 任务栏与 Notepad3 实机、多屏 DPI/现代宿主、
首拼/上下文性能与独立语料质量；随后双拼、ARM64/x86 与 Android。以下保留历史开发记录。

## 用户已确定

- 长期目标：极致性能与兼容性，覆盖 Windows、Linux X11、Linux Wayland、Android。
- 全拼优先，随后双拼。最初优先 Linux 桌面；用户当前方便使用 Windows，已改为优先 Windows 桌面试用版。
- 可以详细规划并分阶段持续开发。用户测试机会很少：首个集中实测包必须包含日常词库、整句、翻页、中间编辑、分段、Windows 设置和宿主候选接口；先完成实现及内部回归，不再交付小演示包等待反馈。
- Windows 交付必须为单文件或单文件安装包；当前选择单个离线安装 EXE。
- 已要求：预载许可明确的开源常用词，兼容搜狗词库，增加智能联想与连续首拼组词组句。
- 本轮九项反馈：完整美化设置、中文字体、候选闪烁/样式、习惯排序、传统输入框触发、左 Shift、显示分隔及 xi'an 手工分隔。
- 用户定位反馈：本项目输入测试与 Notepad3 无候选，ChatGPT 应用可以触发；未给 Notepad3 版本/架构。实际 Windows 复测不能由 Wine 模拟代替。

## 已落地

- Rust workspace：`myswy-core`、`myswy-ffi`、`myswy-cli`；没有第三方 crate。
- 扁平 UTF-8 池、连续 trie、完整同音 terminal、最优后代游标按需分页、完整匹配优先、文本去重；支持显式/省略/部分省略音节分隔符。
- 有界会话状态机，预留缓冲区，输入/歧义超限保留原状态；Ctrl/Alt/Super、取消、原文上屏、选择和 ASCII 标点透传。
- C ABI v1：静态库/动态库、词典共享、会话生命周期、UTF-8 缓冲区、空指针处理、panic 隔离。
- 87,540 条开源日常词条：65,125 条 Rime/Apache-2.0，加 22,415 条 jieba/MIT；固定来源/哈希与原始许可，离线转换/二进制编译。98 条手写 MIT 示例保留用于回归。
- 有界 unigram Viterbi 整句、未完音节预测、原文尾部回退、前后页/中间编辑/分段选择和撤回。
- 首拼、zh/ch/sh 声母、全拼混输、连续缩写组句；小型离线搭配与词库续词、上下文排序、会话内最近选词偏好，失焦/重置清除，不保存输入历史。
- 安全 Rust 导入器：经典搜狗 SCEL 0x44/0x45、Unicode 文本及 v1/v2 二进制；Windows 额外支持 GBK，追加/替换/恢复与原子保存；C ABI v1 增加导入/合并/导出与联想 getter。
- Fcitx 5 C++ 插件：逐上下文状态、候选 UI/鼠标选择、client/panel preedit、释放事件、重置/停用、敏感输入、陈旧候选保护、长度提示。
- Fcitx 原生词典配置：单工作线程读取和构建、最新请求优先、加载失败保留旧词典且不覆盖磁盘设置、活跃组合结束后切换、旧词典后台回收。
- C ABI v1 新增演示词典句柄与空闲会话换词典函数，复用缓冲区并保留上一事件 commit；活跃组合返回 BUSY。
- Debian 原生打包脚本：自动解析实际 ELF 依赖；隔离根目录中验证完整安装/卸载生命周期；不改系统输入法或用户配置。
- Windows x64 TSF：COM 工厂/生命周期、同步编辑锁、逐上下文会话、预编辑与选词、ASCII 标点一次写入、焦点/宿主失败处理、密码/私密/PIN/只读绕过。
- Windows 不抢焦点的候选窗、鼠标选词及延迟/陈旧点击保护、Ctrl+Space 中英文切换、布局通知后的只读刷新、DPI 字体缓存、拥有者窗口销毁后重建。
- Windows 原子词库导入/恢复设置、激活时共享快照、无效加载回退；宿主候选接口支持读取、重排、高亮、提交、取消和重入保护。
- Windows 单 EXE 离线安装器、系统卸载/开始菜单入口、版本目录升级、同版修复、故障回滚、文件校验及用户文件保护；COM/模拟编辑探测、双编辑框/密码框程序、原生 MSVC CI。旧 PowerShell/ZIP 仅保留历史预览。
- 路线图、架构、兼容矩阵、性能预算、Linux/Windows 指南、CI 配置。

## 本地验证证据

环境：Debian 13 x86_64，GCC 14.2，CMake 3.31.6，Fcitx 5.1.12-2。

| 检查 | 结果 |
| --- | --- |
| `cargo fmt --all -- --check` | 通过 |
| `cargo clippy --workspace --all-targets -- -D warnings` | 通过，Rust 1.99 |
| `cargo test --workspace --locked` | 38 项集成测试通过（含首拼/SCEL/上下文、深页持久学习、分隔与长历史排序） |
| `cargo +1.82.0 test --workspace --locked` | 同样通过，最低 Rust 版本已实际检查 |
| `bash scripts/check.sh` | 通过，含 release 构建、C ABI 程序、CLI 演示 |
| 核心堆分配计数 | 初始化后 100 轮示例与真实词库整句/局部编辑/前后页/首拼/联想/reset，共 0 次分配；会话小于 64 KiB |
| 参考实现对照 | 300 条生成词典，多种拼写及所有非空前缀与全量扫描结果一致 |
| C ABI 程序 | 空句柄、非法 UTF-8/字段/候选索引、小缓冲区不写、上屏生命周期、自定义词典引用存活；导入/合并/导出与首拼/联想 |
| `cargo check`：Android arm64、Linux arm64 | 通过；仅构建检查，未链接/运行原生平台组件 |
| Windows GNU x64 TSF | Rust 1.99 / MinGW GCC 14.2 实际链接 PE64 DLL/EXE；静态链接核心与工具链运行时，仅依赖 Windows 系统 DLL |
| Windows 原生代码在 Wine 中 | Wine 10 + Xvfb，CTest 5/5：Unicode/GBK/SCEL 追加与失败保留、按键/UTF-16/候选窗生命周期、COM/TSF、模拟编辑/首拼/联想/隐私/宿主 UI 与故障回归 |
| Windows 注册与注销入口 | 隔离 Wine 前缀中通过首次注册、已注册 COM 创建、重复拒绝、不同 DLL 路径拒绝卸载、注入缺失 TSF profile COM 类时的失败回滚、清理和 regsvr32 往返；不代表真实 Windows 系统列表验收 |
| PowerShell 语法 | PowerShell 7.4.7 Linux parser 通过四个脚本；Windows 5.1 的实际安装/卸载尚待验证 |
| Windows 单 EXE | NSIS 3.11 原生 amd64，PE64/依赖校验与完整负载 SHA-256；隔离 Wine 中完整安装生命周期通过；preview1 ZIP 为历史产物 |
| Fcitx 原生模块与无头集成测试 | CTest 2/2 通过：事件测试及 14 阶段异步热切换/保存失败回归 |
| Fcitx 暂存安装 | 三个原生文件和两个文档文件；没有安装到系统或更改输入法配置 |
| C++ ASan / UBSan | 同样 CTest 2/2 通过；Rust 静态库和系统库未插桩 |
| Debian 测试包 | 隔离 dpkg 安装/重装/升级/回滚/移除/purge 通过，五个文件校验一致且用户配置保留；跳过依赖安装 |
| 插件导出与动态依赖 | 包含 `fcitx_addon_factory_instance`；暂存模块动态依赖解析通过 |
| 性能基准 | 各场景次数单列；全拼/混输/连续上下文、查询、整串和翻页分别测量，见 PERFORMANCE |

本次无头回归发现并修复：构造 `Text("")` 在框架内部不等同于空文本容器，可能使面板保持非空；清理后只在预编辑非空时设置 Text。测试覆盖正常上屏、无 client-preedit 客户端和密码字段清理。

CI 已配置 Linux/Windows 原生 Rust/C ABI、最低 Rust 版本、ARM 构建检查、Fcitx CTest、sanitizer、Debian 打包/生命周期测试及构建产物保存；另有 Windows TSF 构建、五个 CTest、隔离 runner 安装/修复/升级/回滚/卸载和 EXE 保存。**此处为历史记录；最新 GitHub/CI 状态见文档开头**，不能把 CI 配置写成远程测试通过。

## 本轮：词典配置与测试包

词典测试使用真实 Fcitx 事件循环，验证加载期间继续输入、旧组合保留、共享新词典、输入后删除源文件仍能工作、失败不保存、缺失/FIFO/目录/过大/损坏/重复词条拒绝、最新请求优先、手工配置重载和后台回收。新增持久化失败测试发现框架 `safeSaveAsIni` 未报告重命名失败，已改为逐步校验写入与原子替换。

开发包位于 `build/packages/fcitx5-myswy_0.1.0-1_amd64.deb`，目标 Debian 13 amd64；依赖 libc6 ≥2.39、Fcitx Core/Config/Utils ≥5.1.12、libstdc++6 ≥13.1。默认维护者为明确的开发占位地址，正式分发前需配置真实维护者。包内仅模块、两个注册文件和两个文档，没有 maintainer scripts，不会自行重启输入法。

打包和测试用法见 `platforms/fcitx5/README.md`。环境曾重新连接，文件、SDK 与构建产物均保留，安装生命周期测试已在恢复后重新验证。

## 本环境复现说明

系统没有预装 Rust/Fcitx SDK；Rust 工具链在当前用户目录。Debian 官方签名软件包只下载并解包到 `/tmp/myswy-sysroot`，没有 root 安装。解包 SDK 的三个 CMake imported target 的绝对 include 路径被本地重定位到此目录；该临时修正不是源码改动。

```sh
. "$HOME/.cargo/env"
bash scripts/check.sh
LD_LIBRARY_PATH=/tmp/myswy-sysroot/usr/lib/x86_64-linux-gnu \
  /tmp/myswy-sysroot/usr/bin/cmake --build build/fcitx5
LD_LIBRARY_PATH=/tmp/myswy-sysroot/usr/lib/x86_64-linux-gnu \
  /tmp/myswy-sysroot/usr/bin/ctest --test-dir build/fcitx5 --output-on-failure
```

临时 SDK 可能随工作环境消失。普通开发机使用平台 README 的标准安装命令，代码不依赖这些临时路径。

解包 SDK 的打包依赖查询使用 `/tmp/myswy-apt/shlibs.local`，内容来自三个官方 Fcitx 运行库包的 `shlibs` 元数据。复现命令：

```sh
LD_LIBRARY_PATH=/tmp/myswy-sysroot/usr/lib/x86_64-linux-gnu \
  python3 scripts/package_deb.py --cmake /tmp/myswy-sysroot/usr/bin/cmake \
  --library-dir /tmp/myswy-sysroot/usr/lib/x86_64-linux-gnu \
  --shlibs-local /tmp/myswy-apt/shlibs.local
python3 scripts/test_deb.py build/packages/*.deb
```

已有同名包时脚本拒绝覆盖；重新构建可指定新 Debian revision。正常安装 SDK 的开发机不需要这两个额外搜索参数。

## 本轮：Windows x64 TSF 预览

按用户新优先级实现 `platforms/windows`，复用原 C ABI，当前没有新增 Rust crate。按键测试回调无副作用；每次输入只在同步写锁成功后处理核心。候选文本和标点同次写入，宿主文字已写入后不因光标设置失败而重新透传。焦点切换保留旧框已经显示的原拼音，再释放旧 Session。

布局未就绪时暂时隐藏候选；收到布局通知后申请只读编辑会话刷新候选位置。每个上下文最多一个待处理 UI 更新，退役组合的回调用 generation 拒绝，不插入任何迟到文字。模拟宿主验证同步锁拒绝、SetText/SetSelection 失败、外部光标移动、上下文隔离、密码/PIN 绕过、布局只读刷新/陈旧回调、按键释放隔离以及全部 COM 引用释放。

注册测试发现 MinGW 的 `HRESULT_FROM_WIN32` 宏会多次求值；将有输出参数的注册表 API 放入宏中会创建两次、把首次创建误判为“已存在”。现先保存 Win32 返回值再转换，首次/重复注册及卸载保护回归通过。注册阶段逐项记录已开始的操作，失败时只补偿对应步骤；隔离 Wine 中暂时移除并恢复内置 profile COM 类，验证出错不留下 Myswy COM 注册，再恢复正常注册。候选生命周期测试也覆盖父窗口销毁后清空句柄，避免使用失效 HWND。

试用包：`build/packages/myswy-windows-x64-0.1.0-preview1-gnu.zip`，目标 Windows 10 2004+/11、Intel/AMD x64 经典桌面应用。包未签名；安装复制到 Program Files 并注册 COM/TSF，需要用户在管理员 PowerShell 中运行脚本。正常输入不需要管理员。包内 `BUILD_INFO.json` 和 `SHA256SUMS.json` 记录构建来源与每个文件的校验值。完整步骤见 `platforms/windows/README.md`。最终包为 2,023,627 bytes，SHA-256：`647b07185b8b991b26a27f2be0f165875731a25a3581e8015a7a9f065e48c16f`。

本地 Wine 验证使用单独前缀，未修改真实 Windows 注册。远程 CI 未运行；实际 Windows 系统的输入法列表、注销后启用、应用兼容、焦点/候选位置和 PowerShell 生命周期仍待实机确认。当时 UI-element-only/WinUI、安全模式、ARM64、32 位宿主、鼠标候选点击与 Windows 自定义词典设置尚未实现；后续点击已补齐，preview3 又完成日常词库/整句/翻页/编辑、设置和宿主 UI 接口；其他架构与实际应用验收仍待做。

## 本轮：Windows 单 EXE 与集中实测策略

用户新增要求已写入 AGENTS、ROADMAP 和 WINDOWS_RELEASE_GATE：Windows 单文件交付，先完成较完整的一批功能/自动化后再集中实测。

使用 NSIS 3.11 原生 amd64 Unicode stub 和 LZMA，输出一个离线 EXE；负载静态链接工具链运行时，包含完整许可与 SHA-256 清单，无需用户解压、安装运行库、运行脚本或联网。安装创建开始菜单和 Apps & Features 卸载入口，默认输入法不变，程序不自动注销/重启。

安装先校验和只读 COM 自检，再复制新版本目录、切换注册和卸载入口；旧版本保留供回滚。同版文件一致时修复注册，支持缺失 COM 类的恢复；内容不同、降级、未知注册、链接目录拒绝修改。升级/卸载仅移除已知文件，占用文件用 NSIS 的延迟删除机制处理，用户文件保留。旧 preview1 ZIP 的标准目录/ASCII CRLF 标记可迁移。新增 DLL 的显式 repair 入口严格检查注册路径。

候选点击不获取焦点；按下/释放校验候选版本，在 TSF 写锁内再次验证上下文/组合/敏感类型。键入新内容、布局销毁或失焦使排队的旧点击失效，重复点击合并；编辑锁失败无核心/宿主变化。Ctrl+Space 在当前服务中切换中英文，原拼音保留；停用后注销保留键并恢复中文。

Windows 服务改用显式共享词典句柄：激活时取得、最后一个服务停用后释放，不初始化进程永久保留的 Rust 演示缓存；池锁不进入按键路径。100 次激活、输入 `nihao`、提交、停用回归验证 COM/context/sink 释放。该回归不等于真实词库内存/端到端性能测量。

当前 MinGW 链接、Wine + Xvfb 三项 CTest、Build.ps1 parser、工作流 YAML 已通过。完整安装生命周期自动化在隔离 Wine 前缀中通过：完整负载校验、已注册 COM、同版修复/缺失类恢复、文件损坏拒绝、注入升级注册失败回滚、外来路径保护、加载 DLL 时升级与延迟清理、降级拒绝、用户文件保留、卸载/重复清理、首次注册失败回滚、旧 ZIP 布局迁移与普通临时副本卸载。验证后 Myswy 的 COM/ARP 注册与产品目录均已清理。测试 fault DLL 仅在测试构建目录出现，不在安装负载中。

内部构建：`build/packages/myswy-windows-x64-0.1.0-preview2-gnu.exe`，1,917,224 bytes，SHA-256 `0c00fc686ea484d5f2bbadb3cfc6e1a436e8391278821c1e4d452643194f2ebb`。仍使用 98 条示例，尚未达到集中实测门槛。Windows 使用说明见 platforms/windows/README.md，后续从下面第 1 项继续。

## 本轮：日常全拼与单 EXE preview3

用户明确要求词库、整句和翻页等完成后再集中测试。本次替换了 top-9 缓存
原型，完成 65,125 条固定 Rime/Apache-2.0 字词、离线导入脚本与版本化二进制。
TSV SHA256 为 be88144a8a5221268c9551184ccc32fb9c06a1ed8588059ff82d918b0d866dc4，
二进制 SHA256 为 31ef9199120d8dabdd78634b7a99cf4432ce306af2ad3ebc84cc9bcb14a8e597。
原始 LICENSE/README/词典/SOURCE.json 保留；上游该修订没有根目录 NOTICE 文件。
安装包内完整词库许可位于 RUNTIME_LICENSES.zip/vocabulary/。

每个终端保留全部同音词，候选游标按需遍历；整句为每位置 16 条路径的
unigram 基线，未完末音节预测、未知尾部原文回退。可键盘/鼠标翻页、在
拼音中间插入删除、选段后保留尾部、在片段边界撤回；光标在核心 UTF-8
与 Windows UTF-16 范围间转换。长文本撤回超限时保留旧状态也有回归。

C ABI 保持 v1，追加二进制构造、句柄复制、页/光标/跨度 getter 和高亮 setter。
Windows 使用嵌入的日常二进制；开始菜单打开设置，导入先校验再原子替换，
激活时检测配置变化，旧服务/组合保留快照，失败保留可用词库或回退内置。
正常按键无字典文件 I/O；大型自定义 TSV 的激活加载仍是同步操作。

宿主候选接口实现读取、重新排版、高亮、提交和取消。UI-only 模式不弹原生
窗口；经典模式尊重宿主是否接管。测试覆盖陈旧/重复 finalize、abort，以及
宿主在 UpdateUIElement 回调内停用服务时的生命周期，保留写入已完成的消费
结果。固定词库测试 DLL 和注册故障 DLL 均不进入安装包。

证据：Rust 1.99 和最低 1.82 的 21 项测试、C ABI、四个 Windows CTest（Wine
10 + Xvfb）、两个 Fcitx CTest 均通过；ARM Android/Linux 核心构建检查通过。
真实词库逐键回放 242,000 次含 60 字节长串，P99 279.342 µs；词库堆容量
5,856,901 字节，内存二进制加载 28.644 ms，会话工作区+预留堆 60,295 字节。
这些是云 Linux 核心测量，不等同 Windows 端到端或 RSS。详见 PERFORMANCE。

当前单 EXE：build/packages/myswy-windows-x64-0.1.0-preview3-gnu.exe，
4,147,614 bytes，SHA256 51233ecdf4f63304b6fe302e768f26b6b0b979bf02e6881ffe76f1b504e87318。
单 EXE 完整生命周期回归通过，包含实际 LocalAppData/dictionary.custom 的升级与卸载保留；测试结束 COM/ARP/产品和测试配置已清理。真实 Windows/MSVC CI 尚未执行；交叉链接及 Wine 不能代替应用矩阵验收。

## 本轮：开源词库、搜狗兼容与现代快速输入 / preview4

新增 22,415 条 jieba/MIT 常用词，连同 Rime/Apache-2.0 共 87,540 条。
固定 jieba 修订 `67fa2e36e72f69d9134b8a1037b83fbb070b9775`，原始字词/许可/
README/哈希保留于 data/sources/jieba；只补频率至少 30 的 2–6 字词且每字读音
在源词库中唯一，不猜多音字。离线重建再次得到相同 TSV 和音节表。
TSV SHA256 `0b3386677e7f98c047a4f839d878b78cb54f0671611b8e3b8a1583189573eb88`，
v2 二进制 SHA256 `b218e5a87acdc1a8b6f586d1c6c31442b2d5a1075fd797265a4b5cccf8676af9`。
v2 允许最长 255 字节规范词条拼音，输入仍为 63 字节；保留 v1 读取。

同一个安全 Rust 解析器供 CLI/C ABI/Windows 设置使用，支持经典 SCEL 0x44/
0x45、UTF-8/UTF-16LE 搜狗文本和本项目词库，Windows 再转换 GBK。设置可
追加、替换、恢复；首次追加保留全部内置词，重复项取最大频率。互斥锁、
唯一临时文件/刷盘/原子替换保证失败保留旧配置；编译结果限 64 MiB/25 万项。
本地公开格式参考 SCEL 实际转换 3,563 条，最长规范拼音 105 字节；样例
只在忽略目录中，不预载或分发。格式边界见 SOGOU_COMPATIBILITY。

首拼/zh/ch/sh 声母/全拼可混输；`wxhzw` 首选“我喜欢中文”，整句由词图
组合。完整输入先解码，缩写备选按需展开；缩写必须沿音节边界继续，不能
在跳过音节尾部后继续匹配音节内部字母。用独立音节别名枚举器验证该边界。
每位置 16 条路径、缩写图 96 状态，预算不足显式提示；完全匹配、整句、
分段、词尾预测分层，上下文排序不会把预测尾词提升到完整句之前。

离线联想来自自写常用搭配及词库中文字前缀，不使用联网服务或训练模型。
当前中文上下文/最近 32 次选词只存在会话内存，失焦/重置/敏感输入清除。
上屏后空预编辑的联想用 Tab/鼠标确认；Space/数字/Enter 等透传，不意外插字。
TSF 使用折叠的上屏后范围定位，不创建空组合；复用宿主候选接口，光标移动、
过期点击、密码作用域、重入与重复提交均有回归。Fcitx 也映射联想/Tab，
Linux 配置界面仍仅加载 TSV，默认示例词库与历史 Debian 包没有改成正式预载。

最终证据：scripts/check.sh、Rust 1.99 与最低 1.82 的 31 项集成测试、扩展
C ABI、Android/Linux ARM 构建检查通过；Windows 交叉链接后 Wine 4/4 通过；
Fcitx 2/2 与 C++ ASan/UBSan 2/2 通过。真实词库热路径 100 轮仍零次堆分配，
单会话 65,031 字节；单独核心微基准全拼逐键 P99 0.642 ms、首拼混输
2.465 ms、连续上下文 5.313 ms，内存词库加载 77.660 ms。逐键延迟和加载
仍需优化，未达到项目目标；口径/原始输出位置见 PERFORMANCE。

已生成单 EXE `build/packages/myswy-windows-x64-0.1.0-preview4-gnu.exe`，
7,028,499 bytes，SHA256 `9c3aea328d11bcbd28b5a44cab7b9f56c029088c5bf5450c3c308cfe6cd923ec`。
旧 preview3 原包/哈希保留。该最终 EXE 在隔离 Wine 前缀的完整生命周期
六阶段通过：首次安装/完整负载校验/注册/同版修复、注入升级失败回滚、
损坏文件与外来注册保护、占用 DLL 升级/降级拒绝/未知文件保护、卸载/
重复清理/首次注册失败回滚、旧 ZIP 迁移/普通系统卸载入口。实际 LocalAppData
词库的升级/卸载保留通过；两项词库的包内许可证/来源记录逐字节核验，
三个正式 PE64 只依赖 Windows 系统 DLL。测试结束 COM/ARP/产品目录与
测试创建的词库文件已清理。真实 Windows 桌面、原生 MSVC 和远程 CI 尚未执行。

## 本轮：设置、候选与持久学习 / preview5

设置拆成五页原生界面：常规、外观、词库、学习与隐私、帮助与诊断。
统一 YaHei UI 字体并在缺失时回退；候选支持 12–32 字号、系统/浅/深主题、
纵向/横向换行、密度、5/7/9 条每页、拼音副行和分隔预览。词库导入在后台
运行，保留草稿/未保存提示、备份/导入/清除与诊断入口。参考仓库/修订与
实际绘制范围见 WINDOWS_UI_REFERENCES；未复制/打包第三方 UI 代码或字体。

候选保留原生 popup 和离屏 DC/bitmap，抑制背景擦除，单次 BitBlt，不逐键
hide/recreate；重复刷新回归检查 HWND、几何与 GDI 资源。原始拼音与光标
不因显示分隔改变，所有合法音节显示对照通过；手工撇号进入真正解码约束。

纯中文选词/整句在宿主 SetText 成功后显式学习，最多 4,096 项，按实际拼写
（含手工分隔）/文字记录频次和序列；深页词可进首屏。Profile 是独立的
Arc 只读快照，CRC/UTF-8/边界/顺序全校验；长历史保持频次优先。Windows
128 项后台队列合并各应用磁盘状态，互斥/刷盘/原子替换；共享 epoch 防止
清除或导入前的排队事件复活。损坏/空备份、锁定文件均保留原数据；缺失
文件才初始化为空。异常退出、存储失败、队列满时未保存选择可能丢失。
配置、学习导入/清除在服务重新激活后应用；不保存应用、位置或时间，不上传。
Fcitx/Android 尚未接入持久存储。

输入测试显式激活 ITfThreadMgr 并使用 TSF message pump/keystroke manager，
在对话框 Tab 导航前分派输入；含标准 Edit、RichEdit 和密码框。
注册补 COM-less/input-mode 类别和中英文 compartments；左 Shift 只在单独
释放时切换，重复/组合键/左右 Shift 同时按下不会误切换。OnEndEdit 对比
当前组合文本/光标，迟到的自身写通知不退役组合；位置回退涵盖非空范围、
Win32 光标、同框有效锚点和当前视图，明确 clipped 则隐藏。
这些是针对用户失败的实现与模拟证据，未宣称真实 Notepad3 已修复。

最终源码回归：scripts/check.sh、Rust 1.99/最低 1.82 的 38 项集成测试、
新增 Profile C ABI/小缓冲区/确认生命周期、ARM Android/Linux 检查通过；
Windows 交叉链接及 Wine 5/5、Fcitx 2/2、C++ ASan/UBSan 2/2 通过。
五页/主题/滚动/DPI 与候选布局在 Wine 实际绘制并检查截图；没有写用户设置。
加载偏好与分隔显示的 100 轮核心热路径仍零分配，会话 65,142 bytes；
4,096 项共享偏好成功上屏确认 P99 46.278 µs（允许分配，不含磁盘），
60 字节分隔 getter P99 38.056 µs。全拼/混输/连续上下文 P99 为
0.641/2.604/5.412 ms，加载 71.918 ms，仍未达到项目性能目标。
原始日志 build/*preview5*.txt，详细口径见 PERFORMANCE。

preview5 原包已生成并通过完整安装生命周期和真实保留 preview4 EXE 的升级：
7,536,403 bytes，SHA256 `e37113b36fc697449aef7ddbdf8c1a267fcc2d82bebd18e98f7ddaf8514dec9e`。
此时用户新增改名与九项核验要求；集中交付包为 **澄音输入法 / Chengyin IME preview6**。
显示名、设置/测试窗口、宿主候选说明、安装向导/系统应用列表/开始菜单和文档
统一更名；保持 CLSID、内部 ABI 与用户目录标识，升级数据继续兼容。
九项逐项结论/证据见 WINDOWS_FEEDBACK_AUDIT；第 6 项仍为实机待复测。

最终单 EXE：`build/packages/chengyin-windows-x64-0.1.0-preview6-gnu.exe`，
7,499,364 bytes，SHA256
`ed5716c9360b852147e00b0ee0be8ab9ce57bbe7eec85f9bcc9805b4670f656c`。
Windows 五项 CTest 通过；原生输入测试实际进程启动、三个控件创建、密码框
完整可见和正常退出检查通过。检查曾发现跨进程创建/显示的测试时序和密码框
底部截断，均已修正。截图检查会等待绘制消息完成，最终路径为
`build/ui-review-chengyin-final/`；这仍是 Wine，不是真实 Windows。

最终 EXE 完整生命周期七段回归通过：真实保留 preview4 EXE 升级，
安装/完整哈希/已注册 COM/同版修复，注入注册失败回滚，改动文件/外来路径
保护，占用 DLL 升级/版本切换/拒绝降级/未知文件保留，卸载/首次安装失败
清理，以及旧 ZIP 迁移/系统卸载临时副本。核对新系统应用名称、四个新菜单
入口、旧已知入口移除，并保留用户自行放入旧菜单的文件。词库/设置/学习
三种数据升级与卸载后字节不变。结束后隔离 COM/ARP、产品、菜单和自有
配置测试文件已清理；旧 preview3/4/5 包的哈希未变。

Wine 的 `reg.exe` 重定向中文为问号，名称检查改用 Win32 Unicode 读取并输出
UTF-8。Wine 10 尚未实现 TSF 名称 getter，因此仅此环境直接检查已保存的
profile Description；原生 Windows 仍要求正式 TSF getter 通过。最终日志：
`build/windows-tests-preview6.txt`、`build/probe-tests-preview6-final.txt`、
`build/installer-preview6-final.txt`、`build/package-preview6.txt`。

## preview6 时的下一步（历史）

1. 本批单 EXE 内部完成后，集中复测真实 Windows 输入测试/Edit/RichEdit、Notepad3、ChatGPT 及常用应用的激活、定位、Shift、习惯排序、缩放和隐私；记录版本/架构。无需测试开发切片。
2. 优化连续上下文候选展开、增量解码、预编译索引；扩大独立中文语料，记录首选率/按键数和反例。当前 P99 与加载目标仍未达标。
3. 推进双拼与 Profile 在 Linux/Android 的存储接入；补真实现代宿主、x86/ARM64 输入 DLL、签名和原生 MSVC CI 执行证据。
4. Linux 保留 Fcitx 插件和后台 TSV 加载；正式预载/导入界面、X11/Wayland 实测继续。Android JNI/IME 服务尚未实现。
5. 专门环境测 Windows 端到端呈现、长期 GDI/COM/内存、冷启动与峰值；微基准和模拟 text store 不能代替。

preview6 当时源码和产物均在本地，未推送、未执行远程 CI。preview7 已推送并执行，见开头；
没有配置后台自动继续任务。

## 首次远程 CI 与环境修正

初始提交 `7cbb7e4` 已推送 main。首次运行的 Linux Rust、Fcitx（含 sanitizer/包生命周期）、
Rust 1.82 与 ARM 核心检查通过。Windows Rust 的核心测试通过，但 C ABI 步骤写死了 VS 2022 路径；
TSF job 使用 runner 已有的 NSIS 3.10，未达到原生 amd64 installer 所需 3.11。
现改为 vswhere 查找 VS，并显式升级到 NSIS 3.11.0；原生 Windows 完整结果以修正后的 CI 为准。

## preview7 原生 Windows 通过记录

交付代码 `b14116e` 的 GitHub Actions `37212769471` 六个 job 全部通过：
Linux/Windows Rust 格式、Clippy、38 项测试、release 与 C ABI，最低 Rust 1.82，
Linux/Android ARM 核心检查，Fcitx CTest/sanitizer/Debian 生命周期，以及 Windows TSF。
Windows job 使用 Windows 2022 / VS 2022，原生六个 CTest 全通过，100 次 TSF
激活/输入/停用通过；GDI 对象在预热/flush 后及 150 次重绘后均为 7。

首次原生验证补齐 MSVC 的 SDK 宏冲突、局部变量遮蔽、显式 manifest 与自动 manifest
重复问题。GDI 检查改为创建绘图缓冲、预热缓存并 GdiFlush 后计数，仍要求连续重绘零增长；
Wine 的 GetGuiResources 返回 0，不能作为原生 GDI 计数证据。

官方 NSIS 3.11 Windows 安装缺少 AMD64 stubs/plugins；现用固定 SHA-256 的 Debian
3.11 Windows PE 组件准备私有构建工具链。NSIS 源文件显式按 UTF-8 读取，修复 Windows
默认代码页导致中文安装名称、菜单及应用列表名称错误的问题。来源/组件哈希随包保存。

原生隔离安装回归六阶段通过：安装/完整负载哈希/注册 COM/同版修复；故障注册回滚；
外来注册与修改文件保护；占用 DLL 升级/降级拒绝/用户文件保留；重复卸载与失败新装回滚；
旧 ZIP 迁移及正常系统卸载命令。故障和测试 DLL 不进入交付包。
同一交付 MSVC EXE 从真实旧 preview6 GNU EXE 升级另在独立 Wine prefix 验证，七阶段全通过，
含旧测试程序/入口删除、用户词库/设置/学习保留、回滚/修复/卸载和旧 ZIP 迁移。
这与原生 CI 的版本 fixture 区分记录。
