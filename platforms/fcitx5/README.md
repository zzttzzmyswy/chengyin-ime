# Fcitx 5 Linux 原型

目标框架版本 ≥5.1，C++17；实际本地验证版本见 STATUS。Arch 包默认使用随包安装的完整词库；默认源码构建及当前 CI 的 deb 包仍使用 98 条内置演示词条，都可在 Fcitx 配置工具里改成自定义 TSV。当前为开发预览，不要把它设为唯一的日常输入法。

## 构建与暂存安装

Debian/Ubuntu 示例，发行版包名可能不同：

```sh
sudo apt-get install build-essential cmake extra-cmake-modules libfcitx5core-dev fcitx5 fcitx5-config-qt
cargo build --release -p chengyin-ffi --locked
cmake -S platforms/fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/fcitx5
ctest --test-dir build/fcitx5 --output-on-failure
DESTDIR="$PWD/build/stage" cmake --install build/fcitx5
```

最后一步只把文件放在 `build/stage`，不修改系统。默认动态插件目录取自发行版 Fcitx SDK 的 `FCITX_INSTALL_ADDONDIR`；Debian amd64 通常为 `/usr/lib/x86_64-linux-gnu/fcitx5`。可用 `-DCHENGYIN_ADDON_DIR=...` 指定，但必须匹配框架实际查找目录。交叉编译时通过 `-DCHENGYIN_RUST_LIBRARY=/path/to/libchengyin_ime.a` 指向同一目标架构的 Rust 静态库。

插件静态链接本项目 Rust 核心，动态依赖 Fcitx 框架；不需另装 `libchengyin_ime.so`。打包时保留 `LICENSE`（GPL-3.0-or-later）并列明平台依赖许可证。

## 在测试桌面安装

在准备好的测试桌面、检查暂存目录和 `build/fcitx5/install_manifest.txt` 后执行：

```sh
sudo cmake --install build/fcitx5
```

重新启动该桌面的 Fcitx 5 服务，在其配置工具中添加 **澄音输入法（全拼原型）**，使用框架配置的快捷键切换。X11/Wayland 的启动与应用模块配置遵循发行版和桌面说明，见 [兼容性矩阵](../../docs/COMPATIBILITY.md)。本轮开发没有替换当前系统输入法。

源码安装的卸载依据 `install_manifest.txt`：本项目模块、两个注册文件和两个文档文件，并在 Fcitx 配置工具移除该输入法。不批量删除整个 Fcitx 目录。使用下面的 Debian 包安装时，由包管理器跟踪和移除这些文件。

## 设置（与 Windows 端对齐的部分）

在本批（I17）之前，Fcitx 插件的设置页只有“词典 TSV 绝对路径”，候选页宽固定为 9，且插件从不把用户偏好告诉共享核心。现在配置工具里有下面这些项，语义与默认值与 Windows 设置页一致（`Learning` 为 I19 新增）：

| 选项 | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `PageSize` | 枚举 5/7/9 | **5** | 每页候选数。**默认值由 9 改为 5**，与 Windows 一致 |
| `Associations` | 布尔 | 开 | “提交中文后显示联想词” |
| `Learning` | 布尔 | 开 | “根据选词习惯排序（本机保存，不联网）”。见下文「选词学习」 |
| `DefaultEnglish` | 布尔 | 关 | “启动输入服务时默认使用英文” |
| `ShiftSwitch` | 枚举 不使用/左 Shift/左右 Shift | **左 Shift** | “Shift 切换键”。单独轻按才切换 |
| `ChinesePunctuation` | 布尔 | 开 | “中文模式使用中文标点” |
| `Fuzzy` | 子分组，11 个布尔 | 全关 | 模糊音（双向匹配）：`zh↔z`、`ch↔c`、`sh↔s`、`n↔l`、`f↔h`、`l↔r`、`an↔ang`、`en↔eng`、`in↔ing`、`ian↔iang`、`uan↔uang` |
| `Correction` | 子分组，4 个布尔 | 全关 | 常见键盘失误：相邻字母按反、漏按一个字母、QWERTY 相邻键误按、重复按键 |
| `Dictionaries` | 子配置列表 | 空 | 「附加词库」，见下文「词典配置与更新」。每项含显示名、绝对路径与启用开关，可增删、可逐条停用 |

保存后立即对正在运行的空闲会话生效，不需要重启。**正在输入的组合不受影响**：设置只在一个组合结束后应用，因此保存设置不会丢键，也不会改变或取消当前组合；下一次输入即按新设置进行。候选列表的页宽与数字选词键始终跟随核心实际生效的页宽，二者不会错位。

只改上述设置（`DictionaryPath` 未变）时**不重载词典**：直接应用并写回配置。只有 `DictionaryPath` 变化才走后台加载。设置写回磁盘失败时会像词典保存失败一样在提示里报告，不静默。

`PageSize` 用枚举而不是整数范围：配置工具因此只提供 5/7/9 三个选项，手工写入集合外的值会退回该选项的默认值，而不会被悄悄夹到用户没选过的宽度。

Fcitx 配置工具只显示本批已有的项。中文标点、中英切换与 Shift 切换自 I20 起已接入，见下文「中/英文模式与中文标点」；**多词库管理**自 I21 起接入，见下文「词典配置与更新」。

## 选词学习（本机持久保存）

本批（I19）加入 `Learning` 开关，默认开，文案与 Windows 设置页一致。打开时，**每次中文上屏都会记录一次选词**，
同拼音的候选排序随之向用户的习惯倾斜；关了就不再记录，也不会学习。学习数据只保存在本机，不联网、不上传。

**档案位置**：`${XDG_DATA_HOME:-$HOME/.local/share}/fcitx5/chengyin/profile.bin`。目录权限 `0700`，文件 `0600`，
因为里面是用户自己的输入文字。可用 `XDG_DATA_HOME` 改变位置，插件通过 Fcitx 的 StandardPaths 解析，
不自己拼路径。

**清除与导入导出没有配置界面按钮**，按任务卡要求用文件操作：

- 清除：删除 `profile.bin`，然后 `fcitx5-remote -r`（或在配置工具里保存一次设置）。
- 导出/备份：直接复制 `profile.bin`。
- 导入：把备份放回该路径并 `fcitx5-remote -r`。

重新读取时**已排队但尚未落盘的记录会被丢弃**：这些记录是针对被替换掉的旧内容确认的，重放到新内容上会得到
错误的习惯，所以宁可丢弃。

**损坏的档案不会被覆盖**：文件存在但校验失败（或不是普通文件、超过 4 MiB）时，插件用空档案运行、
在候选面板提示一次、并在**本次进程内停止写盘**，原文件一字不动，方便用户自行检查或修复。
此时内存中的学习仍然生效（本次输入内排序会变），只是重启后不保留。

**敏感输入不学习**：密码/私密字段的按键由插件直接透传，既不组合也不记录。

**多个输入上下文共享同一份学习结果**：插件持有一份内存主档案，空闲会话取它的快照，所以在 A 窗口学到的排序
在 B 窗口下一次输入就生效。正在组合中的会话不会被改动，等它空闲后再切换。

`Learning` 关闭时，共享核心会同时清空会话内的最近选词与联想短语状态；这是"关闭即停止个性化"的预期行为，
**不是**删除已保存的档案——档案还在，重新打开开关就继续用。

**写盘在后台线程**：按键线程只把已确认的选词拷进一个 128 槽的有界队列，序列化、`fsync`、原子替换都在工作线程；
队列满则丢弃并计数，按键不会被磁盘拖慢。写盘失败按有上限的次数与时间窗重试（4 次 / 30 秒，退避翻倍起于 50 ms），
超限则丢弃这一批并记入日志，内存中的排序不受影响。退出时尽力刷盘但有上限，不会让 Fcitx 卡在退出上。
日志与提示里**只出现条数与错误原因，不出现拼写或文字内容**。

与 Windows 的差异（不声称"已完全一致"）：

- 单个插件进程，**没有跨进程锁**，也没有 Windows 的 generation/revision 映射通道；同一台机器上多个
  Fcitx 实例（不同 `XDG_DATA_HOME`）各写各的档案。
- 没有设置界面里的"清除/导入"按钮，按上面的文件操作完成；Windows 有。
- 未做多屏、真实桌面焦点/候选窗的实机验证，只跑无头与真实 `fcitx::Instance` 测试。


## 中/英文模式与中文标点（I20）

本批之前插件不处理中/英状态与标点，任何非字母键都由核心处理后再透传给应用，ASCII 逗号就是 ASCII 逗号。现在有了与 Windows 设置页一致的三个选项，以及输入状态栏上的两个可点击动作。

| 选项 | 默认 | 行为 |
| --- | --- | --- |
| `DefaultEnglish` | 关 | 只决定**新建**输入上下文的初始模式；改它不改变已存在上下文的当前模式（与 Windows 设置页文案一致） |
| `ShiftSwitch` | 左 Shift | 单独轻按并释放该键切换中/英；Shift+字母、与其他键组合、长按重复都不切换 |
| `ChinesePunctuation` | 开 | 中文模式下把 ASCII 标点转成对应中文标点 |

**状态栏动作**：输入状态栏（以及托盘菜单）上有「中/英」和「中/英标点」两个动作，文字与图标随当前状态变化，点击即切换。它们注册在 `UserInterfaceManager` 上，名字分别是 `chengyin-mode` 与 `chengyin-punctuation`。

**标点映射**（与 Windows 的 `platforms/windows/punctuation.h` 逐字符一致，由 CTest `fcitx5-punctuation-parity` 把两张表钉在一起）：

| 键 | 中文 | 键 | 中文 | 键 | 中文 |
| --- | --- | --- | --- | --- | --- |
| `,` | ， | `.` | 。 | `!` | ！ |
| `?` | ？ | `:` | ： | `;` | ； |
| `(` | （ | `)` | ） | `[` | 【 |
| `]` | 】 | `<` | 《 | `>` | 》 |
| `/` `\` | 、 | `"` | “ / ”（成对交替） | `'` | ‘ / ’（成对交替） |
| `_` | —— | `^` | …… | `~` | ～ |
| `$` | ￥ |  |  |  |  |

**触发条件**（与 Windows `translate()` 相同）：中文模式、非 Ctrl/Alt/Super 组合键、未开 CapsLock、`ChinesePunctuation` 开、字符在上表内。

- **没有组合时**：直接上屏中文标点并接受该键。
- **有组合时**：先把该键交给共享核心，核心提交它选中的候选，中文标点**追加在同一次提交里**（不是两次写入），然后清空组合。这也保证了拼音里的 `'` 仍是音节分隔符（`xi'an`），不会被当成引号。
- **联想列表显示时**：按标点会关闭联想并直接上屏中文标点（与 Windows 的 `!active || association` 一致）。

**关闭 `ChinesePunctuation` 或处于英文模式**：标点原样透传给应用（有组合时候选仍由核心先提交）。英文模式下所有键都不处理，字母也不组合。

**标点触发的提交不算选词**，不写入学习档案——只有按空格/数字/鼠标选中的那次才学习。

**成对引号状态**在 reset / 失焦 / 敏感字段时清零；**中/英模式与标点开关不受 reset 影响**，只由用户切换或新建上下文决定（与 Windows 一致）。

**保存一次设置会把「中/英标点」按选项重新播种**：状态栏点出来的开关是覆盖值，配置工具里点「应用」后以选项为准。中/英模式不参与播种，改「默认英文」只影响之后新建的上下文。

与 Windows 的差异（不声称"已完全一致"）：

- **没有切换提示窗**（Windows 的 ModeHint 不在本批）。
- **Ctrl+Space 由 Fcitx 全局控制**，插件不重复绑定；Windows 有自己的 Ctrl+Space 保留键。
- Shift 判定用的是 Fcitx 的键符：`左 Shift` 只认 `FcitxKey_Shift_L`，因此只报告无左右之分的 Shift 键符的前端不会触发它（不猜侧别）。
- 真实桌面的状态栏渲染、配置工具界面与多屏未做实测，只跑无头与真实 `fcitx::Instance` 测试。


## 词典配置与更新

词库分两部分，二者**合并**使用，不是替换关系：

- **基础词库**：`DictionaryPath` 指定的一个文件；留空表示用随包安装（或内置演示）的词库。语义与本功能引入前完全相同。
- **附加词库**：`Dictionaries` 列表。每项含显示名、绝对路径与启用开关，可在配置工具里增删、逐条启用或停用。

附加词库自 I21 起接入。有空列表时行为与之前完全一致：只加载基础词库。

### 格式与上限

每个附加文件由共享导入器自动识别格式，与 Windows 端同一套代码：

| 格式 | 说明 |
| --- | --- |
| 经典搜狗 SCEL | 0x44 / 0x45 版本 |
| UTF-8 / UTF-16LE 文本 | TSV，含 BOM |
| 搜狗文本导出 | 形如 `'ni'hao 你好` |
| 本项目二进制 | MSWYDICT v1 / v2 |

单个文件上限 64 MiB，全部附加文件字节总和上限 64 MiB；条目数上限 64 个。合并后的词库上限 **250,000 条**，这是共享核心的限制：基础词库（随包安装的那份有 184,173 条）已经占去大部分，附加词库合计约剩 65,000 条空间。超过时加载失败并明确报告「合并后超过 250000 条」，**不会静默截断**。

一个条目占用一个合并名额，基础词库再占一个，因此**最多 63 个条目能同时启用**；第 64 个启用条目会被拒绝并提示。列表本身允许 64 项（多出的一项可以停用着）。

### 加载语义

- 任一**启用**条目失败（文件不存在、不是普通文件、过大、格式无法识别或内容无效、合并超限）→ **整体失败，保留旧词库**，提示为 `附加词库“<显示名>”：<原因>`。提示与日志只含显示名，**不含词条内容，也不含路径**。
- **停用的条目不读取、不校验**：文件被移走或损坏都不算错误。
- 只读取普通文件：目录、FIFO、设备文件一律拒绝（`O_NONBLOCK` + `fstat`，不会阻塞后台线程）。
- 加载全程在一个后台线程；多次保存只采用最后一次。读文件与每次导入前后都检查取消，被取代的请求不会发布结果。
- 内存峰值：逐个导入后立即释放原始字节，只保留各自的已编译句柄到合并，因此峰值是「单个文件的字节数 + 全部句柄的编译结果」，而不是所有文件字节数之和。
- 成功后空闲会话共享新词库；**正在输入的组合继续使用旧词库**，到上屏、取消或重置后切换。旧词库无人使用时在后台回收。
- 加载成功后在 Fcitx 日志记录**一行**：`基础 N 条 + 附加 M 个，合计 K 条`。

### 生效条件

`DictionaryPath` 与 `Dictionaries` 任一变化 → 走后台重载；只有设置类项（页宽 / 联想 / 模糊音 / 纠错 / 学习 / 中英 / 标点）变化 → 不重载，沿用 I17 的路径；配置完全没变 → 重读全部词库（用户原地替换文件后点 Apply 或 `fcitx5-remote -r` 即触发）。词库没有文件监视，手工替换文件后需要 `fcitx5-remote -r`。

配置写回沿用「加载成功后 `saveConfig`」：加载失败时**写回被拒的配置不会落盘**，磁盘上仍是正在生效的那份。重启后若配置加载失败，则保留内置演示词典。

### 示例

```ini
DictionaryPath=/usr/share/chengyin/daily.tsv
PageSize=7

[Dictionaries/0]
Name=医学
Path=/home/me/dicts/医学词库.scel
Enabled=True

[Dictionaries/1]
Name=自造词
Path=/home/me/dicts/mine.tsv
# Enabled 缺省为 True
```

旧版本写下的配置文件没有 `Dictionaries` 段，加载时得到空列表，不报错也不覆盖原文件；保存一次后才会写出该段。

### 与 Windows 词库管理的差异

两端不是完全一致，这里只列本卡实现的行为与下面的差异：

- Linux **按路径引用**原文件，不复制进私有存储；源文件被移走则下次加载该条目失败并保留旧词库（Windows 是把内容复制进自己的数据文件）。
- 配置工具**没有「文件选择」按钮**，路径需要手填绝对路径。
- **不显示「当前词库条数」**；条数只在加载完成后写入 Fcitx 日志一次。
- **没有启用/停用的图形按钮**：列表项自身带一个 `Enabled` 布尔项，由配置工具的列表编辑器呈现。
- 合并上限 250,000 条意味着附加词库合计约 65,000 条，超限是明确报错而非截断。
- **真实配置工具界面未实测**：列表的增删与保存由无头测试经 `setConfig()` 的同一入口验证，没有在真实 `fcitx5-configtool` 里点过。
- 不支持 GBK 文本（Windows 设置额外转换 GBK），Linux 侧只认 UTF-8 / UTF-16LE。

## Debian 测试包

在目标发行版原生构建上述模块后，安装 `dpkg-dev` 与 Python ≥3.11，再执行：

```sh
python3 scripts/package_deb.py
python3 scripts/test_deb.py build/packages/*.deb
```

支持原生 Debian amd64/arm64 的打包路径；arm64 尚未实机验证。脚本从实际 ELF 链接关系生成运行依赖，缺少依赖元数据时停止。默认输出到 `build/packages`，已有同名文件不会覆盖；后续构建可传 `--version 0.1.0-2`。默认维护者地址是开发占位值，正式分发前应通过 `--maintainer 'Name <real-address>'` 配置真实维护者。

历史开发包：`build/packages/fcitx5-chengyin_0.1.0-1_amd64.deb`，基于 **Debian 13 amd64 / Fcitx 5.1.12**。该包早于本轮共享核心更新；本轮重建和回归了模块，没有覆盖历史包。依赖至少 libc6 2.39、Fcitx Core/Config/Utils 5.1.12、libstdc++6 13.1；不是面向任意 Debian/Ubuntu 版本的通用包。

在匹配的测试桌面，用 `sudo apt install /absolute/path/to/fcitx5-chengyin_0.1.0-1_amd64.deb` 安装；从配置工具移除该输入法后，用 `sudo apt remove fcitx5-chengyin` 卸载。升级或回滚也通过安装对应版本包执行。包不自动重启 Fcitx，不更改当前输入法或全局环境变量，不删除用户词典和配置。

`test_deb.py` 在空临时根目录实际调用 dpkg，检查安装、重装、升级、回滚、移除和 purge；逐文件校验内容并确认用户配置保留。该文件布局测试有意跳过运行依赖，**不证明依赖安装成功或桌面输入可用**。

## Arch Linux 包（PKGBUILD）

原生 PKGBUILD 在 `packaging/arch/`，从固定 revision 的源码归档重建，不转换 deb：

```sh
(cd packaging/arch && makepkg)
sudo bash scripts/test_arch_package.sh packaging/arch/fcitx5-chengyin-0.1.0.preview27-1-x86_64.pkg.tar.zst
```

`pkgver` 由 `scripts/version.py --print arch-pkgver` 派生（上游 tag 的连字符是 pkgver/pkgrel 分隔符，不能在 pkgver 里出现），`prepare()` 会重新推导并拒绝与 `version.json` 不一致的值。`makepkg` 的 `check()` 会用同一份源码跑模块自身的 CTest。

包默认完整词库：`CHENGYIN_PACKAGED_DATA=ON` 同时安装 `daily.tsv` 与许可、并把安装路径编译为默认值，新用户无需填路径。安装说明（`sudo pacman -U`、配置工具添加、卸载、许可与已知限制）见 `packaging/arch/README.arch`，随包装到 `/usr/share/doc/fcitx5-chengyin/`。

`test_arch_package.sh` 在隔离的 `pacman --root` 中实测安装、重装、升级、卸载与逐文件清单，并确认用户配置和自建词库保留；**有意跳过运行依赖解析**（`-dd`），不证明依赖安装成功或桌面输入可用。

## 原型行为

- 每个 InputContext 独立会话；只读词典在同进程内共享。
- 字母全拼、`'` 音节约束、空格/数字选词（数字只到当前页宽）、上下键、退格、Esc、Enter 原文上屏。
- 共享核心支持连续全拼/首拼/声母混输、前后翻页、中间编辑和分段选择；词条覆盖取决于配置的 TSV。Arch 包及显式开启 `CHENGYIN_PACKAGED_DATA=ON` 的构建默认加载随包的 `daily.tsv`（184,173 条）；默认源码构建及当前 CI 的 deb 包仍是 98 条示例，需自行指定 `data/daily.tsv`。
- 上屏后提供离线联想；Tab 或鼠标确认，普通空格/数字直接交给应用；重置和敏感输入清除上下文与临时偏好。
- 具备 Preedit 能力的应用使用 client preedit，其余使用输入面板；候选位置交给框架。
- 候选可鼠标选中；列表版本变化后旧回调不提交。
- Ctrl/Alt/Super/AltGr 透传；释放事件不重复上屏；ASCII 标点先提交候选，再透传。
- reset/deactivate 清空组合；Password/Sensitive 字段透传并清空状态。
- 达到长度/歧义上限时保留组合并显示提示。

无头测试分两层。`test_engine.cpp`/`test_dictionary_reload.cpp`/`test_default_dictionary.cpp` 直接构造 `Engine` 与假 `InputContext`；`test_instance.cpp`（CTest `fcitx5-instance`）启动真实 `fcitx::Instance`，由框架按 addon 配置加载插件与 `testfrontend`/`testim`，经 `InputMethodManager` 路由按键，覆盖组合/候选/翻页/数字选词/编辑键/修饰键/双上下文隔离/敏感字段/切换输入法/失焦/重置/鼠标陈旧候选/长度上限/词库热切换，共 128 条断言（翻页用例随默认页宽改为 5 并补数字键越界断言，原 122 条），自造 TSV 词库且不读写用户配置。`test_settings.cpp`（CTest `fcitx5-settings`）另驱动同一个 `Engine` 走配置工具自身的入口 `setConfig()`，逐档验证 5/7/9 页宽与对应的数字键、11 项模糊音与 4 项键盘纠错逐位映射、联想开关、旧配置文件回落默认值、持久化往返、只改设置不重载词典，以及**活跃组合中保存设置不丢键、不改组合**，共 122 条断言。`test_learning.cpp`（CTest `fcitx5-learning`）驱动同一个 `Engine`，逐条验证本机持久选词学习：默认开关下反复选同一候选会使它升到首位、档案写入临时目录且权限为 0600/0700、重启（同路径新 `Engine`）后排序保留、`Learning` 关闭时文件字节与 mtime 都不动且排序不变、敏感字段不学不写、两个输入上下文共享同一份学习结果且正在组合的上下文不被打断、损坏档案既不覆盖也不阻止内存学习并有一次提示、删档重读等于清除、把档案放回重读等于导入、写盘目标无法写入时按键仍被处理且失败有计数有上限、提示与错误文本里不出现任何拼写或文字、以及**基线陈旧（一直在组合）的会话不会覆盖别的上下文刚学到的选词**，共 104 条断言；同样只用私有 HOME/XDG 与注入的临时档案路径。真实守护进程端到端 `e2e.sh`（CTest `fcitx5-e2e`）在私有 HOME/XDG 与私有 session bus 下启动真实 `fcitx5`、加载 `DESTDIR` 暂存安装的插件，客户端经框架自身的 `org.fcitx.Fcitx.InputMethod1`/`InputContext1` D-Bus 接口建立真实输入上下文、送按键并在 `CommitString` 信号里断言上屏文本，覆盖 `nihao`+空格→`你好`、`nihao`+Esc→不上屏、`nihao`+`2`→第二条候选三个场景；该路径不需要 X 服务器，不启动 Xvfb。缺 `fcitx5`/`dbus-daemon`/`cmake` 或该 fcitx5 不带 `dbusfrontend` addon 时以退出码 77 跳过。`test_dictionary_library.cpp`（CTest `fcitx5-dictionary-library`）驱动同一个 `Engine`，逐条验证附加词库：空列表与之前完全一致、四种格式（UTF-16LE 文本、搜狗文本、经典 SCEL、本项目二进制）各一个条目都被识别并生效、多个条目合并后各自的词条同时可输入、`Enabled=false` 的条目不读取也不校验（文件已删除仍不报错）、改回启用后生效、任一启用条目文件不存在/是目录/FIFO/损坏/过大 → 整体失败且旧词库仍可输入并提示 `附加词库“<名>”：<原因>`、合并超限时明确报「合并后超过 250000 条」（并与核心自身的 merge 拒绝对拍）、65 项列表被拒绝而 63 个启用条目正好可以加载、活跃组合中保存词库配置不改组合与候选、连续快速保存只采用最后一次（并直接驱动 `DictionaryLoader` 断言被取代的请求不发布）、配置往返（`dump`/`load`、旧配置文件无 `Dictionaries` 段时为空的且原文件未被改写、写出再读回逐项相同）、只改设置类项不重载而只改词库项重载、以及提示与日志不含任何词条内容，共 215 条断言。该套件只用私有 XDG 与注入的档案路径，词库全部在测试内构造（SCEL 样本按 `crates/ime-core/tests/modern.rs` 的方式逐字节生成），不读写用户数据、不下载。两层都不启动 compositor、GTK/Qt 应用或真实候选窗，不验证光标定位。C++ 测试另以 AddressSanitizer/UndefinedBehaviorSanitizer 运行；Rust 静态库及系统库未做 sanitizer 插桩。

已知限制：页宽、联想、模糊音、键盘纠错、**本机持久选词学习**与**多词库管理**已对齐 Windows；实际桌面焦点/导航/协议测试尚未完成。后台加载不等于无限容量：TSV 构建仍有临时内存峰值，磁盘/内核阻塞和正在执行的 Rust 构建不能瞬间取消，关闭插件需要等待工作线程结束。附加词库合计约 65,000 条，见「词典配置与更新」。
