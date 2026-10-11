#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic generator for the I24 prefix/incremental measurement inputs.

Provenance and licensing
------------------------
Every row is built from spellings in `data/daily.tsv` (Rime pinyin-simp,
Apache-2.0, plus jieba MIT; see `data/README.md`). No third-party corpus and no
real user input history is read. The seed is fixed, so the file is reproducible
from the pinned dictionary plus this script; it is committed so the census, the
benchmarks and the property test all quote one frozen artifact.

What it is for
--------------
Two I24 questions need a corpus that is *not* the quality set, so a change
cannot be tuned against the set that judges it:

* A2 needs a census of how often an exact whole-input word exists and whether
  the sentence DP contributes anything to the first page.
* A3 needs every prefix of many real spellings to compare "carried forward"
  against "recomputed from zero".

Sampling
--------
One row is 1..4 dictionary spellings concatenated with no separator, which is
how the benchmark corpora are written (`nihao`, `jintiantianqihenhao`,
`woxihuanzhongwen`) and the dominant real typing style. Building rows out of
whole dictionary *words* rather than out of single syllables matters for the A2
census: a row is then usually not itself a dictionary word, so "an exact
whole-input word exists" stays a minority case instead of being trivially true
for every row. Separators are never emitted, so a boundary cannot hide an
incremental bug.

Run:
    python3 scripts/generate_prefix_corpus.py
"""

from __future__ import annotations

import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data" / "daily.tsv"
OUTPUT = ROOT / "data" / "eval" / "prefix.tsv"

# One fixed seed: the corpus is quoted by the A2 census, the A2/A3 benchmarks and
# the A3 property test, so regenerating it with another seed would silently move
# the evidence those three quote.
SEED = 20261011
COUNT = 2000
# Mirrors `MAX_INPUT_BYTES` in crates/ime-core/src/dictionary.rs.
MAX_INPUT_BYTES = 63
# 1..4 dictionary words per row: the composition lengths the benchmarks replay.
MIN_WORDS = 1
MAX_WORDS = 4


def load_spellings() -> list[str]:
    """Dictionary spellings as typed pinyin: apostrophes removed, lowercase."""
    seen: set[str] = set()
    with SOURCE.open(encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("#") or not line.strip():
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 2:
                continue
            spelling = fields[0].replace("'", "")
            if spelling and spelling.isascii() and spelling.isalpha():
                seen.add(spelling.lower())
    return sorted(seen)


def build(pool: list[str]) -> list[str]:
    random.seed(SEED)
    outputs: list[str] = []
    seen: set[str] = set()
    while len(outputs) < COUNT:
        row = ""
        for _ in range(random.randint(MIN_WORDS, MAX_WORDS)):
            candidate = row + random.choice(pool)
            if len(candidate) > MAX_INPUT_BYTES:
                break
            row = candidate
        if not row or row in seen:
            continue
        seen.add(row)
        outputs.append(row)
    return outputs


def main() -> int:
    pool = load_spellings()
    if not pool:
        print("no spellings in data/daily.tsv", file=sys.stderr)
        return 1
    outputs = build(pool)
    header = (
        "# 澄音输入法 I24 逐键增量/前缀测量输入集。由 scripts/generate_prefix_corpus.py 生成，请勿手工编辑。\n"
        "# 来源与许可：全部由 data/daily.tsv（Rime pinyin-simp Apache-2.0 + jieba MIT）的词条拼写拼接而成；\n"
        f"# 固定种子 {SEED}，{COUNT} 条，每行 1~{MAX_WORDS} 个词条拼写直接相连（无分隔符），≤{MAX_INPUT_BYTES} 字节。\n"
        "# 每行一个输入串；对每个输入取其全部前缀，用于「增量 = 从零」等价性与首页候选等价性检查。\n"
    )
    OUTPUT.write_text(header + "\n".join(outputs) + "\n", encoding="utf-8")
    lengths = [len(s) for s in outputs]
    print(
        f"wrote {OUTPUT.relative_to(ROOT)}: {len(outputs)} inputs, "
        f"length min={min(lengths)} max={max(lengths)} mean={sum(lengths) / len(lengths):.1f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
