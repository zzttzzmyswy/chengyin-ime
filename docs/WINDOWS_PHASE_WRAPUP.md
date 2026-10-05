# Windows preview8–16 阶段结项

结项日期：2026-10-05。用户确认本段开发结束，后续功能另起新分支。
本次只整理开发记录、构建入口、Git 分支和本地临时产物，未修改输入算法或个人安装。

## 最终交付和代码定位

- 最终产品：`0.1.0-preview16`，源码提交 `8809c2e`，本地注释标签 `v0.1.0-preview16`。
- 安装包：`build/packages/chengyin-windows-x64-0.1.0-preview16-msvc.exe`，4,385,302 bytes。
  SHA-256：`c0d0bc1e9203033e5506194adca18632813afa77560dfac1cd1eee66e437c299`。
- `preview15` 安装包留作回退；安装包保持原交付字节，不用整理后的文档覆盖重打包。
- 阶段从 `f3ec729` 开始，共十个开发提交，最后是 `8809c2e`；全部以 fast-forward 合入本地 `main`。
  结项整理另有一个文档/构建入口提交，删除已合并的 `codex/preview8-windows-polish` 分支。
- 当前聊天工作树保留为 detached 的交付参考，以保留之前给用户的安装包和证据链接；
  日常开发入口是 `C:/Users/xhxez/Documents/ChatGPT/中文输入法` 的 `main` 工作树。
- 本轮没有推送远端、发布 GitHub Release 或启动新的远程 CI；`origin/main` 保持原状。

## 本阶段功能与决定

- Windows 候选尺寸、紧凑布局、设置字体、系统亮暗跟随、DPI/屏幕工作区及跨应用配置同步。
- 系统、白、黑三主题及主题设置页保留；按用户决定移除三个角色主题，后续美化另立需求。
- 默认关闭候选拼音；启用时保留按字母标注模糊/错拼位置。可在应用直接编辑拼音时隐藏重复显示。
- 自定义词库列表管理；关于页、MIT/第三方许可、仓库和可校验更新；Shift 透传和空格提交拼音原文。
- 模糊音及漏键、相邻键、重复键、相邻交换的纠错；整词/单字独立匹配，历史各层最多两项。
- 8192 学习记录、动态查询缓存及近期命中率遗忘；整句质量约束和历史可靠性判断。
- 整词/整句之外的准确前缀字词，部分选择立即上屏，剩余拼音重新匹配；保留旧 C ABI 默认行为。

各版任务清单仍在 docs/；最新实现说明见 [DEVELOPMENT](DEVELOPMENT.md)。

## 验证与边界

- 最终功能批次：67 项 Rust 测试、fmt、Clippy、MSVC C ABI、7/7 原生 Windows CTest。
- 结项整理：CMake 重新生成后验证手动桌面驱动不再进入默认 ALL_BUILD；7/7 CTest 再次通过（18.56 s）。
- 此处手动驱动为开发工具，显式 target 仍可用；不是随安装包交付的独立输入测试应用。
- 最新与回退安装包哈希不变；原 preview16 包全负载哈希、COM、许可及候选截图验证记录仍保留。
- 更广的实际宿主矩阵、物理混合 DPI、多样长句语料质量和本批安装生命周期/远程 CI 未完成。
  这些作为未来验收事项保留，阶段结项不改写为“全部实机已验证”。

## 本地文件整理

- STATUS 只保留最新状态和后续验收范围；旧状态、实现补充及环境说明转入 docs/history/，历史不删除。
- README、AGENTS、ROADMAP 和当前环境说明统一到已结项的 preview16；个人 `.codex/` 配置忽略而不删除。
- 最终 preview16 安装包、preview15 回退包、preview16 日志/截图、Rust release、原生构建及 NSIS 工具链保留。
- 759 个历史文件压缩至 `build/archive/windows-preview8-16-evidence.zip`，压缩流及逐文件 SHA-256 全部核对。
  归档 144,772,564 bytes；SHA-256 `3767ea196979c4f63bd7814e52c631ded76cb690d6024bec475991699f32f0f4`。
- 批量递归删除被自动审批策略拦截；222 个历史顶层项目改为可恢复移动到 `build/archive/originals/`。
  该目录和 ZIP 都只留在本机，不提交 Git；其中包含本地诊断备份，不作为公开附件。
- `cargo clean --profile dev` 移除 2338 个文件、645.5 MiB 可重建 debug 缓存；release 产物保留。
- 清理清单、校验和结果在 `build/archive/cleanup-plan.json`、`cleanup-archive-verified.json`、`cleanup-report.json`。
  新文件位置可按 originals/ 下的原相对路径恢复，ZIP 也保留 build/ 相对目录结构。

复现入口见 [ENVIRONMENT](ENVIRONMENT.md)。个人词库、学习数据、注册和已安装输入法均未改动。
