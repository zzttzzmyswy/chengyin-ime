# 开源拼音输入法匹配/排序方案调研（librime / libime / libpinyin）

调研日期 2026-10-09，随 MYS-2037（协议改 GPL-3.0-or-later）展开。目的：对照澄音共享核心，找出值得借鉴的拼音匹配、纠错、排序、学习与词库设计，并列出可验证的采纳顺序。

## 方法与证据口径

- 三个项目均浅克隆后读源码：librime `7bc3fb0`（BSD-3-Clause）、libime `171edcf` + fcitx5-chinese-addons（LGPL-2.1-or-later）、libpinyin `85905e5`（GPL-3.0-or-later）。
- 结论由辅助检索得到，关键条目已回源核对：librime `spelling.h:15-21`、`calculus.cc:14-16`、`corrector.cc:299-309`；libime `pinyindictionary.cpp:53,449,474`；libpinyin `pinyin_custom2.h:50-79`、`phonetic_lookup.h:39,72-76`。其余行号为检索结果，采纳前须逐条复核。
- **只读、未运行**：未对三者做延迟或质量基准，不比较“谁更准”。下文“更好/不如”仅指机制设计，不是效果结论。
- 澄音核心现状引用 `docs/ARCHITECTURE.md` 与 `crates/ime-core/src/{fuzzy,decoder,session}.rs`；质量基线为 `data/eval/quality.tsv`（488 条）。

## 一、三者机制对照

| 维度 | librime | libime | libpinyin | 澄音（现状） |
| --- | --- | --- | --- | --- |
| 切分 | 音节图 `SyllableGraph`，Darts 双数组 trie 存“拼写”，按位置做前缀搜索（`syllabifier.cc:22,87`） | `SegmentGraph` DAG，最小堆展开，歧义边比较“后段有效性/总长/完整性”（`pinyinencoder.cpp:196-358`），增量 merge 复用前缀（`segmentgraph.cpp:101`） | DP 先出单一最优切分，再由 85 条重切分 + 20 条内部拆分表补出多路（`phonetic_key_matrix.cpp:87,169`） | trie 沿边/省略分隔符匹配，每输入位置生成字词边，保留拼写切分歧义 |
| 拼写类型 | 阶梯 `Normal < Fuzzy < Abbreviation < Completion < Ambiguous`，全路径有更优类型时反向剪掉劣类型（`syllabifier.cc:179-198`） | 不完整音节枚举全部韵母并加模糊代价（`pinyindictionary.cpp:431`） | 单声母作 `PINYIN_INCOMPLETE` 表项；每词写“纯声母”和“全拼”两份索引（`chewing_large_table2.cpp:179-196`） | 首拼/混输：缩写状态只能续到下一音节；有完整全拼解析时先生成全拼 |
| 模糊音 | 建库期 `derive/fuzz` 规则，惩罚 log 0.5（`calculus.cc:14-16`）；无独立开关，靠 schema `__patch` | 22 个 `PinyinFuzzyFlag`，每命中记 `log10(0.5)`（`pinyinencoder.h:25-67`，`pinyindictionary.cpp:53`）；纠错因子 10、高级拼写因子 5 | 10 个 `PINYIN_AMB_*`（含 g/k），无惩罚、全部等权（`pinyin_custom2.h:50-60`） | 11 对双向（zh/z、ch/c、sh/s、n/l、f/h、l/r、an/ang、en/eng、in/ing、ian/iang、uan/uang），带代价对齐 |
| 键盘纠错 | 运行期只有 QWERTY 邻键替换，最多 4 条；编辑距离版被 `#if 0` 禁用（`corrector.cc:299-309`）；固定改写 `ao→oa` 等 | 只有同行相邻键替换，不含交换/漏键/重复（`pinyincorrectionprofile.cpp:23-46`） | 无；仅静态笔误表 `PINYIN_CORRECT_*`：gn/ng、mg/ng、iou/iu、uei/ui、uen/un、ue/ve、v/u、on/ong | 交换、漏键、邻键、重复四类，每音节最多一次、每词最多两次 |
| 排序/语言模型 | 词频 log 权重 + 固定未知词惩罚 −13.8；真正的语法模型在外部插件 octagram（未审） | KenLM 3 元 + 用户历史 bigram 混合（`max(lm, log10(0.8·10^lm+0.2·10^user))`），frameSize=40、beam=20、n-best | 单/双元插值 λ≈0.3127 × 多音读音占比 `pinyin_poss`；起点束 32；长句容忍 `log 1.2` | 词频代价 + 手写搭配奖励；每位置 16 路径 |
| 用户学习 | 使用计数 + 时间衰减 `formula_p`（`dynamics.h:6-15`） | `HistoryBigram`：三级 LRU 句池，几何衰减，unigram/bigram 混合（`historybigram.cpp`） | 用户 bigram，seed 翻倍有上限 22080，选词作 `CONSTRAINT_ONESTEP` 重算 | Profile：词库确认词立即学，未知组合选三次才优先，近期低命中取消优先 |
| 每键性能 | 触发整句的条件：无精确词且 ≥2 音节（`script_translator.cc:~498`） | 增量切分图/词格、两级 LRU（容量 80）、帧/束剪枝 | 起点束 32、词长上限 16、`SEARCH_CONTINUED` 前缀剪枝 | 零堆分配会话工作区；P99 目标未达（见 `STATUS.md` I15） |

## 二、澄音已具备且三者不具备（勿“倒退”）

- 四类键盘失误规则（交换/漏键/邻键/重复）比三者的运行期纠错都完整：librime 仅邻键，libime 仅邻键，libpinyin 仅静态笔误表。
- 模糊音带代价而非等权（libpinyin 完全没有代价）。
- ian/iang、uan/uang（libpinyin 没有）。
- 零按键堆分配、64 KiB 会话预算（三者均依赖大量动态结构和重依赖链）。

## 三、可借鉴项与采纳建议（均须以 `data/eval/quality.tsv` 零回退为门槛）

按“收益/成本/许可风险”排序。许可口径：读思路与自行实现不受限；复制代码/数据须保留原版权声明并与 GPL-3.0-or-later 兼容（libime 为 LGPL-2.1-or-later，可并入但需保留声明与许可文本）。

1. **数据驱动的笔误/变体别名表**（libpinyin `pinyin_custom2.h:71-79`、`special_table.h`）。澄音音节集已有 `lue/nue/lv/nv`（`data/syllables.txt`），但缺 `jv/lve/nve/jiou/guei/gun`、`on→ong` 这类别名与 libpinyin 的 85 条重切分、20 条内部拆分对照。成本小；风险低（表可自己生成，若直接取表须保留头部声明）。**建议先做**，并在 `quality.tsv` 新增 `alias` 类用例。
2. **逐键增量复用**（libime 切分图 merge + 词格节点复用 + 两级 LRU，`segmentgraph.cpp:101`、`decoder.cpp`）。直接对应 I15 遗留的 `Decoder::transition` miss 路径与逐 start DP 重复展开（`STATUS.md` I15 “后续方向”）。成本中等偏高；风险：与“零堆分配”预算冲突，需先设计固定容量缓存。**建议作为下一轮性能迭代**。
3. **整句触发条件与候选截断**（librime `script_translator.cc:~498`；libime `scoreFilter`/`wordCandidateLimit`）：无精确词且 ≥2 音节才做整句；相对最优分差超过阈值的候选不进入首页。成本小；需用质量集确认不降低 Top-9/可达率。
4. **拼写类型阶梯 + 反向剪枝**（librime `syllabifier.cc:179-198`）。澄音已有“全拼优先于缩写”的结构，是否存在“缩写/模糊候选压过正确全拼”需用质量集量化后再决定；先做诊断，不预设收益。
5. **多音字读音占比**（libpinyin `pinyin_poss`，`phrase_index.h:136-165`）。需要词条×读音频次字段与对应数据；`data/sources` 现有数据是否含读音频次未核对。成本中等，**依赖数据来源**。
6. **用户学习公式**（librime 时间衰减、libime 三级 LRU 句池、libpinyin seed 翻倍+上限）。与 Linux 持久学习（MYS-2037 的 I-B）同批评估：目标是比现有“选三次”更抗刷、有上限，且不引入隐私外泄。
7. **真 n-gram**（libime KenLM 3 元；libpinyin 插值）。收益可能最大但成本最高，且**语料/模型数据来源未核实**——见下节，不得直接引入现成模型数据；若做，须自建带来源清单的语料与计数管线，列入 ROADMAP M2 而非本批。

## 四、词库与数据许可（不得含糊）

- librime 本体 BSD-3-Clause；`rime-luna-pinyin` 词库为 LGPL-3.0，其上游词源（CC-CEDICT、Android PinyinIME、Chewing 等）的各自条款**未逐一核对**。`rime/rime-essay`（LGPL-3.0）此前因 MIT 不兼容被排除（`data/README.md`），协议改 GPL 后兼容性障碍已消除，是否重新评估由维护者决定；CC-CEDICT 为 CC BY-SA，与 GPL-3.0 的单向兼容关系需另行核实。
- libime：代码 LGPL-2.1-or-later；其 `dict-*.tar.zst`（约 2.3 MB 压缩）与 `lm_sc.arpa-*.tar.zst`（约 77.7 MB 压缩）在 REUSE 中标为 LGPL-2.1-or-later，但**上游词表与训练语料出处仓库内未说明**，KenLM 许可未核实。
- libpinyin：代码 GPL-3.0-or-later；模型数据 `model20.text.tar.gz` 不在仓库，**压缩包内无任何版权/语料说明**，仓库仅声明其中 opengram 条目来自 android-pinyin-ime（Apache-2.0）。**主语料授权未知，禁止并入其 `interpolation2.text`**。
- 结论：协议改为 GPL 只解决“代码可借鉴”；**词库/语言模型数据仍须逐源核对来源与授权后再入库**，并继续在 `data/sources/*` 保留固定修订、原始许可与哈希。

## 五、对 Linux 设置对齐（I-A/I-B/I-C）的影响

- 用户可见选项命名参考：libime/fcitx5-chinese-addons 的 `PageSize`（默认 7，范围 3–10）、`FuzzyConfig`（逐项开关）、`Learning`、`Prediction`、`WordCandidateLimit`、`LongWordLengthLimit`；librime 的 `page_size`、`enable_completion/correction/sentence/user_dict`。澄音 Windows 侧每页 5/7/9 与 11 项模糊音 + 4 项纠错，Linux 应使用同一套语义（I-A）。
- 候选截断、长词补全阈值（调研项 3）若落地，应作为**可选高级项**，默认值以质量集决定，不在 I-A 一并引入。
- 学习算法（调研项 6）与 I-B 同批设计，避免 Linux 与 Windows 学习行为不一致。

## 六、未核实项（不得当作结论）

octagram 打分公式；essay 词频库大小与许可；libime 解压后词典/LM 大小；KenLM 许可；`CommonTypo` 是否覆盖交换；chinese-addons 中 GPL-2.0-or-later 的具体文件；三者实际逐键延迟与候选质量。
