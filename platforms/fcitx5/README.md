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
| `Fuzzy` | 子分组，11 个布尔 | 全关 | 模糊音（双向匹配）：`zh↔z`、`ch↔c`、`sh↔s`、`n↔l`、`f↔h`、`l↔r`、`an↔ang`、`en↔eng`、`in↔ing`、`ian↔iang`、`uan↔uang` |
| `Correction` | 子分组，4 个布尔 | 全关 | 常见键盘失误：相邻字母按反、漏按一个字母、QWERTY 相邻键误按、重复按键 |

保存后立即对正在运行的空闲会话生效，不需要重启。**正在输入的组合不受影响**：设置只在一个组合结束后应用，因此保存设置不会丢键，也不会改变或取消当前组合；下一次输入即按新设置进行。候选列表的页宽与数字选词键始终跟随核心实际生效的页宽，二者不会错位。

只改上述设置（`DictionaryPath` 未变）时**不重载词典**：直接应用并写回配置。只有 `DictionaryPath` 变化才走后台加载。设置写回磁盘失败时会像词典保存失败一样在提示里报告，不静默。

`PageSize` 用枚举而不是整数范围：配置工具因此只提供 5/7/9 三个选项，手工写入集合外的值会退回该选项的默认值，而不会被悄悄夹到用户没选过的宽度。

Fcitx 配置工具只显示本批已有的项；**多词库管理尚未对齐**，中文标点、中英切换、Shift 切换也未接入，列在后续批次。

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


## 词典配置与更新

在 Fcitx 配置工具中打开澄音的设置，填写“词典 TSV 绝对路径”。留空恢复内置演示词典。自定义词典是**替换**演示词典，而非合并；格式见 [词典说明](../../data/README.md)。路径不展开 `~` 或环境变量。

也可编辑 `${XDG_CONFIG_HOME:-$HOME/.config}/fcitx5/conf/chengyin.conf`；一个把上面设置都改过的文件长这样：

```ini
DictionaryPath=/absolute/path/to/my-dictionary.tsv
PageSize=7
Associations=True

[Fuzzy]
AnAng=True

[Correction]
Swap=True
```

旧版本写下的配置文件只有 `DictionaryPath`，加载时其余项各自取本节表格里的默认值，不报错也不覆盖。

配置工具保存会自动请求加载。手工编辑配置，或替换同一路径的 TSV 后，执行 `fcitx5-remote -r` 重新加载；当前不监视文件变更。建议先写新文件再原子重命名，避免读取到正在写入的半个词典。

加载只在一个后台线程中执行，多次修改只采用最后一次请求。成功后空闲会话共享新词典；正在输入的组合继续使用原词典，到上屏、取消或重置后切换。旧词典无人使用时在后台回收。工作线程仅在有旧会话待释放时每 250 ms 检查一次，其余空闲时间不轮询。

路径不存在、目录/FIFO、超过 64 MiB、非法 UTF-8/TSV/重复项等均拒绝加载。配置工具提交失败不会覆盖磁盘上的旧设置；错误记录在 Fcitx 日志和下一次组合的提示中。手工写坏的配置文件不会被自动改回，但运行中的旧词典仍可使用。程序重启后若配置加载失败，则保留内置演示词典。

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

无头测试分两层。`test_engine.cpp`/`test_dictionary_reload.cpp`/`test_default_dictionary.cpp` 直接构造 `Engine` 与假 `InputContext`；`test_instance.cpp`（CTest `fcitx5-instance`）启动真实 `fcitx::Instance`，由框架按 addon 配置加载插件与 `testfrontend`/`testim`，经 `InputMethodManager` 路由按键，覆盖组合/候选/翻页/数字选词/编辑键/修饰键/双上下文隔离/敏感字段/切换输入法/失焦/重置/鼠标陈旧候选/长度上限/词库热切换，共 128 条断言（翻页用例随默认页宽改为 5 并补数字键越界断言，原 122 条），自造 TSV 词库且不读写用户配置。`test_settings.cpp`（CTest `fcitx5-settings`）另驱动同一个 `Engine` 走配置工具自身的入口 `setConfig()`，逐档验证 5/7/9 页宽与对应的数字键、11 项模糊音与 4 项键盘纠错逐位映射、联想开关、旧配置文件回落默认值、持久化往返、只改设置不重载词典，以及**活跃组合中保存设置不丢键、不改组合**，共 122 条断言。`test_learning.cpp`（CTest `fcitx5-learning`）驱动同一个 `Engine`，逐条验证本机持久选词学习：默认开关下反复选同一候选会使它升到首位、档案写入临时目录且权限为 0600/0700、重启（同路径新 `Engine`）后排序保留、`Learning` 关闭时文件字节与 mtime 都不动且排序不变、敏感字段不学不写、两个输入上下文共享同一份学习结果且正在组合的上下文不被打断、损坏档案既不覆盖也不阻止内存学习并有一次提示、删档重读等于清除、把档案放回重读等于导入、写盘目标无法写入时按键仍被处理且失败有计数有上限、提示与错误文本里不出现任何拼写或文字、以及**基线陈旧（一直在组合）的会话不会覆盖别的上下文刚学到的选词**，共 104 条断言；同样只用私有 HOME/XDG 与注入的临时档案路径。真实守护进程端到端 `e2e.sh`（CTest `fcitx5-e2e`）在私有 HOME/XDG 与私有 session bus 下启动真实 `fcitx5`、加载 `DESTDIR` 暂存安装的插件，客户端经框架自身的 `org.fcitx.Fcitx.InputMethod1`/`InputContext1` D-Bus 接口建立真实输入上下文、送按键并在 `CommitString` 信号里断言上屏文本，覆盖 `nihao`+空格→`你好`、`nihao`+Esc→不上屏、`nihao`+`2`→第二条候选三个场景；该路径不需要 X 服务器，不启动 Xvfb。缺 `fcitx5`/`dbus-daemon`/`cmake` 或该 fcitx5 不带 `dbusfrontend` addon 时以退出码 77 跳过。两层都不启动 compositor、GTK/Qt 应用或真实候选窗，不验证光标定位。C++ 测试另以 AddressSanitizer/UndefinedBehaviorSanitizer 运行；Rust 静态库及系统库未做 sanitizer 插桩。

已知限制：页宽、联想、模糊音、键盘纠错与**本机持久选词学习**已对齐 Windows；**多词库管理**、中文标点、中英切换与 Shift 切换未接入；Linux 配置界面当前只加载严格 TSV，尚未接入 Windows 的搜狗导入/追加界面；实际桌面焦点/导航/协议测试尚未完成。后台加载不等于无限容量：TSV 构建仍有临时内存峰值，磁盘/内核阻塞和正在执行的 Rust 构建不能瞬间取消，关闭插件需要等待工作线程结束。
