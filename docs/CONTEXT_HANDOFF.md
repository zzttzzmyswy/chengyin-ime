# 项目精简交接 · 2026-10-05

## 当前任务

用户要求：推送已完成改动，压缩上下文，对整个项目详细 review，并自行排期修复问题。
第一步已完成：12 个未发布提交快进合入 main，推送并验证远端为 d07fe57。
review 和首批修复在 codex/review-hardening 完成；详见 CODE_REVIEW_2026-10-05 和 REVIEW_REPAIR_PLAN。
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
preview17 d07fe57 CI 六作业与隔离安装生命周期通过（37309334271）；preview18 推送后另验。
实装宿主矩阵、物理混合 DPI 和独立语义评测仍有缺口。

## 复现与开发

- 先读 AGENTS.md、STATUS、ROADMAP、DEVELOPMENT。审查报告与修复计划另存 docs。
- PowerShell 每次调用设置 cargo/CMake PATH；VS2022/MSVC 19.44、Rust 1.99、Win11 build26200。
- cargo fmt；cargo test --workspace --locked；cargo clippy --workspace --all-targets --locked -- -D warnings。
- 静态核心：RUSTFLAGS=-C target-feature=+crt-static，release myswy-ffi，x86_64-pc-windows-msvc。
- CMake 显式构建 production 和七项 CTest 依赖；ctest -C Release --output-on-failure。
- package_windows.py 默认 preview18，固定 NSIS AMD64 helper；版本改变应同步 UI、更新 fixture、CI。
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
