# Windows preview7 任务清单

用户要求（2026-10-04），完成整批实现和内部检查后交付一个离线安装 EXE。

- [x] 1. 保存配置后通知已打开的应用：后台读取新配置，外观立即更新；正在输入的拼音完成后切换词库/候选策略，不要求重开应用。
- [x] 2. 删除独立输入测试程序和入口：在设置中提供标准 Edit、RichEdit、密码测试框，保留真正的 TSF 消息循环。
- [x] 3. 设置及候选改为经典 Windows 风格：原生选项卡/控件、标准系统颜色、矩形列表，保留稳定双缓冲绘制。
- [x] 4. 重排设置内容并精简文案：明确默认模式/切换/标点/候选/词库/学习的关系；按文字实际高度布局，检查 DPI/滚动/控件遮挡。
- [x] 5. 增加 TSF 语言栏状态按钮与澄音标识：中/英状态实时更新，点击切换，菜单打开设置；图标、描述和系统 compartments 同步。
- [x] 6. 中文模式默认使用中文标点，英文模式直接透传：支持成对引号，拼音中的手工撇号与翻页键继续使用；敏感字段及快捷键不转换。
- [x] 7. 完成后提交并推送到 https://github.com/zzttzzmyswy/chengyin-ime；更新 README、开发文档、任务状态和可复现的环境说明。
- [x] 验证与交付：核心/Windows 回归、旧 EXE 升级/回滚/卸载、新布局截图核对、单 EXE 负载及许可检查。

真实 Windows 11 任务栏呈现、Notepad3 及不同宿主接入保留实机验证边界。

## 内部验证记录

- Rust 格式、Clippy、38 项工作区测试、release 构建与 C ABI 冒烟通过。
- Windows MinGW /Werror 构建与六个 CTest 全部通过（Wine 10）。
- Windows 2022 / VS 2022 / MSVC 原生六个 CTest 与完整安装生命周期通过。
  GDI 对象在预热/flush 后及 150 次重绘后均为 7；另有 100 次 TSF 激活/输入/停用。
- 六页实际截图、96/144 DPI、文字不重叠、窗口层级、原生分组背景重绘、测试框保留检查通过。
- 真实 preview6→preview7 EXE 升级、旧测试 EXE/入口移除、数据保留、完整哈希、同版修复、
  故障注册回滚、外来注册保护、占用 DLL 升级、降级拒绝、卸载与旧 ZIP 迁移七阶段通过。
- 交付原生 CI 安装包：`build/packages/chengyin-windows-x64-0.1.0-preview7-msvc.exe`，4,312,794 bytes。
  SHA-256：`9c8f4d22e324808a5a378b93856ceda0cee8d1cd6931cb222e67835e0a5faa3d`。
  已取回 CI artifact，并核对 ZIP/EXE 哈希、大小与 PE64；原有 GNU 内部包保留。
- preview6 保持原文件/哈希，供回退与升级对照；新包不覆盖旧包。

任务 5 的实现及内部协议回归已完成；Windows 11 外壳最终呈现仍待实机确认。

源码已推送 `origin/main`。交付代码提交 `b14116e`，GitHub CI 的全部六个 job 通过，
运行链接：https://github.com/zzttzzmyswy/chengyin-ime/actions/runs/37212769471 。
原生 CI 使用安装版本样本验证升级；交付的 MSVC EXE 从真实旧 preview6 GNU EXE
升级另在隔离 Wine 验证，七阶段全通过，日志 `build/installer-preview7-msvc-from6.txt`。
