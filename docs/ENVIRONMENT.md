# 当前本机 Windows 环境 · preview11

2026-10-05：Windows 11 build 26200，AMD Ryzen 9 9950X（16 核/32 线程），x64。
工作目录是 Codex 的中文路径 Git worktree；源码不依赖绝对路径。
分支 codex/preview8-windows-polish，基于 f3ec729；当前未运行新的远程 CI。

- VS 2022 Build Tools 17.14.41，MSVC 19.44.35229 / 14.44.35207；SDK 10.0.26100.0。
- Rust 1.99.0 x86_64-pc-windows-msvc，rustfmt/clippy/rust-docs；静态核心用 +crt-static。
- CMake/CTest 4.4.3；NSIS 3.11 portable + 仓库 prepare_windows_nsis.py 的固定 AMD64 组件。
- 官方 NSIS 3.11 ZIP SHA-256：c7d27f780ddb6cffb4730138cd1591e841f4b7edb155856901cdf5f214394fa1。
  来源 https://sourceforge.net/projects/nsis/files/NSIS%203/3.11/nsis-3.11.zip/download 。
- AMD64 组件来源、哈希和许可由准备助手写入 CHENGYIN_AMD64_COMPONENTS.json，随包收入许可 ZIP。
- Rustup、VS、CMake 通过官方 Windows 安装/winget 配置；Python 使用 Codex bundled runtime。

```powershell
$env:PATH="C:\Users\xhxez\.cargo\bin;C:\Program Files\CMake\bin;" + $env:PATH
$env:RUSTFLAGS='-C target-feature=+crt-static'
cargo build --release -p myswy-ffi --target x86_64-pc-windows-msvc --locked
cmake -S platforms/windows -B build/windows-msvc -G 'Visual Studio 17 2022' -A x64
cmake --build build/windows-msvc --config Release --parallel 3 --target myswy_tsf myswy_settings myswy_probe windows_ui_test windows_key_test windows_settings_test windows_update_test windows_live_test myswy_tsf_fixture
ctest --test-dir build/windows-msvc -C Release --output-on-failure
python scripts/package_windows.py --build-dir build/windows-msvc --toolchain msvc --makensis build/nsis-amd64/makensis.exe --nsis-notice packaging/windows/NSIS-LICENSE.txt
```

只有隔离 runner 运行安装器的安装/卸载/故障注入生命周期，不能拿当前用户的既有安装作 fixture。
本机真实 Edit/RichEdit 按键已通过；物理混合 DPI 多屏与完整应用矩阵未完成。
以下为 prior preview7 云环境记录，不代表本轮新 CI 或实机结果。

# 开发环境与复现

更新：2026-10-04。仓库工作目录为 `/workspace/myswyIm`，远程仓库为
https://github.com/zzttzzmyswy/myswyIm 。源码不依赖该绝对路径。`main` 已推送并跟踪 `origin/main`；
初始实现提交 `7cbb7e4`；交付代码 `b14116e` 的远程 CI 六个 job 全部通过。
验证运行：https://github.com/zzttzzmyswy/myswyIm/actions/runs/37212769471 。

## 当前云环境

Linux / Debian，bash，3 个可用逻辑 CPU。Rust 1.99 与最低支持版本 1.82 均已安装；
已配置 x86_64-pc-windows-gnu、aarch64-linux-android、aarch64-unknown-linux-gnu。
Windows 使用 MinGW GCC 14.2、CMake/CTest 3.31.6、NSIS 3.11 交叉构建，Wine 10 + Xvfb
运行原生 EXE 与隔离安装回归。PowerShell 7.4.7 用于脚本语法检查；这不是 Windows 操作系统。

SDK 从带官方签名的 Debian 软件包解包到临时目录，不进行全局系统安装：

| 路径 | 工具 |
| --- | --- |
| `/tmp/myswy-sysroot` | CMake/CTest、Fcitx 5 SDK 与库 |
| `/tmp/myswy-mingw`、`/tmp/myswy-toolbin` | MinGW 与本地工具包装 |
| `/tmp/myswy-nsis` | NSIS 3.11 |
| `/tmp/myswy-wine`、`/tmp/myswy-wine-prefix2` | Wine 与本任务独立 prefix |
| `/tmp/myswy-wine-runner.py` | CMake 测试启动包装，DISPLAY=:90 |
| `/tmp/myswy-powershell` | PowerShell |

这些目录可能随环境重建消失。普通开发机应安装标准工具链，并使用两个平台 README 的命令。
不要将此 prefix、SDK、字体、凭据或构建目录提交到仓库。Wine 的中文字体仅用于截图验证，
没有随源码/安装包分发。

本环境常用命令：

```sh
cd /workspace/myswyIm
. "$HOME/.cargo/env"
bash scripts/check.sh
LD_LIBRARY_PATH=/tmp/myswy-sysroot/usr/lib/x86_64-linux-gnu \
  /tmp/myswy-sysroot/usr/bin/cmake --build build/windows-cross -j3
LD_LIBRARY_PATH=/tmp/myswy-sysroot/usr/lib/x86_64-linux-gnu \
  /tmp/myswy-sysroot/usr/bin/ctest --test-dir build/windows-cross --output-on-failure
```

交叉测试需要运行 Xvfb :90。测试进程使用独立 prefix，不修改用户真实 Windows 注册。
安装器回归涉及安装、卸载和 COM 注册，不能直接拿真实日常桌面作为临时 fixture。

## 证据范围与交付

源代码中 `docs/STATUS.md` 和 `docs/WINDOWS_TASKLIST.md` 记录当前测试结论。
`build/` 内保留日志、界面截图与历次 installer；新包使用新的 preview revision，旧包不覆盖。
BUILD_INFO.json 记录工具链与构建信息，SHA256SUMS.txt 覆盖完整安装负载。

本地没有真实 Windows 11 / Notepad3 / Android 设备：任务栏最终呈现、实际宿主激活、
多显示器缩放和端到端延迟仍需实机。GitHub Actions 的 Windows job 使用 Windows 2022 +
VS 2022/MSVC，能补原生构建与回归证据，不能代表 Windows 11 桌面验收。
远程 CI 的执行状态单独更新，不能仅依据 workflow 文件声称通过。
本次已在 Windows 2022 + MSVC 工具集 14.44.35207 完成原生六项 CTest、单 EXE 构建和
安装/修复/升级/回滚/卸载；Linux Rust、最低 Rust、ARM 核心与 Fcitx 检查也全部通过。

Windows 构建安装 NSIS 3.11 后，Build.ps1 自动补齐其缺失的 AMD64 stubs/plugins；
固定来源为 `https://deb.debian.org/debian/pool/main/n/nsis/nsis-common_3.11-1_all.deb`，
SHA-256 为 `103a3284c1a5356efa0aba90fdfc391c3cde8e7df8726377cef0f98471584047`，
已与 Debian trixie 签名 Packages 索引核对。只复制固定目录的 Windows PE 文件，
不执行 Debian 包维护脚本；工具链在忽略的 `build/nsis-amd64`，构建不改系统 NSIS。
`build/nsis-compiler-path.txt` 供后续安装回归使用同一编译器；来源/组件哈希包含在安装负载中。

## preview10 复现补充

核心：cargo fmt --all -- --check；cargo clippy --workspace --all-targets --locked -- -D warnings；cargo test --workspace --locked。
C ABI：MSVC /utf-8 /W4 /WX 编译 tests/ffi_smoke.c，链接 release myswy_ime.dll.lib；原生 Windows CTest 七项。
截图：windows_ui_test.exe build/ui-preview10（测试草稿不保存个人设置）；支持全部八页与 96/120/144/192/288 DPI。
基准：MYSWY_BENCH_ROUNDS=100，cargo bench -p myswy-core --bench latency --locked，输出 build/bench-preview10.txt。
打包使用 scripts/package_windows.py --revision 10，AMD64 NSIS 准备与 UTF-8 输入沿用之前已验证流程。
全 target 构建可能被 build-only windows_desktop_test.exe 的杀毒锁定阻止；本批列出 production/test target，
未重建、重命名或运行被提示的辅助程序，未修改杀毒设置。真实应用与物理多屏另验。
