// SPDX-License-Identifier: GPL-3.0-or-later
use chengyin_core::{
    fuzzy::{NEIGHBOR, OMIT, OPTIONS_MASK, PHONETIC_MASK, REPEAT, SWAP},
    Dictionary, Key, Modifiers, Profile, Session,
};
use std::sync::Arc;
fn type_keys(session: &mut Session, input: &str) {
    for c in input.chars() {
        session.process(Key::Character(c), Modifiers::default());
    }
}
#[test]
fn complete_single_syllables_show_characters_first() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for options in [
        0,
        PHONETIC_MASK,
        SWAP | OMIT | NEIGHBOR | REPEAT,
        OPTIONS_MASK,
    ]
    .into_iter()
    .chain((0..11).map(|bit| 1 << bit))
    .chain([SWAP, OMIT, NEIGHBOR, REPEAT])
    {
        for input in ["bao", "shi", "hao", "an", "xian"] {
            let mut session = Session::new(Arc::clone(&dictionary));
            session.configure(5, true, false);
            session.configure_incremental(true);
            session.configure_matching(options);
            type_keys(&mut session, input);
            let first: Vec<_> = (0..session.candidate_count())
                .map(|i| {
                    let c = session.candidate(i).unwrap();
                    (c.text.to_owned(), c.pinyin.to_owned())
                })
                .collect();
            assert!(
                first.iter().all(|(text, _)| text.chars().count() == 1),
                "{input}/{options}: {first:?}"
            );
            for i in 0..session.candidate_count() {
                assert_eq!(session.candidate_consumed(i), Some(input.len()));
                assert_eq!(session.candidate_marks(i), Some([0; 4]));
            }
        }
    }
}

#[test]
fn character_lane_paginates_completely_before_ambiguous_words() {
    let mut source = String::from("xi'an\t西安\t1000000\nxian'shen\t现身\t900000\n");
    let mut wanted = Vec::new();
    for i in 0..137 {
        let text = char::from_u32(0x4e00 + i).unwrap().to_string();
        source.push_str(&format!("xian\t{text}\t{}\n", 138 - i));
        wanted.push(text);
    }
    let dictionary = Arc::new(Dictionary::from_tsv(&source).unwrap());
    for options in [0, OPTIONS_MASK] {
        let mut session = Session::new(Arc::clone(&dictionary));
        session.configure(5, true, false);
        session.configure_matching(options);
        type_keys(&mut session, "xian");
        let first = session.candidate(0).unwrap().text.to_owned();
        let mut rows = Vec::new();
        let mut words_started = false;
        loop {
            for i in 0..session.candidate_count() {
                let text = session.candidate(i).unwrap().text.to_owned();
                if text.chars().count() > 1 {
                    words_started = true;
                    assert!(rows.len() >= 137);
                } else {
                    assert!(!words_started, "character repeated after word lane: {text}");
                }
                assert!(!rows.contains(&text), "duplicate: {text}");
                rows.push(text);
            }
            if !session.has_next_page() {
                break;
            }
            assert!(rows.len() < 200, "pagination did not terminate");
            session.process(Key::PageDown, Modifiers::default());
        }
        assert!(wanted.iter().all(|text| rows.contains(text)));
        assert!(rows.contains(&"西安".to_owned()));
        assert!(rows.contains(&"现身".to_owned()));
        while session.page() > 0 {
            session.process(Key::PageUp, Modifiers::default());
        }
        assert_eq!(session.candidate(0).unwrap().text, first);
        session.process(Key::Select(0), Modifiers::default());
        assert_eq!(session.commit(), first);
        assert_eq!(session.preedit(), "");
        assert_eq!(session.learning_key(), "xian");
    }
}

#[test]
fn character_history_precedes_word_history_without_losing_either() {
    let dictionary = Arc::new(Dictionary::from_tsv(
        "xian\t先\t50\nxian\t线\t40\nxian\t县\t30\nxian\t现\t20\nxian\t鲜\t10\nxi'an\t西安\t1000\n",
    ).unwrap());
    let mut profile = Profile::default();
    for _ in 0..20 {
        assert!(profile.record("xian", "西安"));
    }
    for text in ["县", "现", "鲜"] {
        assert!(profile.record("xian", text));
    }
    for options in [0, OPTIONS_MASK] {
        let mut session = Session::new(Arc::clone(&dictionary));
        session.configure(5, true, false);
        session.configure_matching(options);
        session.set_profile(Arc::new(profile.clone()));
        for _ in 0..2 {
            type_keys(&mut session, "xian");
            assert_eq!(session.candidate(0).unwrap().text, "鲜");
            assert_eq!(session.candidate(1).unwrap().text, "现");
            assert_eq!(session.candidate(2).unwrap().text, "先");
            session.process(Key::PageDown, Modifiers::default());
            assert!((0..session.candidate_count())
                .any(|i| session.candidate(i).unwrap().text == "西安"));
            session.reset();
        }
    }
}

#[test]
fn explicit_separator_and_remaining_suffix_keep_their_own_intent() {
    let dictionary = Arc::new(Dictionary::from_tsv(
        "bao\t保\t30\nshi\t是\t50\nshi\t时\t40\nbao'shi\t宝石\t100\nshi'jie\t世界\t90\nxian\t先\t50\nxi'an\t西安\t100\n",
    ).unwrap());
    for options in [0, OPTIONS_MASK] {
        let mut session = Session::new(Arc::clone(&dictionary));
        session.configure(5, true, false);
        session.configure_incremental(true);
        session.configure_matching(options);
        type_keys(&mut session, "xi'an");
        assert_eq!(session.candidate(0).unwrap().text, "西安");
        session.reset();
        type_keys(&mut session, "baoshi");
        assert_eq!(session.candidate(0).unwrap().text, "宝石");
        let prefix = (0..session.candidate_count())
            .find(|&i| session.candidate(i).unwrap().text == "保")
            .unwrap();
        session.process(Key::Select(prefix), Modifiers::default());
        assert_eq!(session.commit(), "保");
        assert_eq!(session.preedit(), "shi");
        assert_eq!(session.candidate(0).unwrap().text, "是");
        assert_eq!(session.candidate_consumed(0), Some(3));
        session.process(Key::Select(0), Modifiers::default());
        assert_eq!(session.commit(), "是");
        assert_eq!(session.preedit(), "");
        session.reset();
        type_keys(&mut session, "shi");
        // Windows maps Space to the ABI raw-commit action; legacy core Space
        // continues selecting a candidate for existing Linux/ABI callers.
        session.process(Key::Enter, Modifiers::default());
        assert_eq!(session.commit(), "shi");
        session.reset();
        type_keys(&mut session, "xian'");
        assert_eq!(session.candidate(0).unwrap().text, "先");
    }
}
