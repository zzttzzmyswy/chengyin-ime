use myswy_core::{fuzzy::*, Dictionary, Key, Modifiers, Profile, Session};
use std::sync::Arc;
fn type_keys(s: &mut Session, raw: &str) {
    for c in raw.chars() {
        assert!(s.process(Key::Character(c), Modifiers::default()).handled);
    }
    assert_eq!(s.preedit(), raw);
}
fn session(d: Arc<Dictionary>, flags: u32) -> Session {
    let mut s = Session::new(d);
    assert!(s.configure(5, true, false));
    assert!(s.configure_incremental(true));
    assert!(s.configure_matching(flags));
    s
}
#[test]
fn production_four_heads_precede_shorter_corrections_under_every_rule() {
    let d =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    let flags = [
        0,
        PHONETIC_MASK,
        SWAP | OMIT | NEIGHBOR | REPEAT,
        OPTIONS_MASK,
    ];
    for flags in flags
        .into_iter()
        .chain((0..11).map(|bit| 1 << bit))
        .chain([SWAP, OMIT, NEIGHBOR, REPEAT])
    {
        for (raw, wanted) in [
            ("ssdd", "世世代代"),
            ("s's'd'd", "世世代代"),
            ("zgrm", "中国人民"),
            ("zhrm", "走火入魔"),
        ] {
            let mut s = session(Arc::clone(&d), flags);
            type_keys(&mut s, raw);
            assert_eq!(s.candidate(0).unwrap().text, wanted, "{raw}/{flags}");
            assert!(!s.budget_limited(), "{raw}/{flags}");
            for i in 0..s.candidate_count() {
                assert_eq!(s.candidate(i).unwrap().text.chars().count(), 4);
                assert!(s.candidate(i).unwrap().frequency > 0);
                assert_eq!(s.candidate_consumed(i), Some(raw.len()));
                assert_eq!(s.candidate_marks(i), Some([0; 4]));
            }
            s.process(Key::Select(0), Modifiers::default());
            assert_eq!(s.commit(), wanted);
            assert_eq!(s.preedit(), "");
            assert!(s.learn_commit());
            s.reset();
            type_keys(&mut s, raw);
            assert_eq!(s.candidate(0).unwrap().text, wanted);
            assert_eq!(s.candidate(0).unwrap().pinyin.split('\'').count(), 4);
            assert_eq!(s.candidate_marks(0), Some([0; 4]));
        }
    }
}
#[test]
fn complete_initials_keep_all_137_homophones_across_pages() {
    let mut source = String::from("she'de\t舍得\t100000\nsha'de\t杀得\t90000\n");
    let mut wanted = Vec::new();
    for i in 0..137 {
        let word = format!("词{}世界", char::from_u32(0x4e00 + i).unwrap());
        source.push_str(&format!("shi'shi'dai'dai\t{word}\t{}\n", 137 - i));
        wanted.push(word);
    }
    let binary = Dictionary::from_tsv(&source).unwrap().to_binary();
    let d = Arc::new(Dictionary::from_binary(&binary).unwrap());
    for flags in [0, OPTIONS_MASK] {
        let mut s = session(Arc::clone(&d), flags);
        type_keys(&mut s, "ssdd");
        let mut got = Vec::new();
        loop {
            for i in 0..s.candidate_count() {
                got.push(s.candidate(i).unwrap().text.to_owned());
                assert_eq!(s.candidate_marks(i), Some([0; 4]));
            }
            if !s.has_next_page() {
                break;
            }
            assert!(got.len() < 137);
            s.process(Key::PageDown, Modifiers::default());
        }
        assert_eq!(got, wanted);
    }
}
#[test]
fn eligible_history_is_capped_and_shorter_history_cannot_displace_whole_word() {
    let d = Arc::new(Dictionary::from_tsv("shi'shi'dai'dai\t世世代代\t1\nshi'shi'dai'dai\t时时待待\t2\nshi'shi'dai'dai\t试试带带\t3\nshe'de\t舍得\t1000\n").unwrap());
    let mut p = Profile::default();
    p.record("ssdd", "世世代代");
    for _ in 0..4 {
        p.record("ssdd", "时时待待");
    }
    for _ in 0..3 {
        p.record("ssdd", "试试带带");
    }
    for _ in 0..30 {
        p.record("ssdd", "舍得");
    }
    for flags in [0, OPTIONS_MASK] {
        let mut s = session(Arc::clone(&d), flags);
        s.set_profile(Arc::new(p.clone()));
        for _ in 0..2 {
            type_keys(&mut s, "ssdd");
            let words: Vec<_> = (0..s.candidate_count())
                .map(|i| s.candidate(i).unwrap().text.to_owned())
                .collect();
            assert_eq!(words, ["时时待待", "试试带带", "世世代代"]);
            s.reset();
        }
    }
}
#[test]
fn absent_full_initial_word_still_allows_shorter_fallback_and_dictionary_refresh() {
    let full = Arc::new(
        Dictionary::from_tsv("shi'shi'dai'dai\t世世代代\t1\nshe'de\t舍得\t1000\n").unwrap(),
    );
    let shorter = Arc::new(Dictionary::from_tsv("she'de\t舍得\t1000\n").unwrap());
    let mut s = session(full, OPTIONS_MASK);
    type_keys(&mut s, "ssdd");
    assert_eq!(s.candidate(0).unwrap().text, "世世代代");
    s.process(Key::Backspace, Modifiers::default());
    s.process(Key::Character('d'), Modifiers::default());
    assert_eq!(s.candidate(0).unwrap().text, "世世代代");
    s.reset();
    assert!(s.set_dictionary(shorter));
    type_keys(&mut s, "ssdd");
    assert_eq!(s.candidate(0).unwrap().text, "舍得");
    assert_ne!(s.candidate_marks(0), Some([0; 4]));
}
#[test]
fn arbitrary_length_and_unseparated_canonical_spellings_use_valid_syllables() {
    let long_word = "你".repeat(40);
    let source = format!("ni'hao\t你好\t100\nni'hao'shi\t你好诗\t90\nnihaoshijie\t你好世界\t80\nni'hao'shi'jie'hao\t你好世界好\t70\n{}\t{long_word}\t1\nssdd\t伪造词条\t9999\n", "ni".repeat(40));
    let d = Arc::new(Dictionary::from_tsv(&source).unwrap());
    let long_raw = "n".repeat(40);
    for (raw, word) in [
        ("nh", "你好"),
        ("nhs", "你好诗"),
        ("nhsj", "你好世界"),
        ("nhsjh", "你好世界好"),
        (long_raw.as_str(), long_word.as_str()),
    ] {
        let mut s = session(Arc::clone(&d), OPTIONS_MASK);
        type_keys(&mut s, raw);
        assert_eq!(s.candidate(0).unwrap().text, word);
        assert!(!s.budget_limited());
    }
}
#[test]
fn full_pinyin_and_typo_marks_remain_independent_of_initials_priority() {
    let d = Arc::new(
        Dictionary::from_tsv("zhang\t张\t100\nni'hao\t你好\t100\nshi'shi'dai'dai\t世世代代\t10\n")
            .unwrap(),
    );
    let mut s = session(d, OPTIONS_MASK);
    type_keys(&mut s, "ssdd");
    s.reset();
    type_keys(&mut s, "zhnag");
    assert_eq!(s.candidate(0).unwrap().text, "张");
    assert_ne!(s.candidate_marks(0), Some([0; 4]));
    s.reset();
    type_keys(&mut s, "nihao");
    assert_eq!(s.candidate(0).unwrap().text, "你好");
    assert_eq!(s.candidate_marks(0), Some([0; 4]));
}
