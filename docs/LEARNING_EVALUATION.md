# 选词学习评估：澄音现有机制 vs librime / libime / libpinyin

I23 算法 A4 的评估阶段。**只做评估，不改核心行为**：本文回答“澄音现有选词学习相对
三个参照还缺什么”，结论全部有评测数据或源码依据。核心代码、`include/`、`platforms/`、
`data/daily.tsv`、`version.json`、`.github/workflows` 均未改动。

参照源码为浅克隆到工作目录外（未提交）：librime（`rime/librime`）、libime
（`fcitx/libime`）、libpinyin（`libpinyin/libpinyin`），均取默认分支的最新一次 clone。
下文引用只给文件名与函数名，不给行号——参照是外部仓库，行号会随其上游变动而失效；
澄音自身的引用一律给 `文件:行号`。

## 一、结论先行

| # | 候选差距 | 是否真实存在（数据） | 是否值得做 | 建议 |
| --- | --- | --- | --- | --- |
| 1 | **持久化二元上下文**（“上一个提交词 → 本词”的个人偏好跨会话保留） | **真实存在**。`ln2;context` 12 对上下文行 Top-1 恰为 50.0%（12/24），即引擎对“前一词不同”完全不敏感：同一训练、只换前词，两行期望不同而引擎给出同一个词，因此最多命中一半。 | **暂不做**（见 §5 代价） | 记录为已知缺口；若要做得单独立项，见 §5 的最小方案 |
| 2 | 时间衰减 | 已具备。`Preference::recent`（`profile.rs:138`）按 epoch 右移，`ln3;decay` 12 行**含 `idle:4096` 与不含的阈值完全相同**（N=2/3/5 均为 M=N 翻转、M=N−1 不翻转） | 不需要 | 无需改动 |
| 3 | 记录数上限 + 淘汰 | 已具备，且比参照更细。`MAX_PROFILE_RECORDS=8192`（`profile.rs:7`）满时按平滑近期命中率淘汰（`profile.rs:198-214`）；librime 无条目上限，靠 `kDiscardThreshold=1e-200` 的权重衰减自然淘汰 | 不需要 | 无需改动 |
| 4 | 竞争词惩罚（选了 A 就压 B） | 已具备。`record_selection` 对同键其他读法记 `trials`（`profile.rs:240-268`），`trials≥16 && hits*10≤trials` 直接淘汰。libpinyin 的 `train_result3` 只做 seed 翻倍，**没有**竞争词惩罚 | 不需要 | 无需改动 |
| 5 | 二元 bigram 打分本身 | 真实存在，但**与 #1 是同一件事的两面**：参照的 bigram 强度全部依赖持久化统计 | 同 #1 | 同 #1 |

一句话：**调研列的“时间衰减/上限”澄音已经有了，不应重复造；唯一真实的差距是持久化的二元上下文，而它当前不值得做。**

## 二、澄音现有学习机制清单（文件:行号）

| 机制 | 位置 | 行为 |
| --- | --- | --- |
| 偏好表条目 | `crates/ime-core/src/profile.rs:10-19` | `key`（拼音）/`text`/`count`/`sequence`/`hits`/`trials`/`epoch` |
| 记录上限 | `profile.rs:7` `MAX_PROFILE_RECORDS = 8192` | 满时淘汰近期命中率最低者（`profile.rs:198-214`），平滑式 `(hits+1)(trials+2)`，recency 仅作 tie-break |
| 时间衰减 | `profile.rs:138-150` | `recent()` 每 256 次事件右移一位（shift 上限 16）；`observe()` 先按当前 epoch 衰减再累加 |
| 可靠性门槛 | `profile.rs:155-165` | 无据text 需 `count≥3` 且近期 hits≥3；词库确认词（`attested`）可直接学；近期命中率过低者失去提升资格 |
| 竞争词惩罚 | `profile.rs:240-268` | 一次真实选词对同键其他读法记 `trials`；`trials≥16 && hits*10≤trials` 淘汰该行 |
| 学习入口 | `session.rs:1603-1630` | `learn_commit()`：受 `learning_enabled` 与 `learning_key` 双重把关，宿主确认写入后才训练；增量模式还学整句 `phrase` |
| 会话内上下文 | `session.rs:180`（`context`）、`session.rs:1804-1831`（`remember_commit`） | 最近一次提交的**文本**（截尾 63 字节）；`reset()` 清空（`session.rs:584-592`） |
| 会话内近期选词 | `session.rs:181`（`recent[32]`）、`session.rs:1821-1829` | 最近 32 个选中词的 entry id，MRU 排序 |
| 上下文打分 | `session.rs:1745` → `dictionary.rs:916-939` | `context_bonus` = `language::bonus`（106 行人工关联表）与“上下文后缀+候选自身文本”能否组成词库词 |
| 排序生效范围 | `session.rs:1555-1562`、`session.rs:1689-1802` | only when `context`/`recent` 非空或打开了模糊匹配；首个 64 条内重排 |
| 持久化格式 | `profile.rs:275-360` | `MSWYUSR2`（magic 8B + count + sequence + crc32 + 逐行），可读 `MSWYUSR1` 并迁移；上限 4 MiB |
| 二进制快照缓存 | `profile_cache.rs:57-179` | 会话内 16 槽历史查询缓存，**不持久化**，`invalidate()` 于每次学习后 |

### 关键否定事实（本文最重要的一条）

`context` 只喂给两处：`Dictionary::context_bonus`（词库搭配）与 `language::bonus`
（106 行人工关联表）。**它从不参与 profile 偏好的排序**——`profile.rs` 里没有任何函数
接收上下文参数，`learn_commit` 的两次 `record_selection` 只带 `key`/`text`/`flags`。
因此“在 A 后面常选 X、在 C 后面常选 Y”这种个人习惯在澄音里无处存放，也无法跨会话保留。

## 三、三参照对比

| 维度 | 澄音（chengyin-ime） | librime | libime | libpinyin |
| --- | --- | --- | --- | --- |
| 个人统计的键 | 拼音 key → 文本（一元，`profile.rs:169`） | 拼音 code+文本 → `{commits, dee, tick}`（`dict/user_dictionary.h`、`user_dictionary.cc::UpdateEntry`） | **词序列**（unigram + bigram，`core/historybigram.h`） | 词 token → 频率；**上一词 → 当前词**（`storage/ngram.h`、`pinyin.cpp::train_result3`） |
| 时间衰减 | **有**：epoch 右移（`profile.rs:138`） | **有**：`algo/dynamics.h::formula_d`，按 tick 指数衰减 | **无显式衰减**：靠三级 LRU 池下沉（`historybigram.cpp::HistoryBigramPool::add`） | **无衰减**：seed 只增不减（`train_result3`） |
| 条目上限 | **有**：8192 + 命中率淘汰（`profile.rs:7,198`） | 无硬上限；`kDiscardThreshold=1e-200` 时丢弃（`user_dictionary.cc::CreateDictEntry`） | **有**：三级池 128 / 8192 / 65536 句（`historybigram.cpp` 构造函数） | 无显式上限（BDB 存储） |
| 竞争词惩罚 | **有**：`trials` 记漏选，命中率过低淘汰（`profile.rs:240`） | 有：未选中者 `commits=0` 时 `formula_d(0.1, …)` 轻度衰减（`UpdateEntry`） | 有：bigram 只在被选项上加 1，未选项相对概率自然下降 | **无**：只对选中项加 seed |
| 二元上下文 | **无（本文的缺口）** | 部分：`commit_history.{h,cc}` 保留 20 条提交记录，但只被 `gear/table_translator.cc`、`punctuator.cc` 用于**编码/标点**；语法打分走 `gear/grammar.h::Grammar::Query`，本仓库默认未启用（`plugins/` 无 grammar 实现，`plugins_module.cc` 未注册） | **有**：`scoreWithCode` 用 `0.8*bigram + 0.2*unigram`，`ratio = bigramFreq(prev,cur)/(unigramFreq(prev)+…)`，持久化（含 magic/version） | **有**：`train_result3` 写 `m_user_bigram[last_token][token]`，选词时作为**约束**重算（`pinyin.cpp::pinyin_train` → `train_result3`） |
| 冷启动/未知词 | 未 attested 文本需近期 3 次命中（`profile.rs:161`） | 权重公式含 `dee`/`tick`，无独立门槛 | `<unk>` 惩罚 + `HISTORY_BIGRAM_ALPHA_VALUE` 池权重（`constants.h`） | seed 翻倍有上限 `ceiling_seed = 23*15*64`（`train_result3`） |
| 上限相对规模 | 8192 条，≥2 MiB 快照（`profile.rs` 测试） | 无界限 | 128+8192+65536 句，全序列 | 依 BDB 文件 |

对比要点：**参照的“高级”机制集中在二元上下文**；其余（衰减、上限、竞争惩罚）
澄音不但都有，而且在有些点上更细（竞争词显式命中率淘汰、无据文本的近期证据门槛）。

## 四、`learn` 评测类基线

新增 `learn` 类 48 行（dev 24 / test 24），三个场景各占一个难度分档。评测器新增
`ctx:<拼音>=><文字>` 前置步骤（在**真实会话**上打字、翻页选中、确认写入），`record:`
与 `idle:<n>` 沿用/新增。

复现命令（`--incremental` 必须经 `--` 传）：

```sh
cargo run --release -p chengyin-core --example quality_report --locked
cargo run --release -p chengyin-core --example quality_report --locked -- --incremental
cargo test -p chengyin-core --test quality_corpus --locked
```

分档基线（两种模式**完全相同**，说明该机制在增量/整句模式下行为一致）：

| 分档 | n | Top-1 | Top-9 | 可达 | 错误组合率 | 按键 P50/P95 | 考什么 |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| `ln1;word` | 12 | **100.0%** | 100.0% | 100.0% | 0.0% | 8 / 10 | 单词级：一次确认即把该读音第二读法提到首位 |
| `ln2;context` | 24 | **50.0%** | 100.0% | 100.0% | 0.0% | 8 / 9 | **上下文条件偏好：证明缺口（见下）** |
| `ln3;decay` | 12 | **100.0%** | 100.0% | 100.0% | 0.0% | 8 / 9 | 衰减：翻转阈值，含/不含 `idle:4096` |
| `learn` 合计 | 48 | 75.0% | 100.0% | 100.0% | 0.0% | 8 / 9 | |

全部 580 行整体：Top-1 92.4%、Top-9 98.8%、可达 99.7%、错误组合率 0.2%（两种模式同）。

### 4.1 `ln2;context` 的 50.0% 是**证明**而非样本

`ln2;context` 每对两行的训练部分**逐字节相同**（`ctx:<A>` + 3×`record:key=>默认读法`
+ 3×`record:key=>对手读法`），只有**末尾的 `ctx:` 步骤**不同：
A 行在其后期望默认读法，C 行在其后期望对手读法。

于是引擎若不看前词，两行从**同一个状态**出发，只能返回同一个词：
它至多命中两行中的一行。实测恰为 24 行中命中 12 行 = **50.0%**，与推理完全吻合，
且两种模式一致。这个 50.0% 是“上下文维度对排序零贡献”的直接读数，不是精度估计——
消掉该维度（例如给两行同一前词）后 Top-1 会变成 100%，差额全部来自这一维度。

该性质由 `crates/ime-core/tests/quality_corpus.rs::learn_context_pairs_are_byte_identical_but_for_the_trailing_context`
自动守住：它按（split + 训练部分）分组，要求每组恰好 2 行、两行期望不同、末尾步骤为
`ctx:`；若将来重新生成、手工编辑或重新分半把一对拆开，测试会失败，而不是让一对
“看起来仍然合理”的行悄悄失去证明力。

**阳性对照：`ctx:` 不是空操作。** 50.0% 必须排除“上下文根本没接上”这一解释，
否则两行空转也会得到同样的数。实测同一会话内先提交人工关联表里的词，再查下一词，
排序**确实改变**：

| 会话内已提交 | 再查 `hao` | 再查 `xihuan` |
| --- | --- | --- |
| `你` | **好**、号、浩 | — |
| `我` | — | 喜欢、喜欢你、喜欢吃 |
| `中国` | **号**、好、浩 | 喜欢、喜欢你、喜欢吃 |
| （全新会话，无上下文） | 好、号、浩 | 喜欢、喜欢你、喜欢吃 |

即上下文**确实**进入了打分路径并改变了首选（`你→hao` 把“好”提上来，`中国→hao`
则不提）。所以 `ln2;context` 的 50.0% 是一个**有区分度的读数**：上下文能影响
“词库搭配 / 人工关联表”这一路，却完全不影响**学习到的 profile 偏好**——
这正是本节要定位的缺口边界。

### 4.2 `ln3;decay` 的阈值

每行是“N 次默认读法，再 M 次对手读法”。对每个 N 发出（M=N 时翻转，M=N−1 时保持）
两行，于是阈值被**两侧夹住**，而不是只报一个下界。

| N | M=N−1（保持） | M=N（翻转） | 加 `idle:4096` 后 |
| ---: | --- | --- | --- |
| 2 | 默认读法 ✓ | 对手读法 ✓ | 与左侧完全相同 |
| 3 | 默认读法 ✓ | 对手读法 ✓ | 与左侧完全相同 |
| 5 | 默认读法 ✓ | 对手读法 ✓ | 与左侧完全相同 |

即**翻转阈值为 M = N**：谁近期次数多谁在前，与参照里“seed 翻倍”的直觉一致，
但澄音不需要翻倍——是对手追平就翻转。`idle:4096`（16 个衰减 epoch，即 `recent`
的 shift 上限）**不改变任何一行**，因为 `recent()` 对两侧同等衰减，比值不变；
衰减只在“新证据 vs 旧证据”之间起作用，这也是 §5 里持久化方案必须把它算进去的原因。

### 4.3 `ln1;word`

`record:` 一次对手读法即把它提到首位（12/12），复现并细化了既有 `learning` 类的
“见证提权”结论，同时把键形统一到与另两个场景可比。

### 4.4 既有 532 条逐类逐行不变

`learn` 类的 48 行是**追加**的：与改动前相比，既有 532 行（488 + 44 alias）
**逐字节且**行序**完全相同，两种模式下的逐类聚合也逐字段相同。

```sh
diff <(grep -v '^#' <改动前的 data/eval/quality.tsv>) \
     <(grep -v '^#' data/eval/quality.tsv | awk -F'\t' '$1!="learn"')
# 无输出
```

**顺带修掉了一个既有隐患**：`scripts/generate_eval_corpus.py` 原先用
`sorted(frozen)` 输出保留类别，而 I22 把 `alias` 追加在文件**末尾**；于是“只追加”
的契约在 **main 上就已经不成立**——在 `4b50c04` 上原样重跑生成器，会把这 44 行
从末尾搬到开头（44 insertions / 44 deletions）。本次改为按文件既有顺序输出，
保留类别不再重排，生成器现在是**幂等**的（连续两次运行 `diff` 为空）。

## 五、差距结论与建议（持久化二元上下文）

### 5.1 是否真实存在

**是**，且由 §4.1 的 50.0% 直接证明。三参照的做法与代价：

- librime：`commit_history` 只留 **20 条**、仅服务编码与标点，语法打分需要
  `Grammar` 组件而本仓库未注册；即参照自身也**没有**把这段历史用于个性化排序。
- libime：`HistoryBigram` 有**三级池 128/8192/65536 句**、bigram 权重 0.8，
  全序列落盘（含 magic `0x000fc315`、version 4）。
- libpinyin：`user_bigram[prev][cur]` + `train_result3` 选词作约束重算，seed 翻倍上限
  `23*15*64`。

### 5.2 是否值得做

**当前不值得**，三条理由：

1. **收益面窄**。二元上下文只在“同一拼音、两种读法、且用户按前词分流”时才改变结果。
   本评估的 `ln2;context` 12 对是**构造出来**的演示，不是从真实输入统计来的发生率；
   真实语料里这一维度的命中率**未核实**（需要用户输入历史，本项目禁止读取）。
2. **三个参照里最接近的 librime 自己也没做**。真正把它做进产品的是 libime/libpinyin，
   而它们的实现代价（三级池 / 全序列落盘）远大于澄音 profile 的规模。
3. **成本与风险都落在澄音最敏感的地方**（见 5.3）。

### 5.3 预估代价

| 项 | 评估 |
| --- | --- |
| 持久化格式 | 需新 magic（`MSWYUSR3`）或新表；现有 `MSWYUSR2` 是**扁平 `key→text`**，加 `prev` 维度要么改行宽、要么加独立表。必须保留 `MSWYUSR1/2` 读取路径（`profile.rs:296-303` 已有迁移先例） |
| 两端兼容 | Windows TSF 与 Linux Fcitx 5 共用同一 Rust 核心与同一 profile 文件，格式一变两端都必须能读旧、写新；跨版本回退（新写旧读）需要明确策略 |
| 体积 / 8192 上限 | 现在是 8192 **条**偏好、快照上限 4 MiB（`profile.rs:8`）。二元对的数量是“不同前词 × 不同拼音”的组合，量级远大于 8192；沿用同一上限会立刻挤掉现有的一元偏好，必须**独立配额**，否则会回退本评估 `ln1;word`/`ln3;decay` 已经 100% 的能力 |
| 隐私 | 二元上下文等价于**记录词与词的相邻关系**，比“单个拼音的偏好”更接近可还原的句子片段。本项目明令不依赖用户真实输入历史、不改动个人学习数据；存储相邻对需要重新做一次隐私口径评审 |
| 衰减 | §4.2 显示 `idle:4096` 对同键两侧无效；二元对必须有自己的 epoch 与淘汰规则，否则旧搭配永不失效 |

### 5.4 建议

1. **不实现**。把“持久化二元上下文”记为已知缺口，与既有 `docs/QUALITY_BASELINE.md`
   的缺口清单并列；`learn` 类的 48 行作为**回归基线**留下，任何将来的尝试都必须至少
   把 `ln2;context` 从 50.0% 提上去，同时不让 `ln1;word`/`ln3;decay` 从 100% 掉下来。
2. 若将来要做，**最小可行方案**是：独立表 + 独立配额 + `MSWYUSR3`（保留 1/2 读取），
   只在“同键 ≥2 读法且存在二元证据”时参与排序，并先补一份真实场景发生率的
   **未核实说明**（现在没有数据，不得声称收益）。
3. 优先级低于其它已知缺口：`docs/QUALITY_BASELINE.md` 记录的 `initials` 深层词、
   `typo` 的 omit/swap、`long_sentence` 可达性，都是**已量化**且不涉及新持久化格式的问题。

## 六、未核实项（明确列出）

- libime 三级池在真实使用中的容量压力与淘汰代价：**未核实**（只读源码，未构造负载）。
- libpinyin `train_result3` 在实际使用中的训练频率与磁盘写入量：**未核实**。
- librime 的 `Grammar` 插件在上游是否作为独立仓库发布、以及启用后的排序效果：**未核实**
  （本次浅克隆内 `plugins/` 只有 `plugin.cc`/`plugins_module.cc`，未注册 grammar）。
- 真实用户输入中“同一拼音因前词而分流”的发生率：**未核实**（不得读取用户真实输入历史）。
- 二元上下文对澄音现有 8192 条一元偏好的实际挤占程度：**未核实**（未实现）。
