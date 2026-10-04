#!/usr/bin/env python3
"""Offline, deterministic conversion of pinned Rime (Apache-2.0) and jieba (MIT)."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'data/sources/rime-pinyin-simp'
record = json.loads((SOURCE / 'SOURCE.json').read_text())
for name, expected in record['files'].items():
    raw = (SOURCE / name).read_bytes()
    if len(raw) != expected['bytes'] or hashlib.sha256(raw).hexdigest() != expected['sha256']:
        raise SystemExit(f'Source integrity failure: {name}')
entries = {}
source_syllables = set()
for line in (SOURCE / 'pinyin_simp.dict.yaml').read_text().split('\n...\n', 1)[1].splitlines():
    if not line.strip() or line.startswith('#'):
        continue
    text, syllables, weight = line.split('\t')
    key = syllables.replace(' ', "'")
    if not re.fullmatch(r"[a-z]+(?:'[a-z]+)*", key):
        raise SystemExit(f'Unsupported pronunciation: {line}')
    source_syllables.update(syllables.split())
    frequency = max(1, int(weight))
    entries[key, text] = max(entries.get((key, text), 0), frequency)
characters = {}
for key, text in entries:
    if len(text) == 1 and "'" not in key:
        characters.setdefault(text, set()).add(key)
modern = ROOT / 'data/sources/jieba'
manifest = json.loads((modern / 'SOURCE.json').read_text())
for name, expected in manifest['files'].items():
    raw = (modern / name).read_bytes()
    if len(raw) != expected['bytes'] or hashlib.sha256(raw).hexdigest() != expected['sha256']:
        raise SystemExit(f'Source integrity failure: jieba/{name}')
added = 0
for line in (modern / 'dict.txt').read_text().splitlines():
    text, frequency, _ = line.split(' ')
    # Conservative pronunciation: only uniquely pronounced characters already
    # present in the Apache source. Ambiguous/unknown characters are skipped.
    if not 2 <= len(text) <= 6 or int(frequency) < 30:
        continue
    if not all(len(characters.get(c, ())) == 1 for c in text):
        continue
    key = "'".join(next(iter(characters[c])) for c in text)
    if (key, text) not in entries:
        entries[key, text] = max(1, int(frequency) // 50)
        added += 1
output = '# Rime pinyin-simp (Apache-2.0) + jieba common words (MIT); see sources/*/SOURCE.json\n'
output += '# Converted: syllable spaces to apostrophes; zero weights to 1; duplicate pairs use maximum weight.\n'
output += ''.join(f'{key}\t{text}\t{frequency}\n' for (key, text), frequency in sorted(entries.items()))
(ROOT / 'data/daily.tsv').write_text(output, encoding='utf-8', newline='\n')
(ROOT / 'data/syllables.txt').write_text(' '.join(sorted(source_syllables))+'\n', encoding='utf-8', newline='\n')
print(f'{len(entries)} entries ({added} added common words); sha256={hashlib.sha256(output.encode()).hexdigest()}')
