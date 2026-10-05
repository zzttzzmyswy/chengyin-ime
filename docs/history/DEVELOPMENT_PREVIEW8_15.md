# preview8–15 实现记录（历史）

2026-10-05 归档；当前工程指南见 [DEVELOPMENT](../DEVELOPMENT.md)。

# preview15 长拼音质量和历史可信度

2026-10-05：见 [WINDOWS_PREVIEW15_TASKLIST](../WINDOWS_PREVIEW15_TASKLIST.md)。整词与单字仍互不干扰。
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

2026-10-05：见 [WINDOWS_PREVIEW14_TASKLIST](../WINDOWS_PREVIEW14_TASKLIST.md)。Profile 上限 8192 / 4 MiB。
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

2026-10-05：见 [WINDOWS_PREVIEW13_TASKLIST](../WINDOWS_PREVIEW13_TASKLIST.md)。用户明确整词与单字互不干扰。
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

2026-10-05：见 [WINDOWS_PREVIEW12_TASKLIST](../WINDOWS_PREVIEW12_TASKLIST.md)。ConfigurationWatcher 在后台每 200 ms
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

2026-10-05：见 [WINDOWS_PREVIEW11_TASKLIST](../WINDOWS_PREVIEW11_TASKLIST.md)。四类纠错为通用 trie/字母对齐规则，
不限定 zhang。设置每项标注“示例”，分别展示 zhang、ping、hao、shi，并在组内说明适用于词库拼音。
扩展既有键盘失误回归到不同声母、韵母和 ni'hao 多音节，核对逐字母标记和提交；算法范围不变。

# preview10 匹配与主题补充

2026-10-05：见 [WINDOWS_PREVIEW10_TASKLIST](../WINDOWS_PREVIEW10_TASKLIST.md)。三个角色主题及绘制代码删除，保留通用系统/白/黑绘制器。
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

2026-10-05：批次见 [WINDOWS_PREVIEW9_TASKLIST](../WINDOWS_PREVIEW9_TASKLIST.md)。
theme_art.cpp 为候选窗口与设置预览共享的原创矢量绘制器，GDI+ 由系统提供。
主题 0 保留经典 GDI；其他主题绘制圆角、插画、纹样、选中态和序号徽章。
不在共享 Rust 核心加入 GUI 依赖；插画不下载第三方资产。
Shift 测试回调只允许取消切换，不能触发切换；空闲透传按键不谎报 eaten。
真实安装的 Notepad3 与模拟编辑对象的结果分别记录，见 STATUS。

# preview8 本机 Windows 开发补充

2026-10-05：批次见 [WINDOWS_PREVIEW8_TASKLIST](../WINDOWS_PREVIEW8_TASKLIST.md)。
Windows-only 空格映射到原文提交；共享核心/C ABI 不改语义。
设置 UI 字体在所有控件换用新句柄后才释放旧句柄；UI/桌面测试使用隔离路径。
词库列表原子封装于 dictionary.custom，内置词库与各启用项在后台合并为只读快照。
系统亮暗颜色读取 AppsUseLightTheme，响应系统设置/颜色通知；高对比度仍优先。
build-only windows_desktop_test 用进程内 COM 工厂测试实际新 DLL，不安装独立测试应用。
自动更新通过 WinHTTP 只读查询 Releases，在后台下载、验证 SHA-256/PE64 后由用户启动安装。

原生构建和七项 CTest 通过；日志在 build/native-build-preview8.txt、
build/native-test-preview8.txt。工具链和复现见 ENVIRONMENT，桌面边界见 STATUS。

以下保留已有开发指南：

