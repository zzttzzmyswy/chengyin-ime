#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline, deterministic rebuild of the shipped daily vocabulary.

Sources (each pinned and hash-verified against its own `SOURCE.json`):

* `rime-pinyin-simp`  Apache-2.0  base characters and words
* `jieba`             MIT         common words and frequency
* `thuocl`            MIT         domain words (IT, medical, law, places, ...)
* `phrase-pinyin-data` MIT        words with their own attested readings

Every rule below is measurement-driven; the numbers behind them are recorded in
`docs/QUALITY_BASELINE.md` and the I05 delivery note.

Weight policy
-------------
Imported words get weight 1. None of the new sources carries a frequency on the
same scale as the existing lexicon (THUOCL's DF counts documents in a different
corpus; the highest DF would map above the existing p95), so a merged weight
would let new words outrank established ones. A constant 1 keeps the existing
ranking intact.

Admission rules
---------------
A new word is admitted only when it cannot displace a pre-existing ranking:

1. `weight`  - the exact key and the initials key must either be absent from the
   baseline or have a baseline top weight of at least 2. The engine orders a key
   by (frequency, text), so a weight-1 newcomer can only displace another
   weight-1 entry (and then only by text order); a baseline top of 1 or more
   could be tied or beaten.
2. `shadow`  - the word's letter string must not be a *typed image* of an
   existing baseline key under an enabled phonetic rule. Typing such letters used
   to resolve to the baseline word through that rule at penalty 1; an exact
   entry would take the key at penalty 0 and, because the penalty is scaled far
   above any frequency difference, would win regardless of its weight. This is
   the rule that removes the one `fuzzy` regression (`cila`, expected `吃辣`:
   `phrase-pinyin-data` adds `刺啦` at `ci'la`).
3. `evidence` - the word must carry frequency evidence: a THUOCL DF of at least 1,
   or a jieba frequency of at least 1. The task card rejects low-frequency noise,
   and a first audit of 200 sampled additions found 11 (5.5%) with no evidence at
   all - every one of them from THUOCL_animal, whose DF column is 0 for 13,734 of
   its 17,287 rows. A word nobody's corpus has ever counted is not a word this
   lexicon should claim.

Anything skipped is reported by reason so the cost of each rule is visible.

Run: python3 scripts/import_daily.py
"""

from pathlib import Path
import hashlib
import json
import re
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / 'data/sources'
VALID = re.compile(r"[a-z]+(?:'[a-z]+)*")
HAN = re.compile(r'^[一-鿿]+$')
# Mirrors crates/ime-core/src/fuzzy.rs RULES.
RULES = [
    ('zh', 'z', True), ('ch', 'c', True), ('sh', 's', True),
    ('n', 'l', True), ('f', 'h', True), ('l', 'r', True),
    ('an', 'ang', False), ('en', 'eng', False), ('in', 'ing', False),
    ('ian', 'iang', False), ('uan', 'uang', False),
]


def verified(source):
    """Read a pinned source directory, failing on any hash mismatch."""
    directory = SOURCES / source
    record = json.loads((directory / 'SOURCE.json').read_text(encoding='utf-8'))
    for name, expected in record['files'].items():
        raw = (directory / name).read_bytes()
        if len(raw) != expected['bytes'] or hashlib.sha256(raw).hexdigest() != expected['sha256']:
            raise SystemExit(f'Source integrity failure: {source}/{name}')
    return directory


def strip_tone(text):
    return ''.join(c for c in unicodedata.normalize('NFD', text) if not unicodedata.combining(c))


def letters(key):
    return key.replace("'", '')


def typed_images(key):
    """Letter images of `key` under one enabled phonetic rule, either direction."""
    parts = key.split("'")
    out = set()
    for index, part in enumerate(parts):
        for canonical, typed, initial in RULES:
            for can, typ in ((canonical, typed), (typed, canonical)):
                if initial:
                    if index != 0 or not part.startswith(can):
                        continue
                    replaced = typ + part[len(can):]
                else:
                    if not part.endswith(can):
                        continue
                    replaced = part[: len(part) - len(can)] + typ
                if replaced != part:
                    out.add(letters("'".join(parts[:index] + [replaced] + parts[index + 1:])))
    return out


def baseline():
    """rime-pinyin-simp plus jieba: the vocabulary this change extends."""
    rime = verified('rime-pinyin-simp')
    entries = {}
    syllables = set()
    body = (rime / 'pinyin_simp.dict.yaml').read_text(encoding='utf-8')
    for line in body.split('\n...\n', 1)[1].splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        text, spelled, weight = line.split('\t')
        key = spelled.replace(' ', "'")
        if not VALID.fullmatch(key):
            raise SystemExit(f'Unsupported pronunciation: {line}')
        syllables.update(spelled.split())
        entries[key, text] = max(entries.get((key, text), 0), max(1, int(weight)))
    readings = {}
    for key, text in entries:
        if len(text) == 1 and "'" not in key:
            readings.setdefault(text, set()).add(key)

    modern = verified('jieba')
    for line in (modern / 'dict.txt').read_text(encoding='utf-8').splitlines():
        text, frequency, _ = line.split(' ')
        # Conservative pronunciation: only uniquely pronounced characters already
        # present in the Apache source. Ambiguous/unknown characters are skipped.
        if not 2 <= len(text) <= 6 or int(frequency) < 30:
            continue
        if not all(len(readings.get(c, ())) == 1 for c in text):
            continue
        key = "'".join(next(iter(readings[c])) for c in text)
        if (key, text) not in entries:
            entries[key, text] = max(1, int(frequency) // 50)
    return entries, readings, syllables


def jieba_frequencies():
    """The pinned jieba corpus frequency for every word it lists, as evidence."""
    modern = verified('jieba')
    table = {}
    for line in (modern / 'dict.txt').read_text(encoding='utf-8').splitlines():
        text, frequency, _ = line.split(' ')
        table[text] = int(frequency)
    return table


def load_thuocl(readings):
    """THUOCL has no readings; derive them from the base single-character table.

    The DF column is the document frequency, and is also the frequency evidence
    rule 3 checks: a DF of 0 means no document in THUOCL's corpus contained the
    word, so the lexicon has no reason to carry it.
    """
    directory = verified('thuocl')
    for path in sorted((directory / 'data').glob('*.txt')):
        # The place-name file uses CR-only line endings and a UTF-8 BOM, so it is
        # split with `splitlines` and the BOM stripped rather than by '\n'.
        body = path.read_text(encoding='utf-8').lstrip('﻿')
        for line in body.splitlines():
            if not line.strip():
                continue
            fields = line.split('\t')
            text = fields[0].strip()
            if not 2 <= len(text) <= 6 or not HAN.match(text):
                continue
            if not all(len(readings.get(c, ())) == 1 for c in text):
                continue
            key = "'".join(next(iter(readings[c])) for c in text)
            if VALID.fullmatch(key):
                evidence = int(fields[1]) if len(fields) > 1 and fields[1].strip().isdigit() else 0
                yield key, text, evidence


def load_phrase_pinyin(syllables):
    """Words shipped with their own attested reading (tones stripped)."""
    directory = verified('phrase-pinyin-data')
    for line in (directory / 'pinyin.txt').read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        line = line.split('#')[0].strip()
        if ': ' not in line:
            continue
        text, spelled = line.split(': ', 1)
        text = text.strip()
        if not 2 <= len(text) <= 6 or not HAN.match(text):
            continue
        parts = [strip_tone(part) for part in spelled.split()]
        if len(parts) != len(text) or not all(part in syllables for part in parts):
            continue
        key = "'".join(parts)
        if VALID.fullmatch(key):
            yield key, text


def build():
    """Return (entries, added, skipped, syllables) without writing anything."""
    entries, readings, syllables = baseline()
    baseline_keys = {key for key, _text in entries}
    baseline_weight = {}
    baseline_initials = {}
    for (key, _text), weight in entries.items():
        baseline_weight[key] = max(baseline_weight.get(key, 0), weight)
        initials = ''.join(part[0] for part in key.split("'"))
        baseline_initials[initials] = max(baseline_initials.get(initials, 0), weight)
    # Letter strings some baseline key already spells. A newcomer whose letters are
    # absent here had no exact parse before, so a phonetic rule was the only route.
    baseline_letters = {letters(key) for key in baseline_keys}
    shadowed = set()
    for key in baseline_keys:
        shadowed |= typed_images(key)
    jieba = jieba_frequencies()

    def protected(key, text, evidence):
        initials = ''.join(part[0] for part in key.split("'"))
        if evidence < 1 and jieba.get(text, 0) < 1:
            return 'evidence'
        if 0 < baseline_weight.get(key, 0) < 2:
            return 'weight'
        if 0 < baseline_initials.get(initials, 0) < 2:
            return 'weight'
        if letters(key) not in baseline_letters and letters(key) in shadowed:
            return 'shadow'
        return None

    added = {}
    skipped = {}
    for source, pairs in (
        ('thuocl', load_thuocl(readings)),
        ('phrase-pinyin', ((key, text, 0) for key, text in load_phrase_pinyin(syllables))),
    ):
        for key, text, evidence in pairs:
            if (key, text) in entries:
                continue
            reason = protected(key, text, evidence)
            if reason:
                skipped[reason] = skipped.get(reason, 0) + 1
                continue
            entries[key, text] = 1
            added.setdefault(source, set()).add((key, text))
    return entries, added, skipped, syllables


def main():
    entries, added, skipped, syllables = build()
    counts = {source: len(pairs) for source, pairs in added.items()}

    output = '# rime-pinyin-simp (Apache-2.0) + jieba (MIT) + THUOCL (MIT) + phrase-pinyin-data (MIT)\n'
    output += '# See data/README.md and data/sources/*/SOURCE.json. Built by scripts/import_daily.py.\n'
    output += '# Syllable spaces become apostrophes; zero weights become 1; duplicates keep the maximum weight.\n'
    output += ''.join(f'{key}\t{text}\t{weight}\n' for (key, text), weight in sorted(entries.items()))
    (ROOT / 'data/daily.tsv').write_text(output, encoding='utf-8', newline='\n')
    (ROOT / 'data/syllables.txt').write_text(
        ' '.join(sorted(syllables)) + '\n', encoding='utf-8', newline='\n')
    print(f'{len(entries)} entries; added {counts}; skipped {skipped}')
    print(f'sha256={hashlib.sha256(output.encode()).hexdigest()}')


if __name__ == '__main__':
    main()
