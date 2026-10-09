// SPDX-License-Identifier: GPL-3.0-or-later
use chengyin_core::{fuzzy::OPTIONS_MASK, Dictionary, Key, Modifiers, Profile, Session};
use std::sync::Arc;

// Two dictionary words whose concatenation is deliberately absent, so the whole
// phrase can only reach the profile by being learned from a segmented commit.
const TSV: &str =
    "zheng'ze\t正则\t800\nzheng\t正\t900\nze\t则\t600\nbiao'da'shi\t表达式\t700\nshi\t是\t1000\n";
const RAW: &str = "zhengzebiaodashi";
const PHRASE: &str = "正则表达式";

fn type_keys(s: &mut Session, input: &str) {
    for c in input.chars() {
        assert!(s.process(Key::Character(c), Modifiers::default()).handled);
    }
}

fn session(d: &Arc<Dictionary>, incremental: bool, flags: u32) -> Session {
    let mut s = Session::new(Arc::clone(d));
    assert!(s.configure(9, true, false));
    assert!(s.configure_matching(flags));
    assert!(s.configure_incremental(incremental));
    s
}

fn find(s: &mut Session, text: &str, consumed: usize) -> usize {
    loop {
        for i in 0..s.candidate_count() {
            if s.candidate(i).unwrap().text == text && s.candidate_consumed(i) == Some(consumed) {
                return i;
            }
        }
        assert!(s.has_next_page(), "missing {text}/{consumed}");
        assert!(s.process(Key::PageDown, Modifiers::default()).handled);
    }
}

/// Commit one composition in two segments, acknowledging each write the way a
/// host does: `learn_commit` after every individual on-screen commit.
fn commit_in_segments(s: &mut Session) -> u32 {
    type_keys(s, RAW);
    let at = find(s, "正则", 7);
    assert!(s.process(Key::Select(at), Modifiers::default()).handled);
    assert_eq!(s.commit(), "正则");
    assert_eq!(s.learning_key(), "zhengze");
    assert!(s.learn_commit());
    let at = find(s, "表达式", 9);
    assert!(s.process(Key::Select(at), Modifiers::default()).handled);
    assert_eq!(s.commit(), "表达式");
    assert!(s.preedit().is_empty());
    assert_eq!(s.learning_key(), "biaodashi");
    assert!(s.learn_commit());
    s.profile().entry_count() as u32
}

#[test]
fn incremental_segments_learn_the_whole_phrase() {
    for flags in [0, OPTIONS_MASK] {
        let d = Arc::new(Dictionary::from_tsv(TSV).unwrap());

        // Baseline: nothing learned yet, so the phrase exists only as a generated
        // sentence, which carries no observed frequency.
        let mut fresh = session(&d, true, flags);
        type_keys(&mut fresh, RAW);
        assert_eq!(fresh.candidate(0).unwrap().text, PHRASE);
        assert_eq!(fresh.candidate(0).unwrap().frequency, 0);

        // One segmented composition trains both segments *and* their phrase.
        let mut s = session(&d, true, flags);
        assert_eq!(commit_in_segments(&mut s), 3);
        assert_eq!(s.profile().entry_count(), 3);

        // The whole-phrase row survives a profile reload and is reachable by the
        // complete spelling. It is not yet the first candidate: the phrase is not
        // dictionary-attested, and learned rows need repeated recent evidence
        // before promotion (see the repetition test below).
        let bytes = s.profile().to_binary();
        let mut reopened = session(&d, true, flags);
        assert!(reopened.set_profile(Arc::new(Profile::from_binary(&bytes).unwrap())));
        reopened.reset();
        type_keys(&mut reopened, RAW);
        let at = find(&mut reopened, PHRASE, RAW.len());
        assert_eq!(reopened.candidate(at).unwrap().text, PHRASE);
    }
}

#[test]
fn repeated_segmented_input_promotes_the_phrase_to_the_first_candidate() {
    for flags in [0, OPTIONS_MASK] {
        let d = Arc::new(Dictionary::from_tsv(TSV).unwrap());
        let mut s = session(&d, true, flags);
        // Unattested text needs repeated recent evidence before it is promoted,
        // which is the profile's existing rule for learned rows.
        for _ in 0..3 {
            commit_in_segments(&mut s);
            s.reset();
        }
        let bytes = s.profile().to_binary();
        let mut reopened = session(&d, true, flags);
        assert!(reopened.set_profile(Arc::new(Profile::from_binary(&bytes).unwrap())));
        reopened.reset();
        type_keys(&mut reopened, RAW);
        assert_eq!(reopened.candidate(0).unwrap().text, PHRASE);
        assert_eq!(reopened.candidate(0).unwrap().frequency, 3);
        assert_eq!(reopened.candidate_consumed(0), Some(RAW.len()));
    }
}

#[test]
fn closing_segment_served_from_a_learned_row_still_completes_the_phrase() {
    // The second composition of the same spelling is served by the learned rows
    // the first one created, so the closing segment takes the learned-candidate
    // path instead of the dictionary-prefix path. Both must extend the phrase.
    for flags in [0, OPTIONS_MASK] {
        let d = Arc::new(Dictionary::from_tsv(TSV).unwrap());
        let mut s = session(&d, true, flags);
        for round in 1..=3 {
            type_keys(&mut s, RAW);
            let at = find(&mut s, "正则", 7);
            assert!(s.process(Key::Select(at), Modifiers::default()).handled);
            assert!(s.learn_commit());
            // From the second round on this row is the profile's own.
            assert_eq!(s.candidate(0).unwrap().text, "表达式", "round {round}");
            assert!(s.process(Key::Select(0), Modifiers::default()).handled);
            assert_eq!(s.commit(), "表达式");
            assert!(s.learn_commit());
            s.reset();
        }
        // Both segments and the phrase, counted once per round.
        let bytes = s.profile().to_binary();
        let mut reopened = session(&d, true, flags);
        assert!(reopened.set_profile(Arc::new(Profile::from_binary(&bytes).unwrap())));
        reopened.reset();
        type_keys(&mut reopened, RAW);
        let at = find(&mut reopened, PHRASE, RAW.len());
        assert_eq!(reopened.candidate(at).unwrap().frequency, 3);
    }
}

#[test]
fn staged_selection_learns_the_whole_phrase_once() {
    let d = Arc::new(Dictionary::from_tsv(TSV).unwrap());
    let mut s = session(&d, false, 0);
    type_keys(&mut s, RAW);
    let at = find(&mut s, "正则", 7);
    assert!(s.process(Key::Select(at), Modifiers::default()).handled);
    // Staged segments stay reversible: nothing has been written to the host yet.
    assert!(s.commit().is_empty());
    assert_eq!(s.learning_key(), "");
    let at = find(&mut s, "表达式", 9);
    assert!(s.process(Key::Select(at), Modifiers::default()).handled);
    assert_eq!(s.commit(), PHRASE);
    assert_eq!(s.learning_key(), RAW);
    assert!(s.learn_commit());
    assert_eq!(s.profile().entry_count(), 1);
}

#[test]
fn cancelled_composition_learns_no_phrase_and_cannot_seed_the_next_one() {
    for flags in [0, OPTIONS_MASK] {
        let d = Arc::new(Dictionary::from_tsv(TSV).unwrap());
        let mut s = session(&d, true, flags);
        type_keys(&mut s, RAW);
        let at = find(&mut s, "正则", 7);
        assert!(s.process(Key::Select(at), Modifiers::default()).handled);
        // The accepted segment is still learned on its own.
        assert!(s.learn_commit());
        assert_eq!(s.profile().entry_count(), 1);
        assert!(s.process(Key::Escape, Modifiers::default()).handled);
        assert!(s.preedit().is_empty());
        assert!(!s.learn_commit());
        assert_eq!(s.profile().entry_count(), 1);
        // A later composition starts from scratch: the abandoned segment must
        // not combine with the new one into a phrase.
        type_keys(&mut s, RAW);
        let at = find(&mut s, "正则", 7);
        assert!(s.process(Key::Select(at), Modifiers::default()).handled);
        assert!(s.learn_commit());
        assert_eq!(s.profile().entry_count(), 1);
    }
}

#[test]
fn disabled_learning_records_neither_segments_nor_phrase() {
    let d = Arc::new(Dictionary::from_tsv(TSV).unwrap());
    let mut s = session(&d, true, 0);
    assert!(s.configure(9, false, false));
    type_keys(&mut s, RAW);
    let at = find(&mut s, "正则", 7);
    assert!(s.process(Key::Select(at), Modifiers::default()).handled);
    assert!(!s.learn_commit());
    let at = find(&mut s, "表达式", 9);
    assert!(s.process(Key::Select(at), Modifiers::default()).handled);
    assert!(!s.learn_commit());
    assert_eq!(s.profile().entry_count(), 0);
}

#[test]
fn one_whole_phrase_commit_learns_exactly_one_row() {
    for flags in [0, OPTIONS_MASK] {
        let d = Arc::new(Dictionary::from_tsv(TSV).unwrap());
        let mut s = session(&d, true, flags);
        type_keys(&mut s, RAW);
        // Choosing the whole phrase directly is one segment, not a phrase built
        // from several: it must not be recorded twice.
        let at = find(&mut s, PHRASE, RAW.len());
        assert!(s.process(Key::Select(at), Modifiers::default()).handled);
        assert_eq!(s.commit(), PHRASE);
        assert_eq!(s.learning_key(), RAW);
        assert!(s.learn_commit());
        assert!(!s.learn_commit());
        assert_eq!(s.profile().entry_count(), 1);
    }
}
