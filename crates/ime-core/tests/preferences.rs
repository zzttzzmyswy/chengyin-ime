use myswy_core::{demo_dictionary, Dictionary, Key, Modifiers, Profile, Session};
use std::sync::Arc;
fn type_keys(s: &mut Session, input: &str) {
    for c in input.chars() {
        s.process(Key::Character(c), Modifiers::default());
    }
}
#[test]
fn learned_deep_homophone_survives_profile_reload_and_reset() {
    let mut source = String::new();
    for i in 0..137 {
        source.push_str(&format!(
            "shi\t字{}\t{}\n",
            char::from_u32(0x4e00 + i).unwrap(),
            138 - i
        ));
    }
    let dictionary = Arc::new(Dictionary::from_tsv(&source).unwrap());
    let mut session = Session::new(Arc::clone(&dictionary));
    type_keys(&mut session, "shi");
    for _ in 0..10 {
        session.process(Key::PageDown, Modifiers::default());
    }
    let preferred = session.candidate(7).unwrap().text.to_owned();
    session.process(Key::Select(7), Modifiers::default());
    assert_eq!(session.learning_key(), "shi");
    assert_eq!(session.commit(), preferred);
    assert!(session.learn_commit());
    assert!(!session.learn_commit());
    let bytes = session.profile().to_binary();
    let profile = Arc::new(Profile::from_binary(&bytes).unwrap());
    let mut reopened = Session::new(dictionary);
    assert!(reopened.set_profile(profile));
    type_keys(&mut reopened, "shi");
    assert_eq!(reopened.candidate(0).unwrap().text, preferred);
    reopened.reset();
    type_keys(&mut reopened, "shi");
    assert_eq!(reopened.candidate(0).unwrap().text, preferred);
    let mut rows = Vec::new();
    loop {
        for i in 0..reopened.candidate_count() {
            rows.push(reopened.candidate(i).unwrap().text.to_owned());
        }
        if !reopened.has_next_page() {
            break;
        }
        reopened.process(Key::PageDown, Modifiers::default());
    }
    let mut unique = rows.clone();
    unique.sort();
    unique.dedup();
    assert_eq!(unique.len(), 137);
    assert_eq!(rows.len(), 137);
}
#[test]
fn learning_requires_acknowledgement_and_can_be_disabled() {
    let d = Arc::new(Dictionary::from_tsv("shi\t是\t100\nshi\t试\t1\n").unwrap());
    let mut s = Session::new(Arc::clone(&d));
    type_keys(&mut s, "shi");
    s.process(Key::Select(1), Modifiers::default());
    assert_eq!(s.profile().entry_count(), 0); // A rejected host write must not train.
    s.reset();
    assert!(s.configure(5, false, false));
    type_keys(&mut s, "shi");
    s.process(Key::Select(1), Modifiers::default());
    assert!(!s.learn_commit());
    assert_eq!(s.profile().entry_count(), 0);
    assert!(!s.is_association());
    s.reset();
    assert!(s.configure(5, true, false));
    type_keys(&mut s, "shi");
    s.process(Key::Enter, Modifiers::default());
    assert!(!s.learn_commit());
}
#[test]
fn learned_sentence_and_explicit_separator_constraints() {
    let d = Arc::new(
        Dictionary::from_tsv(
            "wo\t我\t20\nxi'huan\t喜欢\t20\nzhong'wen\t中文\t20\nxian\t先\t50\nxi'an\t西安\t40\n",
        )
        .unwrap(),
    );
    let mut s = Session::new(Arc::clone(&d));
    type_keys(&mut s, "wxhzw");
    s.process(Key::Space, Modifiers::default());
    assert_eq!(s.commit(), "我喜欢中文");
    assert!(s.learn_commit());
    let mut p = (*s.profile()).clone();
    for _ in 0..5 {
        assert!(p.record("xian", "先"));
    }
    assert!(!p.record("shi", "plain ASCII"));
    assert!(!p.record("'shi", "是"));
    let mut next = Session::new(d);
    next.set_profile(Arc::new(Profile::from_binary(&p.to_binary()).unwrap()));
    type_keys(&mut next, "wxhzw");
    assert_eq!(next.candidate(0).unwrap().text, "我喜欢中文");
    next.reset();
    type_keys(&mut next, "xi'an");
    assert_eq!(next.candidate(0).unwrap().text, "西安");
}
#[test]
fn profile_rejects_damage_and_every_truncation() {
    let mut p = Profile::default();
    p.record("shi", "是");
    p.record("xi'an", "西安");
    let bytes = p.to_binary();
    assert_eq!(Profile::from_binary(&bytes).unwrap().to_binary(), bytes);
    for n in 0..bytes.len() {
        assert!(
            Profile::from_binary(&bytes[..n]).is_none(),
            "truncation {n}"
        );
    }
    for i in 20..bytes.len() {
        let mut damaged = bytes.clone();
        damaged[i] ^= 0x40;
        assert!(Profile::from_binary(&damaged).is_none());
    }
    assert!(Profile::from_binary(&[0; 4 * 1024 * 1024 + 1]).is_none());
    for i in 0..8300 {
        p.record("shi", &format!("词{}", char::from_u32(0x4e00 + i).unwrap()));
    }
    assert_eq!(p.entry_count(), 8192);
    assert!(Profile::from_binary(&p.to_binary()).is_some());
}
#[test]
fn display_separators_do_not_modify_raw_caret_and_pages_are_configurable() {
    let d = Arc::new(
        Dictionary::from_tsv("ni'hao\t你好\t20\nxi'an\t西安\t20\nzhong'guo\t中国\t20\n").unwrap(),
    );
    let mut s = Session::new(d);
    for (raw, expected) in [
        ("nihao", "ni'hao"),
        ("xi'an", "xi'an"),
        ("zhongguo", "zhong'guo"),
        ("zhongg", "zhong'g"),
    ] {
        s.reset();
        type_keys(&mut s, raw);
        let mut buffer = [0; 319];
        let n = s.display_preedit(&mut buffer);
        assert_eq!(std::str::from_utf8(&buffer[..n]).unwrap(), expected);
        assert_eq!(s.preedit(), raw);
        assert_eq!(s.preedit_cursor(), raw.len());
        s.process(Key::Home, Modifiers::default());
        assert_eq!(s.preedit_cursor(), 0);
    }
    s.reset();
    assert!(!s.configure(0, true, true));
    assert!(!s.configure(10, true, true));
    let mut source = String::new();
    for i in 0..23 {
        source.push_str(&format!("shi\t字{}\t{}\n", i, 100 - i));
    }
    let mut s = Session::new(Arc::new(Dictionary::from_tsv(&source).unwrap()));
    assert!(s.configure(5, true, true));
    type_keys(&mut s, "shi");
    assert!(!s.configure(7, true, true));
    let mut rows = 0;
    loop {
        rows += s.candidate_count();
        assert!(s.candidate_count() <= 5);
        if !s.has_next_page() {
            break;
        }
        s.process(Key::PageDown, Modifiers::default());
    }
    assert_eq!(rows, 23);
}

#[test]
fn frequent_preference_stays_first_after_long_history_and_context() {
    let mut profile = Profile::default();
    assert!(profile.record("nihao", "你好"));
    assert!(profile.record("nihao", "你好"));
    for _ in 0..9000 {
        assert!(profile.record("hao", "好"));
    }
    assert!(profile.record("nihao", "拟好"));
    let mut session = Session::new(demo_dictionary());
    assert!(session.set_profile(Arc::new(profile)));
    for c in "hao".chars() {
        session.process(Key::Character(c), Modifiers::default());
    }
    session.process(Key::Space, Modifiers::default());
    for c in "nihao".chars() {
        session.process(Key::Character(c), Modifiers::default());
    }
    assert_eq!(session.candidate(0).unwrap().text, "你好");
}

#[test]
fn display_keeps_every_legal_syllable_intact() {
    let source = include_str!("../../../data/syllables.txt");
    let syllables: Vec<_> = source.split_whitespace().collect();
    assert!(syllables.windows(2).all(|p| p[0] < p[1]));
    let mut session = Session::new(demo_dictionary());
    let mut display = [0u8; 319];
    for syllable in syllables {
        assert!(syllable.len() <= 6);
        session.reset();
        type_keys(&mut session, syllable);
        let size = session.display_preedit(&mut display);
        assert_eq!(std::str::from_utf8(&display[..size]).unwrap(), syllable);
    }
}
