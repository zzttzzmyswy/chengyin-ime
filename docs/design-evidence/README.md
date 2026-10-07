# I07 设计阶段实测证据

本目录只服务 `docs/MULTIPLATFORM_RELEASE_DESIGN.md`，**不是产品代码、不参与构建**。目的：让设计里每条"已实测"结论可被独立复跑。

环境：13.24（Arch Linux，Linux 7.2.7-zen1），Rust 1.98.0，cmake 4.4.3，python 3.14.7，docker 29.8.1。

## 1. Fcitx5 跨版本矩阵（对应设计 §3.1）

```bash
# Debian trixie (Fcitx 5.1.12) —— 通过
docker run --rm -v "$PWD":/src:ro -w /tmp debian:trixie bash -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq build-essential cmake ninja-build pkg-config libfcitx5core-dev
  cp -r /src /w && cd /w && rm -rf build
  cargo build --release -p myswy-ffi --locked
  cmake -S platforms/fcitx5 -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build && ctest --test-dir build --output-on-failure'

# Ubuntu 24.04 (Fcitx 5.1.7) —— 通过（同上，镜像换 ubuntu:24.04，需先装 curl+rustup）
# Arch 本机 (Fcitx 5.1.23) —— 失败，原因见设计 §3.1
```

要点结论：Fcitx **≤5.1.12** 的 `fcitx-config/iniparser.h` include `standardpath.h`；**≥5.1.13** 改为 include `standardpaths.h`，`StandardPath`/`StandardPathTempFile` 变为 `FCITXUTILS_DEPRECATED_EXPORT`。`engine.cpp` 未显式 include，故 5.1.13+ 编不过。

修复验证：
- 5.1.12 + 显式 `#include <fcitx-utils/standardpath.h>` + `-Werror`（不抑制）→ 编过
- 5.1.23 + 显式 include + `-Wno-error=deprecated-declarations` → 编过，CTest 2/2

## 2. IBus 无头端到端（对应设计 §2）

脚本：`ibus-e2e.sh`（探针原型的整理版）。要点（踩过的坑都写在里面）：

- 必须用**隔离 HOME**：`ibus-daemon` 在 `$HOME/.config/ibus/bus/` 写地址文件，残留条目会让新 daemon 报 `current session already has an ibus-daemon`。
- 必须 `--single`，否则同样报已有 daemon。
- `$XDG_RUNTIME_DIR` 必须存在且权限 0700。

观察到的输出：

```
[client] set_global_engine => OK
[client] PREEDIT:
[client] COMMIT: 你
```

引擎侧日志显示候选来自 Rust 核心（`candidates=8 first=你`，空格 `flags=1` 后 `committed 你`）。

## 3. Fcitx5 在 Xvfb 下加载插件（对应设计 §3.2）

```
Xvfb :97 -screen 0 1024x768x24 &
DISPLAY=:97 FCITX_ADDON_DIRS=<staged>/usr/lib/fcitx5 fcitx5 -d --enable=myswy
# 断言日志含: Loaded addon myswy
```

注意 `myswy-addon.conf` 里 `OnDemand=True` 时该行**不会**出现（框架按需加载），探针里改成 `False` 才能观察到。

## 4. 基线

`scripts/check.sh` 在 `cac073f` 上全绿（fmt / clippy -D warnings / workspace tests / Release / C ABI smoke / CLI）。
