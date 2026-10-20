// SPDX-License-Identifier: GPL-3.0-or-later
//! The prefix lane, in both adapters (MYS-2270 stage 1).
//!
//! The legacy (non-incremental) adapter is what Fcitx 5 runs, and it used to reach
//! multi-syllable input through the decoder's `segments` list only — which
//! `Decoder::decode` never filled whenever any matching rule was enabled, and which
//! the phase-3 gate skipped for any input that had a whole-word lexical match. So
//! `quanjiatong` showed 全家童 alone and `nihaoma` showed 你好吗 alone.
//!
//! Both adapters now read the same `next_prefix()` lane. What stays incremental is
//! `exact_prefix`, which rewrites the decoder's sentence graph; the whole-input
//! candidates of a legacy page therefore stay byte-identical to the pre-change page,
//! and the prefix words are appended after them. Every case below runs under all
//! four combinations of the two configuration axes, because a page that only works
//! with the rules off is the bug this file exists to catch.

use chengyin_core::fuzzy::OPTIONS_MASK;
use chengyin_core::{Dictionary, Key, Modifiers, Session};
use std::sync::Arc;

/// The committed dictionary the adapters ship.
fn dictionary() -> Arc<Dictionary> {
    Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap())
}

fn session(incremental: bool, options: u32) -> Session {
    let mut session = Session::new(dictionary());
    assert!(session.configure(9, true, false));
    assert!(session.configure_matching(options));
    assert!(session.configure_incremental(incremental));
    session
}

/// The visible page: the Top-9 the adapters draw.
fn page(session: &Session) -> Vec<String> {
    (0..session.candidate_count())
        .map(|i| session.candidate(i).unwrap().text.to_owned())
        .collect()
}

fn input(session: &mut Session, text: &str) {
    for c in text.chars() {
        let result = session.process(Key::Character(c), Modifiers::default());
        assert!(result.handled, "{text} / {c}");
    }
}

/// Both configuration axes, named so a failure says which combination moved.
fn configurations() -> [(bool, u32, &'static str); 4] {
    [
        (false, 0, "legacy/no-rules"),
        (false, OPTIONS_MASK, "legacy/all-rules"),
        (true, 0, "incremental/no-rules"),
        (true, OPTIONS_MASK, "incremental/all-rules"),
    ]
}

#[test]
fn quanjiatong_first_page_reaches_phrase_and_character_prefixes() {
    for (incremental, options, name) in configurations() {
        let mut session = session(incremental, options);
        input(&mut session, "quanjiatong");
        let words = page(&session);
        assert_eq!(words.first().map(String::as_str), Some("全家童"), "{name}");
        for wanted in ["全家", "劝架", "全"] {
            assert!(
                words.iter().any(|w| w == wanted),
                "{name}: {wanted} missing from {words:?}"
            );
        }
    }
}

#[test]
fn nihaoma_first_page_reaches_phrase_prefixes() {
    for (incremental, options, name) in configurations() {
        let mut session = session(incremental, options);
        input(&mut session, "nihaoma");
        let words = page(&session);
        assert_eq!(words.first().map(String::as_str), Some("你好吗"), "{name}");
        for wanted in ["你好", "你"] {
            assert!(
                words.iter().any(|w| w == wanted),
                "{name}: {wanted} missing from {words:?}"
            );
        }
    }
}

/// The two adapters stage a prefix selection differently, and neither is changed
/// here. The legacy adapter stages: the chosen word is held in the preedit with the
/// rest of the spelling behind it, and nothing is committed until the composition
/// finishes. The incremental adapter commits the chosen word at once and leaves only
/// the remainder in the preedit. Both are asserted, under all four configurations,
/// because the page itself must be the same either way.
#[test]
fn prefix_selection_stages_in_legacy_and_commits_in_incremental() {
    for (incremental, options, name) in configurations() {
        let mut session = session(incremental, options);
        input(&mut session, "quanjiatong");
        let index = page(&session)
            .iter()
            .position(|w| w == "全家")
            .unwrap_or_else(|| panic!("{name}: 全家 missing from {:?}", page(&session)));
        assert!(
            session
                .process(Key::Select(index), Modifiers::default())
                .handled
        );
        let rest = page(&session);
        assert!(rest.iter().any(|w| w == "桶"), "{name}: {rest:?}");
        let barrel = rest.iter().position(|w| w == "桶").unwrap();
        if incremental {
            assert_eq!(session.preedit(), "tong", "{name}");
            assert_eq!(session.commit(), "全家", "{name}");
            assert!(
                session
                    .process(Key::Select(barrel), Modifiers::default())
                    .handled
            );
            assert_eq!(session.commit(), "桶", "{name}");
        } else {
            assert_eq!(session.preedit(), "全家tong", "{name}");
            assert_eq!(session.commit(), "", "{name}");
            assert!(
                session
                    .process(Key::Select(barrel), Modifiers::default())
                    .handled
            );
            assert_eq!(session.commit(), "全家桶", "{name}");
        }
    }
}
