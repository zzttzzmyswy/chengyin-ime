use myswy_core::{demo_dictionary, Key, Modifiers, Profile, Session};
use std::sync::Arc;

fn type_keys(s: &mut Session, text: &str) {
    for c in text.chars() {
        s.process(Key::Character(c), Modifiers::default());
    }
}
#[test]
fn cache_reuses_queries_and_invalidates_snapshot_flags_and_capacity_budget() {
    let mut p = Profile::default();
    for _ in 0..3 {
        p.record("nihao", "拟好");
    }
    let mut s = Session::new(demo_dictionary());
    s.set_profile(Arc::new(p));
    type_keys(&mut s, "nihao");
    assert_eq!(s.candidate(0).unwrap().text, "拟好");
    let before = s.history_cache_stats();
    s.process(Key::Backspace, Modifiers::default());
    s.process(Key::Character('o'), Modifiers::default());
    assert!(s.history_cache_stats().hits > before.hits);
    let mut replacement = Profile::default();
    replacement.record("nihao", "你好");
    s.reset();
    s.set_profile(Arc::new(replacement));
    assert_eq!(s.history_cache_stats().entries, 0);
    type_keys(&mut s, "nihao");
    assert_eq!(s.candidate(0).unwrap().text, "你好");
    s.reset();
    s.configure_matching(myswy_core::fuzzy::OPTIONS_MASK);
    let before = s.history_cache_stats();
    type_keys(&mut s, "nihao");
    assert!(s.history_cache_stats().misses > before.misses);
    assert_eq!(s.candidate(0).unwrap().text, "你好");
    s.process(Key::Select(0), Modifiers::default());
    assert!(s.learn_commit());
    assert_eq!(s.history_cache_stats().entries, 0);
}
#[test]
fn cache_capacity_adapts_to_reused_working_set_and_remains_bounded() {
    let mut s = Session::new(demo_dictionary());
    for _ in 0..20 {
        s.reset();
        type_keys(&mut s, "nihao");
    }
    let reused = s.history_cache_stats();
    assert!(reused.hits > 0 && reused.capacity > 4);
    assert!(reused.capacity <= 16 && reused.entries <= reused.capacity);
    for i in 0..128 {
        s.reset();
        type_keys(
            &mut s,
            &format!(
                "a{}{}{}",
                char::from(b'a' + i % 26),
                char::from(b'a' + i / 26),
                'z'
            ),
        );
    }
    let mixed = s.history_cache_stats();
    assert!((4..=16).contains(&mixed.capacity));
    assert!(mixed.entries <= mixed.capacity);
    let other = Session::new(demo_dictionary());
    assert_eq!(other.history_cache_stats().hits, 0);
}

#[test]
fn dictionary_replacement_invalidates_history_attestation() {
    use myswy_core::Dictionary;
    let mut p = Profile::default();
    p.record("nihao", "拟好");
    let mut s = Session::new(demo_dictionary());
    s.set_profile(Arc::new(p));
    type_keys(&mut s, "nihao");
    assert_eq!(s.candidate(0).unwrap().text, "你好");
    s.reset();
    assert!(s.history_cache_stats().entries > 0);
    s.set_dictionary(Arc::new(
        Dictionary::from_tsv("ni'hao\t你好\t100\nni'hao\t拟好\t1\n").unwrap(),
    ));
    assert_eq!(s.history_cache_stats().entries, 0);
    type_keys(&mut s, "nihao");
    assert_eq!(
        s.candidate(0).unwrap().text,
        "拟好",
        "one explicit selection of an attested word still learns promptly"
    );
}
