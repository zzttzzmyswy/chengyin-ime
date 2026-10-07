# 项目精简交接 · 2026-10-06

## 当前追加：preview23 句末助词与准确前缀

codex/exact-sentence-prefix 基于316d7f3。原问题xianzaikaishiba→想再开时报，来自逐词纠错预算累加、缺少句末语气词连接。
多字词＋吧/吗/呢/啊/呀仅准确、仅终止位置；生成句整串align cost<=2；任何准确完整路径压制非准确生成句。
Windows立即分段模式保护完整拼音的准确多音节词组前缀，完整字典模糊词仍先召回，前缀在猜测生成句前。
首选现在开始吧，当前页现在开始消费13字节、ba重匹配。默认staged/C ABI行为不变，无平台IO，个人历史不参与测试。
102RustRelease/fmt/Clippy/零按键分配64KiB/C ABI/8CTest38.61s通过，新增TSF host接受前缀+取消余拼音回归。
quality_report --incremental覆盖实际Windows路径，旧/新488条总体/分类/半集汇总一致，Top1/Top9/可达92.0/97.7/98.8%。
version.json23、头文件与模拟bump通过，CI包名自动派生；源码 `2fc0fa3` 的 [Actions 37453639077](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37453639077) 六作业成功，含 Windows 隔离安装、升级、回滚、卸载。
本机交付 chengyin-windows-x64-0.1.0-preview23-msvc.exe，5135063 bytes，SHA256 `628a33c65c765c74a11edd6c9870cb6ff3ca16c9068111f6b2fceeb58f44e126`。
PE/7-Zip/全负载SHA/COM/生产字节一致/22项许可与插画署名CRC通过，已复制主目录。
见WINDOWS_PREVIEW23_TASKLIST、PERFORMANCE、DEVELOPMENT与build/delivery-preview23.json。
真实升级宿主/物理DPI未验证，D其余规则/性能与E兼容继续；不操作UAC，不改个人安装/设置/学习。

## 当前追加：preview22 全身 Q 版大肥鱼与准确结构组句

分支 codex/q-dafeiyu-skin；本批 c08ac94 功能已与 main 0b2ae4b 的 B/C/D 基线合并。
用户明确授权本地裁切去白底：指定设定图左上正面全身，保留白色服饰/鲸尾，透明449×512部署；详见资产 README。
准确 A/不A、A不/A、多字词＋的用词典边锚定，仅准确路径可用；可靠准确句压住纠错扩张组句，独立词语/前缀仍保留。
空历史四种匹配配置首选开不开/电脑的/手机的；自造词条和分段提交回归防止示例硬编码。
修复 GDI+ DLL 全局析构等待；图片仅持 RGBA/至多四项 DIB，GDI+ 调用内关闭。
UI 进程本地 COM 工厂绑定 fixture，避免已安装 preview21 TIP 在关闭时干扰；不改个人安装/配置/学习。
96 Rust Release、fmt/Clippy、零按键分配/64KiB、MSVC C ABI、8/8 CTest31.04s、完整五档模拟DPI/UI通过。
version.json 单版本源 preview22，CI 安装包名动态派生，模拟 bump 通过。
488 条独立集重放总体与 I03 一致：Top1 92.0%，Top9 97.7%，可达98.8%；D优化/E实装继续待办。
本机安装包5104648 bytes，SHA256 110e733b16b1a7a1ef77de40abde114988a66d52ba93b118e6491e2242a6786c，主目录副本一致。
包PE/全负载SHA/COM/生产字节一致/22项许可与插画署名CRC通过；见 preview22 tasklist 和 build/delivery-preview22.json。
合并前未交付的旧preview22构建已移到 build/archive/preview22-before-integration；不能与最终包混淆。
源码 `40fec9d` 的 [Actions 37445555758](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37445555758) 六作业全部成功，含 Windows 隔离安装、升级、回滚、卸载。
真实升级宿主与物理混合DPI待集中验证，不自动操作UAC。

## 当前追加：候选皮肤 · preview21

分支 codex/theme-skins，基于 e42516a。用户重新要求完整皮肤、自定义与鲸鱼娘，最终指定 TreapGoGo 网站形象。
最终使用该网站 `assets/whale-girl-logo-dynamic.png` 透明头像，原字节保留于 assets/skins/whale-girl*.png；
此前 image_gen 草案与抠图尝试未进入交付。上游图片不能套用其网站代码 MIT 许可，来源说明在资产 README 和 THIRD_PARTY。
皮肤标识 10–12/13，不复用旧 3–5；.cyskin 为有界 UTF-8 字段+内嵌 PNG Base64，导入/导出/收藏最多64份。
设置内可编辑配色、圆角、留白、纹样、PNG；draft 应用时另存，文件名使用 GUID，正常配置通知热同步。
不改核心/C ABI/学习。按键和绘制不读文件；共享图像的四槽缩放缓存锁保护，最大边512。
最终 86 Rust Release、C ABI /W4 /WX、8/8 CTest24.89s、截图五档模拟DPI、完整包/COM/二进制/许可检查通过。
包 preview21，4628751 bytes，SHA256 1a701b8f1f342effc87636062637c9d510fd8dd291baaf5d6e25cac500fd07c5，已复制主目录。
看 docs/WINDOWS_PREVIEW21_TASKLIST.md、docs/SKINS.md 和 build/delivery-preview21.json。
个人安装/设置/历史未修改，没有再触发 UAC。新宿主实机、物理混合DPI、review B–E 继续待办。

## 当前追加：单音节与 Explorer · preview20

分支 codex/single-syllable-explorer，基于 a55c97c；完整单音节优先单字，保留词语分页、两线历史和前缀协议。
SECUREMODE 不再 E_NOTIMPL：内嵌字典、内存配置，无个人文件/writer/watcher/设置入口/学习/联想，注册对应能力。
86 Rust Release、fmt/Clippy、C ABI、零分配/64902 bytes、7 CTest31.00s、最终 COM/TSF 重验9.17s通过。
横纵 bao/shi 候选截图、负载/COM/三二进制/许可通过，包已复制主目录：preview20，4392201 bytes，
SHA256 6b68ee3ad027b4e25f8957d194f49529d98c1e48ecb0210329f9a18eac643b41。
实际 Explorer PID55516 加载 preview7，两 WinUI 输入框输入 n 直接显示英文，没有候选。
启动经过校验的 preview20 安装器时，Windows 返回“操作已被用户取消”，session10087已结束，安装未完成。
不要重复弹出 UAC 或自动操作安全弹窗。包已准备好，手动安装后重新加载 Explorer 服务，
再验证实际双输入框；不能只凭 fixture 宣布修复。
Computer-use 的 sky 状态在 node_repl，目标 Explorer window591296；窗口变化后重新选择，禁止复用旧索引。
搜索测试 n 已通过 Escape 取消，没有提交搜索或修改文件。
preview19 CI37318044390已确认六作业/隔离生命周期成功。后续 review B–E 保持。

## 最新追加：拍照候选 · preview19

用户报告 paizhao 出现无依据的拼接。隔离 preview12 全规则复现四项前排拼接；
实际 Chrome/steamwebhelper 加载 preview12，部分其他宿主加载 preview7/11。
preview18 的整词保护正确，但旧 v1 高计数未知组合可直接提权，本次加入 >=3 近期命中门槛；
词库可证明的旧习惯保留，数据没有清空。五项新回归与 native 拍照首选/上屏检查通过。
最新安装包 preview19，4384807 bytes，SHA256 f9f460e54f372af89fa83feda5201080c24ed28cd3d7bcdb224b4385e773132b。
82 Rust Release、fmt/Clippy、零分配/64KiB、C ABI、7 CTest22.08s、包/负载/COM/许可通过。
Debug 已有 initials 测试 EXE 启动遭文件占用，不算通过；Release 正常全量通过。
新包未替换个人安装；安装后必须完整退出重开加载旧 DLL 的应用。任务见 WINDOWS_PREVIEW19_TASKLIST。
原审查后续 B–E 计划保持，当前分支 codex/photo-word-quality。

## 此前任务

用户要求：推送已完成改动，压缩上下文，对整个项目详细 review，并自行排期修复问题。
第一步已完成：12 个未发布提交快进合入 main，推送并验证远端为 d07fe57。
review 和首批修复在 codex/review-hardening 完成，代码 ad84c9d 已合入并推送 main。
详见 CODE_REVIEW_2026-10-05 和 REVIEW_REPAIR_PLAN。
当前工具没有手动触发会话压缩接口，本文件是可恢复的精简交接记录，不宣称会话已压缩。
无需等待用户逐项批准常规修复；按严重程度、依赖和验证门槛安排工作。

## 已确定的产品约束

- Windows x64 TSF、单个离线安装 EXE 优先；共享 Rust 核心保持无平台 I/O/GUI。
- 全拼优先，双拼后续；Linux Fcitx/Android 保留共享边界，不扩展未完成平台的支持承诺。
- 整词、整句与单字有独立匹配线；准确/模糊历史分别最多两项。
- 完整声母缩写同字数词优先；有对应完整词时不让较少字或纠错解释抢先。
- 前缀选择立即上屏，仅用剩余拼音重新匹配；取消/编辑不删除已接受前缀。
- Windows 空格提交剩余原始拼音；Shift 符号和大写透传；数字/鼠标/宿主 UI 选择中文。
- 候选拼音默认关闭；开启时真正的模糊/纠错标记到拼音字母，正常缩写不作错拼标记。
- 设置实时同步已打开应用；二进制升级仍需重开加载旧 DLL 的进程。
- 学习容量 8192，近期命中率遗忘及每会话动态缓存；仅在宿主确认成功写入后学习。
- 角色主题已删除，保留主题页与系统/白/黑；自定义词库按列表管理。
- 保留 C ABI 所有权、UTF-8 缓冲区和提交事件合约，不共享可变输入会话。

## 最新交付与证据

preview18，77 项 Rust、fmt、Clippy、原生 C ABI 和 7 项 Windows CTest（28.50 s）通过。
安装包 chengyin-windows-x64-0.1.0-preview18-msvc.exe，4388187 bytes，
SHA-256 57604fadbb04978675cd651008b1e03e43430ee7319f5721e5d9f400ccb428a4。
生产词库 87540 条；Session inline+预留 64902 bytes，初始化后按键/候选路径零分配。
包、完整负载/COM、三二进制一致和 21 项许可 CRC 验证通过。详见 STATUS 和 preview18 清单。
未把个人设置/学习用于测试，测试通知已隔离；本机未重新安装 preview18。
preview17 d07fe57 CI 六作业/安装生命周期通过（37309334271）；
preview18 ad84c9d 的六作业和隔离安装/升级/回滚/卸载通过（37314439254）。
实装宿主矩阵、物理混合 DPI 和独立语义评测仍有缺口。

## 复现与开发

- 先读 AGENTS.md、STATUS、ROADMAP、DEVELOPMENT。审查报告与修复计划另存 docs。
- PowerShell 每次调用设置 cargo/CMake PATH；VS2022/MSVC 19.44、Rust 1.99、Win11 build26200。
- cargo fmt；cargo test --workspace --locked；cargo clippy --workspace --all-targets --locked -- -D warnings。
- 静态核心：RUSTFLAGS=-C target-feature=+crt-static，release chengyin-ffi，x86_64-pc-windows-msvc。
- CMake 显式构建 production 和七项 CTest 依赖；ctest -C Release --output-on-failure。
- package_windows.py 默认 preview19，固定 NSIS AMD64 helper；版本改变应同步 UI、更新 fixture、CI。
- windows_desktop_test 为手动目标，不交付；已有 AV 提示不绕过、不关闭安全软件。
- build/target 忽略；旧证据在 build/archive。个人备份不加入 Git。
- 性能记录硬件、语料、范围及百分位；核心微基准、模拟 TSF、真实宿主证据分开。
- 有真实复现才认定缺陷；风险、验证缺口和已修复问题分别标明，不杜撰全部覆盖。

## 审查和下一步

- 14 项审查记录，六项缺陷加一项索引覆盖已处理：R01–R06、R10。
- 下一批 R07 普通学习的跨应用 revision 与 pending 竞态；R08 后台有界重试/统计。
- 再处理 R09 失败重载 stamp，R12 词库资源/读音规范化，R13 单版本源。
- R11 独立质量集/长尾性能和 R14 实装矩阵按门槛推进，不扩大支持范围。
- 核心新版本全规则长串 P99 15.1051 ms，同期间旧版本 14.4917 ms，未控制机器背景负载；
  不能宣称性能目标已达成，详见 PERFORMANCE。完整缩写最终键 P99 6.4 µs。
- 没有创建后台自动任务；后续开发按修复计划顺序继续，不将排期误写为已经实现。
