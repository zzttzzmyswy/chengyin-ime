# 词库与许可

Windows 预载 184,173 条字词，不需要下载。`demo.tsv` 是本项目手写的
98 条 MIT 示例，仅用于回归；不代表正式预载词库。

| 来源 | 固定修订 | 许可 | 用法 |
| --- | --- | --- | --- |
| [Rime pinyin-simp](https://github.com/rime/rime-pinyin-simp) | `0c6861ef7420ee780270ca6d993d18d4101049d0` | Apache-2.0 | 65,125 条基础词条及音节表 |
| [jieba](https://github.com/fxsjy/jieba) | `67fa2e36e72f69d9134b8a1037b83fbb070b9775` | MIT | 补充 22,415 条常用词及频率 |
| [THUOCL](https://github.com/thunlp/THUOCL) | `a30ce79d895d01ab5132a5c74c29703ff7efb4cc` | MIT | 70,276 条领域词（IT、医学、法律、地名、诗词等） |
| [phrase-pinyin-data](https://github.com/mozillazg/phrase-pinyin-data) | `cee0ed6e6e4898580cafd2bd5e3723e20b214aa0` | MIT | 26,357 条自带读音的词 |
| Chengyin 自写搭配 | 本仓库 `associations.tsv` | MIT | 离线搭配基线，不是训练语料或准确率评测 |

每个来源的原始文件、完整许可、修订和 SHA256 保存在 `sources/`。
Rime 原始词典说明派生于 Android 开源 PinyinIME，保留原始头部和署名。
代码的 MIT 许可不替代词库许可；Windows 安装包的 RUNTIME_LICENSES.zip
同时保留四项词库的 LICENSE、README 和 SOURCE.json。

主动排除的两个候选来源（避免许可风险）：`rime/rime-essay` 为 LGPL-3.0，
与本仓库 MIT 分发不兼容；phrase-pinyin-data 的 `large_pinyin.txt` 混入
CC-CEDICT（CC BY-SA）与汉典数据，再分发许可不清，只取该仓库中 README
明示为 MIT 血统的 `pinyin.txt`。

`python3 scripts/import_daily.py` 校验四个来源的原始文件哈希，离线重建
`daily.tsv` 与 `syllables.txt`。Rime 的音节空格改为撇号、零权重改为 1、
重复项采用最大权重。jieba 只取频率至少 30、2–6 字且每字在 Rime 中读音
唯一的新增词；新增频率为 `max(1, jieba频率 // 50)`，原有词频保留。
THUOCL 无自带读音，按 Rime 单字表推导，多音字与不在音节表中的读音一律不收；
phrase-pinyin-data 用其自带读音（去声调）。新增词一律权重 1，不搬 THUOCL 的
DF（那是另一语料的文档频次，与本词库词频不同尺度）。

新增词还须通过三条规则（见脚本内注释与
[docs/QUALITY_BASELINE.md](../docs/QUALITY_BASELINE.md) 的 I05 小节）：

1. **有词频证据**：THUOCL 的 DF ≥ 1 或 jieba 词频 ≥ 1。THUOCL_animal 的
   17,287 行中有 13,734 行 DF 为 0，即该语料从未统计到这些词。
2. **不扰动既有排名**：目标键与首字母键在基线中的最高权重须为 0 或 ≥2。
3. **不制造精确键压过模糊命中**：该词的字母串不能是某个基线键在一条已启用
   模糊音规则下的“键入像”（否则会凭空造出一个 penalty 0 的精确键，压过原本
   靠该规则命中的词）。

三条规则共跳过 16,276 条候选。被跳过的词不进词库，但同键的既有词不受影响。

```sh
python3 scripts/import_daily.py
cargo run --release -p chengyin-cli -- --compile data/daily.tsv data/daily.mswydict
cargo run --release -p chengyin-cli -- --import 我的词库.scel 我的词库.mswydict
```

当前写出二进制 v2，兼容读取 v1。v2 沿用连续数组、UTF-8 池和 CRC32，
允许规范词条拼音最长 255 字节；实际一次输入仍最多 63 字节，因此较长
词条可用首拼输入。文字最多 256 UTF-8 字节，词库最多 250,000 条/64 MiB。

共享导入器支持本项目二进制、UTF-8/UTF-16LE TSV、搜狗文本导出
（例如 `'ni'hao 你好`）及经典搜狗 SCEL 0x44/0x45 格式。Windows 设置额外
转换 GBK 文本。重复拼音/文字项取最大词频；追加导入保留原词库。
格式边界与验证证据见 [搜狗兼容说明](../docs/SOGOU_COMPATIBILITY.md)。

词库包含低频古字及异体字；整句由词图组合，不要求每句话预先存在。
联想使用本地词库、常用搭配及当前会话偏好，不读取应用周边文本或联网。
