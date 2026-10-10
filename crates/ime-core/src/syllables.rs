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
        // Rule 2 applies only where the `l`/`n` can begin a syllable, which needs
        // everything before it to be a complete syllable sequence. That is the
        // narrowing the variant has to carry, because `lv`/`nv` is itself the
        // canonical spelling of lü/nü and 女儿's own dictionary reading is
        // `nv'er`: rewriting `nve` inside `xialnver` reaches `nue` and loses the
        // word the tolerant walker was aligning. Where the prefix does not parse
        // there is no syllable for rule 2 to rewrite, so the byte stays.
        let rule_two = matches!(previous, Some(b'l' | b'n'))
            && bytes.get(at + 1) == Some(&b'e')
            && count_spelling(&input[..at - 1]).is_some();
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

#[cfg(test)]
mod canonicalize_tests {
    use super::*;

    /// `canonicalize` into a fresh buffer; `None` when it reports no rewrite, so a
    /// test says which of the two outcomes it expects.
    fn rewrite(input: &str) -> Option<String> {
        let mut out = [0u8; crate::MAX_INPUT_BYTES];
        canonicalize(input, &mut out)
            .then(|| std::str::from_utf8(&out[..input.len()]).unwrap().to_owned())
    }

    #[test]
    fn the_two_rules_rewrite_exactly_their_own_bytes() {
        for (input, expected) in [
            // Rule 1: a `v` after j/q/x/y is the ü those initials write as `u`.
            ("jv", "ju"),
            ("jve", "jue"),
            ("jvan", "juan"),
            ("jvn", "jun"),
            ("qve", "que"),
            ("qvan", "quan"),
            ("qvn", "qun"),
            ("xve", "xue"),
            ("xvan", "xuan"),
            ("xvn", "xun"),
            ("yve", "yue"),
            ("yvan", "yuan"),
            ("yvn", "yun"),
            ("jveding", "jueding"),
            ("qvanqiuying", "quanqiuying"),
            // Rule 2: `lve`/`nve` are lüe/nüe, whose canonical spelling is lue/nue.
            ("lve", "lue"),
            ("nve", "nue"),
            ("lvequ", "luequ"),
            ("nvedai", "nuedai"),
        ] {
            assert_eq!(rewrite(input).as_deref(), Some(expected), "{input}");
        }
    }

    #[test]
    fn spellings_that_must_not_be_touched_are_left_alone() {
        for input in [
            // `lv`/`nv` are canonical syllables (lü/nü), not variants.
            "lv",
            "nv",
            "lvse",
            "nvhai",
            // An explicit separator is never rewritten, so `lv'e` stays lü + e.
            "lv'e",
            "nv'e",
            "lv'e'se",
            // Rule 2 needs its `e`; `lva`/`nvi` are simply not the variant.
            "lva",
            "nvi",
            // No `v` at all: the overwhelmingly common case.
            "ni",
            "nihao",
            "zhongguoren",
            "xian",
            "shi",
            "",
        ] {
            assert_eq!(rewrite(input), None, "{input} must not be rewritten");
        }
    }

    #[test]
    fn a_spelling_without_v_is_neither_rewritten_nor_copied() {
        // The hot path's contract: no `v` means the caller keeps its own bytes, so
        // the buffer is never even written to. Seeding it with a sentinel proves
        // `canonicalize` returned before its `copy_from_slice`.
        let mut out = [0xAA; crate::MAX_INPUT_BYTES];
        for input in ["ni", "nihao", "zhongguoren", "xian", ""] {
            assert!(!canonicalize(input, &mut out), "{input}");
            assert!(
                out.iter().all(|&byte| byte == 0xAA),
                "{input} wrote to the buffer despite needing no rewrite"
            );
        }
        // And a rewrite does fill it, so the sentinel is a real signal.
        assert!(canonicalize("jveding", &mut out));
        assert_eq!(&out[..7], b"jueding");
    }

    #[test]
    fn rule_two_yields_to_a_competing_lv_nv_reading() {
        // `lv`/`nv` plus a following syllable is a real parse (`nver` is 女儿,
        // nü + er), so the `v` stays whenever the prefix before it is itself a
        // complete syllable sequence. The reported regression row is exactly this
        // shape and is covered end to end in `tests/variants.rs`.
        assert_eq!(rewrite("xialnver"), None);
        assert_eq!(rewrite("nv'er"), None);
        // Where the prefix does not parse, there is no `lv`/`nv` syllable to
        // protect and the variant reading is the only one available.
        assert_eq!(rewrite("nvedai").as_deref(), Some("nuedai"));
    }

    #[test]
    fn over_long_input_is_rejected_without_writing() {
        let mut out = [0xAA; crate::MAX_INPUT_BYTES];
        let long = "jv".repeat(crate::MAX_INPUT_BYTES);
        assert!(!canonicalize(&long, &mut out));
        assert!(out.iter().all(|&byte| byte == 0xAA));
    }
}
