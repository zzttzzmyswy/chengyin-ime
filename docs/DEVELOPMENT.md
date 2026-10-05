# preview16 立即分段上屏

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

# preview15 长拼音质量和历史可信度

2026-10-05：见 [WINDOWS_PREVIEW15_TASKLIST](WINDOWS_PREVIEW15_TASKLIST.md)。整词与单字仍互不干扰。
历史槽位只接受可靠记录：文字/读音被当前词库与开启规则确认，或累计成功选择至少三次；
至少八次近期机会、平滑命中率低于 50% 的记录先取消提权，不改变 16 次/10% 的实际遗忘条件。
未观测的旧记录不算零命中率，也不删除；未知的一次记录不会盖过真实词条。缓存随词库变化失效。

Decoder 完整准确多字词直接保护输入范围，不生成拆词同音替代组句。无完整词条时，
每个边界检查左末 1–3 字与右首 1–3 字组成的完整词库词、既有自写搭配或有限代词框架。
代词框架罚分 2；无依据单字边界禁止，无依据多字词边界只在准确路径兜底，最多一处，罚分 6。
根路径优先可靠边界；有完全可靠的准确解析时丢弃未知组合，否则最多保留一个最佳未知组句。
这个额度跨首轮及后续分页/缩写备选解码共享，避免第二轮补回同音组合。模糊/纠错/预测/缩写不允许未知边界。

Dictionary 初始化建立共享只读 HashSet<u128>，每 Unicode 标量编码为 scalar+1，基数 2^21，
至多六个标量共 126 bits，无指纹碰撞、无截断；保留完整文字/拼音索引作历史词读音确认。
词库二进制无需修改，text pool 和所有权不变。转换缓存 128 项，清空/换词库时重置，
会话 inline + 预分配容量 64,878 bytes、按键零堆分配。索引增加的共享词库内存单独计入估算。

质量约束是离线可解释基线，不是完整语义模型；准确多字词之间的单项未知兜底用于未登录整句。
更广的首选率、错分词召回率及匹配的高百分位成本继续单独评估，不靠截图词语黑名单。

# preview14 学习扩容、缓存与遗忘

2026-10-05：见 [WINDOWS_PREVIEW14_TASKLIST](WINDOWS_PREVIEW14_TASKLIST.md)。Profile 上限 8192 / 4 MiB。
MSWYUSR2 记录每词 u16 hits/trials 和 u32 epoch，CRC/排序/范围均验证；可读旧 MSWYUSR1，旧计数/顺序保留，
缺失观测统计用 0/0 中性先验。C ABI v1 增加 record_selection，不改变旧函数签名或句柄所有权。
每次宿主成功接受中文选择后，一次统计完整输入匹配到的历史；同组拼音只对齐一次，包含已启用模糊/纠错规则。
统计机会不依赖首屏展示，不以每个按键作分母。原文、取消、敏感字段及失败写入不记录。
每 256 次接受的选择将旧 hits/trials 右移衰减，至少 16 次机会且 hits/trials≤0.1 的未选中记录自动删除。
满容量时比较 (recent_hits+1)/(recent_trials+2)，以交叉乘法比较，不让最近一次使用取代命中率。
序号重基时迁移衰减周期，统计不因溢出复活。旧频次仍用于提权，不把未知曝光当失败。

HistoryCache 是每个 Session 独立的 16 项固定预分配区，活动容量 4–16；每 32 查询按复用率扩/缩，
8 项 ghost hash 检测刚淘汰查询的复访，触发扩容。hash 仅参与容量调节，实际结果比较完整键/规则/剩余字节。
查询频次衰减，优先淘汰低复用记录。缓存不共享可变会话，不在按键期间分配；Profile 替换或 learn_commit 后失效。
缓存保存两条线的准确/模糊历史各两项 ID，完整规范拼音仍取不可变 Profile 快照；字典或上下文评分不进入缓存。
Windows writer 事件附带接受时的 MatchingOptions，在原有命名锁内加载最新 Profile、观测和原子保存，
避免另一个应用的反馈被旧快照覆盖。后台磁盘协议、clear/import epoch 与成功上屏后训练契约不变。

# preview13 候选质量与独立查询线

2026-10-05：见 [WINDOWS_PREVIEW13_TASKLIST](WINDOWS_PREVIEW13_TASKLIST.md)。用户明确整词与单字互不干扰。
启用模糊/纠错后，Session 用完整输入分别查整词和单字，各自按准确历史（最多两项）、准确词条、
模糊历史（最多两项）、模糊词条排序；不插入前缀单字。历史频次/顺序挑选和同文字去重使用固定栈，
词条和会话仍通过不可变 Arc 快照共享。Profile 的规范拼音仅在读入/记录时派生，磁盘格式仍为 MSWYUSR1。

Decoder 的相邻单字组合必须有完整词条或本项目自写搭配支持；缓存 32 个转换对，避免无意义同音字笛卡尔积。
词级边继续支持真实长句组合。完整一至两音节全拼不做缩写/纠错组句回退；普通补全仍位于准确/模糊查询之后。
Tolerant cursor 只取输入完整消费的 terminal，不扩大到任意后缀补全，保留每个 root 的最小匹配距离。
堆 rank 用高位距离、低 18 位词条 ID；终端续项保留距离，不增加堆节点或会话预分配容量。
首屏排序同样计入距离。键盘失误每音节一次、每词两次；音节末尾和显式分隔符前的漏键均支持。
标记对齐增加完整匹配入口，历史不因只匹配前缀而误召回；原始输入、逐字母标记和提交协议不变。

实际随包词库测试覆盖 yingshe/yinshe/显式分隔符、全页排除无依据拼字、泛化错拼、两条线历史独立与跨页排序。
Windows 自有窗口 fixture 从实际嵌入词库加载候选并绘制截图，不读取用户真实学习或词库文件。
质量规则不含截图词语黑名单；词库条目本身仍继承原始来源质量，继续做独立语料评测。

# preview12 设置同步与拼音开关

2026-10-05：见 [WINDOWS_PREVIEW12_TASKLIST](WINDOWS_PREVIEW12_TASKLIST.md)。ConfigurationWatcher 在后台每 200 ms
检测共享通知和 preferences.ini 的修改时间/大小；首次补读覆盖初始化竞态，通知映射不可用时仍检测文件。
无效配置保留当前设置并间隔重试；磁盘/词库读取只在后台，快照通过消息窗返回所属 TSF apartment。
receiveConfiguration 直接使用缓存的宿主与光标位置刷新当前候选，再申请布局刷新；外观不再依赖编辑锁成功。
词库、页容量和匹配会话仍在下一段输入切换，不在进行中的 composition 重配共享核心。

Preferences::candidatePinyin 默认 false；新 ShowCandidatePinyin 保存显式开启，旧 CandidatePinyin 仍验证并保存供降级读取。
缺少新字段时默认关闭，避免旧版默认开启值抵消新要求；只读加载不改个人文件。
关闭时隐藏原文和标准/纠错拼音，保留联想和长度限制状态。开启时遵循普通内嵌去重，并显示修正位置的字母标记。
回归增加不同进程的已有接收者、映射创建失败、拒绝编辑锁时即时外观变化、旧配置与开关绘制。
原有点击 fixture 改成按实际窗口 DPI 换算坐标；截图用 PrintWindow 绘制自有窗口，避免捕获遮挡它的其他应用。
本次共享 Rust/C ABI 不改；实机现代应用和安装生命周期证据另验，不将 native fixture 泛化为所有宿主。

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
