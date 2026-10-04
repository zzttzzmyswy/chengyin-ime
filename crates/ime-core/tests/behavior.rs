use myswy_core::*;
use std::sync::Arc;

fn words(dictionary: &Dictionary, query: &str) -> Vec<String> {
    let results = dictionary.lookup(query).unwrap();
    (0..results.len())
        .map(|i| results.get(dictionary, i).unwrap().text.to_owned())
        .collect()
}
fn type_keys(session: &mut Session, input: &str) {
    for c in input.chars() {
        assert_eq!(
            session.process(Key::Character(c), Modifiers::default()),
            ProcessResult {
                handled: true,
                limited: false
            }
        );
    }
}
fn press(session: &mut Session, key: Key) -> ProcessResult {
    session.process(key, Modifiers::default())
}

#[test]
fn exact_before_completion_and_deterministic_top_nine() {
    let d = demo_dictionary();
    assert_eq!(words(&d, "ni")[0], "你"); // lower frequency than 你好, but exact
    assert_eq!(words(&d, "nihao")[0], "你好");
    assert_eq!(words(&d, "NIHAO"), words(&d, "nihao"));
    assert_eq!(words(&d, "shi").len(), MAX_CANDIDATES);
    assert_eq!(&words(&d, "shi")[..3], ["是", "时", "事"]);
    assert!(words(&d, "").is_empty());
    assert!(words(&d, "vvvv").is_empty());
}

#[test]
fn explicit_and_optional_syllable_boundaries() {
    let d = demo_dictionary();
    assert_eq!(words(&d, "xian"), ["先", "西安", "线"]);
    assert_eq!(words(&d, "xi'an"), ["西安"]);
    for query in [
        "zhongguoren",
        "zhong'guoren",
        "zhongguo'ren",
        "zhong'guo'ren",
    ] {
        assert_eq!(words(&d, query)[0], "中国人", "{query}");
    }
    assert_eq!(words(&d, "ni'")[0], "你好");
    assert_eq!(words(&d, "nv'er")[0], "女儿");
    assert!(words(&d, "n'ihao").is_empty());
    for invalid in ["'ni", "ni''hao", "你好", "ni hao", "ni\0hao"] {
        assert_eq!(d.lookup(invalid).unwrap_err(), LookupError::InvalidInput);
    }
}

#[test]
fn selection_commit_lifetime_and_editing() {
    let mut s = Session::new(demo_dictionary());
    type_keys(&mut s, "nihaox");
    assert!(s.candidate_count() > 0); // final syllable completion for continuous input
    assert_eq!(s.preedit(), "nihaox");
    press(&mut s, Key::Backspace);
    assert_eq!(s.preedit(), "nihao");
    assert_eq!(s.candidate(0).unwrap().text, "你好");
    press(&mut s, Key::Space);
    assert_eq!(s.commit(), "你好");
    assert!(s.preedit().is_empty());
    assert!(s.is_association());
    assert!(!press(&mut s, Key::Space).handled);
    assert!(s.commit().is_empty());
    type_keys(&mut s, "shi");
    press(&mut s, Key::Down);
    press(&mut s, Key::Space);
    assert_eq!(s.commit(), "时");
    s.reset();
    type_keys(&mut s, "shi");
    press(&mut s, Key::Character('3'));
    assert_eq!(s.commit(), "事");
    type_keys(&mut s, "nihao");
    press(&mut s, Key::Enter);
    assert_eq!(s.commit(), "nihao");
    type_keys(&mut s, "vvvv");
    press(&mut s, Key::Space);
    assert_eq!(s.commit(), "vvvv");
}

#[test]
fn shortcuts_focus_and_passthrough_do_not_lose_or_repeat_commits() {
    let mut s = Session::new(demo_dictionary());
    for key in [
        Key::Backspace,
        Key::Enter,
        Key::Escape,
        Key::Character('3'),
        Key::Up,
    ] {
        assert!(!press(&mut s, key).handled);
    }
    type_keys(&mut s, "ni");
    for m in [
        Modifiers {
            control: true,
            ..Default::default()
        },
        Modifiers {
            alt: true,
            ..Default::default()
        },
        Modifiers {
            super_key: true,
            ..Default::default()
        },
    ] {
        assert!(!s.process(Key::Character('a'), m).handled);
        assert_eq!(s.preedit(), "ni");
    }
    assert!(!press(&mut s, Key::Character(',')).handled);
    assert_eq!(s.commit(), "你"); // must be committed before forwarding comma
    assert!(!press(&mut s, Key::Other).handled);
    assert_eq!(s.commit(), "");
    type_keys(&mut s, "ni");
    press(&mut s, Key::Escape);
    assert!(s.preedit().is_empty());
    assert!(s.commit().is_empty());
    type_keys(&mut s, "hao");
    s.reset();
    assert!(s.preedit().is_empty());
    assert_eq!(s.candidate_count(), 0);
}

#[test]
fn input_and_selection_limits_preserve_state() {
    let mut s = Session::new(demo_dictionary());
    type_keys(&mut s, &"a".repeat(MAX_INPUT_BYTES));
    assert!(press(&mut s, Key::Character('a')).limited);
    assert_eq!(s.preedit().len(), MAX_INPUT_BYTES);
    press(&mut s, Key::Enter);
    assert_eq!(s.commit(), "a".repeat(MAX_INPUT_BYTES));
    type_keys(&mut s, "ni");
    assert!(press(&mut s, Key::Select(usize::MAX)).handled);
    assert_eq!(s.preedit(), "ni");
    assert!(s.commit().is_empty());
}

#[test]
fn dictionaries_are_shared_but_sessions_are_independent() {
    let d = demo_dictionary();
    let handles: Vec<_> = (0..8)
        .map(|i| {
            let d = Arc::clone(&d);
            std::thread::spawn(move || {
                let mut s = Session::new(d);
                type_keys(&mut s, if i % 2 == 0 { "nihao" } else { "zhongguo" });
                press(&mut s, Key::Space);
                s.commit().to_owned()
            })
        })
        .collect();
    for (i, handle) in handles.into_iter().enumerate() {
        assert_eq!(
            handle.join().unwrap(),
            if i % 2 == 0 { "你好" } else { "中国" }
        );
    }
}

#[test]
fn dictionary_switch_preserves_active_composition_and_last_commit() {
    let replacement = Arc::new(Dictionary::from_tsv("ni'hao\t新候选\t10\n").unwrap());
    let mut session = Session::new(demo_dictionary());
    type_keys(&mut session, "nihao");
    assert!(!session.set_dictionary(Arc::clone(&replacement)));
    assert_eq!(session.preedit(), "nihao");
    assert_eq!(session.candidate(0).unwrap().text, "你好");
    press(&mut session, Key::Space);
    assert!(session.set_dictionary(replacement));
    assert_eq!(session.commit(), "你好");
    type_keys(&mut session, "nihao");
    press(&mut session, Key::Space);
    assert_eq!(session.commit(), "新候选");
}

#[test]
fn malformed_dictionary_is_rejected_without_panics() {
    for source in [
        "",
        "# comment\n",
        "ni\t你",
        "ni\t你\t0",
        "ni\t你\t4294967296",
        "NI\t你\t1",
        "'ni\t你\t1",
        "ni'\t你\t1",
        "ni''hao\t你好\t1",
        "ni\t\t1",
        "ni\t你\0\t1",
        "ni\t你\t1\nni\t你\t2",
    ] {
        assert!(Dictionary::from_tsv(source).is_err(), "{source:?}");
    }
    assert!(Dictionary::from_tsv("ni\t你\t1\r\nhao\t好\t2\r\n").is_ok());
    assert!(Dictionary::from_tsv(&format!("{}\t长\t1", "a".repeat(256))).is_err());
    assert!(Dictionary::from_tsv(&format!("a\t{}\t1", "长".repeat(86))).is_err());
}

#[test]
fn duplicate_text_is_ranked_once_even_across_pronunciations() {
    let d = Dictionary::from_tsv("xian\t先\t10\nxi'an\t先\t30\nxi'an\t西安\t20\nxian'a\t先啊\t100")
        .unwrap();
    assert_eq!(words(&d, "xian"), ["先", "西安", "先啊"]);
    assert_eq!(words(&d, "xi'an"), ["先", "西安"]);
}

#[test]
fn ambiguity_limit_is_explicit_and_session_rolls_back() {
    let mut source = String::new();
    for mask in 0..512 {
        let mut key = String::from("a");
        for bit in 0..9 {
            if mask & (1 << bit) != 0 {
                key.push('\'');
            }
            key.push('a');
        }
        source.push_str(&format!("{key}\t词{mask}\t1\n"));
    }
    let d = Arc::new(Dictionary::from_tsv(&source).unwrap());
    assert_eq!(
        d.lookup("aaaaaaaaaa").unwrap_err(),
        LookupError::TooAmbiguous
    );
    let mut s = Session::new(d);
    type_keys(&mut s, "aaaaaaaaa");
    let first = s.candidate(0).unwrap().text.to_owned();
    assert!(press(&mut s, Key::Character('a')).limited);
    assert_eq!(s.preedit(), "aaaaaaaaa");
    assert_eq!(s.candidate(0).unwrap().text, first);
}

#[test]
fn trie_matches_exhaustive_reference_for_generated_dictionary() {
    // Independent scan oracle: required separators must occur; omitted ones are skipped.
    fn match_key(key: &str, query: &str) -> Option<bool> {
        let mut canonical = key.bytes().peekable();
        for q in query.bytes() {
            if q != b'\'' && canonical.peek() == Some(&b'\'') {
                canonical.next();
            }
            if canonical.next() != Some(q) {
                return None;
            }
        }
        Some(canonical.next().is_none())
    }
    let mut entries = Vec::new();
    let mut source = String::new();
    for i in 0..300usize {
        let key = format!(
            "{}{}'{}{}",
            (b'a' + (i % 7) as u8) as char,
            (b'a' + (i / 7 % 7) as u8) as char,
            (b'a' + (i / 49 % 7) as u8) as char,
            (b'a' + (i / 343 % 7) as u8) as char
        );
        let text = format!("词{i:03}");
        let frequency = (i * 37 % 97 + 1) as u32;
        source.push_str(&format!("{key}\t{text}\t{frequency}\n"));
        entries.push((key, text, frequency));
    }
    let dictionary = Dictionary::from_tsv(&source).unwrap();
    for (key, _, _) in &entries {
        for spelling in [key.clone(), key.replace('\'', "")] {
            for end in 1..=spelling.len() {
                let query = &spelling[..end];
                let mut expected: Vec<_> = entries
                    .iter()
                    .filter_map(|(k, t, f)| {
                        match_key(k, query).map(|exact| (!exact, std::cmp::Reverse(*f), t, k))
                    })
                    .collect();
                expected.sort();
                let expected: Vec<_> = expected
                    .iter()
                    .take(MAX_CANDIDATES)
                    .map(|e| e.2.clone())
                    .collect();
                assert_eq!(words(&dictionary, query), expected, "query={query}");
            }
        }
    }
}
