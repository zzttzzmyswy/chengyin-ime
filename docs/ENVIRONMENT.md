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
