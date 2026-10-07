use chengyin_core::{fuzzy::*, Dictionary, Key, Modifiers, Profile, Session};
use std::sync::Arc;

fn replay(dictionary: Arc<Dictionary>, input: &str, options: u32) -> Session {
    let mut session = Session::new(dictionary);
    assert!(session.configure_matching(options));
    for letter in input.chars() {
        assert!(
            session
                .process(Key::Character(letter), Modifiers::default())
                .handled
        );
    }
    assert_eq!(session.preedit(), input);
    session
}

fn complete_rows(session: &mut Session, raw: &str) -> Vec<(String, u32)> {
    let mut rows = Vec::new();
    loop {
        for i in 0..session.candidate_count() {
            if session.candidate_consumed(i) == Some(raw.len()) {
                let c = session.candidate(i).unwrap();
                rows.push((c.text.to_owned(), c.frequency));
            }
        }
        if !session.has_next_page() {
            break;
        }
        assert!(rows.len() < 4096);
        session.process(Key::PageDown, Modifiers::default());
    }
    rows
}

#[test]
fn complete_terms_are_not_diluted_by_split_homophone_sentences() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in [
        0,
        PHONETIC_MASK,
        SWAP | OMIT | NEIGHBOR | REPEAT,
        OPTIONS_MASK,
    ] {
        for (raw, wanted) in [
            ("zhengzebiaodashi", "正则表达式"),
            ("zheng'ze'biao'da'shi", "正则表达式"),
            ("rengongzhineng", "人工智能"),
            ("caozuoxitong", "操作系统"),
        ] {
            let mut s = replay(Arc::clone(&dictionary), raw, flags);
            assert_eq!(s.candidate(0).unwrap().text, wanted, "{raw}, {flags}");
            let rows = complete_rows(&mut s, raw);
            assert!(
                rows.iter().all(|(_, frequency)| *frequency > 0),
                "{raw}: synthesized term alternatives {rows:?}"
            );
        }
    }
}

#[test]
fn supported_long_compositions_suppress_weak_alternative_phrase_joins() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in [0, OPTIONS_MASK] {
        for (raw, wanted) in [
            ("jisuanjikexue", "计算机科学"),
            ("zhonghuarenmingongheguo", "中华人民共和国"),
            ("woxihuanzhongwen", "我喜欢中文"),
            ("womenmingtianjian", "我们明天见"),
        ] {
            let mut s = replay(Arc::clone(&dictionary), raw, flags);
            assert_eq!(s.candidate(0).unwrap().text, wanted, "{raw}, {flags}");
            let rows = complete_rows(&mut s, raw);
            assert!(
                !rows.iter().any(|(text, _)| text == "计算计科学"),
                "{rows:?}"
            );
        }
    }
}

#[test]
fn corrected_technical_terms_remain_available_with_letter_marks() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for (raw, flags) in [("zhengzebiaodahsi", SWAP), ("zengzebiaodasi", 1 | 1 << 2)] {
        let s = replay(Arc::clone(&dictionary), raw, flags);
        let i = (0..s.candidate_count())
            .find(|&i| s.candidate(i).unwrap().text == "正则表达式")
            .expect("corrected technical term");
        assert!(s.candidate_marks(i).unwrap().iter().any(|&n| n != 0));
    }
}

#[test]
fn unknown_phrase_joins_offer_one_exact_fallback_instead_of_homophone_products() {
    let dictionary = Arc::new(
        Dictionary::from_tsv(
            "mo'xing\t模型\t100\nmo'xing\t魔性\t10\nshi'yan\t实验\t100\nshi'yan\t誓言\t10\n",
        )
        .unwrap(),
    );
    for flags in [0, OPTIONS_MASK] {
        let mut s = replay(Arc::clone(&dictionary), "moxingshiyan", flags);
        let rows = complete_rows(&mut s, "moxingshiyan");
        assert_eq!(
            rows,
            [("模型实验".to_owned(), 0)],
            "only the strongest exact fallback survives: {flags}"
        );
    }
}

#[test]
fn one_off_unknown_history_cannot_override_terms_but_repeated_intent_can() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in [0, OPTIONS_MASK] {
        for count in [1, 2, 3] {
            let mut p = Profile::default();
            for _ in 0..count {
                p.record("zhengzebiaodashi", "正则表达是");
            }
            let bytes = p.to_binary();
            let history = Arc::new(Profile::from_binary(&bytes).unwrap());
            let mut s = Session::new(Arc::clone(&dictionary));
            s.set_profile(Arc::clone(&history));
            s.configure_matching(flags);
            for c in "zhengzebiaodashi".chars() {
                s.process(Key::Character(c), Modifiers::default());
            }
            assert_eq!(
                s.candidate(0).unwrap().text,
                if count < 3 {
                    "正则表达式"
                } else {
                    "正则表达是"
                }
            );
            assert_eq!(
                history.to_binary(),
                bytes,
                "query must not delete probationary history"
            );
        }
    }
}

#[test]
fn boundaries_reject_unsupported_characters_and_keep_attested_sentence_frames() {
    // No whole-word entry for the sentence: its joining evidence is lexical.
    let source = "ruan'jian\t软件\t100\nshe'ji\t设计\t100\nwan'cheng\t完成\t100\nshe\t设\t99999\nshe\t社\t99998\nji\t计\t99999\nji\t集\t99998\njian'she\t建设\t1\nshe'ji'wan'cheng\t设计完成\t1\n";
    let dictionary = Arc::new(Dictionary::from_tsv(source).unwrap());
    for flags in [0, OPTIONS_MASK] {
        let mut s = replay(Arc::clone(&dictionary), "ruanjiansheji", flags);
        assert_eq!(s.candidate(0).unwrap().text, "软件设计");
        let rows = complete_rows(&mut s, "ruanjiansheji");
        assert!(
            !rows
                .iter()
                .any(|(text, _)| text.contains('社') || text.ends_with('集')),
            "{rows:?}"
        );
    }
    // Crossing witnesses cannot be fabricated from a word wholly on one side.
    let d = Arc::new(Dictionary::from_tsv("ruan'jian\t软件\t100\nshe\t设\t99999\n").unwrap());
    let mut s = replay(d, "ruanjianshe", OPTIONS_MASK);
    assert!(!complete_rows(&mut s, "ruanjianshe")
        .iter()
        .any(|(text, _)| text == "软件设"));
}

#[test]
fn unobserved_legacy_history_and_poor_recent_hits_do_not_receive_promotion() {
    fn crc32(bytes: &[u8]) -> u32 {
        let mut crc = !0u32;
        for &b in bytes {
            crc ^= u32::from(b);
            for _ in 0..8 {
                crc = (crc >> 1) ^ if crc & 1 != 0 { 0xedb88320 } else { 0 };
            }
        }
        !crc
    }
    let key = "zhengzebiaodashi";
    let text = "正则表达是";
    let mut legacy = b"MSWYUSR1".to_vec();
    for n in [1u32, 1, 0] {
        legacy.extend(n.to_le_bytes());
    }
    legacy.extend((key.len() as u16).to_le_bytes());
    legacy.extend((text.len() as u16).to_le_bytes());
    for n in [1u32, 1] {
        legacy.extend(n.to_le_bytes());
    }
    legacy.extend(key.as_bytes());
    legacy.extend(text.as_bytes());
    let crc = crc32(&legacy[20..]);
    legacy[16..20].copy_from_slice(&crc.to_le_bytes());
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    let mut p = Profile::from_binary(&legacy).unwrap();
    for count in [0, 2] {
        for _ in 0..count {
            p.record(key, text);
        }
        if count == 2 {
            for _ in 0..8 {
                p.record_selection(key, "正则表达式", 0);
            }
        }
        let mut s = Session::new(Arc::clone(&dictionary));
        s.set_profile(Arc::new(p.clone()));
        s.configure_matching(OPTIONS_MASK);
        for c in key.chars() {
            s.process(Key::Character(c), Modifiers::default());
        }
        assert_eq!(s.candidate(0).unwrap().text, "正则表达式");
        assert_eq!(
            p.entry_count(),
            if count == 0 { 1 } else { 2 },
            "demotion does not prematurely forget data"
        );
    }
}

#[test]
fn daily_mapping_words_precede_and_exclude_unattested_character_pairs() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for options in [
        0,
        1 << 8,
        PHONETIC_MASK,
        SWAP | OMIT | NEIGHBOR | REPEAT,
        OPTIONS_MASK,
    ] {
        for raw in ["yingshe", "ying'she", "yinshe", "yin'she"] {
            let mut session = replay(Arc::clone(&dictionary), raw, options);
            let first: Vec<_> = (0..session.candidate_count())
                .map(|i| session.candidate(i).unwrap().text.to_owned())
                .collect();
            println!("{raw}, flags={options}: {first:?}");
            if raw.starts_with("ying") || options == 1 << 8 {
                assert_eq!(
                    first[0], "映射",
                    "whole exact dictionary word must outrank synthesis"
                );
                assert_eq!(first[1], "影射");
            }
            if options == OPTIONS_MASK {
                assert!(
                    first.iter().position(|w| w == "映射").unwrap()
                        < first
                            .iter()
                            .position(|w| w == "听着")
                            .unwrap_or(first.len()),
                    "one fuzzy change must precede a fuzzy change plus keyboard error"
                );
            }
            let mut count = 0;
            loop {
                for i in 0..session.candidate_count() {
                    let word = session.candidate(i).unwrap();
                    if options != 0 {
                        assert!(
                            word.frequency > 0,
                            "short full spelling must use attested words: {}",
                            word.text
                        );
                        assert_eq!(session.candidate_consumed(i), Some(raw.len()));
                    }
                    assert!(
                        ![
                            "应设", "应社", "应射", "应蛇", "应舍", "应摄", "赢设", "应舌", "英设",
                            "因设", "因社", "因射", "因蛇", "因舍", "因摄", "因舌", "引设", "银设"
                        ]
                        .contains(&word.text),
                        "{raw}: bogus pair {}",
                        word.text
                    );
                    count += 1;
                }
                if !session.has_next_page() {
                    break;
                }
                assert!(count < 4096, "paging must terminate");
                session.process(Key::PageDown, Modifiers::default());
            }
        }
    }
}

#[test]
fn corrected_words_survive_without_cross_product_of_isolated_characters() {
    let dictionary = Arc::new(Dictionary::from_tsv(
        "ying\t应\t90000\nying\t赢\t80000\nshe\t设\t90000\nshe\t社\t80000\nying'she\t映射\t1000\nying'she\t影射\t500\n",
    ).unwrap());
    for (raw, options) in [
        ("yinshe", 1 << 8),
        ("yinshe", OMIT),
        ("yin'she", OMIT),
        ("yingsh", OMIT),
        ("yignshe", SWAP),
        ("yinghse", SWAP),
        ("yiingshe", REPEAT),
        ("yingshr", NEIGHBOR),
    ] {
        let mut session = replay(Arc::clone(&dictionary), raw, options);
        assert_eq!(session.candidate(0).unwrap().text, "映射", "{raw}");
        assert!(session.candidate_marks(0).unwrap().iter().any(|&m| m != 0));
        loop {
            for i in 0..session.candidate_count() {
                let word = session.candidate(i).unwrap();
                if session.candidate_consumed(i) == Some(raw.len()) {
                    assert!(
                        ["映射", "影射"].contains(&word.text),
                        "{raw}: {}",
                        word.text
                    );
                }
            }
            if !session.has_next_page() {
                break;
            }
            session.process(Key::PageDown, Modifiers::default());
        }
    }
}

#[test]
fn word_and_character_pipelines_have_separate_history_and_accuracy_tiers() {
    let exact_chars = [
        "黎", "梨", "理", "礼", "李", "里", "力", "利", "立", "丽", "粒", "莉", "离",
    ];
    let mut source = "li'zi\t栗子\t5000\nli'zi\t例子\t2000\nli'zi\t李子\t1000\nzi\t子\t1000\nni'zi\t腻子\t90000\nni'zi\t妮子\t500\nni'zi\t逆子\t300\nni\t你\t700\nni\t尼\t600\n".to_owned();
    for (i, text) in exact_chars.iter().enumerate() {
        source.push_str(&format!("li\t{text}\t{}\n", 900 - i));
    }
    let dictionary = Arc::new(Dictionary::from_tsv(&source).unwrap());
    let mut history = Profile::default();
    for (key, text, count) in [
        ("lizi", "例子", 3),
        ("lizi", "李子", 2),
        ("lizi", "利兹", 1),
        ("nizi", "妮子", 6),
        ("nizi", "逆子", 4),
        ("nizi", "拟字", 1),
    ] {
        for _ in 0..count {
            assert!(history.record(key, text));
        }
    }
    let history = Arc::new(Profile::from_binary(&history.to_binary()).unwrap());
    let before = history.to_binary();
    let mut session = Session::new(Arc::clone(&dictionary));
    assert!(session.set_profile(Arc::clone(&history)));
    assert!(session.configure_matching(1 << 3));
    for c in "lizi".chars() {
        session.process(Key::Character(c), Modifiers::default());
    }
    let mut rows = Vec::new();
    loop {
        for i in 0..session.candidate_count() {
            rows.push(session.candidate(i).unwrap().text.to_owned());
        }
        if !session.has_next_page() {
            break;
        }
        session.process(Key::PageDown, Modifiers::default());
    }
    assert_eq!(&rows[..3], ["例子", "李子", "栗子"]);
    assert!(
        !rows
            .iter()
            .any(|text| ["利兹", "拟字"].contains(&text.as_str())),
        "only two promotions from each history tier"
    );
    let at = |text: &str| rows.iter().position(|row| row == text).unwrap();
    for text in exact_chars {
        assert!(
            !rows.iter().any(|r| r == text),
            "prefix character {text} must not enter whole-word recall"
        );
    }
    assert!(at("妮子") < at("逆子") && at("逆子") < at("腻子"));
    assert!(!rows.iter().any(|r| ["你", "尼"].contains(&r.as_str())));
    assert_eq!(
        history.to_binary(),
        before,
        "recall does not mutate history"
    );
    let independent = replay(dictionary, "lizi", 1 << 3);
    assert_eq!(independent.candidate(0).unwrap().text, "栗子");
    session.reset();
    assert!(session.configure(9, false, false));
    for c in "lizi".chars() {
        session.process(Key::Character(c), Modifiers::default());
    }
    assert_eq!(
        session.candidate(0).unwrap().text,
        "栗子",
        "learning off disables both history tiers"
    );
    let mut char_history = Profile::default();
    for (key, text, count) in [
        ("li", "李", 3),
        ("li", "黎", 2),
        ("li", "锂", 1),
        ("ni", "你", 6),
        ("ni", "尼", 4),
        ("ni", "倪", 1),
    ] {
        for _ in 0..count {
            char_history.record(key, text);
        }
    }
    let mut char_source = source.clone();
    char_source.push_str("ni\t泥\t90000\n");
    let mut single = Session::new(Arc::new(Dictionary::from_tsv(&char_source).unwrap()));
    single.set_profile(Arc::new(
        Profile::from_binary(&char_history.to_binary()).unwrap(),
    ));
    single.configure_matching(1 << 3);
    for c in "li".chars() {
        single.process(Key::Character(c), Modifiers::default());
    }
    let mut rows = Vec::new();
    loop {
        for i in 0..single.candidate_count() {
            let c = single.candidate(i).unwrap();
            assert_eq!(
                c.text.chars().count(),
                1,
                "word completions must not enter single-character recall"
            );
            assert_eq!(single.candidate_consumed(i), Some(2));
            rows.push(c.text.to_owned());
        }
        if !single.has_next_page() {
            break;
        }
        single.process(Key::PageDown, Modifiers::default());
    }
    assert_eq!(&rows[..2], ["李", "黎"]);
    let at = |text: &str| rows.iter().position(|r| r == text).unwrap();
    for c in exact_chars {
        assert!(at(c) < at("你"));
    }
    assert!(at("你") >= 9 && at("你") < at("尼") && at("尼") < at("泥"));
    assert!(!rows.iter().any(|r| ["锂", "倪"].contains(&r.as_str())));
}
