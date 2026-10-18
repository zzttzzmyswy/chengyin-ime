// SPDX-License-Identifier: GPL-3.0-or-later
//! Where the character fallback is allowed to land.
//!
//! `unattested.rs` pins that the fallback *exists* and that it only ever builds
//! one-character-per-syllable chains. It says nothing about *position*, and the
//! report for this change got position wrong: it claimed the fallback "necessarily
//! ranks behind every witnessed candidate" because `transition` charges the
//! unattested penalty. That claim is false, and these tests pin what is actually
//! true so the difference is not lost again.
//!
//! Two facts, both exercised below with hand-written dictionaries:
//!
//! 1. A dictionary entry that spells the *whole* input suppresses the fallback
//!    entirely — `compute` sets `protected_word` and returns before any fallback
//!    runs, so the attested reading keeps the first slot.
//! 2. Where coverage is only *partial*, the fallback does take the lead, and the
//!    partial attested word drops below it. That is a deliberate trade — a string
//!    covering the typed input beating one that covers a prefix of it — but it is
//!    not what the report described, so it is pinned here rather than left to
//!    prose.
//!
//! The corpus-wide consequence is recorded in `docs/STATUS.md`: on the shipped
//! dictionary 926 pages lead with a zero-frequency straight reading where a
//! frequency-bearing prefix completion led before.

use chengyin_core::{Dictionary, Key, Modifiers, Session};
use std::sync::Arc;

fn rows(tsv: &str, input: &str) -> Vec<(String, u32, String)> {
    let dictionary = Arc::new(Dictionary::from_tsv(tsv).unwrap());
    let mut session = Session::new(dictionary);
    assert!(session.configure(9, true, false));
    assert!(session.configure_incremental(true));
    for c in input.chars() {
        assert!(
            session
                .process(Key::Character(c), Modifiers::default())
                .handled,
            "{input} / {c}"
        );
    }
    let mut all = Vec::new();
    loop {
        for i in 0..session.candidate_count() {
            let candidate = session.candidate(i).unwrap();
            all.push((
                candidate.text.to_owned(),
                candidate.frequency,
                candidate.pinyin.replace('\'', ""),
            ));
        }
        if !session.has_next_page() {
            break;
        }
        assert!(all.len() < 4096, "runaway candidate list");
        session.process(Key::PageDown, Modifiers::default());
    }
    all
}

/// Three characters, no attested join between them.
const CHARACTERS: &str = "zhang\t张\t100\nze\t泽\t100\ntao\t涛\t100\n";

/// An entry attested for the entire input outranks the fallback: `protected_word`
/// short-circuits the graph, so the straight reading `张泽涛` is never generated at
/// frequency 0 and cannot compete with the real entry.
#[test]
fn an_attested_whole_input_entry_keeps_the_first_slot() {
    let source = format!("{CHARACTERS}zhang'ze'tao\t张泽涛\t999\n");
    let all = rows(&source, "zhangzetao");
    assert_eq!(
        all[0],
        ("张泽涛".to_owned(), 999, "zhangzetao".to_owned()),
        "the attested entry leads: {all:?}"
    );
    // The fallback did not run, so there is no second zero-frequency reading of the
    // whole input alongside it.
    assert!(
        all.iter().filter(|r| r.1 == 0).count() <= 1,
        "protected_word suppresses the fallback graph: {all:?}"
    );
}

/// Without that entry the same input has no path, and the fallback supplies one.
#[test]
fn without_that_entry_the_fallback_supplies_the_reading() {
    let all = rows(CHARACTERS, "zhangzetao");
    assert_eq!(
        all[0],
        ("张泽涛".to_owned(), 0, "zhangzetao".to_owned()),
        "the fallback fills the hole: {all:?}"
    );
}

/// The trade this change makes, pinned explicitly. 软红 is attested (frequency 50)
/// but spells only `ruanhong`; 软红涛 spells the whole `ruanhongtao` at frequency 0.
/// The fallback takes the lead and the attested prefix completion drops to second.
///
/// This is *not* "the fallback ranks behind every witnessed candidate" — the claim
/// the original report made. It is the opposite, and it is the intended behaviour:
/// coverage of the typed input outranks coverage of a prefix of it.
#[test]
fn a_partial_attested_word_yields_to_full_coverage() {
    let source = "ruan\t软\t100\nhong\t红\t100\ntao\t涛\t100\nruan'hong\t软红\t50\n";
    let all = rows(source, "ruanhongtao");
    assert_eq!(
        all[0],
        ("软红涛".to_owned(), 0, "ruanhongtao".to_owned()),
        "full coverage leads: {all:?}"
    );
    let partial = all
        .iter()
        .position(|r| r.0 == "软红")
        .expect("软红 is still offered");
    assert_eq!(
        partial, 1,
        "the attested partial word keeps the second slot: {all:?}"
    );
    assert_ne!(
        all[partial].2, "ruanhongtao",
        "软红 spells `ruanhong`, not the whole input"
    );
}

/// A single syllable never reaches the fallback, so its own lane is untouched.
#[test]
fn a_single_syllable_keeps_its_own_lane() {
    let all = rows(CHARACTERS, "zhang");
    assert_eq!(
        all[0],
        ("张".to_owned(), 100, "zhang".to_owned()),
        "{all:?}"
    );
    assert!(
        all.iter().all(|r| r.2 == "zhang"),
        "no multi-syllable reading is manufactured: {all:?}"
    );
}
