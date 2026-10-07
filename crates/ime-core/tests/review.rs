use chengyin_core::{fuzzy::OPTIONS_MASK, Dictionary, Key, Modifiers, Profile, Session};
use std::sync::Arc;

fn replay(source: &str, raw: &str, flags: u32) -> Session {
    let mut s = Session::new(Arc::new(Dictionary::from_tsv(source).unwrap()));
    s.configure(5, true, false);
    s.configure_matching(flags);
    s.configure_incremental(true);
    for c in raw.chars() {
        assert!(s.process(Key::Character(c), Modifiers::default()).handled);
    }
    s
}

#[test]
fn explicit_digraph_syllables_are_not_reinterpreted_as_four_single_heads() {
    let source = "zhao'ren'min\t照人民\t100\nzou'huo'ru'mo\t走火入魔\t1\n";
    let plain = replay(source, "zhrm", 0);
    assert_eq!(plain.candidate(0).unwrap().text, "走火入魔");
    let separated = replay(source, "zh'r'm", 0);
    assert_eq!(separated.candidate(0).unwrap().text, "照人民");
}

#[test]
fn word_length_disambiguates_unseparated_dictionary_pronunciation() {
    // ba'ban and ba'ba'n both parse; only the latter fits three characters.
    // This synthetic spelling tests the index, not vocabulary quality.
    let source = "baban\t巴巴嗯\t1\nba\t巴\t1000\nn\t嗯\t1000\n";
    for flags in [0, OPTIONS_MASK] {
        let s = replay(source, "bbn", flags);
        assert_eq!(s.candidate(0).unwrap().text, "巴巴嗯");
        assert_eq!(s.candidate(0).unwrap().frequency, 1);
        assert!(!s.budget_limited());
    }
    let bytes = Dictionary::from_tsv(source).unwrap().to_binary();
    let dictionary = Arc::new(Dictionary::from_binary(&bytes).unwrap());
    let mut s = Session::new(dictionary);
    for c in "bbn".chars() {
        s.process(Key::Character(c), Modifiers::default());
    }
    assert_eq!(s.candidate(0).unwrap().text, "巴巴嗯");
}

#[test]
fn zero_onset_initials_are_supported_without_reinterpreting_full_pinyin() {
    let source = "xi'an'gang\t西安港\t1\nxi\t西\t1000\nan\t安\t1000\ngang\t港\t1000\n";
    for flags in [0, OPTIONS_MASK] {
        let s = replay(source, "xag", flags);
        assert_eq!(s.candidate(0).unwrap().text, "西安港");
        assert_eq!(s.candidate(0).unwrap().frequency, 1);
        assert!(!s.budget_limited());
    }
    let source = "ai'qing'dian'ying\t爱情电影\t1\nni'hao\t你好\t100\nneng'er'hao'ao\t能儿好奥\t1\n";
    assert_eq!(
        replay(source, "aqdy", OPTIONS_MASK)
            .candidate(0)
            .unwrap()
            .text,
        "爱情电影"
    );
    assert_eq!(
        replay(source, "nihao", OPTIONS_MASK)
            .candidate(0)
            .unwrap()
            .text,
        "你好"
    );
}

#[test]
fn trailing_separator_does_not_prevent_whole_word_learning() {
    for flags in [0, OPTIONS_MASK] {
        let mut s = replay("ni'hao\t你好\t100\n", "nihao'", flags);
        assert_eq!(s.candidate_consumed(0), Some(6));
        assert!(s.process(Key::Select(0), Modifiers::default()).handled);
        assert_eq!(s.commit(), "你好");
        assert!(s.preedit().is_empty());
        assert_eq!(s.learning_key(), "nihao");
        assert!(s.learn_commit());
        assert_eq!(s.profile().entry_count(), 1);
        assert_eq!(
            Profile::from_binary(&s.profile().to_binary())
                .unwrap()
                .entry_count(),
            1
        );
    }
}
