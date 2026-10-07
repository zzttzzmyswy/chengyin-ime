# 多端兼容与整体 release 流程设计（迭代 I07）

状态：设计稿，待评审。日期：2026-10-07。基线：`main` `cac073f`（`version.json` = 0.1.0 preview24）。

本文件只做设计与调研，**不含产品代码改动**。文中标"已实测"的结论都在 13.24（Arch Linux，或容器内）真实执行过；未执行的标"待实测"，不写成已支持。

---

## 0. 结论摘要

1. **IBus 引擎用 C + 现有 C ABI**，不用 Rust `ibus` 绑定。已在 13.24 用真实 `ibus-daemon` 跑通端到端：引擎进程经 C ABI 调用共享核心，客户端收到 `你`（见 §2.3）。工作量小、复用最彻底。
2. **Fcitx5 存在真实跨版本编译缺陷**：`platforms/fcitx5/engine.cpp:22` 用了 `fcitx::StandardPath`，而 Fcitx **5.1.13** 起 `iniparser.h` 改为 include `standardpaths.h`，导致 5.1.13+ 上该类型不可见。已实测：Debian trixie(5.1.12)、Ubuntu 24.04(5.1.7)、Arch(5.1.23) 三方对照，并验证一行 `#include <fcitx-utils/standardpath.h>` 在三方都能编过（§1.4）。**这是 M1「X11/Wayland 可安装产物」的前置阻塞项**，必须先修。
3. **Wayland 覆盖要分两层说**：Fcitx5 插件在 **Weston headless** 下能加载（已实测 `Loaded addon chengyin`）；但 Weston headless 只提供 `zwp_text_input_manager_v1`，**不提供 input-method-v2 / virtual-keyboard**，所以 compositor 级的输入法协议无法在纯 headless 下验证。13.24 可自动化的边界是：插件加载、InputContext 事件级测试、X11/Xvfb 下真实按键。协议/焦点/候选定位必须留实机（§3）。
4. **Release 流程缺一条腿**：现有 CI 只产出 artifact，**没有 tag→Release 的作业**；仓库 `releases` 与 `tags` 均为空（已查）。而 Windows 更新器 `update.cpp` 硬绑定 `releases/download/<tag>/chengyin-windows-x64-<tag>-msvc.exe` + `sha256:` digest，因此 release 流程必须与既有下载契约严格对齐（§4）。
5. **签名策略**：当前包 `"signed": false`（`package_windows.py` BUILD_INFO）。无代码签名证书，必须显式声明，不能含糊。给出三条可选路径与推荐（§4.5）。

---

## 1. IBus 引擎方案

### 1.1 为什么是 C 而不是 Rust `ibus` 绑定

| 方案 | 评估 |
| --- | --- |
| **C + 现有 C ABI（推荐）** | 与 `platforms/fcitx5` 同一模式（薄平台层 + 静态链 Rust 核心）。`include/chengyin_ime.h` 已是稳定 v1，无需新绑定。已实测可编译并通过真实 daemon 端到端。 |
| Rust `ibus` crate / 自己写 GObject 绑定 | 需要引入新依赖并对 GObject 类型系统做 unsafe 绑定；核心已明确"禁用 unsafe、不做平台 I/O"。为 IBus 打破这条边界不划算。 |
| Python + `gi`（`ibus` Python 引擎） | 13.24 有 `gi`，但需 Python 运行时依赖，且与"单文件原生产物"的发布口径不符。 |

结论：**C（C11）+ GLib/IBus C API + 静态链 `libchengyin_ime.a`**，与 Fcitx5 插件对称。

### 1.2 与 Fcitx5 的复用边界

已核对源码，可复用的部分比预期多：

- `platforms/fcitx5/dictionary_loader.{h,cpp}` 中 **`grep -n fcitx` 为空**——它只依赖 C ABI + POSIX，**本身与 Fcitx 无关**。IBus 可复用同一份加载器语义（后台单 worker、一次一个 pending 请求、成功才换、失败保留旧词典）。
- 需要各自实现的：事件/按键映射、预编辑与候选展示、焦点/重置、配置持久化。

建议抽出一个**平台无关的 C++ 小层**（`platforms/common/`）：

```
platforms/common/dictionary_loader.{h,cpp}   # 从 fcitx5/ 迁移，零 Fcitx 依赖
platforms/fcitx5/    → 复用 common，保留 Fcitx 专属：config、InputContext、候选 UI
platforms/ibus/      → 复用 common，新增 IBus 专属：IBusEngine 子类、component XML、配置
```

迁移 `dictionary_loader` 的风险低（无 Fcitx 符号），但属于改动既有平台目录，需单独子迭代 + 回归。

### 1.3 关键映射（C ABI → IBus）

IBus 引擎按 `process_key_event` 收 `(keyval, keycode, state)` 返回 `gboolean`（TRUE=已消费）。映射要点：

| IBus 侧 | C ABI 侧 | 注意 |
| --- | --- | --- |
| `IBusEngine::process_key_event` | `chengyin_session_process` | 返回 `CHENGYIN_HANDLED` 才 return TRUE |
| `update_preedit_text` | `CHENGYIN_TEXT_DISPLAY_PREEDIT` + `chengyin_session_preedit_cursor` | 光标是 **UTF-8 字节**偏移，IBus 要按字符（`g_utf8_strlen` 截断） |
| `update_lookup_table` / `update_auxiliary_text` | `chengyin_session_candidate_count` / `CHENGYIN_TEXT_CANDIDATE` | 候选页 9 条，`CHENGYIN_TEXT_CANDIDATE_PINYIN` 做注释 |
| `commit_text` | `CHENGYIN_TEXT_COMMIT` | **每次 process 后都要读一次**（包含 handled=false），读到即写回宿主 |
| `focus_in` / `focus_out` / `reset` | `chengyin_session_reset` | 每个 InputContext 独立 Session |
| 上屏成功 | `chengyin_session_learn_commit` | 只在宿主确认写入后调用 |
| 空格/Enter | `CHENGYIN_KEY_SPACE` / `CHENGYIN_KEY_ENTER` | 显式映射物理键，不能直接传 keyval |
| Ctrl/Alt/Super | `CHENGYIN_MOD_*` | 带修饰键直接透传 |

两个易错点（写实现时必须覆盖测试）：

1. **commit 必须在 process 之后无条件读取一次**，漏读会导致宿主丢字或重复上屏。头文件已明确这个合约。
2. **`chengyin_session_text` 是"先查容量"协议**：传 `NULL` 拿含 NUL 的所需容量，容量不足**完全不写**（不会截断出半个 UTF-8）。IBus 侧要用两段式调用，不能假定 256 够。

### 1.4 IBus 打包

- **component XML**：装到 `${datadir}/ibus/component/chengyin.xml`，`<exec>` 指向 `libexec` 下的引擎可执行文件。参考 `simple.xml` 的结构（name/description/exec/version/author/license/homepage/textdomain/engines）。
- **引擎可执行文件**：IBus 引擎是独立进程（与 Fcitx5 的进程内插件不同）。静态链 Rust 核心 → 单文件，无额外 `.so` 依赖。
- **包**：
  - Debian：扩展现有 `packaging/debian` 思路，新增 `scripts/package_ibus_deb.py`，复用 `package_deb.py` 的**严格校验**风格（native arch 检查、`dpkg-shlibdeps` 解析依赖、不覆盖同名产物、`--root-owner-group`）。
  - Arch：新增 `packaging/arch/PKGBUILD`（13.24 本地可 `makepkg` 验证）。
  - 两者都要装 `LICENSE` + 来源说明。

> **已实测的包装约束**：`scripts/package_deb.py` 在 13.24 **无法直接跑通**——Arch 无 Debian shlibs 元数据，`dpkg-shlibdeps` 报 `no dependency information found for /lib/libFcitx5Core.so.7`。Debian 包必须在 Debian/Ubuntu 容器或 CI 里构建（§3.4 已给出容器命令）。

### 1.5 IBus 落地阶段

**不**在本迭代实现。建议拆成独立子迭代：

- **I-IBus-1**：`platforms/common/` 抽取 + `platforms/ibus/` 骨架（引擎注册、按键、预编辑、提交、候选、reset/焦点）。
- **I-IBus-2**：配置持久化（对齐 Windows/Fcitx 的语义）、词典热切换、component XML + 两种包。
- **I-IBus-3**：无头回归（真实 daemon 端到端）+ 实机验收清单。

**I09 已落地 I-IBus-1（2026-10-07）**，与本节设想的差别有三处，都记在下面：

| 项 | 结果 |
| --- | --- |
| `platforms/common/` 抽取 | `dictionary_loader.{h,cpp}` 原样迁入，Fcitx5 改为引用 common；行为不变（CTest 2/2） |
| 引擎可测性 | 按键映射/提交/文本协议/光标换算放进**不依赖 GLib 的** `engine.c`，`ibus_engine.c` 只做 IBus 桥接；A 档 CTest 因此可直接驱动 |
| 无头端到端 | `platforms/ibus/e2e.sh` 复用 §2.3 方法（隔离 HOME + `--single`），断言 `PREEDIT: ni` 与 `COMMIT: 你` |

两处实现期发现的坑（设计阶段未预见，已写入代码注释）：

1. **libibus 的浮引渡让语义**：`ibus_text_new_from_string` / `ibus_lookup_table_new` 返回 floating 引用，而 `ibus_engine_commit_text` / `update_preedit_text` / `update_auxiliary_text` / `update_lookup_table` / `lookup_table_append_candidate` **会 sink 掉它**（接管所有权）。调用后再 `g_object_unref` 会二次释放，实测表现为 `ibus_serializable_serialize_object: assertion failed` 后段错误。同理 `ibus_component_add_engine` 与 `ibus_bus_register_component` 接管 component/desc。
2. **组件查找路径不是 `XDG_DATA_HOME`**：`ibus-daemon` 只扫 `IBUS_COMPONENT_PATH`（默认 `/usr/share/ibus/component`），并把扫描结果缓存在 `$XDG_CACHE_HOME/ibus/bus/registry`。本迭代按范围不产出 XML，端到端直接拉起引擎进程（引擎启动时自行在总线上注册组件），符合 §1.4 把 XML 留给 I10 的划分。

可测性的边界同 §3.2 的 A 档：引擎逻辑、协议编解码、提交语义已覆盖；焦点路由、候选窗定位、compositor 协议仍留实机。


---

## 2. 已实测证据（IBus 可行性）

### 2.1 环境

```
Arch Linux, Linux 7.2.7-zen1
ibus 1.5.34, /usr/bin/ibus-daemon 存在, pkg-config ibus-1.0 = 1.5.34
fcitx5 5.1.23-1, Xvfb 21.1.24, weston 15.0.1, docker 29.8.1 / podman(rootless)
Rust 1.98.0, cmake 4.4.3, python 3.14.7
```

### 2.2 C + C ABI 编译通过

一个最小 IBus 引擎（`G_DEFINE_TYPE` 子类 `IBusEngine`，在 `process_key_event` 里调 `chengyin_session_process` / `chengyin_session_text` / `ibus_engine_commit_text`）在 `-Wall -Wextra -Werror` 下编译通过，链接 `libchengyin_ime.so`：

```
IBUS+C-ABI COMPILE: OK
```

### 2.3 端到端跑通（关键证据）

脚本 `evidence/e2e.sh`：新建隔离 `HOME` + `XDG_RUNTIME_DIR` + 私有 session bus → 起真实 `ibus-daemon --single` → 引擎进程注册并 `set_global_engine` → 客户端进程接 IBus 信号。

结果：

```
[client] set_global_engine => OK
[client] PREEDIT: 
[client] COMMIT: 你
```

**这是经真实 IBus daemon、真实 D-Bus、真实客户端信号链路，把 Rust 共享核心的候选提交出来的完整路径**，不是 mock。说明 IBus 方案在 13.24 可自动化验证，不需要实机即可覆盖"引擎逻辑 + 协议编解码 + 提交语义"。

> 说明：过程中还暴露一个 IBus 环境细节——`ibus-daemon` 会在 **`$HOME/.config/ibus/bus/`** 写地址文件，残留条目会让新 daemon 报 `current session already has an ibus-daemon`（且**必须用 `--single`**）。无头测试脚本必须用隔离 `HOME`。这条直接写进将来的 CI 脚本，避免踩坑。

---

## 3. X11 / Wayland 覆盖路径

### 3.1 现状与真实缺陷

Fcitx5 插件当前状态（已实测，非推测）：

| 环境 | Fcitx 版本 | 插件构建 | CTest |
| --- | --- | --- | --- |
| Debian trixie 容器 | 5.1.12 | ✅ | ✅ 2/2 |
| Ubuntu 24.04 容器 | 5.1.7 | ✅ | ✅ 2/2 |
| Arch 13.24 本机 | 5.1.23 | ❌ | — |

Arch 失败原因（精确到行）：

```
platforms/fcitx5/engine.cpp:22: error: 'fcitx::StandardPath' 尚未声明；你是说 'fcitx::StandardPaths' 吗？
platforms/fcitx5/engine.cpp:27: error: 'StandardPathTempFile' 在命名空间 'fcitx' 中不是一个类型名
```

根因已定位到上游版本分界：

- Fcitx **≤5.1.12**：`fcitx-config/iniparser.h` → `#include <fcitx-utils/standardpath.h>`
- Fcitx **≥5.1.13**：改为 → `#include <fcitx-utils/standardpaths.h>`（`standardpaths.h` 文件本身 5.1.13 才出现），旧类型变成 `FCITXUTILS_DEPRECATED_EXPORT`

`engine.cpp` 用了 `StandardPath`/`StandardPathTempFile` 但**只靠 `iniparser.h` 间接引入**，于是 5.1.13+ 丢失该类型。

**修复已验证**：在 `engine.cpp` 显式 `#include <fcitx-utils/standardpath.h>`。

| 组合 | 结果 |
| --- | --- |
| 5.1.12 + 显式 include + `-Werror`（不抑制） | ✅ 编过 |
| 5.1.23 + 显式 include + `-Werror=deprecated-declarations` 抑制 | ✅ 编过，2/2 通过 |

> 注意：5.1.23 下 `StandardPath` 是 deprecated，`-Werror` 会因 `-Wdeprecated-declarations` 失败。所以**只加 include 还不够**，还要决定策略：迁移到 `StandardPaths`（新 API，但 5.1.13 以下没有），或对这两个符号局部抑制弃用告警。建议子迭代里做**版本化适配**：`#if FCITX_VERSION >= 5.1.13` 走新 API，否则走旧 API；这样两代都能用且无告警。这是需要单独设计与测试的活，不塞进本设计。

**I08 已按上述"版本化适配"落地（2026-10-07）**，与本节设想的唯一差别是版本来源：Fcitx 头文件**不导出**版本宏（`FCITX_VERSION` 在 5.1.23 头文件里不存在），改由 CMake 的 `Fcitx5Core_VERSION` 生成 `CHENGYIN_FCITX_VERSION`（数值化，`5.1.12 → 50112`）传给 `engine.cpp`。三方复测结果：

| 环境 | Fcitx | 派生的宏 | `iniparser.h` 引入 | 构建 | CTest |
| --- | --- | --- | --- | --- | --- |
| Ubuntu 24.04 容器 | 5.1.7 | 50107 | `standardpath.h` | ✅ | ✅ 2/2 |
| Debian trixie 容器 | 5.1.12 | 50112 | `standardpath.h` | ✅ | ✅ 2/2 |
| Arch 13.24 本机 | 5.1.23 | 50123 | `standardpaths.h` | ✅ | ✅ 2/2 |

弃用告警不需要抑制：走新 API 分支时不再引用 `StandardPath`/`StandardPathTempFile`。CI 新增 `fcitx5-cross-version` 作业固定 5.1.12/5.1.23 两侧。

### 3.2 Wayland 与 X11 的可自动化边界

已实测：

- **Weston headless** 可起（需 `XDG_RUNTIME_DIR` 且权限 0700），暴露 `wl_compositor v5`、`wp_viewporter`、`xdg_wm_base v5`、`zwp_input_panel_v1`、**`zwp_text_input_manager_v1 v1`**。
- **Weston headless 不暴露** `zwp_input_method_v2`，也不暴露 virtual-keyboard。
- **Fcitx5 插件在 Xvfb 下真实加载成功**：日志出现 `Loaded addon chengyin`（`OnDemand=False` 时）。

⚠️ **一处未定论，不要当结论用**：探针日志同时出现 `Found 0 input method(s) in addon chengyin`。查上游 `inputmethodmanager.cpp:196` 可知该行来自 `engine->listInputMethods()`，即插件**自己的** `Engine::listInputMethods()` 返回值。本次探针用的 `chengyin.so` 是**直接拷贝的构建产物**，没有走 `cmake --install`，其 inputmethod conf 未被 Fcitx 正常发现——**尚不能区分**"插件实现有问题"和"探针装配不完整"。因此本设计**不主张**"IM 注册已通过"；该项留到 I08/I09 用正规 `cmake --install` 流程验证。这也是把它列为待办而非既成事实的原因。

**I08 已用正规 `cmake --install` 判定（2026-10-07，Fcitx 5.1.23）：IM 注册通过，"Found 0" 属探针装配问题。** 做法：`cmake --install` 到独立 `CMAKE_INSTALL_PREFIX`（走 `$HOME/.local/lib/fcitx5` 与 `share/fcitx5/{addon,inputmethod}`），再用框架自己的 `AddonManager` + `InputMethodManager` 枚举——`addonInfo("chengyin")` 存在、`foreachEntries` 枚举到 `name=Chengyin Pinyin (Prototype) addon=chengyin label=拼`；删掉安装出来的 `addon/`、`inputmethod/` 后同一探针立刻变 NO（阴性对照成立）。

根因（上游可查）：`inputmethodmanager.cpp` 的 `Found N input method(s) in addon X` 只对**非 OnDemand** addon 打印（`loadDynamicEntries` 开头即 `if (!addonInfo || addonInfo->onDemand()) continue;`）。本插件的 `chengyin-addon.conf` 是 `OnDemand=True`，其入口按设计由 `inputmethod/*.conf` 静态注册，本来就不经 `listInputMethods()`，所以那行 `Found 0` 与插件实现无关。原探针只是把 `chengyin.so` 拷进构建目录、没安装 `inputmethod/chengyin.conf`，于是两侧都没有入口。

因此自动化能力分三档：

| 档 | 能验证 | 不能验证 |
| --- | --- | --- |
| A. 单元/事件级（现有 CTest） | InputContext 事件转换、UTF-8 上屏、会话隔离、陈旧候选、reset/敏感字段、词典热切换 | 焦点路由、光标定位、真实候选窗 |
| B. 无头集成（**本设计新增**） | **已实测**：Xvfb 下插件加载（`Loaded addon chengyin`）；IBus 真实 daemon 端到端提交。**待打通**：addon/IM 注册（§3.2 未定论）、Xvfb 下"真实 X 按键 → 上屏"闭合（需装到正规路径后压键，本设计未做） | compositor 协议（text-input-v3 / input-method-v2） |
| C. 实机（**必须 MYSWY**） | KDE/GNOME/sway 的 Wayland 协议、GTK/Qt immodule、候选窗定位、多屏/分数缩放 | — |

**B 档能做但 C 档绝不能省的**，原因就是 §3.2 那条：headless compositor 没有 input-method 协议，谁也没法在 13.24 上"模拟"出 KWin 的 text-input-v3 行为。不要用 B 档结果声称 Wayland 支持。

### 3.3 实机测试矩阵（G* 关口）

沿用并细化 `docs/COMPATIBILITY.md` 已有的表，按三层记录（系统/桌面/应用/Fcitx 版本、图形后端、配置、输入序列、实际结果）：

| 会话 | 应用 | 重点 |
| --- | --- | --- |
| X11 | GTK3/4、Qt5/6、XIM 客户端 | 预编辑、候选矩形、焦点切换、终端快捷键 |
| KDE Plasma Wayland | Qt 原生 Wayland、GTK、Firefox | text-input-v3、缩放、多屏 |
| GNOME Wayland | GTK、Qt、Firefox、Electron | immodule 依赖差异 |
| wlroots（先 Sway） | GTK/Qt、终端、Chromium | compositor 协议版本与配置 |
| 任意 Wayland 的 XWayland | X11 版 Electron、Qt/XCB | 混合焦点、光标定位 |
| 沙盒 | Flatpak、（必要时 Snap） | immodule/门户继承 |

> 明确反对：全局盲设 `GTK_IM_MODULE`/`QT_IM_MODULE` 并当作 Wayland 万能修复——已写在 `COMPATIBILITY.md`，继续保持。

### 3.4 建议的自动化命令（可直接用于 CI）

```bash
# A 档：现有 Fcitx5 无头 CTest
cmake -S platforms/fcitx5 -B build/fcitx5 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/fcitx5 && ctest --test-dir build/fcitx5 --output-on-failure

# 跨版本矩阵（本设计新增，已验证可行）
docker run --rm -v "$PWD":/src:ro -w /tmp debian:trixie bash -c \
  'apt-get update -qq && apt-get install -y -qq build-essential cmake ninja-build pkg-config libfcitx5core-dev &&
   cp -r /src /w && cd /w && rm -rf build && cmake -S platforms/fcitx5 -B build -G Ninja &&
   cmake --build build && ctest --test-dir build --output-on-failure'
# 同法对 ubuntu:24.04；Arch 侧用 makepkg/本地 5.1.23

# B 档：Xvfb + fcitx5 加载
Xvfb :97 -screen 0 1024x768x24 &
DISPLAY=:97 fcitx5 -d --enable=chengyin -r    # 断言日志含 "Loaded addon chengyin"
```

---

## 4. Release 流程

### 4.1 现状（已核实）

- `version.json` 是唯一手写版本源；`scripts/version.py --check/--generate/--verify-bump` 已能证明"改一处，全派生面跟着动"（`scripts/check.sh` 第一步就跑 `--check`）。
- CI 六作业（`msrv` / `rust` / `portable-core` / `windows-tsf` / `fcitx5`）**只 `upload-artifact`，没有 tag 触发、没有 Release 作业**。
- 远端 `releases` = `[]`，`tags` = `[]`——**从未发过 Release**。
- 但 Windows 更新器**已经实现并绑定**了发布契约（`platforms/windows/update.cpp`）：
  - 轮询 `api.github.com/repos/zzttzzmyswy/chengyin-ime/releases?per_page=20`
  - 只认 `draft == false`
  - 资产名必须恰为 `chengyin-windows-x64-<tag>-msvc.exe`（`v` 前缀会被剥离）
  - URL 必须恰为 `https://github.com/zzttzzmyswy/chengyin-ime/releases/download/<tag>/<同名文件>`
  - `digest` 必须 `sha256:` + 64 位小写十六进制
  - 标签必须解析成比当前更新的版本

**所以 release 流程不是"从零设计"，而是"补齐一条已存在的契约"。** 任何偏离（例如资产改名、用大写 SHA、draft 留下）都会让应用内更新静默失效——`test_update.cpp` 已对这些做了回归。

### 4.2 流程（阶段化）

```
人工 bump version.json
   └─ python3 scripts/version.py --generate && --check && --verify-bump
   └─ scripts/check.sh（fmt / clippy -D warnings / workspace tests / C ABI / CLI）
   └─ 本地或 CI 产出各平台包
        ├─ Windows: Build.ps1 + package_windows.py → chengyin-windows-x64-<tag>-msvc.exe
        ├─ Debian : package_deb.py（须在 Debian/Ubuntu 容器）
        ├─ Arch   : PKGBUILD（待建）
        └─ IBus   : 待 I-IBus-* 子迭代
   └─ 生成 SHA256 清单 + RUNTIME_LICENSES
打 tag（v<tag>）并推送
   └─ CI release 作业（**尚未存在，需新建**）：
        仅 tag 触发；重建各平台产物；产出 SHA256SUMS；创建 **draft** Release
   └─ 人工核对 draft：资产名、digest、版本、许可、CHANGELOG
   └─ 发布（draft=false）→ 应用内更新开始可见
```

要点：

- **禁止 `--clobber`**：同 tag 资产一律不覆盖。要改就用新 preview 号（`version.json` 的 `preview` 是单调的）。
- **draft 先行**：先建 draft、人工核对、再发布。因为更新器只认 `draft=false`，draft 阶段不会误推给用户。
- **版本单调性**：`preview` 上限 998（`version.py` 已校验，为未来 tag 留位）。回滚靠"再发一个更高 preview"，**不靠删旧 Release**。
- **产物命名与 digest 大小写**：`package_windows.py` 内部用大写 SHA-256（`SHA256SUMS.txt`），而 GitHub Release 的 `digest` 字段是小写。更新器只接受小写。这是**已实测的真实坑**（`update.cpp` 校验小写 hex），CI 生成 Release 时必须分别处理，不能混用。

### 4.3 版本源扩展建议

`version.py` 目前只服务 Windows。多端后建议扩为"一个 tag，多平台产物名"：

```
chengyin-windows-x64-<tag>-msvc.exe        # 已有契约，不可改
chengyin-<tag>-ubuntu24.04-amd64.deb       # 建议
chengyin-<tag>-arch-x86_64.pkg.tar.zst     # 建议（PKGBUILD 产出）
```

**Windows 名字一个字符都不能动**（更新器硬绑定）。新增名走各自平台脚本，并让 `version.py --check` 一并校验，防止手写漂移。

### 4.4 回滚与升级路径

| 场景 | 做法 |
| --- | --- |
| 升级 | 装更高 preview 的包；Windows 由应用内更新器（校验 digest 后原子写） |
| 回滚 | **发一个新的更高 preview 号**（内容回退），不删不覆盖旧 Release |
| 用户数据 | Windows 安装器已管理同版修复/升级/回滚与用户文件保留（CI 隔离 runner 已覆盖）；Linux 包不动用户配置/词典 |
| 坏包 | digest 不匹配则更新器拒绝安装（`verifyReleaseImage`）；Release 页保留旧版可手动下载 |

### 4.5 签名策略（明确声明）

现状：**无签名**。`package_windows.py` 写 `"signed": False`；安装 EXE 未做 Authenticode，Linux 包未做 GPG/detached 签名。

三条路径：

| 方案 | 成本 | 说明 |
| --- | --- | --- |
| **A. 明确声明未签名（推荐先做）** | 0 | 在 Release 说明、`BUILD_INFO.json`、README 顶部显著写"未签名"，并给出 SHA256 供人工核对。诚实、可立即交付。 |
| B. Linux 包 GPG 签名 | 低 | 生成项目密钥，`dpkg-sig`/`gpg --detach-sign`，公钥随文档发布。可先只做 Linux。 |
| C. Windows Authenticode | 高 | 需 OV/EV 代码签名证书（有年费、需组织身份审核）。**当前不承诺**。 |

推荐：**A 立即执行 + B 在 Linux 包稳定后补**。C 需要用户决策与预算，列为待决，不在本设计内承诺。

---

## 5. 分阶段计划与验收

沿用「同一时间只做一个开发迭代」。建议拆分（依赖顺序）：

| 子迭代 | 内容 | 验收命令 | 需要 MYSWY |
| --- | --- | --- | --- |
| **I08** | Fcitx5 跨版本兼容（§3.1）：版本化适配 `StandardPaths`/`StandardPath`，修 5.1.13+ 构建 | 三方构建+CTest 全绿（trixie/ubuntu24.04/Arch）；`scripts/check.sh` 不变绿 | 否 |
| **I09** | `platforms/common/` 抽取 + IBus 引擎骨架（§1） | 编译 `-Werror`；无头 daemon 端到端断言 `COMMIT: 你`；A 档 CTest | 否 |
| **I10** | IBus 打包（component XML、Debian + PKGBUILD） | `test_deb.py` 隔离根通过；`makepkg` 产物可装 | 否 |
| **I11** | Release 自动化作业（tag→draft Release，含 SHA256 清单） | 干跑一次 draft，资产名/digest/版本与 `update.cpp` 契约逐项对齐 | 否 |
| **G-Win** | Windows 实机矩阵（`WINDOWS_RELEASE_GATE.md`） | 单包 + 检查单 | **是** |
| **G-Lin** | Linux 会话矩阵（§3.3） | 按会话记录系统/桌面/应用/协议 | **是** |

每个子迭代的产物：分支 + PR + 子 issue 回贴（命令与输出摘要、逐条对照验收）。

---

## 6. 风险清单

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| R1 | Fcitx 5.1.13+ 构建失败（§3.1，**已确认存在**） | 新版发行版（Arch/Fedora）装不上 | I08 版本化适配 + 三方 CI 矩阵 |
| R2 | 无头 compositor 无 input-method 协议 | 无法在 13.24 声称 Wayland 支持 | 严格区分 A/B/C 档证据；C 档走 G-Lin |
| R3 | Release 契约（资产名/小写 digest/draft）不满足 | 应用内更新静默失效 | I11 干跑 + `test_update.cpp` 既有回归；禁止 `--clobber` |
| R4 | Windows 未签名 | SmartScreen 警告、用户信任 | §4.5 方案 A 显式声明；C 待用户决策 |
| R5 | IBus 引擎是独立进程，与 Fcitx5 进程内插件模型不同 | 生命周期/敏感字段/焦点语义需重做 | I09 单独设计+测试，不照搬 Fcitx 状态机 |
| R6 | `dictionary_loader` 迁移改动既有平台代码 | 可能回归 Fcitx5 | I09 内先迁移+全量 CTest 回归，再动 IBus |
| R7 | Linux 包在 Arch 上无法构建（`dpkg-shlibdeps`，已实测） | 本地误判"打包成功" | 打包只在对应发行版容器/CI 执行 |
| R8 | `preview` 号用尽（上限 998） | 无法再发版 | 远未接近；在 STATUS 记录规则 |

---

## 7. 本设计的边界（不做的事）

- 不写 IBus 产品代码，不改 Fcitx5 源码，不动 `version.json`，不改 workflows。
- 不声称任何未实测的兼容性；不改动 `docs/COMPATIBILITY.md` 的既有证据口径（仅在其上补设计）。
- 不做实机操作（Windows 实装、真实桌面会话）。

---

## 附：本设计引用到的实测证据（`docs/design-evidence/`，非产品代码）

- `ibus-engine-probe.c` / `ibus-client-probe.c` — IBus 引擎与客户端探针（C + C ABI）
- `ibus-e2e.sh` — 无头端到端脚本，自带隔离 HOME/bus，末尾断言 `PASS: end-to-end commit observed`
- `README.md` — 各条结论的复跑命令（含 Fcitx5 三方容器矩阵）

对照结论：

- Fcitx5 构建矩阵：`debian:trixie`(5.1.12) ✅、`ubuntu:24.04`(5.1.7) ✅、Arch 本机 5.1.23 ❌（显式补 `#include <fcitx-utils/standardpath.h>` 后 ✅）
- IBus：真实 daemon + C ABI 端到端 ✅（`COMMIT: 你`）
- `scripts/check.sh` 基线全绿 ✅
