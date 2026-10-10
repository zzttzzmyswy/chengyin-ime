// SPDX-License-Identifier: GPL-3.0-or-later
// Compile-time token offsets into the sorted, audited syllable source.
// The display path probes up to six bytes with binary searches; no runtime cache.
const SOURCE: &str = include_str!("../../../data/syllables.txt");
const fn separator(byte: u8) -> bool {
    matches!(byte, b' ' | b'\n' | b'\r' | b'\t')
}
const fn count() -> usize {
    let bytes = SOURCE.as_bytes();
    let mut at = 0;
    let mut total = 0;
    while at < bytes.len() {
        if !separator(bytes[at]) && (at == 0 || separator(bytes[at - 1])) {
            total += 1;
        }
        at += 1;
    }
    total
}
const _: () = assert!(SOURCE.len() <= u16::MAX as usize);
const COUNT: usize = count();
const fn offsets() -> [(u16, u16); COUNT] {
    let bytes = SOURCE.as_bytes();
    let mut table = [(0, 0); COUNT];
    let mut at = 0;
    let mut row = 0;
    while at < bytes.len() {
        if separator(bytes[at]) {
            at += 1;
            continue;
        }
        let start = at;
        while at < bytes.len() && !separator(bytes[at]) {
            at += 1;
        }
        table[row] = (start as u16, at as u16);
        row += 1;
    }
    table
}
const OFFSETS: [(u16, u16); COUNT] = offsets();
pub(crate) fn contains(text: &str) -> bool {
    OFFSETS
        .binary_search_by(|&(start, end)| SOURCE[start as usize..end as usize].cmp(text))
        .is_ok()
}
/// The spelling the lexicon is queried with for `input`: the typed bytes with
/// the `ü` variant spellings every common IME also accepts rewritten to the
/// canonical spelling the syllable table and the dictionary are keyed by (I22).
///
/// Two rules, and only these:
/// 1. a `v` directly after `j q x y` is `u` — `jv` `qve` `xvan` `yvn` `jvn`.
///    Canonical pinyin writes the ü of these initials as `u`, so no canonical
///    reading starts `jv`/`qv`/`xv`/`yv` and the rewrite is unambiguous.
/// 2. a `v` directly after `l`/`n` immediately followed by `e` is `u` — `lve`,
///    `nve` (= lüe/nüe). Here `lv`/`nv` *is* a canonical syllable (lü/nü), so
///    the byte after the `v` decides: `nver` is 女儿 (nü + er) and keeps its `v`,
///    while `lve`/`nve` have no `lv`/`nv` reading to protect. An explicit
///    separator is never rewritten either (`lv'e` keeps its `v`, since the byte
///    after it is `'`).
///
/// Writes the result into `out[..input.len()]` and reports whether any byte
/// changed. `false` also covers an over-long input, which no query can use and
/// which the caller's own validation rejects. Every rewrite is one byte for one
/// byte, so byte offsets, the caret and the preedit positions stay valid in both
/// spellings; callers keep showing and remembering what the user actually typed.
pub(crate) fn canonicalize(input: &str, out: &mut [u8; crate::MAX_INPUT_BYTES]) -> bool {
    let bytes = input.as_bytes();
    // The single fast path for the overwhelming majority of keystrokes: no `v`
    // anywhere means the caller's own slice is already the query spelling, so
    // nothing is copied and nothing below runs.
    if bytes.len() > out.len() || !bytes.contains(&b'v') {
        return false;
    }
    out[..bytes.len()].copy_from_slice(bytes);
    let mut rewritten = false;
    for at in 0..bytes.len() {
        if bytes[at] != b'v' {
            continue;
        }
        let previous = at.checked_sub(1).and_then(|before| bytes.get(before));
        // Rule 2 needs the `e` to end the syllable; a letter after it means the
        // `lv`/`nv` reading is the live one instead, as in `nver` (nü + er).
        let rule_two = matches!(previous, Some(b'l' | b'n'))
            && bytes.get(at + 1) == Some(&b'e')
            && !matches!(bytes.get(at + 2), Some(byte) if byte.is_ascii_alphabetic());
        if matches!(previous, Some(b'j' | b'q' | b'x' | b'y')) || rule_two {
            out[at] = b'u';
            rewritten = true;
        }
    }
    rewritten
}
// Count complete syllables with a bounded stack table, including explicit separators.
pub(crate) fn count_spelling(input: &str) -> Option<u8> {
    let mut count = [u8::MAX; crate::MAX_INPUT_BYTES + 1];
    count[input.len()] = 0;
    for at in (0..input.len()).rev() {
        if input.as_bytes()[at] == b'\'' {
            continue;
        }
        for end in at + 1..=(at + 6).min(input.len()) {
            let next = end + usize::from(input.as_bytes().get(end) == Some(&b'\''));
            if count[next] != u8::MAX && contains(&input[at..end]) {
                count[at] = count[at].min(count[next] + 1);
            }
        }
    }
    (count[0] != u8::MAX).then_some(count[0])
}
// Profile construction only: choose a complete syllable parse, preserving
// explicit separators. Failed parses retain the original shortcut/typo key.
pub(crate) fn profile_spelling(key: &str) -> String {
    let mut output = String::new();
    for part in key.split('\'') {
        let mut end = [0usize; crate::MAX_INPUT_BYTES + 1];
        end[part.len()] = part.len();
        for at in (0..part.len()).rev() {
            for next in (at + 1..=(at + 6).min(part.len())).rev() {
                if (next == part.len() || end[next] != 0) && contains(&part[at..next]) {
                    end[at] = next;
                    break;
                }
            }
        }
        if end[0] == 0 {
            return key.to_owned();
        }
        let mut at = 0;
        while at < part.len() {
            if !output.is_empty() {
                output.push('\'');
            }
            output.push_str(&part[at..end[at]]);
            at = end[at];
        }
    }
    output
}
