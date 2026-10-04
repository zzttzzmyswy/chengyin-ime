# 九项反馈核验 / 澄音 preview6（历史记录）

本文件保留 preview6 的核验结果。preview7 的经典界面、配置同步、嵌入测试、
语言栏与中文标点等最新工作见 [本轮任务清单](WINDOWS_TASKLIST.md)。

更新：2026-10-04。对外名称改为“澄音输入法 / Chengyin IME”。保持原来的
CLSID、核心 ABI、配置和安装标识，使旧版升级继续保留词库/设置/学习数据。

“已修复并通过内部验证”指源码机制、自动回归或列出的 Wine 绘制检查通过；
实际 Windows 的显示器/应用 text store 仍有单独验证边界。不能把模拟 TSF
通过写成 Notepad3 实机通过。下表逐项列出结果。

| # | 用户反馈 | 本批处理 | 内部证据 / 边界 |
| --- | --- | --- | --- |
| 1 | 设置太简单，需要完善与美化 | 五页常规/外观/词库/学习隐私/帮助诊断；候选字体、字号、主题、布局、密度、页数、分隔、联想、学习、Shift、默认模式、位置回退；后台导入与备份/恢复/清除、预览、DPI/滚动、草稿保存 | `windows_ui_test` 检查五页、字体、96→144 DPI、主题、滚动与新名称；Wine 实际截图检查；WinUI 参考记录见 WINDOWS_UI_REFERENCES |
| 2 | 字体选择不好看 | 设置统一 Microsoft YaHei UI；候选默认同字体，可选系统字体及 12–32 字号，缺失字体回退；按 DPI 缓存 | 设置/候选截图与 font/DPI 检查；配置 Unicode 保存/重载、非法 UTF-16 拒绝；Wine 的测试替代字体没有打入包；Windows 实际 ClearType 未测 |
| 3 | 每打一字候选框闪烁 | 保留 HWND/离屏 DC/bitmap；抑制背景擦除，一次 BitBlt；迟到自身编辑通知不退役组合，临时 NOLAYOUT 保留同框有效锚点 | 连续 150 次刷新检查 HWND/可见性/几何/GDI 资源；模拟迟到编辑通知、布局失效/恢复/clipped 检查；真实主机逐帧呈现仍待测 |
| 4 | 候选框难看 | 圆角、主题配色、高亮/悬停、清晰页脚；纵向/横向换行、密度、字体/页数、拼音副行；不抢焦点 | Wine 纵向/横向/浅深主题实际绘制检查与点击/焦点/陈旧点击回归；美观属于主观验收 |
| 5 | 排序不能跟随习惯 | 成功接受纯中文选词/整句后记录频次与序列；深页词可进入首屏；持久 Profile 重新打开应用可读；同频近期优先；4,096 项上限，隐私/备份/关闭/清除 | 深页 137 个同音候选去重分页、整句重载、长历史频次优先、跨工作线程合并、损坏与锁定文件保留、清除 epoch 不复活；上下文内存不写盘 |
| 6 | 输入测试/Notepad3 部分框不触发 | 输入测试显式激活 TSF、使用 message pump/keystroke manager；标准 Edit/RichEdit/密码框；补 COM-less/input-mode 注册、模式同步及范围/Win32 光标/视图定位回退 | **实现改进与模拟回归通过，尚不能判定实机问题已解决。** 原生程序在 Wine 中实际启动、三个控件创建及正常退出检查通过；当前没有真实 Windows 的 Notepad3 版本/架构/复测；位置回退只能处理已分派输入事件的上下文；x86/ARM64 DLL 尚无 |
| 7 | 缺少左 Shift 快切 | 默认左 Shift 单独释放切换；可启用左右 Shift 或关闭；Ctrl+Space 保留；重复、Shift 组合键、两个 Shift 同按不误切换 | 纯状态机及实际 Service 的 OnTest/OnKeyDown/OnKeyUp 回归；OnTest 无状态副作用；系统快捷键与 Caps Lock 透传 |
| 8 | 拼音缺少显示分隔 | 显示层按合法音节切分，如 ni'hao / zhong'guo；可关闭；不改宿主原拼音/光标 | 所有合法音节原样、混输/显式分隔、raw/caret 不变、小缓冲区 C ABI 与热路径零分配检查；60 字节 getter P99 38.056 µs |
| 9 | 缺少手工分隔 xi'an | `'` 进入解码并约束音节边界；候选只显示的自动分隔独立；xian 与 xi'an 的学习记录分开 | 核心与 TSF 键盘 xi→'→an→Space 提交“西安”；学习“先”不影响 xi'an；编辑/分页及 C ABI 回归 |

当前整体证据：38 项核心集成测试（Rust 1.99/最低 1.82），C ABI、Windows
GNU 交叉链接后的五个 Wine CTest；Fcitx 两项和 C++ ASan/UBSan 两项通过。
原生 MSVC CI 仅配置，未执行。性能目标还未完全达到，见 PERFORMANCE。

本批交付单个离线 EXE。名称同步到输入法 profile、宿主候选说明、设置/测试
窗口、安装向导、已安装应用、开始菜单、负载文档与构建信息；旧开始菜单
迁移只移除四个已知快捷方式，未知文件保留。升级/失败回滚仍保留原服务和数据。

名称核对在 Wine 中直接读取 Unicode 注册值，避免 `reg.exe` 的旧代码页把
中文输出为问号。Wine 10 的 TSF `GetLanguageProfileDescription` 返回
`E_NOTIMPL`，该环境改为核对 `AddLanguageProfile` 保存的 Description；
原生 Windows 检查仍要求正式 TSF getter 通过，未执行的实机结果不计入证据。

最终 preview6 单 EXE 完整安装生命周期七段全部通过，含真实保留 preview4
EXE 升级和新旧菜单迁移；词库、设置、学习数据与未知菜单文件保留。
包为 `build/packages/chengyin-windows-x64-0.1.0-preview6-gnu.exe`，
7,499,364 bytes；哈希及日志见 STATUS。第 6 项依然标记实机待确认。
