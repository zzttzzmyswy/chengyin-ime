# 词库与许可

Windows 预载 87,540 条字词，不需要下载。`demo.tsv` 是本项目手写的
98 条 MIT 示例，仅用于回归；不代表正式预载词库。

| 来源 | 固定修订 | 许可 | 用法 |
| --- | --- | --- | --- |
| [Rime pinyin-simp](https://github.com/rime/rime-pinyin-simp) | `0c6861ef7420ee780270ca6d993d18d4101049d0` | Apache-2.0 | 65,125 条基础词条及音节表 |
| [jieba](https://github.com/fxsjy/jieba) | `67fa2e36e72f69d9134b8a1037b83fbb070b9775` | MIT | 补充 22,415 条常用词及频率 |
| Myswy 自写搭配 | 本仓库 `associations.tsv` | MIT | 离线搭配基线，不是训练语料或准确率评测 |

每个来源的原始文件、完整许可、修订和 SHA256 保存在 `sources/`。
Rime 原始词典说明派生于 Android 开源 PinyinIME，保留原始头部和署名。
代码的 MIT 许可不替代词库许可；Windows 安装包的 RUNTIME_LICENSES.zip
同时保留两项词库的 LICENSE、README 和 SOURCE.json。

`python3 scripts/import_rime.py` 校验原始文件哈希，离线重建 `daily.tsv`。
Rime 的音节空格改为撇号、零权重改为 1、重复项采用最大权重。jieba 只取
频率至少 30、2–6 字且每字在 Rime 中读音唯一的新增词；不猜多音字读音。
新增频率为 `max(1, jieba频率 // 50)`，原有词频保留。音节表 `syllables.txt`
来自同一固定 Rime 源。jieba 数据不是输入法专用语料，仍需独立质量评测。

```sh
python3 scripts/import_rime.py
cargo run --release -p myswy-cli -- --compile data/daily.tsv data/daily.mswydict
cargo run --release -p myswy-cli -- --import 我的词库.scel 我的词库.mswydict
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
