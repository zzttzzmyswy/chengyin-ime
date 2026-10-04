use myswy_core::*;
use std::sync::Arc;
fn input(s: &mut Session, text: &str) {
    for c in text.chars() {
        assert!(s.process(Key::Character(c), Modifiers::default()).handled);
    }
}
fn find(s: &mut Session, wanted: &str) -> Option<usize> {
    loop {
        if let Some(i) = (0..s.candidate_count()).find(|&i| s.candidate(i).unwrap().text == wanted)
        {
            return Some(i);
        }
        if !s.has_next_page() {
            return None;
        }
        s.process(Key::PageDown, Modifiers::default());
    }
}
#[test]
fn initials_mixed_spelling_sentences_and_middle_edits() {
    let d = Arc::new(
        Dictionary::from_tsv(
            "zhong'guo\t中国\t1000\nzhong'wen\t中文\t900\nwo\t我\t1000\nxi'huan\t喜欢\t1000\n",
        )
        .unwrap(),
    );
    let mut s = Session::new(d);
    for key in [
        "zg",
        "zhongg",
        "zguo",
        "z'g",
        "zhong'g",
        "wxhzw",
        "woxhzhongw",
        "wo'xh'zw",
    ] {
        input(&mut s, key);
        let wanted = if key.starts_with('w') {
            "我喜欢中文"
        } else {
            "中国"
        };
        let i = find(&mut s, wanted).unwrap_or_else(|| panic!("{key}: missing {wanted}"));
        assert_eq!(s.candidate_consumed(i), Some(key.len()));
        s.process(Key::Select(i), Modifiers::default());
        assert_eq!(s.commit(), wanted);
        s.reset();
    }
    input(&mut s, "zgu");
    s.process(Key::Home, Modifiers::default());
    s.process(Key::Right, Modifiers::default());
    s.process(Key::Delete, Modifiers::default());
    s.process(Key::Character('g'), Modifiers::default());
    s.process(Key::End, Modifiers::default());
    s.process(Key::Character('o'), Modifiers::default());
    let i = find(&mut s, "中国").unwrap();
    s.process(Key::Select(i), Modifiers::default());
    assert_eq!(s.commit(), "中国");
}
#[test]
fn abbreviated_homophones_page_without_truncation() {
    let tsv = (0..137)
        .map(|i| format!("zhong'guo\t候选{i:03}\t{}\n", 1000 - i))
        .collect::<String>();
    let mut s = Session::new(Arc::new(Dictionary::from_tsv(&tsv).unwrap()));
    input(&mut s, "zg");
    let mut all = Vec::new();
    loop {
        for i in 0..s.candidate_count() {
            if s.candidate_consumed(i) == Some(2) {
                all.push(s.candidate(i).unwrap().text.to_owned());
            }
        }
        if !s.has_next_page() {
            break;
        }
        s.process(Key::PageDown, Modifiers::default());
    }
    assert_eq!(
        all,
        (0..137).map(|i| format!("候选{i:03}")).collect::<Vec<_>>()
    );
}
#[test]
fn association_is_explicit_and_reset_drops_context() {
    let d = Arc::new(
        Dictionary::from_tsv("ni'hao\t你好\t100\nshi'jie\t世界\t100\nzhong'guo\t中国\t100\n")
            .unwrap(),
    );
    let mut s = Session::new(d);
    input(&mut s, "nihao");
    s.process(Key::Space, Modifiers::default());
    assert_eq!(s.commit(), "你好");
    assert!(s.preedit().is_empty() && s.is_association());
    let i = find(&mut s, "世界").unwrap();
    s.process(Key::Select(i), Modifiers::default());
    assert_eq!(s.commit(), "世界");
    s.reset();
    input(&mut s, "nihao");
    s.process(Key::Space, Modifiers::default());
    assert!(!s.process(Key::Space, Modifiers::default()).handled);
    assert!(s.commit().is_empty() && !s.is_association());
    input(&mut s, "nihao");
    s.process(Key::Space, Modifiers::default());
    s.process(Key::Tab, Modifiers::default());
    assert_eq!(s.commit(), "世界");
    s.reset();
    assert_eq!(s.candidate_count(), 0);
}
#[test]
fn context_changes_homophone_rank_and_recent_choices_are_session_local() {
    let d = Arc::new(
        Dictionary::from_tsv("zhong'guo\t中国\t1000\nren'min\t任敏\t1000\nren'min\t人民\t300\n")
            .unwrap(),
    );
    let mut s = Session::new(Arc::clone(&d));
    input(&mut s, "renmin");
    assert_eq!(s.candidate(0).unwrap().text, "任敏");
    s.reset();
    input(&mut s, "zhongguo");
    s.process(Key::Space, Modifiers::default());
    input(&mut s, "renmin");
    assert_eq!(s.candidate(0).unwrap().text, "人民");
    s.process(Key::Space, Modifiers::default());
    input(&mut s, "renmin");
    assert_eq!(s.candidate(0).unwrap().text, "人民");
    let mut other = Session::new(d);
    input(&mut other, "renmin");
    assert_eq!(other.candidate(0).unwrap().text, "任敏");
    s.reset();
    input(&mut s, "renmin");
    assert_eq!(s.candidate(0).unwrap().text, "任敏");
}
fn scel(version: u8, padded: bool) -> Vec<u8> {
    let mut b = vec![0; 0x1540];
    b[..8].copy_from_slice(&[0x40, 0x15, 0, 0, version, 0x43, 0x53, 1]);
    b[0x120..0x124].copy_from_slice(&1u32.to_le_bytes());
    b.extend(2u32.to_le_bytes());
    for (id, key) in [(10u16, "ni"), (90, "hao")] {
        b.extend(id.to_le_bytes());
        b.extend((key.len() as u16 * 2).to_le_bytes());
        for u in key.encode_utf16() {
            b.extend(u.to_le_bytes());
        }
    }
    if padded {
        b.resize(if version == 0x44 { 0x2628 } else { 0x26c4 }, 0);
    }
    b.extend(2u16.to_le_bytes());
    b.extend(4u16.to_le_bytes());
    b.extend(10u16.to_le_bytes());
    b.extend(90u16.to_le_bytes());
    for word in ["你好", "拟好"] {
        let units: Vec<_> = word.encode_utf16().collect();
        b.extend((units.len() as u16 * 2).to_le_bytes());
        for u in units {
            b.extend(u.to_le_bytes());
        }
        b.extend(10u16.to_le_bytes());
        b.extend(200u16.to_le_bytes());
        b.extend([0; 8]);
    }
    b
}
#[test]
fn scel_versions_padding_sparse_ids_and_every_truncation_are_checked() {
    for version in [0x44, 0x45] {
        for padded in [false, true] {
            let b = scel(version, padded);
            let d = Dictionary::import(&b).unwrap();
            assert_eq!(d.entry_count(), 2);
            assert_eq!(d.lookup("nihao").unwrap().len(), 2);
            for n in 0..b.len() {
                assert!(
                    Dictionary::import(&b[..n]).is_err(),
                    "accepted truncated SCEL {n}"
                );
            }
            let mut broken = b.clone();
            broken[4] = 0x46;
            assert!(Dictionary::import(&broken).is_err());
            let mut broken = b.clone();
            broken[0x154a] = 0xff;
            broken[0x154b] = 0xdb;
            assert!(Dictionary::import(&broken).is_err());
        }
    }
}
#[test]
fn text_exports_duplicates_utf16_and_merging() {
    let d = Dictionary::import("'ni'hao 你好\n'ni'hao 你好 20\n".as_bytes()).unwrap();
    assert_eq!(d.entry_count(), 1);
    let mut b = vec![0xff, 0xfe];
    for u in "'shi'jie 世界 50\r\n".encode_utf16() {
        b.extend(u.to_le_bytes());
    }
    let extra = Dictionary::import(&b).unwrap();
    let merged = d.merge(&extra).unwrap();
    assert_eq!(merged.entry_count(), 2);
    assert_eq!(d.entry_count(), 1);
    assert_eq!(
        Dictionary::from_binary(&merged.to_binary())
            .unwrap()
            .entry_count(),
        2
    );
    for bad in ["'ni'hao 你好 0", "'ni'hao 你好 nan", "'ni'hao", "'' word"] {
        assert!(Dictionary::import(bad.as_bytes()).is_err());
    }
}

#[test]
fn long_dictionary_pronunciation_and_old_binary_migration() {
    let key = ["xin"; 27].join("'");
    let text = "新".repeat(27);
    assert!(key.len() > MAX_INPUT_BYTES);
    let d = Dictionary::from_tsv(&format!("{key}\t{text}\t100\n")).unwrap();
    let bytes = d.to_binary();
    assert_eq!(&bytes[8..12], &2u32.to_le_bytes());
    let mut old = bytes.clone();
    old[8..12].copy_from_slice(&1u32.to_le_bytes());
    assert!(Dictionary::from_binary(&old).is_err());
    let mut s = Session::new(Arc::new(Dictionary::from_binary(&bytes).unwrap()));
    input(&mut s, &"x".repeat(27));
    let i = find(&mut s, &text).unwrap();
    s.process(Key::Select(i), Modifiers::default());
    assert_eq!(s.commit(), text);
    let d = Dictionary::from_tsv("ni\t你\t100\n").unwrap();
    let mut legacy = d.to_binary();
    legacy[8..12].copy_from_slice(&1u32.to_le_bytes());
    let loaded = Dictionary::from_binary(&legacy).unwrap();
    assert_eq!(
        loaded.lookup("ni").unwrap().get(&loaded, 0).unwrap().text,
        "你"
    );
}
#[test]
fn dictionary_continuations_are_available_without_a_static_transition() {
    let d = Arc::new(
        Dictionary::from_tsv("bei'jing\t北京\t100\nbei'jing'da'xue\t北京大学\t90\n").unwrap(),
    );
    let mut s = Session::new(d);
    input(&mut s, "beijing");
    s.process(Key::Space, Modifiers::default());
    assert!(s.is_association());
    let i = find(&mut s, "大学").unwrap();
    assert_eq!(s.candidate_consumed(i), Some(0));
    s.process(Key::Select(i), Modifiers::default());
    assert_eq!(s.commit(), "大学");
}

#[test]
fn mixed_lookup_matches_independent_alias_enumeration() {
    let rows = [
        ("zhang", "张", 50u32),
        ("zhan", "站", 40),
        ("zhe'ge", "这个", 100),
        ("zhong'guo", "中国", 90),
        ("zhi'ge", "制革", 20),
        ("xi'an", "西安", 85),
        ("xian", "先", 80),
        ("xin", "新", 75),
        ("xian'ning", "咸宁", 70),
        ("xi'ning", "西宁", 60),
        ("nv'er", "女儿", 55),
    ];
    let source = rows
        .iter()
        .map(|(p, t, f)| format!("{p}\t{t}\t{f}\n"))
        .collect::<String>();
    let d = Dictionary::from_tsv(&source).unwrap();
    let mut aliases = std::collections::BTreeMap::<String, Vec<(u32, &str, &str)>>::new();
    for &(key, text, frequency) in &rows {
        let syllables = key.split('\'').collect::<Vec<_>>();
        fn enumerate(
            syllables: &[&str],
            pos: usize,
            prefix: String,
            frequency: u32,
            text: &'static str,
            key: &'static str,
            aliases: &mut std::collections::BTreeMap<
                String,
                Vec<(u32, &'static str, &'static str)>,
            >,
        ) {
            if pos == syllables.len() {
                aliases
                    .entry(prefix)
                    .or_default()
                    .push((frequency, text, key));
                return;
            }
            let syllable = syllables[pos];
            let mut options = vec![syllable, &syllable[..1]];
            if ["zh", "ch", "sh"].iter().any(|p| syllable.starts_with(p)) {
                options.push(&syllable[..2]);
            }
            for option in options {
                for separator in [false, true] {
                    if pos == 0 && separator {
                        continue;
                    }
                    let mut next = prefix.clone();
                    if separator {
                        next.push('\'');
                    }
                    next.push_str(option);
                    enumerate(syllables, pos + 1, next, frequency, text, key, aliases);
                }
            }
        }
        enumerate(
            &syllables,
            0,
            String::new(),
            frequency,
            text,
            key,
            &mut aliases,
        );
    }
    for missing in ["zng", "zang", "xn", "nag", "xng", "zg", "xg"] {
        aliases.entry(missing.to_owned()).or_default();
    }
    for (query, mut expected) in aliases {
        expected.sort_by_key(|&(f, t, p)| (std::cmp::Reverse(f), t, p));
        expected.dedup();
        let expected = expected
            .into_iter()
            .map(|e| e.1)
            .take(9)
            .collect::<Vec<_>>();
        let (actual, limited) = d.lookup_fast(&query).unwrap();
        assert!(!limited);
        assert_eq!(
            (0..actual.len())
                .map(|i| actual.get(&d, i).unwrap().text)
                .collect::<Vec<_>>(),
            expected,
            "{query}"
        );
    }
}

#[test]
fn context_ranking_keeps_tail_completions_after_exact_sentences() {
    let d = Arc::new(
        Dictionary::from_tsv(
            "wo\t我\t100\nai\t爱\t90\nwo'ai'ni\t我爱你\t100\nzhong'wen\t中文\t100\n",
        )
        .unwrap(),
    );
    let mut s = Session::new(d);
    input(&mut s, "zhongwen");
    s.process(Key::Space, Modifiers::default());
    input(&mut s, "woai");
    assert_eq!(s.candidate(0).unwrap().text, "我爱");
}
