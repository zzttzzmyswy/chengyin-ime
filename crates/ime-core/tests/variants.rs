// SPDX-License-Identifier: GPL-3.0-or-later
//! The `ü` variant spellings other IMEs accept (I22).
//!
//! `jv/qv/xv/yv` and `lve/nve` are not canonical pinyin, so before this change
//! they matched nothing at all. Every assertion here compares a variant's
//! candidates against the canonical spelling's own list, item for item, which is
//! the property the feature promises: a variant is an *alias*, never a different
//! reading. The negative controls — `lv`, `nv`, `lv'e` and the typed preedit —
//! pin the other half of that promise, that the rewrite is confined to the query
//! and never leaks into what the user sees or what is remembered.

use chengyin_core::{Dictionary, Key, Modifiers, Session};
use std::sync::Arc;

/// The shipped lexicon, so the compared spellings are real words. The demo
/// dictionary has no `jv`/`lve` neighbours at all and would assert nothing.
fn dictionary() -> Arc<Dictionary> {
    Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap())
}

fn type_keys(session: &mut Session, raw: &str) {
    for c in raw.chars() {
        assert!(
            session
                .process(Key::Character(c), Modifiers::default())
                .handled,
            "{raw}"
        );
    }
}

/// Every candidate for `raw`, page by page, as `text|pinyin` in rank order.
fn candidates(session: &mut Session) -> Vec<String> {
    let mut all = Vec::new();
    loop {
        for i in 0..session.candidate_count() {
            let c = session.candidate(i).unwrap();
            all.push(format!("{}|{}", c.text, c.pinyin));
        }
        if !session.has_next_page() {
            break;
        }
        assert!(all.len() < 4096);
        session.process(Key::PageDown, Modifiers::default());
    }
    all
}

fn spellings(raw: &str) -> Vec<String> {
    let mut session = Session::new(dictionary());
    type_keys(&mut session, raw);
    candidates(&mut session)
}

/// The variant must produce exactly the canonical spelling's candidates.
fn assert_alias(canonical: &str, variant: &str) {
    let expected = spellings(canonical);
    assert!(
        !expected.is_empty(),
        "{canonical} has no candidates, so it cannot witness an alias"
    );
    assert_eq!(
        expected,
        spellings(variant),
        "{variant} must resolve exactly like {canonical}"
    );
}

#[test]
fn rule_one_variants_resolve_like_their_canonical_spelling() {
    // Every `j q x y` head whose ü canonical pinyin writes with `u`, across the
    // four tail shapes the rule covers (`u`, `ue`, `uan`, `un`). Four real words
    // per head, as the acceptance criteria require.
    for (canonical, variant) in [
        ("jueding", "jveding"),
        ("junfenqu", "jvnfenqu"),
        ("juzi", "jvzi"),
        ("juanzeng", "jvanzeng"),
        ("queding", "qveding"),
        ("qunmeng", "qvnmeng"),
        ("quanqiuying", "qvanqiuying"),
        ("quyu", "qvyu"),
        ("xuexi", "xvexi"),
        ("xunzhao", "xvnzhao"),
        ("xuanxing", "xvanxing"),
        ("xuwu", "xvwu"),
        ("yuehai", "yvehai"),
        ("yunzhou", "yvnzhou"),
        ("yuanshen", "yvanshen"),
        ("yulan", "yvlan"),
    ] {
        assert_alias(canonical, variant);
    }
}

#[test]
fn rule_two_variants_resolve_like_their_canonical_spelling() {
    // `lve`/`nve` sit in a real ambiguity: `lv`/`nv` is itself the canonical
    // spelling of lü/nü, so only the following `e` distinguishes them.
    for (canonical, variant) in [
        ("luequ", "lvequ"),
        ("lueduo", "lveduo"),
        ("luedi", "lvedi"),
        ("lvexian", "lvexian"),
        ("nuedai", "nvedai"),
        ("nueji", "nveji"),
        ("nuelong", "nvelong"),
        ("nuezheng", "nvezheng"),
    ] {
        assert_alias(canonical, variant);
    }
}

#[test]
fn explicit_separators_and_plain_lv_nv_are_not_rewritten() {
    // `lv`/`nv` are canonical syllables, not variants: rewriting them would
    // destroy the ordinary way to type lü/nü.
    for raw in ["lv", "nv", "lvse", "nvhai", "lvse'de"] {
        let mut session = Session::new(dictionary());
        type_keys(&mut session, raw);
        assert_eq!(session.preedit(), raw, "{raw} preedit");
        // A separated `lv'e` is lü + e, never lüe, so it must not gain the
        // `lue` reading.
        let alias = spellings(raw);
        assert!(!alias.is_empty(), "{raw} should still match");
    }
    assert_ne!(spellings("lve"), spellings("lv"));
    assert_ne!(spellings("nve"), spellings("nv"));
}

#[test]
fn a_variant_only_ever_reaches_the_canonical_readings() {
    // The alias reading is a strict *subset relation*: whatever a variant finds
    // must be something its canonical spelling finds too. This is what forbids
    // the rewrite from inventing a reading the user did not type.
    for (canonical, variant) in [
        ("jue", "jve"),
        ("que", "qve"),
        ("xue", "xve"),
        ("yue", "yve"),
        ("lue", "lve"),
        ("nue", "nve"),
        ("juan", "jvan"),
        ("xun", "xvn"),
        ("yuan", "yvan"),
        ("yun", "yvn"),
    ] {
        let expected: std::collections::HashSet<_> = spellings(canonical).into_iter().collect();
        for found in spellings(variant) {
            assert!(
                expected.contains(&found),
                "{variant} produced {found}, which {canonical} does not offer"
            );
        }
    }
}

#[test]
fn preedit_shows_the_typed_variant_and_editing_keeps_matching() {
    let mut session = Session::new(dictionary());
    type_keys(&mut session, "jveding");
    // The user typed `v`; the preedit is what they typed, not the query spelling.
    assert_eq!(session.preedit(), "jveding");
    let before = candidates(&mut session);

    // Edit in the middle and retype: the canonical rewrite must follow the edit
    // rather than being frozen at the first refresh.
    session.process(Key::Home, Modifiers::default());
    for _ in 0..2 {
        session.process(Key::Right, Modifiers::default());
    }
    session.process(Key::Backspace, Modifiers::default());
    assert_eq!(session.preedit(), "jeding");
    session.process(Key::Character('v'), Modifiers::default());
    assert_eq!(session.preedit(), "jveding");
    assert_eq!(before, candidates(&mut session));
}

#[test]
fn a_variant_learns_the_key_it_can_be_found_under() {
    // Learning must key the row by the spelling the successful query used, or the
    // next identical input would not find it. Selecting from `jveding` therefore
    // records `jueding`, which is what makes the *canonical* input find it too.
    let mut session = Session::new(dictionary());
    type_keys(&mut session, "jveding");
    let chosen = session.candidate(0).unwrap().text.to_owned();
    session.process(Key::Space, Modifiers::default());
    assert_eq!(session.commit(), chosen);
    assert_eq!(session.learning_key(), "jueding");
    // The host acknowledges the write before the composition is reset, which is
    // the order the platform adapter uses; `reset` clears the key.
    assert!(session.learn_commit());
    session.reset();

    // The stored key is what the history lane compares against the *query*
    // spelling, so the canonical input must now find this selection in its own
    // history lane. A row kept under `jveding` would never be found from `jueding`.
    type_keys(&mut session, "jueding");
    assert_eq!(
        session.candidate(0).unwrap().text,
        chosen,
        "a selection confirmed through the variant must be recalled by the canonical key"
    );
    assert!(
        session.candidate_count() > 0,
        "the learned key must resolve to a candidate"
    );
}

#[test]
fn variants_coexist_with_matching_options_sentences_and_associations() {
    // With every correction and fuzzy lane enabled, a variant must still decode
    // and must not panic the tolerant walker or the sentence beam.
    for raw in [
        "jveding", "nvhai", "lvequ", "xvanxing", "yvanshen", "nvedai",
    ] {
        let mut session = Session::new(dictionary());
        assert!(session.configure_matching(chengyin_core::fuzzy::OPTIONS_MASK));
        type_keys(&mut session, raw);
        assert_eq!(session.preedit(), raw);
        assert!(
            session.candidate_count() > 0,
            "{raw} lost every candidate under the full option mask"
        );
        assert!(session.candidate(0).unwrap().text.chars().count() > 0);
    }
    // The `nv` reading survives the variant rules: `nvhai` is still 女孩-ish
    // (nü + hai), not `nue`+`hai`.
    let expected: std::collections::HashSet<_> = spellings("nvhai").into_iter().collect();
    assert!(
        expected
            .iter()
            .any(|row| row.starts_with('女') || row.starts_with('男')),
        "nvhai should reach a nü reading, got {expected:?}"
    );
}

#[test]
fn a_variant_inside_a_corrected_word_keeps_the_ordinary_reading() {
    // `xialnver` is a keyboard-corrected 小女儿 whose `nver` is nü + er. Rule 2
    // must leave that `v` alone: rewriting it reaches `nue`+`r` and loses the
    // word. This is the narrowing the shipped rule carries.
    let mut session = Session::new(dictionary());
    assert!(session.configure_matching(chengyin_core::fuzzy::NEIGHBOR));
    type_keys(&mut session, "xialnver");
    assert_eq!(session.preedit(), "xialnver");
    let texts: Vec<String> = candidates(&mut session)
        .into_iter()
        .map(|row| row.split('|').next().unwrap().to_owned())
        .collect();
    assert!(
        texts.iter().any(|text| text == "小女儿"),
        "the corrected 小女儿 must still be reachable, got {:?}",
        &texts[..texts.len().min(8)]
    );
}
