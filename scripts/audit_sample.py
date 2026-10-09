#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Random 200-entry audit sample of the words the I05 sources contributed.

The sample is drawn from the live build (`import_daily.build()`), so it cannot
drift from `data/daily.tsv`. The seed is fixed, so the exact sample printed in
the delivery record can be reproduced:

    python3 scripts/audit_sample.py            # print the sample
    python3 scripts/audit_sample.py --check    # print the sample plus auto flags

The task card defines "high quality" as readings that are not guessed, no
low-frequency noise or garbage, and a recorded manual review with an error rate
no worse than 2%. This script draws the sample and raises the mechanical flags;
the judgement column is filled in by hand.
"""

from pathlib import Path
import argparse
import hashlib
import importlib.util
import random
import re

ROOT = Path(__file__).resolve().parents[1]
SEED = 'i05-audit-2026-10-06'
COUNT = 200
HAN = re.compile(r'^[一-鿿]+$')
VALID = re.compile(r"[a-z]+(?:'[a-z]+)*")
# Characters that carry tone only in context; a word made only of these is noise.
PARTICLES = set('的了着过地得和与及把被为所以之乎者也')


def importer():
    spec = importlib.util.spec_from_file_location(
        'import_daily', ROOT / 'scripts' / 'import_daily.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()

    entries, added, _skipped, _syllables = importer().build()
    pool = sorted(pair for pairs in added.values() for pair in pairs)
    rng = random.Random(SEED)
    sample = sorted(rng.sample(pool, COUNT))

    flagged = 0
    for index, (key, text) in enumerate(sample, 1):
        flags = []
        if not HAN.match(text):
            flags.append('non-Han')
        if len(text) != len(key.split("'")):
            flags.append('length-mismatch')
        if not VALID.fullmatch(key):
            flags.append('bad-reading')
        if all(char in PARTICLES for char in text):
            flags.append('particles-only')
        if entries[(key, text)] != 1:
            flags.append('unexpected-weight')
        if (key, text) not in set(pool):
            flags.append('not-from-new-sources')
        flagged += bool(flags)
        print(f'{index:>3}\t{key}\t{text}\t{",".join(flags) or "ok"}')

    digest = hashlib.sha256('\n'.join(f'{k}\t{t}' for k, t in sample).encode()).hexdigest()
    print(f'\nsample={len(sample)} of {len(pool)} added  seed={SEED}')
    print(f'sha256={digest}')
    if args.check:
        print(f'auto-flagged={flagged} ({100.0 * flagged / COUNT:.1f}%)')


if __name__ == '__main__':
    main()
