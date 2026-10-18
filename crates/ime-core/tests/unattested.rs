// SPDX-License-Identifier: GPL-3.0-or-later
//! Character fallback for compositions the dictionary never wrote down.
//!
//! The boundary rules reject an unattested join, which is what stops synthesized
//! sentences from claiming lexical relationships the data does not support. The
//! cost was a hole: an input whose *every* join is unattested — an unknown personal
//! name, most obviously — had no path at all, so the page held only prefix words
//! and the first character. 张泽涛 typed `zhangzetao` returned 张 and 泽 and nothing
//! that covered the composition.
//!
//! The fallback re-runs the graph over single-character edges only, and only when
//! no full spelling parsed at all. These tests pin both halves: the hole is
//! filled, and the rules that must not move stay where they were. Most use small
//! hand-written dictionaries so each assertion is about one rule; the last two use
//! the shipped dictionary to show the real-world shape.

use chengyin_core::{Dictionary, Key, Modifiers, Session};
use std::sync::Arc;

fn replay(dictionary: Arc<Dictionary>, input: &str) -> Session {
    let mut session = Session::new(dictionary);
    assert!(session.configure(9, true, false));
    for c in input.chars() {
        assert!(
            session
                .process(Key::Character(c), Modifiers::default())
                .handled,
            "{input} / {c}"
        );
    }
    session
}

/// Every candidate of the composition, walked to the last page.
fn all_rows(session: &mut Session) -> Vec<(String, usize)> {
    let mut rows = Vec::new();
    loop {
        for i in 0..session.candidate_count() {
            let candidate = session.candidate(i).unwrap();
            rows.push((
                candidate.text.to_owned(),
                session.candidate_consumed(i).unwrap(),
            ));
        }
        if !session.has_next_page() {
            break;
        }
        assert!(rows.len() < 4096, "runaway candidate list");
        session.process(Key::PageDown, Modifiers::default());
    }
    rows
}

/// Candidates that consume the whole input, i.e. that cover the composition.
fn covering(rows: &[(String, usize)], len: usize) -> Vec<String> {
    rows.iter()
        .filter(|(_, consumed)| *consumed == len)
        .map(|(text, _)| text.clone())
        .collect()
}

fn dict(tsv: &str) -> Arc<Dictionary> {
    Arc::new(Dictionary::from_tsv(tsv).unwrap())
}

/// Three characters and nothing else: no join between them is attested, and no
/// entry spans more than one syllable, so the ordinary graph has no path at all.
const CHARACTERS: &str = "zhang\t张\t100\nze\t泽\t100\ntao\t涛\t100\n";

#[test]
fn an_unattested_composition_gets_a_full_coverage_candidate() {
    let mut session = replay(dict(CHARACTERS), "zhangzetao");
    let rows = all_rows(&mut session);
    assert_eq!(
        covering(&rows, 10),
        ["张泽涛"],
        "the fallback reads the input out character by character: {rows:?}"
    );
}

#[test]
fn the_fallback_itself_only_builds_character_chains() {
    // With no single characters in the dictionary the fallback has nothing to
    // build, so it contributes no candidate of its own. (The two-word row the page
    // does show predates this change: it comes from the ordinary graph, not from
    // the fallback.) Pinning that keeps the fallback's own contribution honest.
    let words = dict("ruan'jian\t软件\t100\nhong'hai\t红海\t100\n");
    let mut session = replay(words, "ruanjianhonghai");
    let rows = all_rows(&mut session);
    assert!(
        !covering(&rows, "ruanjianhonghai".len())
            .iter()
            .any(|text| text.chars().count() > 4),
        "the fallback cannot chain words: {rows:?}"
    );
}

#[test]
fn the_fallback_never_adds_a_character_per_word() {
    // A character chain has exactly one character per syllable. If the fallback
    // were chaining words, a covering row would carry fewer characters than the
    // syllable count for the same input.
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    let mut session = replay(dictionary, "zhangzetao");
    let rows = all_rows(&mut session);
    let covering = covering(&rows, "zhangzetao".len());
    assert!(
        covering.iter().all(|text| text.chars().count() == 3),
        "zhangzetao is three syllables, so its fallback rows are three characters: {rows:?}"
    );
}

#[test]
fn an_attested_whole_word_is_untouched() {
    // A complete attested word is protected before any graph work, so the fallback
    // never runs and cannot dilute it with a character-by-character reading.
    let source = format!("{CHARACTERS}zhang'ze'tao\t张泽涛\t999\n");
    let mut session = replay(dict(&source), "zhangzetao");
    let rows = all_rows(&mut session);
    assert_eq!(
        covering(&rows, 10),
        ["张泽涛"],
        "the whole word owns its span: {rows:?}"
    );
    assert_eq!(rows.first().map(|(text, _)| text.as_str()), Some("张泽涛"));
}

#[test]
fn a_half_typed_syllable_does_not_become_a_phrase() {
    // `zhangzet` ends in `t`, which is not a syllable, so the input has no
    // complete syllable parse and must not manufacture a three-character phrase.
    let mut session = replay(dict(CHARACTERS), "zhangzet");
    let rows = all_rows(&mut session);
    assert!(
        covering(&rows, "zhangzet".len()).is_empty(),
        "a partial last syllable has no complete parse: {rows:?}"
    );
}

#[test]
fn a_single_syllable_does_not_reach_the_fallback() {
    // One syllable is a character query answered by the character lane; the
    // fallback needs at least two syllables and must add nothing here.
    let mut session = replay(dict(CHARACTERS), "zhang");
    let rows = all_rows(&mut session);
    assert_eq!(
        covering(&rows, 5),
        ["张"],
        "single syllable keeps its own lane: {rows:?}"
    );
}

#[test]
fn an_explicit_separator_is_a_complete_parse() {
    // Separators take part in the completeness check exactly as `matches`
    // consumes them, so a separated unknown name reaches the fallback too.
    let mut session = replay(dict(CHARACTERS), "zhang'ze'tao");
    let rows = all_rows(&mut session);
    assert_eq!(
        covering(&rows, "zhang'ze'tao".len()),
        ["张泽涛"],
        "{rows:?}"
    );
}

#[test]
fn the_shipped_dictionary_covers_an_unknown_name() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    let mut session = replay(dictionary, "zhangzetao");
    let rows = all_rows(&mut session);
    let covering = covering(&rows, "zhangzetao".len());
    assert!(
        !covering.is_empty(),
        "an unknown name must cover the whole input: {rows:?}"
    );
    assert_eq!(
        covering[0].chars().count(),
        3,
        "one character per syllable: {rows:?}"
    );
}

#[test]
fn a_two_syllable_name_whose_rival_parse_is_an_abbreviation_is_covered() {
    // 昂拍 is not in the dictionary, and the only path the ordinary graph finds
    // covers `angpai` as initials (昂 + 派-like heads). Covering as initials is not
    // covering as a spelling, so the fallback has to run.
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    let mut session = replay(dictionary, "angpai");
    let rows = all_rows(&mut session);
    let covering = covering(&rows, "angpai".len());
    assert_eq!(
        covering.first().map(String::as_str),
        Some("昂拍"),
        "{rows:?}"
    );
}
