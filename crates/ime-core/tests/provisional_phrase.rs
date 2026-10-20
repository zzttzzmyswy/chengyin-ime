// SPDX-License-Identifier: GPL-3.0-or-later
//! A whole word built by selecting two segments in a row reaches the first page at
//! once (MYS-2270 stage 2).
//!
//! `data/daily.tsv` has no entry for `quanjiatong` => 全家桶, but picking 全家 and
//! then 桶 commits exactly that string, and `Session::learn_commit` records the
//! whole `spelling => phrase` pair. The profile then holds a row the lexicon does
//! not attest, which `Profile::reliable` keeps off the page until it has repeated
//! recent evidence — so before this change the phrase reappeared only on the third
//! round, and the first two rounds' evidence was invisible.
//!
//! `Profile::provisional` names the middle state: confirmed, not attested, not yet
//! repeated. Such a row is offered directly behind the leader, so it is visible on
//! the first page without displacing the candidate that has real lexical support,
//! and once its evidence accumulates the existing promotion puts it first.
//!
//! Every case runs under all four combinations of the two configuration axes,
//! because a page that only works in one adapter is the bug this file exists to
//! catch. The dictionary is the one the adapters ship.

use chengyin_core::fuzzy::OPTIONS_MASK;
use chengyin_core::{Dictionary, Key, Modifiers, Profile, Session};
use std::sync::Arc;

/// The whole typed spelling, and the phrase the two segments build.
const RAW: &str = "quanjiatong";
const PHRASE: &str = "全家桶";
/// The dictionary's own single read for the whole spelling: what keeps the lead.
const ATTESTED: &str = "全家童";

fn dictionary() -> Arc<Dictionary> {
    Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap())
}

fn session(incremental: bool, options: u32, profile: Option<Arc<Profile>>) -> Session {
    let mut session = Session::new(dictionary());
    assert!(session.configure(9, true, false));
    assert!(session.configure_matching(options));
    assert!(session.configure_incremental(incremental));
    if let Some(profile) = profile {
        assert!(session.set_profile(profile));
    }
    session
}

fn input(session: &mut Session, text: &str) {
    for c in text.chars() {
        assert!(
            session
                .process(Key::Character(c), Modifiers::default())
                .handled
        );
    }
}

/// The visible page: the Top-9 the adapters draw.
fn page(session: &Session) -> Vec<String> {
    (0..session.candidate_count())
        .map(|i| session.candidate(i).unwrap().text.to_owned())
        .collect()
}

/// Index of `text` on whichever page holds it.
fn find(session: &mut Session, text: &str) -> Option<usize> {
    loop {
        if let Some(at) =
            (0..session.candidate_count()).find(|&i| session.candidate(i).unwrap().text == text)
        {
            return Some(at);
        }
        if !session.has_next_page() {
            return None;
        }
        assert!(session.process(Key::PageDown, Modifiers::default()).handled);
    }
}

fn select(session: &mut Session, text: &str) {
    let at =
        find(session, text).unwrap_or_else(|| panic!("{text} missing from {:?}", page(session)));
    assert!(
        session
            .process(Key::Select(at), Modifiers::default())
            .handled
    );
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

/// One round: type the whole spelling, pick 全家, then 桶. The host acknowledges
/// every individual write, which is what a platform adapter does.
///
/// The two adapters stage differently, so only the incremental one writes 全家 on
/// its own: the legacy adapter holds the chosen word in the preedit until the
/// composition closes and then writes 全家桶 in one go. The acknowledgement taken
/// after the closing segment is therefore the one that carries the whole
/// `quanjiatong => 全家桶` pair in both adapters.
fn one_round(session: &mut Session, incremental: bool) {
    input(session, RAW);
    select(session, "全家");
    if incremental {
        assert_eq!(
            session.commit(),
            "全家",
            "the first segment commits at once"
        );
        assert!(session.learn_commit(), "the first segment's write");
    } else {
        assert_eq!(
            session.commit(),
            "",
            "the staged segment writes nothing yet"
        );
        assert_eq!(session.preedit(), "全家tong");
    }
    select(session, "桶");
    // Legacy closes the composition with the whole phrase; incremental already
    // wrote the first segment and writes only the remainder now.
    assert_eq!(session.commit(), if incremental { "桶" } else { PHRASE });
    assert!(session.learn_commit(), "the closing write");
    session.reset();
}

/// Re-open the profile the session just trained, the way a host reloads it.
fn reopen(trained: &Session, incremental: bool, options: u32) -> Session {
    let bytes = trained.profile().to_binary();
    session(
        incremental,
        options,
        Some(Arc::new(Profile::from_binary(&bytes).unwrap())),
    )
}

/// The acceptance criterion: one round is enough for the phrase to reach the first
/// page, and the lexical candidate keeps the lead.
#[test]
fn one_round_puts_the_phrase_second_on_the_first_page() {
    for (incremental, options, name) in configurations() {
        let mut session = session(incremental, options, None);
        one_round(&mut session, incremental);
        let mut reopened = reopen(&session, incremental, options);
        input(&mut reopened, RAW);
        let words = page(&reopened);
        assert_eq!(words.first().map(String::as_str), Some(ATTESTED), "{name}");
        assert_eq!(words.get(1).map(String::as_str), Some(PHRASE), "{name}");
    }
}

/// The same statement in terms of the row rather than the page: the phrase is on
/// the first page after one round, and it is absent before any round.
#[test]
fn the_phrase_is_absent_until_it_is_learnt() {
    for (incremental, options, name) in configurations() {
        let mut fresh = session(incremental, options, None);
        input(&mut fresh, RAW);
        assert!(
            !page(&fresh).iter().any(|w| w == PHRASE),
            "{name}: an untrained profile cannot show the phrase"
        );

        let mut trained = session(incremental, options, None);
        one_round(&mut trained, incremental);
        let mut reopened = reopen(&trained, incremental, options);
        input(&mut reopened, RAW);
        assert!(
            page(&reopened).iter().any(|w| w == PHRASE),
            "{name}: {:?}",
            page(&reopened)
        );
    }
}

/// Two rounds are still provisional: the phrase keeps the second slot rather than
/// taking the lead. Only the third round satisfies `has_repeated_evidence`.
#[test]
fn the_second_round_still_yields_the_lead() {
    for (incremental, options, name) in configurations() {
        let mut session = session(incremental, options, None);
        one_round(&mut session, incremental);
        let mut second = reopen(&session, incremental, options);
        one_round(&mut second, incremental);
        let mut reopened = reopen(&second, incremental, options);
        input(&mut reopened, RAW);
        let words = page(&reopened);
        assert_eq!(words.first().map(String::as_str), Some(ATTESTED), "{name}");
        assert_eq!(words.get(1).map(String::as_str), Some(PHRASE), "{name}");
    }
}

/// Once the row has repeated recent evidence the existing promotion applies
/// unchanged: the phrase leads, exactly as it did before this change.
#[test]
fn the_third_round_promotes_the_phrase_to_the_lead() {
    for (incremental, options, name) in configurations() {
        let mut session = session(incremental, options, None);
        for _ in 0..3 {
            one_round(&mut session, incremental);
        }
        let mut reopened = reopen(&session, incremental, options);
        input(&mut reopened, RAW);
        let words = page(&reopened);
        assert_eq!(words.first().map(String::as_str), Some(PHRASE), "{name}");
        assert_eq!(words.get(1).map(String::as_str), Some(ATTESTED), "{name}");
    }
}

/// A witnessed word is untouched by any of this. 劝架 is in the dictionary for
/// `quanjia`, so one selection makes it `reliable` through the attested half of
/// that predicate and it leads at once — the position it holds on `main`, measured
/// there before this branch existed (see the delivery comment).
#[test]
fn an_attested_word_keeps_its_position() {
    for (incremental, options, name) in configurations() {
        // Beyond the first three candidates the two adapters' untrained pages
        // differ, which is stage 1's doing and not this change's; the shared head is
        // what matters here.
        let fourth = if options == 0 && !incremental {
            "全家人"
        } else if options == 0 {
            "全集"
        } else {
            "欠佳"
        };
        let mut session = session(incremental, options, None);
        input(&mut session, "quanjia");
        assert_eq!(
            page(&session)[..3],
            ["全家", "全价", "劝架"],
            "{name}: the untrained page"
        );
        select(&mut session, "劝架");
        assert!(session.learn_commit());

        let mut reopened = reopen(&session, incremental, options);
        input(&mut reopened, "quanjia");
        assert_eq!(
            page(&reopened)[..4],
            ["劝架", "全家", "全价", fourth],
            "{name}: one selection of a witnessed word keeps the lead"
        );
    }
}

/// A single syllable is the character lane and stays untouched: the provisional
/// slot never runs there, so no learned character appears behind the first one. The
/// character lane's own promotion (a learned character leading) is unchanged.
#[test]
fn a_single_syllable_keeps_its_own_lane() {
    for (_incremental, options, name) in configurations() {
        let mut session = session(_incremental, options, None);
        input(&mut session, "quan");
        let before = page(&session);
        select(&mut session, "犬");
        assert!(session.learn_commit());
        let mut reopened = reopen(&session, _incremental, options);
        input(&mut reopened, "quan");
        let after = page(&reopened);
        // The learned character leads; every other candidate keeps its old order,
        // with nothing inserted behind the leader.
        assert_eq!(after.first().map(String::as_str), Some("犬"), "{name}");
        let mut rest = before;
        rest.retain(|w| w != "犬");
        assert_eq!(after[1..], rest[..], "{name}");
    }
}
