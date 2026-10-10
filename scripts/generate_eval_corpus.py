#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic generator for the independent quality annotation set (review R11).

Provenance and licensing
------------------------
Every row is derived from `data/daily.tsv` (Rime pinyin-simp, Apache-2.0, plus
jieba, MIT; see `data/README.md`) or composed by the rules below from those
entries. No third-party corpus is added and no real user input history is read.
Spellings, texts and frequencies come from the pinned dictionary, so the whole
set is reproducible from `data/daily.tsv` plus this script. The generated file is
committed so reviewers and the evaluator share one frozen artifact.

Design rules
------------
* Dev and test are separated by the *typed spelling*: an input string is hashed
  once, globally, and always lands in the same split. A spelling therefore never
  appears in both splits, which the generator re-checks and fails on.
* Within a category the sample is an evenly spaced slice of a sorted pool rather
  than a random draw, so frequency and homophone coverage is stable and every
  difficulty band is represented.
* `expected` is the dictionary's own most frequent reading of the spelling, not a
  hand-picked rare homophone. Top-1 is then a real accuracy figure for "did the
  engine pick the reading this dictionary ranks first".
* `train` cells are `;`-separated steps the evaluator applies before replaying the
  row, so a row can state *when* something was learned:
  - `record:<spelling>=><text>`: a host-confirmed selection written straight into
    the profile. (Selection-through-the-page cannot produce unattested text, so the
    profile is built directly, the same way the platform adapter loads a saved one.)
  - `ctx:<spelling>=><text>`: the same selection replayed on the live session, so
    its committed word becomes the preceding-word context for what follows.
  - `idle:<n>`: `n` selections of an unrelated key, which advance the event clock
    the profile's decay is measured against without touching the row's own key.
* Inputs are unseparated ASCII pinyin (the dominant real typing style) except in
  the categories that explicitly require another form.
* The set is append-only. A category already in the committed file is kept from
  it verbatim and only a new category is generated, because the lexicon was
  doubled after the set was frozen (I05) and regenerating every category from
  today's `data/daily.tsv` would rewrite all 488 existing rows and silently
  retire the baseline they are quoted against. This script stays their sole
  author either way — no row is ever hand-edited.

Run:
    python3 scripts/generate_eval_corpus.py
"""

from __future__ import annotations

import hashlib
import random
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data" / "daily.tsv"
SYLLABLES = ROOT / "data" / "syllables.txt"
OUTPUT = ROOT / "data" / "eval" / "quality.tsv"

# Matching-option bits, mirrored from crates/ime-core/src/fuzzy.rs.
RULES = [
    ("zh", "z", True),
    ("ch", "c", True),
    ("sh", "s", True),
    ("n", "l", True),
    ("f", "h", True),
    ("l", "r", True),
    ("an", "ang", False),
    ("en", "eng", False),
    ("in", "ing", False),
    ("ian", "iang", False),
    ("uan", "uang", False),
]
SWAP = 1 << 16
OMIT = 1 << 17
NEIGHBOR = 1 << 18
REPEAT = 1 << 19
PHONETIC = (1 << 11) - 1
QWERTY = ["qwertyuiop", "asdfghjkl", "zxcvbnm"]
# The strict initials lane accepts only these bytes (crates/ime-core/src/dictionary.rs).
INITIAL_ALPHABET = set("abcdefghjklmnopqrstwxyz")
MAX_LETTERS = 24

# Target rows per category per split. Fuzzy and typo apply the value per rule, and
# learning per sub-scenario, so those categories are larger by construction.
PER_SPLIT = {
    "whole_word": 30,
    "single_char": 30,
    "long_sentence": 15,
    "homophone": 20,
    "fuzzy": 4,
    "typo": 10,
    "initials": 20,
    "prefix": 15,
    "learning": 10,
    "alias": 12,
}

rows: list[dict] = []
seen: set[tuple] = set()

# ---------------------------------------------------------------- split rules


def stable(text: str) -> bytes:
    return hashlib.sha256(text.encode("utf-8")).digest()


def split_of(input_: str) -> str:
    """Split by the typed spelling, so no input string can straddle dev/test."""
    return "dev" if stable("split:" + input_)[0] % 2 == 0 else "test"


def letters(key: str) -> str:
    return key.replace("'", "")


def spread(items: list, count: int) -> list:
    """Evenly spaced deterministic sample from a sorted pool."""
    if count <= 0:
        return []
    if len(items) <= count:
        return list(items)
    step = len(items) / count
    return [items[int(index * step)] for index in range(count)]


def freq_bucket(frequency: int) -> str:
    if frequency >= 10000:
        return "freq-high"
    if frequency >= 1000:
        return "freq-mid"
    return "freq-low"


def add(category: str, flags: int, input_: str, expected: str,
        train: str, bucket: str, note: str) -> None:
    for field, value in (("input", input_), ("expected", expected), ("train", train),
                         ("bucket", bucket), ("note", note)):
        assert "\t" not in value and "\n" not in value, (category, field, value)
    assert input_ and expected, (category, input_, expected)
    key = (category, input_, expected, train, flags)
    if key in seen:
        return
    seen.add(key)
    rows.append({
        "category": category,
        "split": split_of(input_),
        "flags": flags,
        "input": input_,
        "expected": expected,
        "train": train,
        "bucket": bucket,
        "note": note,
    })


def emit(category: str, specs: list[tuple], per_split: int) -> None:
    """Split the full pool by input, then take an even slice of each half."""
    pool = defaultdict(list)
    for spec in specs:
        pool[split_of(spec[1])].append(spec)
    for split, wanted in (("dev", per_split), ("test", per_split)):
        candidates = sorted(set(pool[split]), key=lambda spec: (spec[5], spec[1], spec[2]))
        chosen = spread(candidates, wanted)
        if len(chosen) < wanted:
            print(f"warning: {category}/{split} only {len(chosen)} of {wanted}",
                  file=sys.stderr)
        for spec in chosen:
            add(category, *spec)


# ---------------------------------------------------------------- corpus load

entries: list[tuple[str, str, int]] = []
for line in SOURCE.read_text(encoding="utf-8").splitlines():
    if not line or line.startswith("#"):
        continue
    spelling, text, frequency = line.split("\t")
    entries.append((spelling, text, int(frequency)))
entries.sort()

SYLLABLE_SET = set(SYLLABLES.read_text(encoding="utf-8").split())

by_key: dict[str, list[tuple[str, int]]] = defaultdict(list)
for spelling, text, frequency in entries:
    by_key[spelling].append((text, frequency))
for value in by_key.values():
    value.sort(key=lambda row: (-row[1], row[0]))

known = {(spelling, text) for spelling, text, _ in entries}
top_of: dict[str, tuple[str, int]] = {key: by_key[key][0] for key in by_key}
# Every text reachable from a given typed letter string, so unattested-pair checks
# are one set lookup instead of a scan over the whole lexicon.
texts_by_letters: dict[str, set[str]] = defaultdict(set)
for spelling, text, _ in entries:
    texts_by_letters[letters(spelling)].add(text)
char_readings: dict[str, set[str]] = defaultdict(set)
for spelling, text, _ in entries:
    if len(text) == 1 and "'" not in spelling:
        char_readings[text].add(spelling)


def syllables(spelling: str) -> list[str]:
    return spelling.split("'")


def top_text(spelling: str) -> str:
    return by_key[spelling][0][0]


# ------------------------------------------------------- whole word / single

def whole_word_specs() -> list[tuple]:
    specs = []
    for spelling in sorted(by_key):
        if len(syllables(spelling)) not in (2, 3) or len(letters(spelling)) > MAX_LETTERS:
            continue
        text, frequency = top_of[spelling]
        note = "sep" if "'" in spelling else "joined"
        specs.append((0, letters(spelling), text, "", f"ww;{freq_bucket(frequency)}", note))
    return specs


def single_char_specs() -> list[tuple]:
    """Single complete syllables, banded by how many homophones compete.

    `expected` is the dictionary's most frequent character for that reading, so
    Top-1 here answers "does a complete syllable rank the dictionary's own first
    choice first under homophone pressure". Reachability of the alternatives is
    already covered by the Top-N and full-page columns in every category.
    """
    specs = []
    for spelling in sorted(by_key):
        if len(syllables(spelling)) != 1:
            continue
        homophones = len(by_key[spelling])
        if homophones <= 5:
            band = "hom-1to5"
        elif homophones <= 20:
            band = "hom-6to20"
        else:
            band = "hom-21plus"
        text, _ = top_of[spelling]
        specs.append((0, spelling, text, "", f"sc;{band}", f"hom-{homophones}"))
    return specs


# ------------------------------------------------------------------- phrases

def parses_unique(spelling: str, parts: list[str]) -> bool:
    """True when `spelling` parses into exactly `parts` under the syllable set."""
    reach: dict[int, list[list[str]]] = {0: [[]]}
    for at in range(len(spelling)):
        for piece in reach.get(at, []):
            for end in range(at + 1, min(at + 6, len(spelling)) + 1):
                if spelling[at:end] in SYLLABLE_SET:
                    reach.setdefault(end, []).append(piece + [spelling[at:end]])
    return reach.get(len(spelling), []) == [parts]


def long_sentence_specs() -> list[tuple]:
    specs = []
    # (a) prepared whole-sentence entries of four or more characters.
    for spelling in sorted(by_key):
        if len(letters(spelling)) > 48:
            continue
        text, frequency = top_of[spelling]
        if len(text) < 4:
            continue
        note = "entry-sep" if "'" in spelling else "entry-joined"
        specs.append((0, letters(spelling), text, "", f"ls;chars-{len(text)}", note))
    # (b) sentences composed from two or three dictionary words. Only compositions
    # whose concatenated spelling parses back to exactly the component syllables
    # are kept, so the intended reading is provably unambiguous.
    words = sorted(
        ((spelling, top_text(spelling)) for spelling in by_key
         if len(syllables(spelling)) in (2, 3) and len(letters(spelling)) <= 24),
        key=lambda row: (-by_key[row[0]][0][1], row[0]),
    )[:80]
    for index in range(120):
        parts = [words[index % len(words)], words[(index * 7 + 3) % len(words)]]
        if index % 3 == 0:
            parts.append(words[(index * 13 + 5) % len(words)])
        spelling = "".join(letters(key) for key, _ in parts)
        text = "".join(text for _, text in parts)
        readings = [syl for key, _ in parts for syl in syllables(key)]
        if not 4 <= len(text) <= 8 or len(spelling) > 48 or (spelling, text) in known:
            continue
        if not parses_unique(spelling, readings):
            continue
        specs.append((0, spelling, text, "", f"ls;chars-{len(text)}", "composed"))
    return specs


# ---------------------------------------------------------------- homophones

def homophone_specs() -> list[tuple]:
    """Spellings whose second reading is a real competitor of the first."""
    specs = []
    for spelling in sorted(by_key):
        candidates = by_key[spelling]
        if len(candidates) < 2 or len(syllables(spelling)) > 3:
            continue
        if len(letters(spelling)) > MAX_LETTERS:
            continue
        top, second = candidates[0][1], candidates[1][1]
        if second * 4 < top:
            continue  # no real competition
        band = "gap-le2" if top <= second * 2 else "gap-le4"
        text, _ = candidates[0]
        specs.append((0, letters(spelling), text, "", f"ho;{band}",
                      f"rivals-{len(candidates)};second-{second}"))
    return specs


# --------------------------------------------------------------------- fuzzy

def fuzzy_groups() -> list[tuple[str, list[tuple]]]:
    """One pool per phonetic rule, so all eleven fuzzy pairs are represented.

    `input` is the string a user actually types under that confusion (for example
    `z` where the word needs `zh`); `expected` stays the intended dictionary word,
    and `flags` enables exactly the one rule bit under test.
    """
    groups = []
    for index, (canonical, typed, initial) in enumerate(RULES):
        collected = []
        for spelling in sorted(by_key):
            if len(syllables(spelling)) not in (2, 3) or "'" not in spelling:
                continue
            for part_index, part in enumerate(syllables(spelling)):
                if initial:
                    if part_index != 0 or not part.startswith(canonical):
                        continue
                    replaced = typed + part[len(canonical):]
                else:
                    if not part.endswith(canonical):
                        continue
                    replaced = part[: len(part) - len(canonical)] + typed
                parts = syllables(spelling)
                parts[part_index] = replaced
                prompt = "".join(parts)
                if prompt == letters(spelling) or not prompt.isalpha() or len(prompt) > 40:
                    continue
                text, frequency = top_of[spelling]
                collected.append((1 << index, prompt, text, "",
                                  f"fz;{canonical}->{typed}", freq_bucket(frequency)))
        groups.append((f"fuzzy/{canonical}->{typed}", collected))
    return groups


# ---------------------------------------------------------------------- typos

def neighbor(c: str) -> list[str]:
    for row, keys in enumerate(QWERTY):
        if c not in keys:
            continue
        at = keys.index(c)
        x = at * 4 + row
        out = []
        for other_row, other_keys in enumerate(QWERTY):
            for other_at, other in enumerate(other_keys):
                if other == c:
                    continue
                if abs(x - (other_at * 4 + other_row)) <= 4 and abs(row - other_row) <= 1:
                    out.append(other)
        return sorted(out)
    return []


def corrupt(kind: str, spelling: str, rng: random.Random) -> str | None:
    if len(spelling) < 3:
        return None
    for _ in range(32):
        at = rng.randrange(len(spelling))
        if kind == "swap":
            if at + 1 >= len(spelling) or spelling[at] == spelling[at + 1]:
                continue
            return spelling[:at] + spelling[at + 1] + spelling[at] + spelling[at + 2:]
        if kind == "omit":
            if at + 1 >= len(spelling):
                continue
            return spelling[:at] + spelling[at + 1:]
        if kind == "repeat":
            return spelling[: at + 1] + spelling[at] + spelling[at + 1:]
        if kind == "neighbor":
            options = neighbor(spelling[at])
            if not options:
                continue
            return spelling[:at] + options[rng.randrange(len(options))] + spelling[at + 1:]
    return None


TYPO_KINDS = [("swap", SWAP), ("omit", OMIT), ("neighbor", NEIGHBOR), ("repeat", REPEAT)]


def typo_groups() -> list[tuple[str, list[tuple]]]:
    """One pool per keyboard-error class; `input` is the mistyped string."""
    groups = []
    for kind, flag in TYPO_KINDS:
        collected = []
        for spelling in sorted(by_key):
            if len(syllables(spelling)) not in (1, 2, 3):
                continue
            if len(letters(spelling)) > MAX_LETTERS:
                continue
            rng = random.Random(f"typo:{kind}:{spelling}")
            typed = corrupt(kind, letters(spelling), rng)
            if typed is None or typed == letters(spelling):
                continue
            text, frequency = top_of[spelling]
            collected.append((flag, typed, text, "",
                              f"ty;{kind}", freq_bucket(frequency)))
        groups.append((f"typo/{kind}", collected))
    return groups


# ------------------------------------------------------------------- initials

def initials_specs() -> list[tuple]:
    """One letter per character, two to four characters, in two probe kinds.

    An initials key is shared by every spelling whose syllable heads match, so the
    fair accuracy target is the *highest-frequency word that key resolves to*, not
    the top word of one arbitrarily chosen spelling. Expecting one spelling's word
    would cap Top-1 at the probability that spelling happens to be the common one.

    * `in;top-*`   expected is that top word: this is the fair Top-1 figure.
    * `in;deep-*`  expected is the *rarest* word of the same key. Top-1 there is
      low by construction and is NOT an accuracy claim; the meaningful columns are
      `full` (is the deep word reachable at all) and `keys` (how many page turns it
      costs). This is the axis the category exists to measure.
    """
    groups: dict[str, list[tuple[str, int]]] = defaultdict(list)
    for spelling in sorted(by_key):
        parts = syllables(spelling)
        if not 2 <= len(parts) <= 4 or "'" not in spelling:
            continue
        heads = [part[0] for part in parts]
        if any(head not in INITIAL_ALPHABET for head in heads):
            continue
        for text, frequency in by_key[spelling]:
            if len(text) != len(parts):
                continue
            groups["".join(heads)].append((text, frequency))
    specs = []
    for key, words in sorted(groups.items()):
        words.sort(key=lambda row: (-row[1], row[0]))
        band = f"in;chars-{len(key)};group-{len(words)}"
        top_text, top_frequency = words[0]
        specs.append((0, key, top_text, "", band, freq_bucket(top_frequency)))
        if len(words) >= 4:
            deep_text, deep_frequency = words[-1]
            specs.append((0, key, deep_text, "", f"in;deep;chars-{len(key)}",
                          freq_bucket(deep_frequency)))
    return specs


# --------------------------------------------------------------------- prefix

def prefix_specs() -> list[tuple]:
    """Type the head syllables of a longer word and expect the complete head word.

    This is the staged-selection axis: the same spelling also starts longer
    dictionary words, so the row asks whether an exact complete word beats the
    longer prefix completions when only the head has been typed.
    """
    specs = []
    for spelling in sorted(by_key):
        parts = syllables(spelling)
        if len(parts) not in (3, 4) or "'" not in spelling:
            continue
        head, tail = parts[:-1], parts[-1]
        head_key = "'".join(head)
        if head_key not in by_key:
            continue
        head_text = top_text(head_key)
        if len(head_text) != len(head):
            continue
        prompt = "".join(head)
        if not 2 <= len(prompt) <= 40:
            continue
        specs.append((0, prompt, head_text, "", f"pf;head-{len(head)}",
                      "longer-word-prefix"))
    return specs


# ------------------------------------------------------------------- learning

def unattested_pair(spelling: str) -> str | None:
    """A two-character string whose reading matches but which the lexicon lacks.

    `spelling` is a canonical two-syllable key. The candidate characters are the
    lexicon's own single-syllable readings, and the pair is rejected when any
    lexicon entry with the same typed letters already spells it.
    """
    parts = syllables(spelling)
    first = sorted(text for text, readings in char_readings.items() if parts[0] in readings)
    second = sorted(text for text, readings in char_readings.items() if parts[1] in readings)
    existing = texts_by_letters[letters(spelling)]
    for a in first[:16]:
        for b in second[:16]:
            if a + b not in existing:
                return a + b
    return None


def learning_groups() -> list[tuple[str, list[tuple]]]:
    """Three long-term-learning scenarios, one quota each.

    `train` holds `record:<spelling>=><text>` steps. Each step is one host-confirmed
    selection already recorded in the profile, which is the state the row then
    measures; the evaluator applies them to the profile directly because unattested
    text is by definition not reachable from the candidate page. This mirrors
    `Profile::record`, the same call the platform adapter makes on a confirmed write.

    (a) promote_attested   one confirmed selection promotes an attested rival;
    (b) promote_unattested three recent selections promote unattested text;
    (c) reject_one_off     a single such selection must not hijack the ranking.
    """
    groups: list[tuple[str, list[tuple]]] = []
    attested = []
    for spelling in sorted(by_key):
        if len(syllables(spelling)) not in (2, 3) or len(by_key[spelling]) < 2:
            continue
        key = letters(spelling)
        text, frequency = by_key[spelling][1]
        attested.append((0, key, text, f"record:{key}=>{text}", "ln;promote_attested",
                         freq_bucket(frequency)))
    groups.append(("learning/attested", attested))
    pairs = []
    for spelling in sorted(by_key):
        # Two-syllable canonical keys: apostrophes are the normal form here, so
        # requiring them keeps the reading explicit. Overloaded readings are skipped
        # because the unattested pair would not survive the reliability gate.
        if len(syllables(spelling)) != 2 or "'" not in spelling:
            continue
        if len(letters(spelling)) > 20 or len(by_key[spelling]) > 40:
            continue
        pair = unattested_pair(spelling)
        if pair is not None:
            pairs.append((letters(spelling), pair, top_text(spelling)))
    promote = [(0, key, pair, ";".join(f"record:{key}=>{pair}" for _ in range(3)),
                "ln;promote_unattested", "unattested") for key, pair, _ in pairs]
    groups.append(("learning/promote", promote))
    one_off = [(0, key, top, f"record:{key}=>{pair}", "ln;reject_one_off", "unattested")
               for key, pair, top in pairs]
    groups.append(("learning/one_off", one_off))
    return groups


# --------------------------------------------------------------------- alias

def alias_groups() -> list[tuple[str, list[tuple]]]:
    """The `ü` variant spellings other IMEs accept, against their canonical form.

    Two rules, mirrored from `crates/ime-core/src/syllables.rs` (I22):
    1. `v` directly after `j q x y` is `u`, so a user may type `jv`/`qve`/`xvan`
       where canonical pinyin writes `ju`/`que`/`xuan`;
    2. `v` directly after `l`/`n` and immediately before a syllable-final `e` is
       `u`, so `lve`/`nve` reach `lue`/`nue` (lüe/nüe).

    Each row takes a real dictionary spelling and rewrites one syllable into its
    variant, so the typed string is one a user actually produces; `expected` is
    that spelling's own top word, exactly as in the neighbouring categories. The
    row then asks whether the variant finds what the canonical spelling finds.

    The whole point of the category is the *negative* baseline: before I22 these
    inputs matched nothing at all. `canonical` records the spelling the row
    derives from, so the two can be compared row by row.
    """
    # (rule label, canonical prefix, variant prefix). `yu` is included for `yv`
    # even though `yu`+`e` is written `yue`, since the rule is purely the letter
    # after the `v`.
    rule_one = [
        ("jv", "ju", "jv"), ("jv", "jue", "jve"), ("jv", "juan", "jvan"),
        ("jv", "jun", "jvn"),
        ("qv", "qu", "qv"), ("qv", "que", "qve"), ("qv", "quan", "qvan"),
        ("qv", "qun", "qvn"),
        ("xv", "xu", "xv"), ("xv", "xue", "xve"), ("xv", "xuan", "xvan"),
        ("xv", "xun", "xvn"),
        ("yv", "yu", "yv"), ("yv", "yue", "yve"), ("yv", "yuan", "yvan"),
        ("yv", "yun", "yvn"),
    ]
    groups: list[tuple[str, list[tuple]]] = []
    collected: list[tuple] = []
    for label, canonical_head, variant_head in rule_one:
        for spelling in sorted(by_key):
            parts = syllables(spelling)
            head = parts[0]
            if head != canonical_head or len(letters(spelling)) > MAX_LETTERS:
                continue
            # Two or three syllables keeps the row comparable with `whole_word`;
            # a single rewritten syllable alone would be a `single_char` row.
            if len(parts) not in (2, 3):
                continue
            typed = variant_head + "".join(parts[1:])
            text, frequency = top_of[spelling]
            collected.append((0, typed, text, "", f"al;{label}", freq_bucket(frequency)))
    groups.append(("alias/rule1", collected))

    rule_two: list[tuple] = []
    for canonical_head, variant_head in (("lue", "lve"), ("nue", "nve")):
        for spelling in sorted(by_key):
            parts = syllables(spelling)
            if parts[0] != canonical_head or len(letters(spelling)) > MAX_LETTERS:
                continue
            if len(parts) not in (2, 3):
                continue
            typed = variant_head + "".join(parts[1:])
            text, frequency = top_of[spelling]
            rule_two.append((0, typed, text, "", f"al;{canonical_head}",
                             freq_bucket(frequency)))
    groups.append(("alias/rule2", rule_two))
    # The two rules are emitted separately so the category reports each one's
    # coverage; `emit` re-splits by input as every other category does.
    return groups


# --------------------------------------------------------------------- learn

# The idle gap models unrelated selections between two runs of training. 4096 is 16
# decay epochs at the profile's one-epoch-per-256-events rate, which is the largest
# shift `Preference::recent` applies, so it is as long a gap as the mechanism can
# tell apart from any longer one.
IDLE_EVENTS = 4096
# The learned-history lane runs only with matching options off on a multi-syllable
# input (`session.rs`, the `matching_options == 0 && !single_syllable` guard), so
# every `learn` row is flags-off.
LEARN_FLAGS = 0

# Rows per split for each scenario. `learn` is generated by scenario rather than by
# row, and the context and decay scenarios emit whole pairs, so these are pair counts
# there (two rows each).
LEARN_WORD_PER_SPLIT = 6
LEARN_CONTEXT_PER_SPLIT = 6
LEARN_DECAY_PER_SPLIT = 3
# The trained run lengths the decay scenario pins its threshold between.
LEARN_NS = (2, 3, 5)


def learn_keys() -> list[str]:
    """Two-syllable canonical keys carrying at least two attested readings.

    That is the shape the learned-history lane needs: it injects profile rows only
    for a multi-syllable exact query with matching off, and a key with one reading
    has nothing to prefer. Ordered by the first reading's frequency so the words are
    ordinary ones, which keeps the set deterministic and reviewable.
    """
    keys = []
    for spelling in sorted(by_key):
        if len(syllables(spelling)) != 2 or "'" not in spelling:
            continue
        if len(letters(spelling)) > MAX_LETTERS or len(by_key[spelling]) < 2:
            continue
        keys.append(spelling)
    keys.sort(key=lambda key: (-by_key[key][0][1], key))
    return keys


def learn_readings(spelling: str) -> tuple[str, str]:
    """The key's default reading and its strongest rival, both lexicon-attested."""
    return by_key[spelling][0][0], by_key[spelling][1][0]


# `Dictionary::context_bonus` probes every suffix of the context joined with a
# candidate's own text, and `language::bonus` probes those same suffixes against the
# authored association table. A context word either probe matches would lift a
# candidate for reasons that have nothing to do with learning, so the context
# scenario uses only words for which neither can fire.
ASSOCIATIONS = set()
for _line in (ROOT / "data" / "associations.tsv").read_text(encoding="utf-8").splitlines():
    if _line and not _line.startswith("#"):
        _context, _next, _weight = _line.split("\t")
        ASSOCIATIONS.add((_context, _next))


def context_is_neutral(word: str, targets: list[str]) -> bool:
    scalars = list(word)
    for start in range(len(scalars)):
        suffix = "".join(scalars[start:])
        if len(suffix) > 4:
            continue
        for target in targets:
            if (suffix + target) in known or (suffix, target) in ASSOCIATIONS:
                return False
    return True


def context_pool(limit: int = 600) -> list[tuple[str, str]]:
    """Frequent two-character words as `(spelling, text)`, most frequent first.

    A context step types the word's own spelling and picks that spelling's own top
    reading, so the word has to be one the dictionary ranks first for its key.
    """
    words = []
    for spelling in sorted(by_key):
        if len(syllables(spelling)) != 2 or "'" not in spelling:
            continue
        if len(letters(spelling)) > MAX_LETTERS:
            continue
        text, frequency = by_key[spelling][0]
        if len(text) != 2:
            continue
        words.append((frequency, spelling, text))
    words.sort(key=lambda row: (-row[0], row[1]))
    return [(spelling, text) for _, spelling, text in words[:limit]]


def learn_word_specs() -> list[tuple]:
    """(a) Word level: one confirmed selection of the rival reading promotes it.

    The mechanism the `learning` category already covers, restated on the same key
    shape as the other two scenarios so the three are comparable. Only `record:`
    steps are used, so no competing reading is observed as a miss and nothing but
    the recorded preference can hold the rival back.
    """
    specs = []
    for spelling in learn_keys():
        key = letters(spelling)
        _, rival = learn_readings(spelling)
        specs.append((LEARN_FLAGS, key, rival, f"record:{key}=>{rival}",
                      "ln1;word", "one-selection"))
    return specs


def learn_context_pairs(pool: list[tuple[str, str]]) -> list[tuple]:
    """(b) Context: one key preferred differently behind two different preceding words.

    Each pair trains one key 3 times on its default reading and 3 times on its rival,
    with a context step either side of the two runs, and the two rows then differ
    **only** in the trailing context step: row A expects the default after context A,
    row C expects the rival after context C. The trained half is byte-identical, so an
    engine that does not condition on the preceding word answers both rows from one
    state and returns one word for the two — it can match at most one of them
    whichever it picks. The pair is therefore a proof of the gap rather than a sample
    of it, and its two rows must stay together or the proof is lost.

    Both expected words are lexicon-attested, so the difference the pair asks for is
    one a ranking rule could in principle produce, and the miss counter is not
    involved because `record:` steps do not observe the skipped reading. The context
    words are chosen so neither `context_bonus` nor the association table can fire for
    the two candidates, which would lift one of them for unrelated reasons.
    """
    pairs = []
    used: set[str] = set()
    for spelling in learn_keys():
        key = letters(spelling)
        top, rival = learn_readings(spelling)
        picked = []
        for other, text in pool:
            if other == spelling or text in used or text in (top, rival):
                continue
            if not context_is_neutral(text, [top, rival]):
                continue
            picked.append((other, text))
            if len(picked) == 2:
                break
        if len(picked) < 2:
            continue
        (a_key, a_text), (c_key, c_text) = picked
        used.update([a_text, c_text])
        trained = [f"ctx:{a_key}=>{a_text}"]
        trained += [f"record:{key}=>{top}"] * 3
        trained += [f"record:{key}=>{rival}"] * 3
        pairs.append((
            (LEARN_FLAGS, key, top, ";".join(trained + [f"ctx:{a_key}=>{a_text}"]),
             "ln2;context", f"after-{a_text}"),
            (LEARN_FLAGS, key, rival, ";".join(trained + [f"ctx:{c_key}=>{c_text}"]),
             "ln2;context", f"after-{c_text}"),
        ))
    return pairs


def learn_decay_pairs() -> list[tuple]:
    """(c) Decay: how many opposite selections it takes to overturn a preference.

    `record:` steps are used rather than real selections, so the scenario is exactly
    "N confirmations of the default reading, then M of its rival" with no competing
    reading observed as a miss. The resulting threshold is then a statement about the
    profile's ordering rule alone, not about the miss counter.

    Each (N, gap) contributes the pair (flipped at M = N, held at M = N - 1). The two
    rows differ in one selection, so together they pin the threshold between N-1 and N
    instead of merely reporting that it lies somewhere beyond a guess; and the same
    pair is emitted again behind an idle gap, which is the half of the scenario that
    asks whether the event clock moves the threshold.
    """
    pairs = []
    for spelling in learn_keys():
        key = letters(spelling)
        top, rival = learn_readings(spelling)
        for n in LEARN_NS:
            trained = [f"record:{key}=>{top}"] * n
            opposite = [f"record:{key}=>{rival}"]
            for gap, note in (([], "fresh"),
                              ([f"idle:{IDLE_EVENTS}"], "after-idle")):
                pairs.append((
                    (LEARN_FLAGS, key, rival, ";".join(trained + gap + opposite * n),
                     "ln3;decay", f"n={n}-flip-{note}"),
                    (LEARN_FLAGS, key, top,
                     ";".join(trained + gap + opposite * (n - 1)),
                     "ln3;decay", f"n={n}-hold-{note}"),
                ))
    return pairs


def emit_pairs(name: str, pairs: list[tuple], per_split: int) -> None:
    """`emit` for a scenario whose unit is a pair rather than a row.

    Slicing rows would drop one half of a pair and quietly turn a provable gap into a
    plain accuracy figure, so the even slice is taken over whole pairs.
    """
    pool = defaultdict(list)
    for pair in pairs:
        pool[split_of(pair[0][1])].append(pair)
    for split, wanted in (("dev", per_split), ("test", per_split)):
        candidates = sorted(set(pool[split]), key=lambda pair: (pair[0][1], pair[0][5]))
        if len(candidates) < wanted:
            print(f"warning: {name}/{split} only {len(candidates)} of {wanted}",
                  file=sys.stderr)
        for pair in spread(candidates, wanted):
            for spec in pair:
                add(name, *spec)


# ----------------------------------------------------------------- write out

emit("whole_word", whole_word_specs(), PER_SPLIT["whole_word"])
emit("single_char", single_char_specs(), PER_SPLIT["single_char"])
emit("long_sentence", long_sentence_specs(), PER_SPLIT["long_sentence"])
emit("homophone", homophone_specs(), PER_SPLIT["homophone"])
for name, collected in fuzzy_groups():
    emit(name, collected, PER_SPLIT["fuzzy"])
for name, collected in typo_groups():
    emit(name, collected, PER_SPLIT["typo"])
emit("initials", initials_specs(), PER_SPLIT["initials"])
emit("prefix", prefix_specs(), PER_SPLIT["prefix"])
for name, collected in learning_groups():
    emit(name, collected, PER_SPLIT["learning"])
for name, collected in alias_groups():
    emit(name, collected, PER_SPLIT["alias"])
# `learn` is generated scenario by scenario: the word scenario is row-shaped, the
# context and decay scenarios are pair-shaped and go through `emit_pairs`.
emit("learn", learn_word_specs(), LEARN_WORD_PER_SPLIT)
emit_pairs("learn", learn_context_pairs(context_pool()), LEARN_CONTEXT_PER_SPLIT)
emit_pairs("learn", learn_decay_pairs(), LEARN_DECAY_PER_SPLIT)

# `fuzzy/<rule>` and `typo/<kind>` groups report as one category each.
for row in rows:
    row["category"] = row["category"].split("/", 1)[0]

# Append-only merge with the committed artifact. The lexicon was doubled after
# this set was frozen (I05), so regenerating every category from today's
# `data/daily.tsv` would rewrite all 488 existing rows and silently retire the
# baseline they are quoted against. A category already present in the committed
# file is therefore kept verbatim from it, and only a category that is new is
# generated. That keeps this script the sole author of the file — nothing here is
# hand-edited — while new categories such as `alias` (I22) are added by rule.
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
frozen: dict[str, list[str]] = defaultdict(list)
if OUTPUT.exists():
    for line in OUTPUT.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        frozen[line.split("\t", 1)[0]].append(line)

rows = [row for row in rows if row["category"] not in frozen]
# Frozen categories keep the order they already have in the file, and a newly
# generated one is appended after them. Sorting them here instead would move
# `alias` — which I22 appended last — back to the front, rewriting 44 committed
# rows that the file's own "append-only" contract promises are untouched.
body = [line for category in frozen for line in frozen[category]]

rows.sort(key=lambda row: (row["category"], row["split"], row["input"], row["expected"],
                           row["bucket"], row["note"], row["flags"]))
new_body = [
    "\t".join([row["category"], row["split"], str(row["flags"]), row["input"],
               row["expected"], row["train"], row["bucket"], row["note"]])
    for row in rows
]
# The dev/test split must stay disjoint across the whole file, frozen rows
# included — the check covers the merged result, not just this run's output.
split_of_input: dict[str, str] = {}
for line in body + new_body:
    fields = line.split("\t")
    spelling, split = fields[3], fields[1]
    previous = split_of_input.setdefault(spelling, split)
    if previous != split:
        raise SystemExit(f"dev/test input overlap on {spelling!r}")
body += new_body
header = [
    "# 澄音输入法独立质量标注集（审查 R11）。由 scripts/generate_eval_corpus.py 生成，请勿手工编辑。",
    "# 来源与许可：全部派生自 data/daily.tsv（Rime pinyin-simp Apache-2.0 + jieba MIT）与本仓库自写规则；",
    "# 不读取任何用户输入历史。说明与指标见 docs/QUALITY_BASELINE.md。",
    "# 列：category split flags input expected train bucket note",
    "# input：用户实际敲入的串（fuzzy/typo 类给出混淆后的串）；initials 为一字一首字母。",
    "# expected：期望的首选文字，取词库中该读音词频最高的读法（initials 取该键最高频词）。",
    "# train：空，或分号分隔的步骤，表示评测前施加的状态。三种步骤：",
    "#   `record:<拼音>=><文字>` 宿主已确认的选词，直接写入 profile；",
    "#   `ctx:<拼音>=><文字>` 在真实会话上打字、选中、确认写入，其提交词成为后续的前一词上下文；",
    "#   `idle:<n>` n 次无关选词，只推进 profile 的事件时钟以模拟时间间隔。",
    "# bucket：难度分档（类别前缀）。note：来源/规则标记。",
]
text = "\n".join(header + body) + "\n"
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(text, encoding="utf-8", newline="\n")

merged = defaultdict(lambda: defaultdict(int))
for line in body:
    fields = line.split("\t")
    merged[fields[0]][fields[1]] += 1
print(f"wrote {OUTPUT.relative_to(ROOT)}: {len(body)} rows "
      f"({len(new_body)} generated, {len(body) - len(new_body)} kept from the committed set)")
for category in sorted(merged):
    counts = merged[category]
    print(f"  {category:<15} dev={counts['dev']:<4} test={counts['test']:<4}")
print("split input overlap: 0")
print(f"sha256={hashlib.sha256(text.encode()).hexdigest()}")
