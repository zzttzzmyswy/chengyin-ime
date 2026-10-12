// SPDX-License-Identifier: GPL-3.0-or-later
//! Incremental-versus-from-scratch equivalence (I24).
//!
//! The A3 candidate is "reuse the DP work of the previous keystroke". Its
//! correctness condition is stated by the I24 task card as an equality: for every
//! prefix of every corpus input, the incrementally produced session state must be
//! *identical* to what a session that typed that prefix from scratch produces.
//! This test is that equality, and it holds today for the trivial reason that no
//! reuse exists — which is exactly why it is worth committing now: it is the
//! property any future A3 must keep, and it fails at the first prefix where a
//! cache forgets to invalidate.
//!
//! Two checks, both over `data/eval/quality.tsv` and `data/eval/prefix.tsv`:
//!
//! * **Prefix equality.** One session types the input; after every keystroke its
//!   candidate page is compared against a second session that was given the same
//!   prefix in one go. A cache that carries a stale suffix across a keystroke
//!   shows up as a different page at the prefix where it went stale.
//! * **Edit equality.** A typed composition is edited with Home / Right / Left /
//!   Delete / Backspace / End, and after every edit the page is compared against a
//!   session that typed the resulting spelling directly. This is the case a
//!   naive prefix cache gets wrong: the buffer changed in the middle, so the
//!   prefix before the caret is *not* the reused prefix.
//!
//! Both run under no rules and the full matching mask, because a cache keyed on
//! the input alone and not on `matching_options` passes the no-rules sweep and
//! fails this one — the invalidation condition the task card names.
//!
//! The corpus is large, so the test is `#[ignore]`d by default and runs in CI's
//! existing sweep; run it explicitly with `--ignored`:
//!
//!     cargo test -p chengyin-core --test incremental_equiv --locked -- --ignored

use chengyin_core::{Dictionary, Key, Modifiers, Session};
use std::sync::Arc;

/// All three matching configurations the acceptance criteria name. "Initials
/// mixed with full spellings" is an input shape rather than a flag, and the
/// prefix corpus already contains those shapes.
const CONFIGS: [(&str, u32); 2] = [
    ("no-rules", 0),
    ("all-rules", chengyin_core::fuzzy::OPTIONS_MASK),
];

/// How many corpus inputs to replay. The full 2,580 inputs x every prefix is
/// hours; an evenly spread slice keeps the check broad while finishing in
/// minutes. `--ignored` runs the slice; `CHENGYIN_EQUIV_STRIDE=1` runs everything.
fn stride() -> usize {
    std::env::var("CHENGYIN_EQUIV_STRIDE")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .filter(|&n| n > 0)
        .unwrap_or(8)
}

fn corpus() -> Vec<String> {
    let mut out = Vec::new();
    for line in include_str!("../../../data/eval/quality.tsv").lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<&str> = line.split('\t').collect();
        assert_eq!(fields.len(), 8, "quality.tsv row: {line:?}");
        out.push(fields[3].to_owned());
    }
    for line in include_str!("../../../data/eval/prefix.tsv").lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        out.push(line.to_owned());
    }
    out
}

fn dictionary() -> Arc<Dictionary> {
    Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap())
}

fn session(dictionary: &Arc<Dictionary>, flags: u32) -> Session {
    let mut s = Session::new(Arc::clone(dictionary));
    assert!(s.configure(9, true, false));
    assert!(s.configure_matching(flags));
    assert!(s.configure_incremental(true));
    s
}

/// The visible page as comparable text: `preedit`, then every candidate.
fn page(session: &Session) -> String {
    let mut out = format!("{}|", session.preedit());
    for i in 0..session.candidate_count() {
        let c = session.candidate(i).unwrap();
        out.push_str(&format!("{}|{}|{};", c.text, c.pinyin, c.frequency));
    }
    format!("{}#{}", out, session.candidate_count())
}

/// Type `spelling` into a fresh session in one pass and return its page.
fn from_scratch(dictionary: &Arc<Dictionary>, spelling: &str, flags: u32) -> String {
    let mut s = session(dictionary, flags);
    for c in spelling.chars() {
        s.process(Key::Character(c), Modifiers::default());
    }
    page(&s)
}

#[test]
#[ignore = "long corpus sweep; run with --ignored"]
fn every_prefix_matches_a_session_that_typed_it_from_scratch() {
    let dictionary = dictionary();
    let inputs = corpus();
    let step = stride();
    let mut checked = 0usize;
    for (name, flags) in CONFIGS {
        for input in inputs.iter().step_by(step) {
            let mut s = session(&dictionary, flags);
            let mut prefix = String::new();
            for c in input.chars() {
                prefix.push(c);
                s.process(Key::Character(c), Modifiers::default());
                let incremental = page(&s);
                let fresh = from_scratch(&dictionary, &prefix, flags);
                assert_eq!(
                    incremental, fresh,
                    "{name}: {prefix:?} differs after typing {:?} incrementally",
                    input
                );
                checked += 1;
            }
            s.reset();
        }
    }
    assert!(checked > 0, "no prefixes checked");
    println!("checked {checked} prefixes");
}

/// Edit sequences applied on top of a typed spelling. Each one moves the caret
/// *into* a composition and then changes it, which is the case a naive "reuse the
/// prefix before the caret" cache gets wrong: the buffer that the decoder must
/// describe is no longer a prefix of what it described before.
const SCRIPTS: [(&str, &[Key]); 4] = [
    ("tail backspace", &[Key::Backspace]),
    ("home then delete", &[Key::Home, Key::Delete]),
    ("left then backspace", &[Key::Left, Key::Backspace]),
    (
        "home, right, insert, delete",
        &[Key::Home, Key::Right, Key::Character('o'), Key::Delete],
    ),
];

#[test]
#[ignore = "long corpus sweep; run with --ignored"]
fn every_edit_sequence_matches_typing_the_edited_spelling_directly() {
    let dictionary = dictionary();
    let inputs = corpus();
    let step = stride();
    let mut checked = 0usize;
    let mut per_script = [0usize; SCRIPTS.len()];
    for (name, flags) in CONFIGS {
        for input in inputs.iter().step_by(step) {
            if input.len() < 4 {
                continue;
            }
            for (at, (label, keys)) in SCRIPTS.iter().enumerate() {
                let mut s = session(&dictionary, flags);
                for c in input.chars() {
                    s.process(Key::Character(c), Modifiers::default());
                }
                for &key in *keys {
                    s.process(key, Modifiers::default());
                }
                // The session tells us what its buffer now holds. That spelling,
                // typed into a fresh session, must produce the same page — so the
                // comparison never has to model caret arithmetic, which is the part
                // of this test that would otherwise be asserting its own assumption.
                let edited_spelling = s.preedit().to_owned();
                if edited_spelling.is_empty() || &edited_spelling == input {
                    // A script whose keys the session declined (an insert that the
                    // MAX_INPUT_BYTES guard rejected, a caret already at the end)
                    // leaves the buffer alone. That is not a failure of the property
                    // under test, and the `preedit` assertion below still requires
                    // *some* inputs to be exercised by each script.
                    continue;
                }
                let edited = page(&s);
                let fresh = from_scratch(&dictionary, &edited_spelling, flags);
                assert_eq!(
                    edited, fresh,
                    "{name}/{label}: {input:?} edited to {edited_spelling:?} does not \
                     match typing that spelling directly"
                );
                checked += 1;
                per_script[at] += 1;
                s.reset();
            }
        }
    }
    for (at, (label, _)) in SCRIPTS.iter().enumerate() {
        assert!(
            per_script[at] > 0,
            "edit script {label:?} never changed a buffer, so it proved nothing"
        );
    }
    println!("checked {checked} edit sequences: {per_script:?}");
}

#[test]
#[ignore = "long corpus sweep; run with --ignored"]
fn reused_state_survives_a_matching_option_change() {
    // The invalidation condition the task card names second: a cache keyed on the
    // input alone and not on `matching_options`. Options can only change with an
    // empty composition (`configure_matching` rejects an active one), so the
    // sequence is: decode, reset, switch options, decode the same spelling again,
    // and require the page of the second decode to equal a session that was
    // configured with those options from the start.
    let dictionary = dictionary();
    let inputs = corpus();
    let step = stride();
    let mut checked = 0usize;
    for input in inputs.iter().step_by(step) {
        if input.len() < 3 {
            continue;
        }
        let mut s = session(&dictionary, 0);
        for c in input.chars() {
            s.process(Key::Character(c), Modifiers::default());
        }
        s.reset();
        assert!(s.configure_matching(chengyin_core::fuzzy::OPTIONS_MASK));
        for c in input.chars() {
            s.process(Key::Character(c), Modifiers::default());
        }
        let after = page(&s);
        let fresh = from_scratch(&dictionary, input, chengyin_core::fuzzy::OPTIONS_MASK);
        assert_eq!(
            after, fresh,
            "a session that switched to all-rules after decoding {input:?} under \
             no-rules differs from one configured that way from the start"
        );
        checked += 1;
    }
    assert!(checked > 0, "no option-change cases checked");
    println!("checked {checked} option-change cases");
}
