# Windows 原生界面与接入参考

preview7 按用户偏好改为经典 Windows 属性页：原生选项卡、分组框、
标准控件与矩形候选列表，移除侧栏卡片、圆角和自定义装饰。以下为历史参考，
当前代码不依赖 WinUI，也未复制参考仓库源码。界面核验包含六页截图、
文字/控件不重叠、背景正确重绘、内容面板层级与 150% DPI 输入框保留。

preview5 实现并在澄音 preview6 核对的设置和候选窗使用项目自写 Win32/GDI 代码，未复制第三方 UI
源码，没有 WebView、额外字体或前端运行时。Microsoft YaHei UI 为 Windows
自带字体；可选择已安装字体，缺失字体回退。以下仓库只用于阅读设计/API。

- [Microsoft WinUI Gallery](https://github.com/microsoft/WinUI-Gallery)，参考
  SettingsPage 的侧栏、分组与间距，查看修订
  `535ce178dbd6ebe24fe63e444b66344d007bdabe`，MIT。实际实现保留经典
  Win32 控件以减少依赖；未嵌入 Gallery 代码、图标或 XAML。
- [Windows classic samples / SampleIME](https://github.com/microsoft/Windows-classic-samples/tree/434f6002bdf9cf9829406c3ff2b33387982d6168/Samples/IME/cpp/SampleIME)，
  修订 `434f6002bdf9cf9829406c3ff2b33387982d6168`，参考 TSF 注册类别和
  TextEditSink 的编辑/选择通知处理。接口/GUID 也核对 windows-rs 0.58.0
  TextServices 元数据。

本地绘制检查：Wine 10 + Xvfb 1440×1000，五个页面、浅/深色、滚动预览、
96→144 DPI 重布局及候选纵向/横向/拼音副行。输入测试程序另外检查新名称、
Edit/RichEdit/密码控件创建和 TSF 启动后正常退出。截图在忽略的
`build/ui-review-chengyin-final/`。Wine 缺少 Windows 中文字体，在隔离前缀安装本机
Noto Sans CJK 并作字体替代，仅用于可读性检查，未打入安装包。此检查不是
Windows 实机的 ClearType、DWM、分数缩放或可访问性验收。
