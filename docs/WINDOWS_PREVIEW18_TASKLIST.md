# Windows preview18：代码审查修复

用户要求先推送既有改动，压缩交接上下文，再详细审查项目并自主排期修复。
原 12 个提交已推送 main `d07fe57`，Actions 37309334271 的六作业及隔离安装生命周期通过。
本批分支 `codex/review-hardening`，报告见 [详细审查](CODE_REVIEW_2026-10-05.md)，
后续批次与退出条件见 [修复计划](REVIEW_REPAIR_PLAN.md)，产品决定见 [精简交接](CONTEXT_HANDOFF.md)。

- [x] 审查共享核心、C ABI、TSF、配置/学习、词库导入、原生 UI、更新/安装和 Fcitx 关键路径。
- [x] 用相同回归复现基线的显式组合声母、未分隔词长和尾部分隔候选缺陷。
- [x] 保护显式 `zh'r'm`；保留 `zhrm/ssdd` 的完整同字数优先。
- [x] 初始化时按 Unicode 字数约束完整音节解析；TSV/二进制索引一致。
- [x] 严格首字母索引覆盖零声母 a/e/o，完整全拼保留原行为。
- [x] 终端吸收尾部分隔，保留真实下一音节边；完整学习键去尾部分隔。
- [x] 学习清空/导入代际绑定规范文件路径，两个文件的 pending 写入互不影响。
- [x] 所有配置相关 fixture 使用独立发布/消费通道，保留跨进程文件检测回归。
- [x] release 版本/唯一包名/完整仓库 URL 严格一致，JSON 字段类型和 image 绑定回归。
- [x] 77 项 Rust、fmt、Clippy -D warnings、扩展零分配/64 KiB、MSVC /W4 /WX C ABI。
- [x] 最终 Windows 7 项 CTest（28.50 s）、UI fixture 与截图、最终安装包一致性检查。
- [x] 本批百分位与同期间基线比较记录；长串性能预算尚未达标，见 PERFORMANCE/R11。
- [x] 源码 ad84c9d 已推送；[Actions 37314439254](https://github.com/zzttzzmyswy/chengyin-ime/actions/runs/37314439254) 六作业及隔离安装/升级/回滚/卸载通过。

待完成的学习快照同步、后台有界重试、词库失败重载、资源/质量/实机矩阵已明确排期，
不把当前修复写成全项目所有问题已经消除。完整同字数词库多解析仍需来源音节证据及预算设计。
本批不使用个人历史或配置作为 fixture；没有替换个人安装，也没有执行远端更新包。

交付：`chengyin-windows-x64-0.1.0-preview18-msvc.exe`，4388187 bytes；
SHA-256 `57604fadbb04978675cd651008b1e03e43430ee7319f5721e5d9f400ccb428a4`。
7-Zip、完整负载 SHA-256/COM、三生产二进制一致、测试通知通道未进入生产、21 项许可 CRC 通过。
证据 `build/*-preview18.txt`、`delivery-preview18.json`、`ui-preview18/`。
