# 澄音输入法 · Chengyin IME

澄音是一款正在开发的开源中文输入法，长期目标是高性能、低延迟和跨平台兼容。
共享 Rust 核心面向 Windows、Linux X11、Linux Wayland 与 Android；全拼优先，随后双拼。
当前优先交付 Windows x64，版本 **0.1.0-preview8**。

## 当前功能

- 预载 87,540 条开源常用字词；全拼、首拼、声母混输、连续组词组句和离线上屏联想。
- 整句与前缀选词、候选翻页、中间编辑、手工音节分隔（例如 `xi'an`）。
- Windows 本地选词学习：按频次与近期选择排序；支持备份、导入和清除。
- 导入常见搜狗 SCEL、UTF-8 / UTF-16 / GBK 文本、TSV 和二进制词库。
- 七页原生 Windows 设置；紧凑候选、六种主题、系统亮暗跟随、自动更新与发行说明。
- 自定义词库列表管理；Shift 符号和大写透传；空格提交拼音原文，数字/鼠标选中文。
- 配置自动同步到已激活的应用；中文标点跟随模式；TSF 中/英状态按钮与澄音标识。
- 设置内嵌 Edit、RichEdit 和密码测试框；一个离线安装 EXE，支持修复、升级与失败回滚。

## 平台状态

| 平台 | 目前实现 | 验证边界 |
| --- | --- | --- |
| Windows 10 2004+/11 x64 | TSF、候选、设置、学习、单 EXE 安装器 | 本机 Win11/MSVC 七项 CTest 与 Edit/RichEdit 桌面输入通过；preview7 安装生命周期已通过；preview8 多屏/应用矩阵仍待验收 |
| Linux X11 / Wayland | Fcitx 5 插件、自定义词库后台切换、Debian 测试包 | 无头事件测试与包生命周期；真实桌面应用矩阵待验收 |
| Android | 共享核心 ARM64 编译检查 | 输入服务与软键盘尚未实现 |
| 双拼、Windows ARM64/x86 | 已规划 | 尚未实现 |

这些自动化结果不等同于实机兼容性验收。核心保持初始化后的按键路径零分配，
真实词库下的部分延迟仍未达到项目预算；测量范围与结果见 [性能记录](docs/PERFORMANCE.md)。

## 安装和体验

Windows 原生构建文件为 `chengyin-windows-x64-0.1.0-preview8-msvc.exe`；开发者的 MinGW
交叉构建以 `-gnu.exe` 结尾。安装包自包含，不需要用户安装 Rust、Python 或额外运行库。
安装后注销并重新登录，Win+Space 选择澄音；开始菜单打开设置。
使用、升级、卸载和集中实测步骤见 [Windows 指南](platforms/windows/README.md)。
构建产物不提交到源码库；[GitHub Actions](https://github.com/zzttzzmyswy/myswyIm/actions/workflows/ci.yml)
通过 Windows 构建及安装回归后保存安装 EXE，可在对应运行的 Artifacts 中下载。
preview8 已在本机 Windows 11/MSVC 构建并通过七项 CTest、Rust 和 C ABI；
当前交付包在 `build/packages/`，4,299,339 bytes，SHA-256：
`d1141b206f8f82a44f21bb4e208b931f3b33db1e9a061e39f91392421abc888b`。
本轮未运行新远程 CI；previous preview7 的[已通过构建](https://github.com/zzttzzmyswy/myswyIm/actions/runs/37212769471)
包括隔离安装生命周期。详见 [最新状态](docs/STATUS.md)。

Linux 安装与构建见 [Fcitx 5 指南](platforms/fcitx5/README.md)。

## 从源码开始

共享核心要求 Rust 1.82+，Rust 工作区没有第三方 crate 依赖：

```sh
cargo test --workspace --locked
cargo run --release -p myswy-cli -- --dict data/daily.mswydict --query woxihuanzhongwen
cargo run --release -p myswy-cli -- --dict data/daily.mswydict --query wxhzw
cargo run --release -p myswy-cli -- --dict data/daily.mswydict --query "xi'an"
# Linux：格式、静态检查、测试、release 与 C ABI 检查
bash scripts/check.sh
```

CLI 不接管系统键盘；未指定词库时使用 98 条演示词。
Windows 原生构建要求 Visual Studio 2022 C++/Windows SDK、CMake 3.20+、Python 3.11+、NSIS 3.11+：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\platforms\windows\Build.ps1
```

## 开发文档

- [开发指南](docs/DEVELOPMENT.md)：目录、构建、回归、配置通知、输入与资源生命周期。
- [环境与复现](docs/ENVIRONMENT.md)：本次云环境、工具版本、验证边界。
- [当前状态](docs/STATUS.md)、[本轮任务清单](docs/WINDOWS_TASKLIST.md)、[长期路线](docs/ROADMAP.md)。
- [架构](docs/ARCHITECTURE.md)、[C ABI](include/myswy_ime.h)、[搜狗格式兼容](docs/SOGOU_COMPATIBILITY.md)。
- [词库来源](data/README.md)、[第三方许可](platforms/windows/THIRD_PARTY.md)、[集中实测门槛](docs/WINDOWS_RELEASE_GATE.md)。

代码采用 [MIT License](LICENSE)。词库分别使用 Apache-2.0 与 MIT，转换来源、固定修订和原始许可
保存在 `data/sources`。Windows 系统字体只通过系统 API 使用，不随项目分发。
