# Windows 包的第三方运行时

本项目源码与手写演示词典：GPL-3.0-or-later，见 LICENSE。平台代码没有导入其他输入法的实现；已导入的词库单独按下述许可分发。

候选主题绘制器 `theme_art.cpp` 属于项目 GPL-3.0-or-later 代码。GDI+ 使用 Windows 系统组件，包内不附带该 DLL。

Rust 标准库及其运行时组件静态链接到输入服务。RUNTIME_LICENSES.zip 收录实际构建工具链的 `COPYRIGHT-library.html` 与 `licenses/`；按文件列出的许可、署名和例外保留。源代码与许可信息：[Rust](https://github.com/rust-lang/rust)。

GNU/MinGW 包还静态链接 GCC/libstdc++/libgcc、MinGW CRT 和线程运行时。GCC 使用 GPL 与 GCC Runtime Library Exception；该例外允许符合条件的独立程序使用运行库，并不将本项目代码改为 GPL。包中的 Debian GCC 和 MinGW copyright 文件列出实际工具链所含组件及其许可；GPL 和运行库例外全文同时保留。参考：[GCC runtime exception](https://www.gnu.org/licenses/gcc-exception-3.1.html)、[MinGW-w64](https://www.mingw-w64.org/)。

MSVC 包使用开发者安装的 Microsoft C/C++ 静态运行时，适用该 Visual Studio 工具链的再分发条款。Windows 系统 DLL 由操作系统提供，不包含在安装包中。

SHA256SUMS.txt 用于文件完整性校验；当前开发包没有代码签名。

安装 EXE 使用 NSIS 3.11 的原生 amd64 Unicode 安装/卸载 stub、System 插件和 LZMA。NSIS 及这些组件的署名、许可和例外完整保留于 INSTALLER_LICENSE.txt，来源记录见源码 packaging/windows/README.md。NSIS 仅是构建工具和安装时运行代码，正常输入不加载安装器。

词库：Rime pinyin-simp，修订 0c6861ef7420ee780270ca6d993d18d4101049d0，
Apache-2.0。原始说明其派生于 Android 开源 PinyinIME。澄音（早期名 Chengyin）将拼音音节
分隔改为撇号、零权重改为 1、重复项取最大值，并编译为二进制；保留完整
词典和许可记录。许可证、来源 SHA256 和转换说明位于 RUNTIME_LICENSES.zip
中的 vocabulary/，源码中的 data/sources/ 保留原始文件。

新增常用词：jieba，修订 67fa2e36e72f69d9134b8a1037b83fbb070b9775，MIT，
Copyright (c) 2013 Sun Junyi。筛选频率至少 30 的 2–6 字词，仅使用 Rime 中
读音唯一的汉字补全拼音，新增权重缩放为 max(1, 原频率/50)。完整 MIT
许可证、来源哈希及原 README 位于 RUNTIME_LICENSES.zip/vocabulary/jieba/。
搜狗格式解析器及自写联想搭配为本项目代码/数据；安装包不附带搜狗词库。

皮肤插画：按用户指定的 TreapGoGo 鲸鱼娘资产库，采用首页 Q 版大肥鱼正面全身形象。
来源：https://treapgogo.github.io/deepseek-whale-girl/assets/dafeiyu-chibi-character-sheet.png
维护来源：TreapGoGo/deepseek-whale-girl；获取日期 2026-10-06。用户明确授权本地裁切、去背景；未重新设计角色。
原图与透明部署资源哈希、处理说明见源码 assets/skins/README.md。
上游将图片授权与网站 MIT 代码许可分开；读取的设定图位置没有独立图像许可声明，不把网站代码许可套用为图片许可。
本项目保留来源署名，不主张原始素材权利。DeepSeek 为社区二创形象，非官方认可；第三方名称/商标不适用项目代码许可。
青瓷枝叶、夜航星图及波纹由项目代码绘制。搜狗官方说明仅作为皮肤系统设计参考，不包含搜狗资源。
