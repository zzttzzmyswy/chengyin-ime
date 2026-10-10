# 项目状态

## 迭代 I21 Fcitx 多词库管理（附加词库列表） · 2026-10-10 已合并（PR #41，随 preview33 发版）

评审结论：通过。技术负责人亲自核对 `dictionary_loader.cpp`（停用条目不读取不校验；任一启用条目失败整体失败并保留旧词库，提示只含条目显示名与固定原因、不含路径与内容；启用条目 ≤63 与合并上限 250000 明确报错；逐文件读取后立即释放字节，峰值为单文件字节加各句柄）并重跑：`check.sh` 通过；全新构建 ctest 连续 3 次 10/10（新增 `fcitx5-dictionary-library`）；`taskset -c 0` 单核重复通过；真实 `fcitx5` 进程集合不变，`~/.local/share/fcitx5` 无 `chengyin/`、`~/.config/fcitx5/conf` 无 `chengyin.conf`；PR CI 6/6。认可执行方 5 处偏差（基础/附加错误措辞分开、启用上限 63、`Enabled` 用列表项字段、新增只读访问器、成功日志仅在落盘加载时输出）。
已知差异：按路径引用文件、无文件选择与条数显示、附加词库合计约 65,000 条上限、不支持 GBK 文本、真实配置工具界面未实测。
发版：[v0.1.0-preview33](https://github.com/zzttzzmyswy/chengyin-ime/releases/tag/v0.1.0-preview33)（target `9ac488d`；Windows EXE/deb 取自 Actions 38033988911；Arch 包固定源码重建，容器内 CTest 10/10、隔离 pacman 生命周期通过；PKGBUILD 已 pin，归档 SHA256 `af39ac80…` 两次下载一致）。仍为预发布，未做实机验收。

（以下为执行方交付记录）

分支 `codex/fcitx-dictionary-library`。**本卡不发版**：`version.json` 未改，仍为 preview32。

**改动面**：`platforms/common/dictionary_loader.{h,cpp}` 的 `request` 由「单个路径」扩展为「基础路径 + 附加条目向量」，新增受限读取的共用函数、逐条 import、`merge_all` 合并、条目名错误格式 `附加词库“<Name>”：<原因>`、以及供测试观察的 `started()`；
`platforms/fcitx5/config.h` 新增 `DictionaryEntryConfig`（Name/Path/Enabled）与 `Dictionaries` 列表选项，并入 `dictionarySources()`；
`engine.h`/`engine.cpp` 的 `loadDictionary` 带上条目列表，`setConfig` 判定「词库项变化」（路径或列表逐项不同）才重载，新增 64 项上限与合并名额上限的拒绝路径、`dictionaryEntries()`/`loadedEntryCount()`/`baseEntryCount()`/`mergedEntryCount()`/`dictionaryRequests()`/`loadedRequest()` 访问器，加载成功后记录一行 `基础 N 条 + 附加 M 个，合计 K 条`；
`CMakeLists.txt` 注册 `fcitx5-dictionary-library`；新增 `test_dictionary_library.cpp`（215 条断言）；`README.md` 的「词典配置与更新」一节重写并新增与 Windows 的差异清单。
未改 `crates/`、`include/chengyin_ime.h`、`platforms/windows/`、`version.json`、`.github/workflows`。

**关键实现说明**：
- 基础词库与附加词库是**合并**关系；空列表时行为与本功能引入前完全一致（不建 union，直接沿用基础句柄）。
- 任一启用条目失败即整体失败并保留旧词库；停用条目不读取、不校验。日志与提示只含显示名，不含路径、不含词条内容。
- 合并上限 250,000 条来自核心，`merge_all` 返回 NULL 时归因到最后一条启用条目并报「合并后超过 250000 条」，不静默截断；测试里用相同两个句柄直接调 `chengyin_dictionary_merge_all` 对拍，证明该上限是核心的真实行为。
- 核心 `merge_all` 只接受 1..=64 个句柄，基础词库占一个，因此最多 63 个条目可同时启用；第 64 个启用条目被拒绝并提示，列表本身允许 64 项。
- 内存峰值：逐个 import 后立即释放原始字节，只保留已编译句柄到合并。
## 迭代 I22 核心 ü 变体拼写（jv/qv/xv/yv、lve/nve）· 2026-10-10 交付待评审

分支 `codex/core-v-variants`（基线 `main` @ `64956a3`，交付前已 rebase 到 `92595df`/preview33）。
**本卡不发版**：`version.json` 未改，仍为 `0.1.0`/preview33；未改 C ABI、`platforms/`、
`data/syllables.txt`、词库来源与 `.github/workflows`。

**改动面**：`crates/ime-core/src/syllables.rs` 新增纯函数 `canonicalize`（`v→u` 两条规则的
等长改写，含单元测试）；`crates/ime-core/src/session.rs` 新增会话内定长缓冲 `canonical` 与
`canonicalized` 标志、自由函数 `query_spelling`，并把全部词库读取（游标 reset/reset_fast/
reset_tolerant、`initials_range`、`attests`、`corrected_pronunciation`、`fuzzy::*`、解码器、
历史缓存与学习键）改经该查询拼写；新增 `crates/ime-core/tests/variants.rs`（8 个用例）；
`crates/ime-core/tests/allocations.rs` 追加变体输入的逐键零分配场景；
`crates/ime-core/tests/quality_corpus.rs` 把 `alias` 计入类别门禁；
`scripts/generate_eval_corpus.py` 新增 `alias` 类并改为**追加式**生成；
`data/eval/quality.tsv` 追加 44 行（+44/−0）；`docs/ARCHITECTURE.md` 增一节规则说明；
`docs/QUALITY_BASELINE.md` 增一节前后对照。

**规则 2 的收窄（与设计的偏差，也是唯一偏差）**：设计写“紧跟 `l n` 且紧接着 `e` 的 `v`
视为 `u`”。按字面实现会改写 `nver`→`nuer`，破坏 女儿（`nv'er` 是词库中 nü 的规范写法）：
实测既有 488 条中 `typo/neighbor` 的 `xialnver`（纠错后的 小女儿）由可达变为不可达，
`combo` 由 0.4% 升至 0.6%。故实现收窄为“仅当该 `l`/`n` 之前已是完整音节序列时改写”，
`nve`/`lveduo` 仍照常改写，`nver`/`xialnver`/`lv'e` 保持原样。这是任务卡“若某条规则使既有
质量集用例变化则该规则不得合入（收窄或去掉）”所要求的收窄。

**学习键取值**：取**查询用的规范拼写**而非原始输入。依据：`refresh` 的历史道以
`row.key == 查询拼写` 判定 `accurate`（`session.rs`），`dictionary.attests` 也以查询拼写
对齐；若按原始输入记录，`jveding` 选中的行在下次输入 `jueding` 时永远查不到。这与既有
“键盘纠错后学习键”一致——纠错按原始键查询，故记原始键；变体按规范拼写查询，故记规范拼写。

**证据摘要**（同机 Intel i9-10900X / 20 核 / Arch Linux / rustc 1.98.0）：
`bash scripts/check.sh` 退出 0（25 个测试二进制全绿，含 FFI 冒烟与 CLI 查询）；
`cargo test -p chengyin-core --test allocations --locked` 通过（新增 `jvn`/`jveding`/`xvan`/
`lve`/`lvequ`/`nvedai` 逐键零分配）；`quality_report` 与 `--incremental` 两模式下既有 488 条
九个类别逐类逐字段**完全相同**（Top-1 93.4% / Top-9 98.6% / 可达 99.6% / 错误组合率 0.2%），
新增 `alias` 44 条由改动前 Top-1 0.0% 提升至 **100.0%**。

## 迭代 I20 Fcitx 中/英文模式与中文标点（Shift 切换、状态栏动作、配置项） · 2026-10-10 已合并（PR #39，随 preview32 发版）

评审结论：通过（评审中删除一处遗留的调试日志 `DBG-ACT`，每次激活输入法都会写一条 WARN）。技术负责人亲自核对与重跑：`check.sh` 通过；全新构建 ctest 连续 3 次 9/9（含新增 `fcitx5-mode-punctuation`、`fcitx5-punctuation-parity`）；`taskset -c 0` 单核重复通过；真实 `fcitx5` 进程集合不变，`~/.local/share/fcitx5` 无 `chengyin/`；PR CI 6/6。认可执行方 4 处偏差（无组合标点走 Windows 的独立标点动作、不额外判 Shift、`'` 有组合时交给核心保持音节分隔、保存设置时重播种标点开关）及切换时“丢弃组合”的依据（`platforms/windows/service.cpp:1190,1224-1227`）；标点映射由 CTest 与 Windows 头逐字符对拍。
已知差异：无切换提示窗；Ctrl+Space 由 Fcitx 全局控制；状态栏动作与配置工具界面、真实桌面未实测。
发版：[v0.1.0-preview32](https://github.com/zzttzzmyswy/chengyin-ime/releases/tag/v0.1.0-preview32)（target `dea0779`；Windows EXE/deb 取自 Actions 38024810062；Arch 包固定源码重建，容器内 CTest 9/9、隔离 pacman 生命周期通过；PKGBUILD 已 pin，归档 SHA256 `eb78e402…` 两次下载一致）。仍为预发布，未做实机验收。

（以下为执行方交付记录）

分支 `codex/fcitx-mode-punctuation`。**本卡不发版**：`version.json` 未改，仍为 preview31。

**改动面**：新增 `platforms/common/punctuation.h`（`char32_t` 版共享标点表，与 `platforms/windows/punctuation.h` 逐字符一致，由新 CTest 钉住）；
`platforms/fcitx5/config.h` 新增 `DefaultEnglish` / `ShiftSwitch`（枚举 不使用/左 Shift/左右 Shift）/ `ChinesePunctuation`，并入 `EngineSettings`；
`engine.h` 新增 `ShiftTap` 轻按状态机与 `State` 的 `english`/`punctuation`/`PunctuationState`/`ShiftTap`；
`engine.cpp` 把 `keyEvent` 改成 Windows `translate()` 的三路分流（独立标点 / 交给核心并合并提交 / 透传），
新增 `activate()`、两个 `SimpleAction`（`chengyin-mode`、`chengyin-punctuation`）与 `updateActions()`；
`factory.cpp` 传入 `Instance::userInterfaceManager()`；`CMakeLists.txt` 注册两个新测试；
新增 `test_mode_punctuation.cpp`、`test_punctuation_parity.cpp`；`test_engine.cpp` 逗号断言按新行为更新；
`test_instance.cpp` 新增 case 16（真实 `fcitx::Instance` 下的注册、状态栏分组与动作点击）；`README.md` 新增一节。
未改 `crates/`、`include/chengyin_ime.h`、`platforms/windows/`、`version.json`、`.github/workflows`。

**关键取值依据（切换时丢组合）**：与 Windows 一致，**丢弃组合**而不是提交原文。依据：`toggleEnglish()` 先调用 `unbind()`
（`platforms/windows/service.cpp:1190`），而 `unbind()` 会 `chengyin_session_reset()` 并把 `punctuation_`/`shift_` 清空
（`service.cpp:1224-1227`）——组合因此被丢掉，不会以拉丁字母形式上屏。Fcitx 侧同样先清会话再切模式（`toggleEnglish()`）。

**状态栏动作 API 版本兼容**：`SimpleAction`/`setShortText`/`setIcon`/`setChecked`/`activate`/`UserInterfaceManager::registerAction(name, action)`/
`StatusArea::addAction`/`StatusGroup::InputMethod` 均存在于 Fcitx 5.1.7（CI 的 Ubuntu 24.04 为 5.1.x），本机 5.1.23 亦一致，
没有引入需要版本宏的新接口；`activate()` 覆写是 `InputMethodEngine` 既有虚函数。`Instance::activateInputMethod()` 在调用
`engine->activate()` 前会 `statusArea().clearGroup(StatusGroup::InputMethod)`，因此两个动作在 `activate()` 里重新加入。

**与设计的偏差**：
1. 设计写“有组合时先用 `chengyin_session_process` 处理……再 `commitString` 合并”，实现按 Windows `translate()` 的真实分流落地：
   只有**有组合**时标点才随核心的提交一起写入；**没有组合**时走 Windows 的独立标点动作（直接上屏，不经过核心），
   因为 `chengyin_session_process` 在无组合时对不可打印字符返回 0，不会产出任何提交。偏差原因：完全按设计写会让无组合的标点无法上屏。
2. 设计写“非 Ctrl/Alt/Win、未按 Shift”，实现**不额外判 Shift**：Windows 的 `translate()` 判 Shift 是因为 Win32 下 Shift+';' 仍报 VK_OEM_1；
   Fcitx 的 `Key::normalize()` 已把 Shift 折进键符（Shift+';' 到达时就是 colon 键符、无 Shift 位），再判 Shift 会把冒号误判成组合键。Windows 自身也是按“已折叠后的字符”取标点。
3. 设计提到“先读 `/usr/include/Fcitx5/Core/fcitx` 确认 API”：已读；`action.h`/`statusarea.h`/`userinterfacemanager.h` 与实现一致，无需版本宏。
4. 本机 `pgrep -a fcitx5` 在测试前后输出一致（真实 Fcitx 未受影响），`~/.local/share/fcitx5` 下无 `chengyin/`。

**证据**：`bash scripts/check.sh` 退出 0；`python3 scripts/version.py --check` 通过（`0.1.0-preview31`，未变）；
全新构建后 `ctest` 连续 3 次 9/9；`taskset -c 0 ctest -R "mode-punctuation|punctuation-parity" --repeat until-fail:10` 全过；
阴性对照三处各自复现预期失败（本地临时变体，未提交）：拆开合并提交 → 单次写入断言失败；
去掉 Shift 组合作废 → 组合键用例失败；标点提交也学习 → 学习用例失败。

**未断言**：只跑无头与真实 `fcitx::Instance`；真实桌面的输入状态栏渲染、配置工具界面与多屏未验证。
不声称“Linux 中英/标点已与 Windows 完全一致”：无切换提示窗，Ctrl+Space 由 Fcitx 全局控制。


## 迭代 I19 Fcitx 选词学习持久化（`Learning` 开关 + Profile 落盘） · 2026-10-10 已合并（PR #36，squash `23ed2b9`，随 preview31 发版）

评审结论：第 2 轮通过（第 1 轮打回：CI `fcitx5-learning` I.4/I.6 依赖墙钟、陈旧会话快照会覆盖其他上下文刚学到的选词）。技术负责人亲自核对并重跑：`Engine::learn` 按 `baselineCurrent` 分流（基线落后时直接记入主档案；`ChengyinProfile` 为 `Arc`+`make_mut` 写时复制，`crates/ime-ffi/src/lib.rs:119`，不重复计数）；新增 case J（两上下文交错学习，阴性对照复现失败）、case I 改为 `flushLearning` 等终态；`check.sh` 通过，全新构建 ctest 连续 3 次 7/7，`taskset -c 0` 单核重复通过，真实 `fcitx5` 进程集合不变，`~/.local/share/fcitx5` 无 `chengyin/`；PR CI 6/6。
已知限制：单进程共享、无跨进程锁；无 GUI 清除/导入按钮（删/换 `profile.bin` 后 `fcitx5-remote -r`）；配置工具实际渲染与真实桌面未验证。
发版：[v0.1.0-preview31](https://github.com/zzttzzmyswy/chengyin-ime/releases/tag/v0.1.0-preview31)（target `7a0df0c`，含 I18 设置对齐；Windows EXE/deb 取自 Actions 38001813951；Arch 包固定源码重建，容器内 CTest 7/7、隔离 pacman 生命周期通过；PKGBUILD 已 pin，归档 SHA256 `00c82805…` 两次下载一致）。仍为预发布，未做实机验收。
未做（后续）：I-C 多词库管理；中文标点、中英切换、Shift 切换（需先判断复用 Fcitx 自带能力还是共用 Windows 逻辑）。

（以下为执行方交付记录）

分支 `codex/fcitx-persistent-learning`。**本卡不发版**：`version.json` 未改，仍为 preview30。

**结果先行**：Fcitx 5 插件接入共享核心的本地选词学习。新增 `Learning` 开关（默认开，文案同 Windows「根据选词习惯排序」），
每次中文上屏记录一次选词，同拼音候选排序随用户习惯变化，重启后保留。档案在
`${XDG_DATA_HOME:-$HOME/.local/share}/fcitx5/chengyin/profile.bin`（目录 0700 / 文件 0600）；清除=删文件 +
`fcitx5-remote -r`，导入=放回文件 + `fcitx5-remote -r`，不新增配置界面按钮。多个输入上下文共享一份内存主档案；
正在组合的会话不被改动。敏感字段不学不写。写盘在独立工作线程 + 128 槽有界队列，按键线程不做序列化与 I/O。

**改动面**：`platforms/fcitx5/config.h`（新增 `Learning` 选项，取代 `kDefaultLearning` 常量；`EngineSettings` 增加
learning 位）、`engine.h`/`engine.cpp`（主档案与 profileRevision、`applyProfile`、`learn`、
`DefaultProfilePath`、启动/重载读取、损坏档案提示、面板提示行）、新增 `platforms/fcitx5/profile_store.{h,cpp}`
（工作线程、有界队列、原子替换、有界重试）、`CMakeLists.txt`（编译新文件并注册 `fcitx5-learning`）、
新增 `test_learning.cpp`、`test_engine.cpp`/`test_settings.cpp`/`test_dictionary_reload.cpp`/
`test_default_dictionary.cpp` 改为显式注入临时档案路径与私有 `XDG_DATA_HOME`（此前会写用户真实
`$HOME`）、`platforms/fcitx5/README.md`。未改 `crates/`、`include/chengyin_ime.h`、`platforms/windows/`、
`version.json`、`.github/workflows`。

**证据**：`bash scripts/check.sh` 退出 0；`python3 scripts/version.py --check` 通过（`0.1.0-preview30`，未变）；
全新构建后 `ctest` 连续 3 次 7/7 通过，运行前后 `pgrep -a fcitx5` 输出不变（本机真实 Fcitx 未受影响），
测试只用私有 HOME/XDG 与注入的临时路径；新增 `fcitx5-learning` 90 条断言，逐条覆盖任务卡验收项。
阴性对照三处各自复现预期失败：删掉敏感检查（入口 + 学习处）→ 敏感用例 F 失败；让学习位与 `learn_commit`
忽略开关（即 I19 之前的行为）→ 开关用例 E 失败；允许覆盖损坏档案 → 损坏用例 G 失败（均为本地临时变体，未提交）。

**与设计的偏差**：学习记录取会话自身的快照作为主档案（`chengyin_session_profile`），而不是在主档案上再录一次——
否则一次选词会被计两次，而核心按命中率/trials 排序，会导致比 Windows 少一半次数就升位；这也与 Windows
`service.cpp` 的交接方式一致。因此 `profile_store` 保存的是事件队列，引擎内存档案以会话快照为准。

**未断言**：只验证无头与真实 `fcitx::Instance`；配置工具界面的实际渲染、真实桌面候选窗/多屏未验证。
不声称「Linux 学习已与 Windows 完全一致」：单进程无跨进程锁与 generation/revision 通道，无 GUI 清除/导入按钮。

## 迭代 I18 Fcitx 设置对齐 Windows（第一批：页宽/联想/模糊音/键盘纠错） · 2026-10-09 已合并（PR #34，squash `f8951ca`，随 preview31 发版）

编号说明：本迭代在 MYS-2041 中按 I17 派出，合并时 I17 已被并行的解码器优化迭代（PR #31，preview30）占用，故更名为 I18。
评审结论：通过。技术负责人亲自重跑 `check.sh`、`version.py --check`（preview29 未 bump）、全新构建后 ctest 连续 3 次 6/6（含新增 `fcitx5-settings` 与 `fcitx5-e2e`）、运行前后 `fcitx5` 进程集合不变；PR CI 6/6 通过。逐行审阅 `config.h`/`engine.cpp`：学习位取核心默认 `true`、页宽用枚举、`applySettings` 仅在空闲会话应用（BUSY 保持旧版本待下次）、面板页宽与核心页宽同源。对“只改设置不重载词典”的条件收紧（设置确有变化且路径未变）认可：避免回归“手工换同路径 TSV 后重读”。
发版：**暂不发版**——本批改动影响 Fcitx 包（deb / Arch），按发布纪律在 I-B（Linux 持久学习）合并后随 preview31 统一 bump 发版，避免一批改动多次发版；Windows 产物不受影响。
遗留：配置工具 GUI 实际渲染（分组标题）与真实桌面候选窗/数字键体验未验证，不作为已支持声称。


**结果先行**：Fcitx 5 插件的设置页从“只有词典路径”扩到本批四组与 Windows 语义、默认值一致的项——
每页候选数（`PageSize`，枚举 5/7/9，**默认由硬编码 9 改为 5**）、联想词（`Associations`，默认开）、
模糊音 11 项与键盘纠错 4 项（`Fuzzy`/`Correction` 子分组，默认全关）。保存后对运行中的空闲会话立即生效，
**活跃组合不丢键、不改组合**；只改这些设置不重载词典。

**改动面**：`platforms/fcitx5/config.h`（选项定义与“选项→匹配位”的唯一映射表）、`engine.h`/`engine.cpp`
（设置快照与版本、`State` 记录已应用的页宽与版本、`applySettings`/`synchronizeAll`、面板页宽与数字键、
`select()` 的索引上限、设置写回失败提示）、`platforms/fcitx5/test_settings.cpp`（新增，122 条断言）、
`platforms/fcitx5/test_instance.cpp`（翻页用例改为默认页宽 5 并补数字键越界断言，122 → 128 条断言）、
`platforms/fcitx5/README.md`、`platforms/fcitx5/CMakeLists.txt`（注册 `fcitx5-settings`）。未改 `crates/`、
`include/`、`platforms/windows/`；**未 bump 版本**（本卡不发版）。

**证据**：`bash scripts/check.sh` 退出 0；`python3 scripts/version.py --check` 通过（`0.1.0-preview29`，未变）；
全新构建后 `ctest` 连续 3 次 6/6 通过，运行前后 `pgrep -a fcitx5` 集合不变（本机真实 Fcitx 未受影响）。
阴性对照两处各自复现预期失败：去掉 `PageSize` 的应用 → 页宽/数字键用例失败；把模糊音映射故意错一位 →
位映射与 `zh/z` 行为用例失败（均为本地临时变体，未提交）。

**未断言**：本批只覆盖无头与真实 `fcitx::Instance`；配置工具界面的实际渲染、真实桌面的候选窗与数字键
体验未验证。学习（持久词频）、多词库管理、中文标点、中英切换与 Shift 切换不在本批。

## 迭代 I16 项目代码许可改为 GPL-3.0-or-later · 2026-10-09 已合并（PR #28，squash `c519407`，preview29）

评审结论：通过。技术负责人亲自重跑 `check.sh`、`version.py --check`（`0.1.0-preview29`）、Fcitx CTest 5/5、暂存安装后包内 `copyright` 首行为 GPLv3；逐行核对 diff，除 SPDX 头外只有许可文案、版本源与「关于」页文案，无产品逻辑改动。**评审中发现并修正**：交付的 `LICENSE` 由网页转换而来（弯引号、`©`、段落重排），不是官方原文；追加提交 `203e39d` 换成 gnu.org `gpl-3.0.txt`（ASCII，674 行，sha256 `3972dc97…`）。PR #29 统一了文档内的旧仓库名链接。
发版：[v0.1.0-preview29](https://github.com/zzttzzmyswy/chengyin-ime/releases/tag/v0.1.0-preview29)（target `c519407`，Windows EXE、Ubuntu deb 取自 Actions 37943375368；Arch 包由固定源码重建，容器内 CTest 5/5、隔离 pacman 生命周期通过；PKGBUILD 已 pin，归档 SHA256 `bfa378a7…` 两次下载一致）。仍为预发布，未做实机验收。


**结果先行**：项目自有代码许可由 MIT 改为 **GPL-3.0-or-later**，词库与美术素材许可保持原样
（独立于代码），preview28 及更早版本仍按当时 MIT 授予、不追溯。`LICENSE` 换为 GPLv3 官方全文
（232 行，首行 `GNU GENERAL PUBLIC LICENSE` / `Version 3, 29 June 2007`）。

**版本**：任务卡要求 bump 到 preview28，但 **preview28 在任务卡创建前已被 I15 发版占用**
（`v0.1.0-preview28` tag 落在 I15 合并提交 `253e13e`，release 已发布 6 个资产）。同一版本号
不能承载两份不同内容，故改为 **preview29**，并同步 `version.json`、`version_generated.h`
与 Arch `pkgver`（`version.py --check` 通过）。已作为偏差记录在交付说明中。

**改动面**：`Cargo.toml`、`packaging/arch/PKGBUILD`（`license=('GPL-3.0-or-later')`）、
Windows 安装器 `LegalCopyright`、设置「关于」页（协议名 + 许可对话框）、`README.md`、
`platforms/windows/THIRD_PARTY.md`、`platforms/windows/README.md`、`packaging/windows/README.md`、
`platforms/fcitx5/README.md`、`platforms/fcitx5/CMakeLists.txt`、`packaging/arch/README.arch`、
`docs/ARCHITECTURE.md`、`docs/QUALITY_BASELINE.md`、`data/README.md`；首方源码文件头加
`SPDX-License-Identifier: GPL-3.0-or-later`；新增 `docs/LICENSING.md`。

**未决点**：`docs/LICENSING.md` 第 4.3 节记录 MSVC C/C++ 静态运行库的 GPLv3 兼容性——
按 GPLv3 第 1 段 System Libraries 定义（含 "a compiler used to produce the work"）与第 6 段，
Static link 的 MSVC CRT 可作为 System Library 排除在 Corresponding Source 之外，但 FSF 未就
此具体情形给出明确答复，且 Microsoft 再分发条款非自由软件许可。**该项标记为未决，不作兼容性
断言**，已按任务卡要求写明；其余随包第三方组件（Rust 运行时、NSIS zlib/LZMA、MinGW GCC、
Windows 系统组件）均给出"兼容"结论及依据。

## 迭代 I17 解码器算法级优化：`transition` miss 路径元数据预判 · 2026-10-09 已合并（PR #31，squash `0d0e363`，preview30）

评审结论：通过（目标 −30% 未达，如实记录）。技术负责人亲自重跑：`check.sh` 全绿、`version.py --check`/`--verify-bump`；`quality_report` 前后仅词库加载耗时行不同；独立编写的逐候选 dump（22 输入 × 3 context × 2 配置，共 15,066 行）前后 `cmp` 逐字节相同；阴性对照（对调 `adjacency_may_contain` 参数）使 2 条 decoder 单测与 3 条 long_input 回归失败、dump 出现 282 行差异；独立二进制 A/B 交替 3 轮（13.24 负载约 20），目标场景 P99 −25.7%/−32.6%/−31.6%（开发方低负载 −24.7%/−22.0%/−24.2%），故按 **约 −22%～−33%** 记录，不声称达到 −30%。非目标场景个别亚毫秒项 P50/P99 波动在高负载下为噪声（`shi`+20 页单独交替复测无系统差异）。
发版：[v0.1.0-preview30](https://github.com/zzttzzmyswy/chengyin-ime/releases/tag/v0.1.0-preview30)（target `0d0e363`，Windows EXE、Ubuntu deb 取自 Actions 37962960103，6 作业成功；Arch 包由固定源码重建，容器内 CTest 5/5、隔离 pacman 生命周期通过；PKGBUILD 已 pin，归档 SHA256 `7e36a9fe…` 两次下载一致）。仍为预发布，未做实机验收；仍未达单键 P99 ≤ 0.5 ms，不声称尾延迟达标。
**版本说明**：任务卡写 preview29/I16，但二者已被许可迭代（PR #28）占用，开发方改记 I17/preview30，评审认可。

**结果先行**：目标场景（全规则六纠错/长串逐键）P99 三轮结果见 `docs/PERFORMANCE.md`
I17 一节末表；**未达任务卡的 −30%，也仍远高于单键 P99 ≤ 0.5 ms 的预算**，
如实记录，不修改场景定义。

**剖析**：只剖析尾键（P99 样本所在）后，self 占比 `transition` 30.3%、`compute` 25.1%、
`language::bonus` 14.7%、`Dictionary::entry` 11.7%；`transition` inclusive 61.1%。
插桩实测调用来源：`matches_tolerant` **97.8%**、词法分支 2.2%；`bonus > 0` 仅 0.003%、
`attests_boundary` 命中 1.6%。

**改动**（输出不变）：新增 `EntryMeta`（首/末码位 + 代词/`的`/语气词/`不` 标志）与两处
字符对闸门（`AdjacencyFilter` 64 KiB 位图、`language::pair_may_match` 105 对有序表），
使 `!lexical && !frame` 的分支**完全不访问字符串池**即可定论，覆盖真实调用的 92.3%。

**正确性**：逐候选 dump（2805 行）基线与本分支逐字节相同；新增「快路径 vs 慢路径」
对照测试（关掉元数据快路径逐候选比对，16 组输入 × 2 种 flag）；`quality_report`
除词库加载耗时行外逐行相同；零按键分配与 64 KiB 会话上限保持。

**过程记录**：快路径初版把 `adjacency_may_contain` 参数写反，逐候选 dump 立即不等，
修正后恢复全等 —— 该对照测试即为长期守住此项。

## 迭代 I15 全规则长串逐键尾延迟：剖析优先、常量开销优化 · 2026-10-09 已合并（PR #26，squash `253e13e`，preview28）

评审结论：通过（性能目标未达成，如实收下）。技术负责人亲自重跑 `check.sh`、`version.py --check`（`0.1.0-preview28`）；`quality_report` 在基线 `0665ac5` 与本分支输出逐行相同（仅词库加载耗时不同）；独立复现阴性对照（把预过滤改为按 `code^1` 查精确集合 → `prefiltered_index_agrees_with_a_plain_set_on_every_encoding` 与 `boundary_attestation_matches_a_direct_encoding_oracle` 失败，11 passed / 2 failed）。基准在 13.24 共享开发机上基线/本分支二进制交替执行 3 轮，全规则长串逐键 P99：第 1 轮 33.6→17.7 ms（两端负载不等，不作结论）、第 2 轮 21.9→17.9 ms（−18.6%）、第 3 轮 18.9→15.2 ms（−19.3%）；负载 4–20，受后台进程影响大。结论与开发方一致：约 −16%~−19%，**未达 −50%，也远高于 ≤0.5 ms 目标**，不声称达标；范围仅共享核心，不含 TSF/IPC/UI。
后续方向（下一迭代候选）：`Decoder::transition` miss 路径（inclusive 70.7%）与 `compute` 逐 start DP 的算法级改造，如 `bonus` 按 `(context 后缀, 首字节)` 预计算、减少逐 start 重复前缀展开；须同样守住质量零回退。
发版：preview28，见 Releases；Arch 包由固定源码 `253e13e` 重建（PKGBUILD 已 pin），隔离 pacman 安装/升级/重装/卸载通过。


**结果先行**：目标场景三轮 P99 分别为 19.07→15.92 ms（−16.5%）、
18.29→15.49 ms（−15.3%）、20.68→17.30 ms（−16.4%），三轮一致；
P95 降幅相近，其余四个基准场景 P99 亦为 −15% 至 −18%，无一场景回退。
**未达到任务卡的 P99 −50%**，也仍远高于性能预算的单键 P99 ≤ 0.5 ms；如实记录。

**剖析（perf，13.24 / i9-10900X / `data/daily.mswydict` 184,173 条）**：
`Decoder::transition` self 32.2%、inclusive **70.7%**；`Decoder::compute` 22.7%；
`language::bonus` 11.6%；`Dictionary::entry` 10.5%；`SipHash hash_one::<&u128>` 8.5%
（全部来自 `boundary_words`）。

**两条否证结论（避免了下一次走弯路）**：

- `transition` 每轮 978,419 次调用对应 537,871 个 distinct `(left,right)`，冗余仅 1.8×；
  128 槽 memo 的 15.3% 命中率是容量上限而非缺陷。
- 编译期变体对照（表大小 128→65536、索引哈希换全域混合）P99 全落在 18.5–19.0 ms，
  **扩大或改良 memo 零收益**，故本批未动 memo。

**改动**（均不改变任何输出）：

1. `boundary_words: HashSet<u128>`（SipHash）→ `BoundaryIndex`：64 KiB 位图预过滤
   （19,196 键置位 18,825 位，误判率 3.6%）+ 同键快速哈希精确集合；无假阴性可能。
2. `language::bonus(context, …)` 从 `Decoder::compute` 的逐 rank 循环内提升到循环外（两处）。
3. `estimated_heap_bytes` 同步改用 `BoundaryIndex::heap_bytes`（共享词库堆 +64 KiB）。

**正确性**：8 组输入（含 64 字节长串）优化前后逐页每候选 `text|pinyin` **104 行全等**；
新增 `tests/long_input.rs`（5 条）与 `dictionary.rs` 内 oracle 单元测试
（对全部 19,196 个编码及 4 种扰动比对成员判定）；零按键分配与 64 KiB 会话上限保持。
详见 `docs/PERFORMANCE.md` 的 I15 一节。

## Fcitx5 真实守护进程端到端（上屏断言）纳入 CTest I14 · 2026-10-09 已合并（PR #24，squash `c655111`）

评审结论：通过。技术负责人亲自重跑 `check.sh`、`version.py --check`、全新构建后 ctest 连续 3 次（5/5，含 `fcitx5-e2e`）、运行前后 `pgrep -a fcitx5`/`pgrep -a Xvfb` 集合不变（MYSWY 的真实 Fcitx PID 未受影响）；独立复现阴性对照 2 处（词库不含 `ni'hao` → 场景 3 超时、退出 1；暂存目录去掉 `addon/chengyin.conf` → “did not load the staged chengyin plugin”、退出 1）；`PATH` 屏蔽后输出 `SKIP: missing host dependencies` 并退出 77。未改产品代码，未发版（仍 preview27）。

I13 遗留的唯一阻塞——真实守护进程链路的上屏文本回收——已解决，`fcitx5-e2e` 已注册进 CTest，
模块 CTest 由 4/4 变为 **5/5**。

**选路**：改用框架自身的 D-Bus 前端，不再走 XIM。`org.fcitx.Fcitx.InputMethod1.CreateInputContext`
建立真实输入上下文，`org.fcitx.Fcitx.InputContext1.ProcessKeyEvent` 送按键，上屏文本经该对象的
`CommitString` 信号取回。这条链路同样经过真实守护进程 → 真实 `InputMethodManager` → 真实插件，
但不需要 X 服务器、不需要窗口管理器，因此 `e2e.sh` 不再启动 Xvfb、不再用 `xdotool`，也不再依赖
libX11 与 `xterm`。I13 试过的 XIM 方案（XFilterEvent 全事件、回调式/Root 式 style、`XNFocusWindow`、
MapNotify 后取焦点、先注入再激活）全部作废并已从脚本删除。

**改动**：`platforms/fcitx5/e2e_client.c` 重写为 GIO D-Bus 客户端（原 XIM 客户端删除）；
`platforms/fcitx5/e2e.sh` 改为私有 HOME/XDG + 私有 session bus + 真实 `fcitx5` + D-Bus 客户端，
3 个场景各由客户端自己断言；`platforms/fcitx5/CMakeLists.txt` 注册 `add_test(fcitx5-e2e)`，
`SKIP_RETURN_CODE 77`、`TIMEOUT 120`，客户端链接 `pkg-config gio-2.0`。**未改产品代码，未 bump 版本。**

**三个场景**（词典为脚本自造 TSV，经 `conf/chengyin.conf` 指定；`ni'hao` 词条两条，第二条为 `拟好`）：
`nihao`+空格 → 收到 `你好`；`nihao`+Esc → 无上屏；`nihao`+`2` → 收到 `拟好`（证明数字键到达插件
候选列表而非应用）。客户端不写固定长 sleep：每个键送入后轮询守护进程回报的 `UpdateFormattedPreedit`，
确认该键已到达引擎才送下一个；终止键也在完整组合可见之后才送。每个阶段有独立超时，失败时报出
是哪一步没发生并附当时的预编辑与上屏计数。

**验证**（13.24，Fcitx 5.1.23）：`scripts/check.sh` 全绿；`python3 scripts/version.py --check` 全绿；
`ctest --test-dir build/fcitx5 --output-on-failure` 5/5 通过，连续 3 次均通过且运行前后
`pgrep -a fcitx5` / `pgrep -a Xvfb` 进程集合不变（本机 MYSWY 的真实 Fcitx 未受影响）。

**阴性对照 2 处**（本地临时变体，未提交）：① 暂存目录去掉 `chengyin.conf` → 守护进程报
`Group Item chengyin in group Default is not valid. Removed.`，脚本以 “did not load the staged chengyin plugin”
失败退出 1；② 词库 TSV 换成不含 `ni'hao` 的内容 → 场景 1 收到原样 `nihao`、场景 3 超时无上屏，
脚本以两条断言失败退出 1。

**依赖跳过**：`PATH` 屏蔽 `fcitx5`/`dbus-daemon`/`cmake` 后输出
`SKIP: missing host dependencies: dbus-daemon fcitx5 cmake` 并以退出码 77 结束。

**已知限制**：这是自动化无头结果，**不等于**真实 X11/Wayland 桌面的候选窗、光标定位与焦点行为
已通过，不作为桌面兼容性声称。本路径验证的是 D-Bus 前端；XIM 与 GTK/Qt 应用内协议仍未覆盖。


## Fcitx5 适配层真实 Instance 回归与守护进程端到端 I13 · 2026-10-09 已合并（PR #22，squash `633327b`）

评审结论：第 1 层通过；技术负责人重跑 `check.sh`、`version.py --check`、ctest 3 次（4/4）、进程集合不变，并独立复现 3 处阴性对照（去敏感保护→5 条失败；去 `select` revision 判断→12.4/12.5 失败；`clear()` 不重置会话→多条失败）；缺 testing 模块时以 `CMAKE_DISABLE_FIND_PACKAGE_Fcitx5ModuleTestFrontend=ON` 模拟，仅跳过且其余 3 项通过。第 2 层降级收下（XIM 脚本入库、未注册 CTest），由 I14 改走 D-Bus 前端后解决并已注册。未改产品代码，未发版（仍 preview27）。

新增**真实框架集成测试** `platforms/fcitx5/test_instance.cpp`
（CTest `fcitx5-instance`）：启动真实 `fcitx::Instance`，由框架按 addon 配置加载
chengyin 插件与 `testfrontend`/`testim`，键盘事件经 `TestFrontend::sendKeyEvent`
进入 `InputMethodManager` 路由，覆盖注册与激活、组合/候选/数字选词/翻页、编辑键、
修饰键与释放事件、双输入上下文隔离、敏感字段、输入法切换、失焦与重置、鼠标选词
（含陈旧候选 revision 保护）、超长输入上限、词库热切换，以及大写/Caps Lock 现状快照，
共 126 条断言。测试自造 TSV 词库并经 `conf/chengyin.conf` 注入，`SKIP_FCITX_USER_PATH`
使所有用户目录为空，不读不写用户配置、词库与输入历史。缺 testing 模块时 CMake `QUIET`
查找失败即不注册该测试（Debian/Ubuntu 部分 `libfcitx5core-dev` 情形）。

新增**真实守护进程端到端脚本** `platforms/fcitx5/e2e.sh` 与 XIM 客户端 `e2e_client.c`：
私有 HOME/XDG、私有 session bus、`Xvfb -displayfd`、真实 `fcitx5` 守护进程 + 暂存安装的
插件 + 真实 XIM 连接，`xdotool` 经 XTEST 注入按键；只结束本脚本启动的 PID，不使用任何
`pkill fcitx5`/`pkill Xvfb`。**该层未达成可断言的端到端上屏**：在 13.24 上守护进程确实加载
chengyin、XIM 前端接管按键、`fcitx5-remote -s chengyin` 可切到插件（此时守护进程内
`Engine::keyEvent` 实测逐键触发），但本机自建 XIM 客户端未能把上屏文本回收到进程内，
`xterm` 因缺位图字体无法起窗，故 3 个场景断言未通过。脚本已注册进 CTest 之前被暂时
注释，避免纳入一个已知失败用例；阻塞点与已尝试做法见脚本头部注释与交付评论。

验证：`scripts/check.sh` 全绿；`python3 scripts/version.py --check` 全绿（未改产品代码，
不 bump 版本）；模块 CTest 4/4 通过（新增 `fcitx5-instance`）。阴性对照 3 处各自复现预期失败
（去敏感字段保护→5 条失败；去 `select` 的 revision 判断→2 条失败；去掉 `clear()` 的会话重置
→多条失败），还原后恢复全绿。均为自动化结果；真实桌面 X11/Wayland 兼容性未验证，不作声称。


## Arch Linux 原生安装包与默认完整词库 I12（preview27，已发版）· 2026-10-08

PR #20（squash `7414c4f`）已合并；[v0.1.0-preview27](https://github.com/zzttzzmyswy/chengyin-ime/releases/tag/v0.1.0-preview27) 已发布，含 Arch 原生包、PKGBUILD、README.arch、Windows x64 EXE、Ubuntu deb 与 SHA256SUMS。
负责人亲自重跑核心检查、版本检查、源码 Fcitx CTest 3/3、固定源码归档 makepkg、隔离 pacman 生命周期及校验和，均通过；交付包在 bubblewrap 内真实加载 Fcitx 插件，词库存在/缺失正负对照通过。
Windows/deb 资产来自 CI 37735992088（源码 `1cb9d3d`，六项检查通过），Arch 源码 pin 为 `de47825`；产品源码与合并树一致。当前 deb 仍默认演示词库，本轮完整词库默认接入适用于 Arch 包及显式开启 `CHENGYIN_PACKAGED_DATA` 的构建。

新增 `packaging/arch/PKGBUILD`：从固定 revision 的源码归档重建 `fcitx5-chengyin-0.1.0.preview27-1-x86_64.pkg.tar.zst`（原生 Arch x86_64，不转换 deb）。

默认词库：`EngineConfig` 现在把「随包安装的完整词库路径」作为选项默认值，`reloadConfig()` 也以它为种子，因此从未写过
`conf/chengyin.conf` 的新用户直接加载 `/usr/share/chengyin/daily.tsv`（184,173 条）；用户显式填写的路径仍优先，清空则
回退 98 条演示词库；默认路径不可用时保留当前词典并在日志/提示报错，不静默换词库。该路径由 `CHENGYIN_PACKAGED_DATA`
一个开关同时决定「安装什么」和「编译进去什么」，并有 CMake 断言防两者漂移。
`CHENGYIN_DEFAULT_DICTIONARY_PATH` 必须是 `PUBLIC` 编译定义：默认实参声明在 `engine.h`，`factory.cpp` 才是插件的调用点，
原先若是 `PRIVATE` 则插件仍编译成演示词库默认值（本轮实测到该缺陷并修正）。

验证：`scripts/check.sh` 全绿；`python3 scripts/version.py --check` 全绿（新增 Arch `pkgver` 与 `version.json` 一致性检查）。
Arch 容器（Fcitx 5.1.23 / GCC 16.2.1）内 `makepkg` 成功，`check()` 跑模块 CTest 3/3，生成
`fcitx5-chengyin-0.1.0.preview27-1-x86_64.pkg.tar.zst`（2,553,313 B，SHA256 `6f6096b58749d63f309ab0dd49c81ec86f1b81c6b144336186f462cf57f41898`）；
`pacman -Qip`/`-Qlp` 可读，30 个文件全部由包管理器跟踪，`ldd` 无 not found，二进制内无 runtime-local 路径。
真实 Fcitx 守护进程（Xvfb）加载已安装插件，`fcitx5-diagnose` 列出 “Chengyin IME 0.1.0”，把 `daily.tsv` 移走后同一进程报
`无法打开词典文件`，移回后无告警——正负对照成立。`scripts/test_arch_package.sh` 在隔离 `pacman --root` 中实测安装、
重装、升级、卸载与清单，用户配置与自建词库均保留（有意 `-dd` 跳过依赖解析）。
均为自动化结果；桌面实机兼容性（X11/Wayland、候选窗、多屏）待 MYSWY 验收，未声称已通过。

## 中英文切换光标提示 I11（preview26，已发版）· 2026-10-08

PR #18（squash `c9ffaf5`）已合并到 `main`；release [v0.1.0-preview26](https://github.com/zzttzzmyswy/chengyin-ime/releases/tag/v0.1.0-preview26) 已发布（target `c9ffaf5`，含 Windows x64 安装 EXE、Fcitx5 deb 与 SHA256SUMS）。
新增 `platforms/windows/mode_hint.{h,cpp}`：切换中/英文时在文本光标附近显示约 0.9 秒的「中」/「英」提示，随后自动隐藏。窗口为 `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT`，不激活、不改变前台窗口、点击穿透；不抢焦点、不做阻塞或 COM 调用，线程模型与候选窗一致（TSF 线程内创建与销毁）。
用户主动切换的四个入口（左 Shift 单击、Ctrl+Space（TSF 保留键与键翻译两条路由）、语言栏按钮、OPENCLOSE / 转换模式外部切换）全部汇入唯一 `Service::notifyModeChanged()`，一次切换只弹一次；启动、焦点切换、`defaultEnglish` 初始化与 `english_=false` 重置均不弹。
定位复用候选窗的光标矩形来源（组合锚点 → 系统光标 → 输入视图角）与 `placeCandidates` 的翻边规则，越界时翻到光标上方并保证完全落在工作区内；尺寸随 DPI 缩放，取色沿用 `palette()`/`drawThemeSurface`。
新增 `Preferences::modeHint`（默认 true，`preferences.ini` 读写、`validPreferences`、设置页「切换中/英文时在光标附近显示提示」复选框）；旧 `preferences.ini` 缺该键按默认 true 加载，不重写用户文件。
新增原生 CTest `windows_mode_hint_test`（第 9 项）覆盖提示文字与目标模式一致、计时到期隐藏与再次切换重置、`modeHint=false` 不显示、旧配置缺键迁移、不可激活与不改前台、屏幕边缘翻边；`chengyin_probe --edits` 增加四个入口与"组合/失焦立即隐藏"的服务级断言。
验证：`scripts/check.sh` 全绿；PR #18 的 CI 6/6 全绿（Actions 37654230298），合并后 `main` 的 CI 6/6 亦全绿（Actions 37655651999，target `c9ffaf5`），`windows-tsf` 原生 CTest 9/9（含新增 `windows_mode_hint_test`）与隔离安装生命周期（安装/升级/回滚/卸载）均通过。本地 MinGW/Wine 下通过 `windows_mode_hint_test`（新增）、`windows_ui_test`、`windows_settings_test`、`windows_update_test`、`windows_com_probe`、`chengyin_probe --edits`（含新增服务级断言）。三项与本次改动无关的既有本地环境差异（改动前后表现一致，均已实测确认）：`windows_key_test` 的 skin 图元断言与 `windows_skin_test` 的 `self-contained skin round trip` 在 Wine/GDI+ 下失败，`windows_live_test` 的跨进程子进程断言在 Wine 下失败。三项均以仓库 GitHub Actions（Windows 2022 + MSVC，已全绿）为准。均为自动化结果；真实应用（记事本、浏览器、Office、多屏、高 DPI）的显示位置需 MYSWY 实机验收，未声称已兼容。

## 项目标识改名 I10（preview25）· 2026-10-07

PR #16：项目英文标识 `myswy` → `chengyin`（crate、C ABI 符号与 `chengyin_ime.h`、构建产物、安装器、deb 包名 `fcitx5-chengyin`），仓库改名为 `zzttzzmyswy/chengyin-ime`。行为不变；CLSID/profile GUID 不变。
升级兼容：安装器识别 preview24 旧注册表键/安装目录/旧布局并迁移，旧用户数据目录 `MyswyIME` 可读取，`.myswyuser` 旧备份仍可导入；词库格式魔数 `MSWYDICT` 保持不变。
验证：`scripts/check.sh` 全绿；CI 6/6（含 windows-tsf 原生 CTest 与安装生命周期）。release v0.1.0-preview25 资产取自 main `7e2caac` 的 CI artifact。均为自动化结果；preview24 用户经旧仓库 URL 重定向自动更新的效果待 MYSWY 实机确认。

## Fcitx5 跨版本兼容 I08 · 2026-10-07

PR #13：修复 Fcitx ≥5.1.13 下 `fcitx::StandardPath` 未声明导致插件无法编译；按 CMake 解析的 `Fcitx5Core_VERSION` 选择 `StandardPaths`（≥5.1.13）或 `StandardPath`（<5.1.13）。
实测 Fcitx 5.1.7（Ubuntu 24.04）/5.1.12（Debian trixie）/5.1.23（Arch）构建 + CTest 2/2；`-Werror` 未放宽；`scripts/check.sh` 全绿；PR 的 fcitx5/windows-tsf CI 通过。
设计文档遗留的 `Found 0 input method(s)` 判定为探针未安装 `inputmethod/chengyin.conf`，`cmake --install` 后 IM 注册通过。
遗留：新版 Fcitx（Arch 容器）CI 矩阵作业补丁因 token 缺 `workflow` scope 未入库，待有权限凭据应用。均为自动化结果，未做 Fcitx 实机会话验收。

## 预设词库翻倍 I05 · 2026-10-07

PR #9：内置词库 87,540 → 184,173 条（2.10×），`.mswydict` 25.7 MiB，未改 v2 格式与 250,000 条 / 64 MiB 上限。
新增来源 THUOCL（MIT，70,276 条）与 phrase-pinyin-data `pinyin.txt`（MIT，26,357 条），固定修订与许可原文在 `data/sources/`；排除 rime-essay（LGPL-3.0）。
新词权重 1，并受“不抢既有首选/不遮蔽模糊音键/须有词频证据”三条准入规则约束；200 条随机抽样无词频证据 0/200。
同命令 `quality_report` 前后对照：全部/dev/test 的 Top-1、Top-9、可达、错误组合率完全一致。词库加载 120→358 ms（启动一次性），日常逐键 P50 137→162 µs。
合并后 main 的 windows-tsf 因 `test_settings.cpp` 写死旧条数（87541）失败，随 preview24 修正。自动化结果，Windows 实机体感待 MYSWY 验收。

## 长拼音分段输入学习完整词组 · 2026-10-06

`crates/ime-core/src/session.rs` 增量模式原先每段上屏后 `learn_commit()` 即清空 `learning_key`，只学到单段，长拼音经分段选词后不会学到合并后的完整词组。
现新增 `PhraseBuffer` 累计同一次组合内各段的拼音与文本；组合在最后一段完成上屏时标记 `pending`，宿主确认写入后 `learn_commit()` 在同一次调用内额外 `record_selection(完整拼音, 完整词组)`，返回值语义不变（仍表示是否有学习成功）。
取消 / Esc / 中途清空 / 非汉字上屏 / `learning_enabled=false` 均不学习；超过 `MAX_INPUT_BYTES`、`MAX_TEXT_BYTES` 或非汉字时整句放弃，不截断出错配键值；内存仍计入 64 KiB 上限。
被学习行也可作为收尾分段（第二次及以后输入走该路径），同样累计。非增量（分段暂存）模式原路径已通过测试确认会学到完整词组，仅补回归。
新增 `crates/ime-core/tests/phrase_learning.rs` 7 项；`scripts/check.sh` 全绿（fmt、Clippy -D warnings、全工作区测试、Release、C ABI smoke、CLI）。

## 质量优化 I04（未发版） · 2026-10-06

PR #7：跨音节换位（`guan'ai` 敲成 `guaani`）召回恢复；词库未按所敲键收录的合成组合不再抢首位。
同命令 `quality_report` 复测（全部 488 / test 244）：全部 Top-1 92.0%→93.4%、Top-9 97.7%→98.6%；
fuzzy Top-1 76.1%→79.5%、错误组合率 3.4%→0.0%；typo Top-1 83.8%→88.8%、可达 93.8%→98.8%；
test Top-1 91.4%→93.4%；其余类别不变。标注集未改。`fuzzy` 基线中约 18/21 条 miss 为“所敲串本身有常用词”，
并非排序缺陷（见 I04 评审）。延迟仅做同机同轮 A/B，未声称尾延迟达标；本项为自动化结果，非实机兼容验收。

## preview23 准确整句与前缀保护 · 2026-10-06

见 WINDOWS_PREVIEW23_TASKLIST。用户输入 xianzaikaishiba 在键盘纠错开启时被“想再开时报”等多词局部纠错组合抢占。
新增准确句末语气词连接、整句纠错总预算与规范拼音校验；立即分段保护准确多音节词组前缀。
四 flags 首选“现在开始吧”，同样修复明天开始吧/我们现在开始吧等，第一页保留“现在开始”；选中只消费13字节，ba继续匹配。
完整词语模糊召回保留；自造词库、高频错误组合、上下文、默认/staged与立即上屏、助词终止位置均有回归。
102 Rust Release、fmt/Clippy、零按键分配/64KiB、MSVC C ABI、8/8 Windows CTest（38.61s）通过，
含TSF host UI准确整句/前缀接受与取消余音节不丢已上屏文字。
独立488条集，在旧/新 Windows立即分段配置下总体/分类/半集指标一致：Top1/Top9/可达92.0%/97.7%/98.8%。
评测集保持不变。
本机交付 chengyin-windows-x64-0.1.0-preview23-msvc.exe，5135063 bytes，SHA256 `628a33c65c765c74a11edd6c9870cb6ff3ca16c9068111f6b2fceeb58f44e126`。
PE/7-Zip/全负载SHA/COM/生产字节一致/22项许可与插画署名CRC通过，已复制主目录。
源码 `2fc0fa3` 的 [Actions 37453639077](https://github.com/zzttzzmyswy/chengyin-ime/actions/runs/37453639077) 六作业成功，含 Windows 隔离安装、升级、回滚、卸载。
个人安装/设置/学习未改，真实宿主/混合DPI仍待E，D其余优化继续。

## preview22 Q 版大肥鱼与准确结构组句 · 2026-10-06

见 [本批清单](WINDOWS_PREVIEW22_TASKLIST.md)。用户授权本地裁切、去背景后，已替换为指定设定图正面全身形象；
主题 ID 12 保持，插画来源与权利说明随安装包保留。
准确 A/不A、A不/A 和多字词＋“的”得到词典锚定的组句支持；
`kaibukai / diannaode / shoujide` 空历史、关闭/全开匹配时首选分别为“开不开 / 电脑的 / 手机的”。
准确句可用时抑制猜测生成句；完整拼音的纠错/缩写组句不扩张音节数，词库模糊词及分段选词仍保留。
合并 main 至 0b2ae4b：B 学习 revision/重试、C 词库加载恢复/一次合并、D 独立评测基线保留。
版本由 version.json 统一生成，CI 安装测试包名也动态派生；模拟 bump 检查通过。
488 条独立标注集本机重放：Top-1 92.0%、Top-9 97.7%、200 页内可达 98.8%，与 I03 基线一致；
测试半集 244 条 Top-1 91.4%，整词/单字类别首选均 100%。此集未覆盖所有自然输入。

96 Rust Release、fmt/Clippy、零按键分配/64 KiB、MSVC /W4 /WX C ABI、8/8 原生 CTest（31.04 s）通过。
全身插画横纵候选和五档 DPI fixture 已核对。旧 preview21 GDI+ 全局退出停滞由实际线程栈证实；
新图片资源仅存 RGBA 与有界 DIB，GDI+ 在调用内结束。200% DPI fixture 坐标与测试进程 TIP 隔离也已修正。
五条结构输入、全部匹配规则、4100 个逐键＋可见标注样本，核心 P50/P95/P99 = 261.9/4994.4/7234.7 µs；
192 DPI 全身皮肤暖重绘为 841.8/1079.0/1337.1 µs，GDI 对象 15→15。口径见 PERFORMANCE。

交付 `chengyin-windows-x64-0.1.0-preview22-msvc.exe`，5104648 bytes，SHA-256
`110e733b16b1a7a1ef77de40abde114988a66d52ba93b118e6491e2242a6786c`，已复制至主工作目录。
PE、7-Zip、全部负载 SHA-256/COM、三生产二进制一致、22 项运行时许可 CRC 与插画署名包校验通过。
证据见 tasklist。个人安装/设置/学习未改；真实升级宿主、物理混合 DPI 和 review D 优化与 E 继续保留；B/C/D 基线已合并。
源码 `40fec9d` 的 [Actions 37445555758](https://github.com/zzttzzmyswy/chengyin-ime/actions/runs/37445555758) 六作业全部成功，含 Windows 隔离安装、升级、回滚、卸载。

## preview21 候选皮肤系统 · 2026-10-06

按用户新需求重新开启主题开发，分支 `codex/theme-skins`；见 [任务清单](WINDOWS_PREVIEW21_TASKLIST.md)、[皮肤指南](SKINS.md)。
系统/白/黑之外，新增青瓷枝叶、夜航星图、深蓝来信 Q 版鲸鱼娘。主题控制面板、圆角、选中样式、装饰侧栏；
文字按原字号排版，空间不足时隐藏装饰，可手动关闭。高对比度优先系统配色。
支持最多 64 个自定义 .cyskin 收藏、导入/导出、PNG 插画、配色、圆角/留白/纹样编辑，预览后应用。
包体/图片有界，主要/次要/纠错文字对比度校验；图片与布局配置作为快照加载，不在按键或绘制时读文件。
缩放图片使用最多四项有界缓存；同一自定义 ID 更换图片或关闭装饰时，活动候选几何同步刷新。
旧 3–5 主题继续迁移系统，新标识 10–13 不复用已删除角色主题。

最终直接采用用户指定 https://treapgogo.github.io/deepseek-whale-girl/ 网站的透明 Q 版头像，字节保持；来源说明随源码和 THIRD_PARTY 保留。
最终素材下 86 Rust Release、MSVC /W4 /WX C ABI、8/8 原生 CTest（24.89 s）及六主题横纵截图/五档 DPI 模拟通过。
候选窗口 300 次暖重绘，鲸鱼娘 P50/P95/P99=524.2/696.9/983.2 µs；GDI 对象稳定，详见 PERFORMANCE。
交付 `chengyin-windows-x64-0.1.0-preview21-msvc.exe`，4628751 bytes，SHA-256
`1a701b8f1f342effc87636062637c9d510fd8dd291baaf5d6e25cac500fd07c5`，已复制至主工作目录。
PE、7-Zip、全负载 SHA-256、COM、三二进制逐字节一致、测试通知隔离与 21 项运行时许可 CRC 校验通过。
证据 `build/native-test-preview21.txt`、`tests-release-preview21.txt`、`abi-test-preview21.txt`、
`ui-preview21/`、`skin-paint-bench-preview21.txt`、`package-verify-preview21.txt`、`delivery-preview21.json`。
preview21 源码 9a005e5 的 [远程 CI 37386348117](https://github.com/zzttzzmyswy/chengyin-ime/actions/runs/37386348117) 已完成且成功；个人实机升级仍单独验收。
没有修改个人安装/学习记录。旧 Explorer 升级验证、物理混合 DPI、review D 优化阶段与 E 保持待办（B、C、D 基线已于 2026-10-06 合并，见 REVIEW_REPAIR_PLAN、QUALITY_BASELINE）。

## 最新：preview20 单音节与受限 TSF 激活 · 2026-10-05

见 [本批清单](WINDOWS_PREVIEW20_TASKLIST.md)。完整单音节优先准确/模糊单字历史及单字，
词语和补全继续保留；分隔、多音节、首拼和前缀上屏后的剩余输入分别解析。
受限 TSF 激活从原先直接拒绝改为内嵌词库/内存配置路径，注册 SECUREMODE 能力。
该模式不读写个人设置/词库/学习、不提供设置入口、不产生学习和上屏联想；普通路径保持。
URL/search/default、密码/private、UI-only 与 100 次交替激活回归通过。

86 Rust Release、fmt/Clippy、零按键分配/64902 bytes、MSVC /W4 /WX C ABI、7/7 CTest（31.00 s）通过；
最终 Service 修改后 COM/TSF 两项再次通过（9.17 s）。横纵 bao/shi 窗口及选字 ABI、PE/7-Zip、
完整负载 SHA-256/COM、三生产二进制一致、21 项许可 CRC 通过。
安装包 preview20，4392201 bytes，SHA-256
`6b68ee3ad027b4e25f8957d194f49529d98c1e48ecb0210329f9a18eac643b41`，复制至主工作目录。
五单音节全规则逐键/可见标注 P50/P95/P99 = 15.5/176.0/199.7 µs；长串预算仍未全部达标，见 PERFORMANCE。

已在实际 Explorer 的 WinUI 地址栏/搜索框复现旧 preview7 直接输出英文且无候选。
该进程尚未加载新 DLL；安装器提权被 Windows 返回“操作已被用户取消”，安装没有完成，
真实新版本控件验收仍待完成。当前调用进程无管理员权限，不重复触发 UAC。
明确的受限激活缺口已修复，但不能认定它是这两个实际控件的唯一原因。
下一步先完成升级后的 Explorer 双输入框验证，再回到普通学习 revision 与有界 writer 重试。

preview19 a55c97c 的 [Actions 37318044390](https://github.com/zzttzzmyswy/chengyin-ime/actions/runs/37318044390)
六作业和隔离安装生命周期通过；preview20 的远程 CI 单独追踪。

## 最新：preview19 拍照候选与旧学习保护 · 2026-10-05

任务见 [preview19 清单](WINDOWS_PREVIEW19_TASKLIST.md)。隔离 preview12、空历史、全部规则复现
无依据的“拍找、派找”等先于“拍照”；在运行的 Chrome/steamwebhelper 等确实仍加载 preview12。
preview18 已有完整词保护；本次补齐未收录旧历史累计计数可直接提权的漏洞：
须有至少三次近期命中才提权，词库可证明的旧习惯继续可用，个人历史没有清空。
`paizhao/pai'zhao` 的空历史首两项“拍照、牌照”；错拼召回、正常学习与前缀选词保持回归。

82 项 Rust Release、fmt、Clippy、零按键分配/64902 bytes、MSVC /W4 /WX C ABI 通过；
原生 7/7 CTest（22.08 s）、窗口绘制/拍照选词、三二进制和包内负载一致、PE/7-Zip/完整 SHA-256/COM、
21 项许可 CRC 通过。Debug 有一项已有测试 EXE 启动时文件占用错误，未把它算作通过，详见本批清单。
交付 `chengyin-windows-x64-0.1.0-preview19-msvc.exe`，4384807 bytes，SHA-256
`f9f460e54f372af89fa83feda5201080c24ed28cd3d7bcdb224b4385e773132b`，复制至主工作目录。
尚未安装到个人电脑；升级后完整退出重开加载旧 DLL 的应用才能使用新算法。
下一步仍优先学习跨应用 revision/有界重试，再按审查计划推进词库、性能与实装矩阵。

## 最新：preview18 代码审查修复 · 2026-10-05

原 preview8–17 的 12 个未推送提交已快进合入并推送 main `d07fe57`。
[Actions 37309334271](https://github.com/zzttzzmyswy/chengyin-ime/actions/runs/37309334271)
六作业全部通过，包括隔离安装/升级/回滚/卸载；补足下述 preview17 的远程验证记录。
审查与修复在 `codex/review-hardening`；详细结论见 [代码审查](CODE_REVIEW_2026-10-05.md)，
[修复排期](REVIEW_REPAIR_PLAN.md)、[本批清单](WINDOWS_PREVIEW18_TASKLIST.md)和[精简交接](CONTEXT_HANDOFF.md)。

本批修复显式组合声母误读、词库音节数与字数解析遗漏、尾部分隔的整词候选/学习、
学习文件代际未隔离、更新版本/包名/下载 URL 未绑定、测试配置通知干扰；补齐 a/e/o 首字母索引。
77 项 Rust、fmt、Clippy、零按键分配/64 KiB、MSVC /W4 /WX C ABI 已通过。
最终 MSVC 原生 **7/7 CTest（28.50 s）**、隔离 UI 与截图检查通过；生产三个二进制无测试通知通道。
安装包 `build/packages/chengyin-windows-x64-0.1.0-preview18-msvc.exe`，**4388187 bytes**，
SHA-256 **57604fadbb04978675cd651008b1e03e43430ee7319f5721e5d9f400ccb428a4**。
7-Zip、完整 SHA-256/COM、三生产二进制一致与 21 项许可 CRC 通过，并复制到主工作目录同路径。
证据 `build/tests-preview18.txt`、`clippy-preview18.txt`、`abi-test-preview18.txt`、
`native-test-preview18.txt`、`ui-test-preview18.txt`、`package-preview18.txt`、`delivery-preview18.json`。
截图 `build/ui-preview18/`；没有替换个人安装或使用个人历史作为 fixture。
源码 `ad84c9d` 的 [Actions 37314439254](https://github.com/zzttzzmyswy/chengyin-ime/actions/runs/37314439254) **六作业全部通过**，
包括 Linux/Windows Rust 与 ABI、MSRV、portable core、Fcitx、原生 TSF 和隔离安装/升级/回滚/卸载。
本地交付与远程 runner 交付各有自己的构建环境及摘要，不能混用哈希。

100 轮、87540 条词库，Win11 build26200 / Ryzen9950X / Rust1.99 MSVC bench：
全规则六缩写逐键 P50/P95/P99 **2.5/348.2/3147.2 µs**，最终键 **1.2/3.5/6.4 µs**；
六纠错/长串逐键 **1122.7/12380.6/15105.1 µs**。同期间旧版长串 P99 14491.7 µs，
单次差值不能证明因果或未退化，性能预算仍未满足；详见 [性能口径](PERFORMANCE.md)。
会话仍 **64902 bytes**，共享词库堆估算 **13209127 bytes**，单次加载 80.944 ms。

下一批优先解决普通学习跨应用快照同步和后台存储失败的有界重试；再处理词库失败重载、
峰值/导入规范化、版本统一、独立质量与性能基准、实装宿主/物理混合 DPI。
排期以测试门槛推进，不能用核心或模拟 TSF 检查宣布全部实机兼容。

## preview17 完整声母缩写优先 · 2026-10-05

任务见 [preview17 清单](WINDOWS_PREVIEW17_TASKLIST.md)，新分支 `codex/initials-word-priority`。
修复开启全部模糊音/纠错后 `ssdd` 首页被“舍得、杀得”等较少字候选占据的问题。
共享不可变首字母索引优先完整的一字一个声母匹配，`ssdd` 首选“世世代代”，
`zgrm` 首选“中国人民”，`zhrm` 首选四字的“走火入魔”。有完整同字数词条时仅显示对应词语和
最多两项对应字数的准确历史；没有才走既有较少字、纠错、组句回退。全拼和前缀选择保持原行为。
完整索引避免旧有界首拼搜索提前裁掉低频词，完整同音词分页；正常缩写不标为错拼。

**73 项 Rust**、fmt、Clippy -D warnings、MSVC /W4 /WX C ABI、**7/7 原生 CTest（25.91 s）**通过。
新增 6 项回归覆盖 11 单独模糊音规则、四类独立纠错及合并规则、137 同音词分页、历史上限/短词过滤、
缓存、编辑、词库替换、分隔符、长规范拼音安全及 2/3/4/5/40 字输入；初始化后路径仍零按键分配。
会话 inline+预留 **64,902 bytes** <64 KiB，共享词库堆估算 **13,209,127 bytes**；单次加载 86.957 ms。
100 轮、87540 条真实词库、全规则六个声母缩写的全部逐键/可见标记 P50/P95/P99 为
**1.3/217.8/2260.9 µs**；只计完整匹配的最后一键为 **0.9/1.2/2.3 µs**，不可代替整串或 UI 延迟。
Win11 build 26200、Ryzen 9 9950X 16C/32T、Rust 1.99 MSVC release，详见 [性能口径](PERFORMANCE.md)。

原生生产词库的横/纵候选窗口及 ABI 选词上屏检查通过，截图 build/ui-preview17/。
交付：build/packages/chengyin-windows-x64-0.1.0-preview17-msvc.exe，**4353979 bytes**；
SHA-256 **71b82e45dcf68750d8a095a4ce64407646a055a04ffe7082ef58259e5a610173**。
7-Zip、完整负载/COM、最终三二进制逐字节一致与 21 项许可 ZIP CRC 通过。
证据：build/tests-preview17.txt、initials-test-preview17.txt、clippy-preview17.txt、abi-test-preview17.txt、
native-test-preview17.txt、bench-preview17.txt、ui-test-preview17.txt、package-preview17.txt、delivery-preview17.json。
未修改个人安装、设置、学习；未运行新远程 CI、公开 Release 或安装生命周期。
下一步：安装 preview17 后复验真实宿主的首拼输入及前缀上屏，物理混合 DPI 和安装生命周期继续独立验收。

## 已结项阶段：preview8–16

**2026-10-05：Windows preview8–16 开发阶段已结束。** 整理、分支合入及交付清单见
[阶段结项记录](WINDOWS_PHASE_WRAPUP.md)。后续需求另起开发分支，未验收范围保留在记录中。

本阶段最终批次：**preview16 增加从前到后的词组/单字选择，立即上屏所选部分，再匹配剩余拼音。**
任务见 [WINDOWS_PREVIEW16_TASKLIST](WINDOWS_PREVIEW16_TASKLIST.md)。整词、整句和准确/模糊历史字词两线保持优先。
前缀来自准确词库边，按消耗长度优先并支持同音分页；不依赖组句猜测，不受单项未知整句兜底额度约束。
`zhengzebiaodashi` 首选“正则表达式”，随后“正则”、“正”等前缀；选“正则”即上屏这两个字，
剩余 `biaodashi` 重新给出“表达式”、“表达”、“表”等选项，也可连选“正”→“则”→“表达式”。
单个完整音节不做不必要的内部截断；未知尾部仍可修改、取消或通过空格提交其原始拼音。

Windows 默认启用新模式；C ABI v1 追加显式 configure_incremental，旧适配器默认暂存段协议保持不变。
TSF 同一写锁提交选择及剩余预编辑，再收缩 composition 起点，已选文字退出临时编辑。
Home、退格和 Escape 仅影响剩余 raw；数字、鼠标和宿主 Finalize 同一路径，重复异步选择仍合并。
学习只记录被接受的前缀拼音与文字，末尾分隔符不进入学习键，未接受的范围变更不训练。
故障注入发现短输出后的旧光标超界，已在 ShiftStart/GetRange 失败时恢复有效光标、结束编辑并吞掉选择。
鼠标持续编辑检查还补全了模拟宿主写后 EditRecord，使立即及异步选择后的剩余 composition 都受到真实式通知核对。

**67 项 Rust 测试**、fmt、Clippy -D warnings、MSVC /W4 /WX C ABI、
**7/7 原生 Windows CTest（18.44 s）**通过。新增真实词库及 137 同音前缀分页、分隔符、连选、取消、
模式忙拒绝、C ABI 双输出/旧模式兼容、三种宿主选择与 SetText/ShiftStart 故障回归。
8192 历史的初始化后按键路径零分配测试启用立即分段，会话 inline + 预分配容量 **64,894 bytes** <64 KiB。
候选横/纵截图显示整词加前缀，选中后的剩余窗口已人工核对；八页原生设置、字体、模拟多 DPI 回归通过。

性能：Win11 build 26200，Ryzen 9 9950X 16C/32T，Rust 1.99 MSVC release，87540 条词库，100 轮；
以下 real Session 均启用立即分段，计核心 process 与可见字母标记，排除宿主/UI/磁盘：

| 语料/范围 | 样本 | P50 | P95 | P99 |
| --- | --- | --- | --- | --- |
| 全规则六输入，逐键及可见标记 | 4900 | 1.001 ms | 10.713 ms | 13.004 ms |
| 全规则 yingshe/yinshe/yignshe，逐键及标记 | 2000 | 0.261 ms | 3.291 ms | 3.395 ms |
| 无规则五输入，含 60 bytes 长句，逐键 | 12100 | 0.066 ms | 2.683 ms | 3.512 ms |

词库加载 **55.508 ms**，共享词库堆估算 **12,637,655 bytes**。零按键分配不包含成功学习确认的快照分配。
合成 8192 历史与缓存、确认和查询基准另列 build/bench-preview16.txt，不能替代独立输入质量或端到端延迟测量。

交付：build/packages/chengyin-windows-x64-0.1.0-preview16-msvc.exe，**4385302 bytes**；
SHA-256 **c0d0bc1e9203033e5506194adca18632813afa77560dfac1cd1eee66e437c299**。
包预检查、7-Zip、解包全负载哈希/COM、三二进制与最终构建一致及 21 项许可 ZIP CRC 通过。
证据：build/tests-preview16.txt、clippy-preview16.txt、abi-test-preview16.txt、native-test-preview16.txt、
bench-preview16.txt、ui-test-preview16.txt、package-preview16.txt、delivery-preview16.json；截图 build/ui-preview16/。
本批没有替换个人安装或修改个人设置/学习；无新安装生命周期、远程 CI 或公开 Release。

下一步：安装后验证更多真实宿主的分段上屏与 composition 属性；物理混合 DPI 多屏、隔离安装生命周期及独立
长句/歧义切分质量语料继续单独验收；继续降低长句高百分位成本。
升级后需重开已加载旧 DLL 的应用一次，既有学习和用户词库无需清除。

历次状态记录见 [历史归档](history/STATUS_THROUGH_PREVIEW15.md)，各版任务清单继续保留。
