// SPDX-License-Identifier: GPL-3.0-or-later
use chengyin_core::*;
use std::sync::Arc;
fn press(s: &mut Session, k: Key) {
    assert!(s.process(k, Modifiers::default()).handled);
}
fn input(s: &mut Session, text: &str) {
    for c in text.chars() {
        let r = s.process(Key::Character(c), Modifiers::default());
        assert!(r.handled && !r.limited, "{text} / {c}");
    }
}
fn page_words(s: &Session) -> Vec<String> {
    (0..s.candidate_count())
        .map(|i| s.candidate(i).unwrap().text.to_owned())
        .collect()
}

#[test]
fn all_homophones_page_forward_backward_and_select_once() {
    let mut tsv = String::new();
    for i in 0..137 {
        tsv.push_str(&format!("shi\t候选{i:03}\t{}\n", 1000 - i));
    }
    let mut s = Session::new(Arc::new(Dictionary::from_tsv(&tsv).unwrap()));
    input(&mut s, "shi");
    let mut actual = Vec::new();
    loop {
        actual.extend(page_words(&s));
        if !s.has_next_page() {
            break;
        }
        press(&mut s, Key::PageDown);
    }
    assert_eq!(
        actual,
        (0..137).map(|i| format!("候选{i:03}")).collect::<Vec<_>>()
    );
    assert_eq!(s.page(), 15);
    assert_eq!(s.candidate_count(), 2);
    press(&mut s, Key::PageUp);
    assert_eq!(s.page(), 14);
    assert_eq!(s.candidate(0).unwrap().text, "候选126");
    press(&mut s, Key::Select(5));
    assert_eq!(s.commit(), "候选131");
    assert!(s.preedit().is_empty());
    assert!(!s.process(Key::Space, Modifiers::default()).handled);
    assert!(s.commit().is_empty());
}
#[test]
fn resumable_cursor_matches_scan_for_all_pronunciations_and_completions() {
    let mut tsv = String::new();
    let mut expected = Vec::new();
    for i in 0..100 {
        let key = if i % 2 == 0 { "xi'an" } else { "xian" };
        let text = format!("字{i:03}");
        tsv.push_str(&format!("{key}\t{text}\t{}\n", i + 1));
        expected.push((100 - i, text));
    }
    tsv.push_str("xi'an'ren\t西安人\t9999\n");
    let d = Dictionary::from_tsv(&tsv).unwrap();
    let mut cursor = CandidateCursor::new();
    cursor.reset(&d, "xian").unwrap();
    // Cursor deliberately exposes IDs; lookup/session handle equal-text dedup.
    // Compare observable pages independently to exact-first frequency ordering.
    let mut s = Session::new(Arc::new(d));
    input(&mut s, "xian");
    let mut words = Vec::new();
    loop {
        words.extend(page_words(&s));
        if !s.has_next_page() {
            break;
        }
        press(&mut s, Key::PageDown);
    }
    expected.sort();
    let mut wanted: Vec<_> = expected.into_iter().map(|e| e.1).collect();
    wanted.push("西安人".into());
    assert_eq!(words, wanted);
}
#[test]
fn sentence_selection_segmentation_and_unlock_are_lossless() {
    let d = Arc::new(
        Dictionary::from_tsv("wo\t我\t100\nxi'huan\t喜欢\t50\nni\t你\t100\nni\t尼\t2\n").unwrap(),
    );
    let mut s = Session::new(d);
    input(&mut s, "woxihuanni");
    assert_eq!(s.candidate(0).unwrap().text, "我喜欢你");
    press(&mut s, Key::Space);
    assert_eq!(s.commit(), "我喜欢你");
    input(&mut s, "woxihuanni");
    let segment = (0..s.candidate_count())
        .find(|&i| s.candidate(i).unwrap().text == "我" && s.candidate_consumed(i) == Some(2))
        .unwrap();
    press(&mut s, Key::Select(segment));
    assert_eq!(s.preedit(), "我xihuanni");
    assert!(s.commit().is_empty());
    press(&mut s, Key::Home);
    assert_eq!(s.preedit_cursor(), 3);
    press(&mut s, Key::Backspace);
    assert_eq!(s.preedit(), "woxihuanni");
    assert_eq!(s.preedit_cursor(), 2);
    press(&mut s, Key::End);
    press(&mut s, Key::Space);
    assert_eq!(s.commit(), "我喜欢你");
    input(&mut s, "wo'xi'huan'ni");
    assert_eq!(s.candidate(0).unwrap().text, "我喜欢你");
    press(&mut s, Key::Enter);
    assert_eq!(s.commit(), "wo'xi'huan'ni");
}
#[test]
fn caret_insert_delete_and_failed_edit_preserve_correct_position() {
    let mut s = Session::new(demo_dictionary());
    input(&mut s, "nhao");
    press(&mut s, Key::Home);
    press(&mut s, Key::Right);
    press(&mut s, Key::Character('i'));
    assert_eq!(s.preedit(), "nihao");
    assert_eq!(s.preedit_cursor(), 2);
    press(&mut s, Key::Delete);
    assert_eq!(s.preedit(), "niao");
    assert_eq!(s.preedit_cursor(), 2);
    press(&mut s, Key::Character('h'));
    assert_eq!(s.preedit(), "nihao");
    press(&mut s, Key::Home);
    let r = s.process(Key::Character('\''), Modifiers::default());
    assert!(!r.limited);
    assert_eq!(s.preedit(), "nihao");
    assert_eq!(s.preedit_cursor(), 0);
    press(&mut s, Key::End);
    press(&mut s, Key::Space);
    assert_eq!(s.commit(), "你好");
}
#[test]
fn binary_roundtrip_rejects_truncation_damage_and_invalid_structure() {
    let d = Dictionary::from_tsv(include_str!("../../../data/daily.tsv")).unwrap();
    let bytes = d.to_binary();
    assert_eq!(bytes, include_bytes!("../../../data/daily.mswydict"));
    let decoded = Dictionary::from_binary(&bytes).unwrap();
    assert_eq!(decoded.entry_count(), d.entry_count());
    assert_eq!(decoded.to_binary(), bytes);
    for query in ["nihao", "zhongguo", "shuru", "nv'er", "xian", "shi", "ren"] {
        let a = d.lookup(query).unwrap();
        let b = decoded.lookup(query).unwrap();
        assert_eq!(
            (0..a.len())
                .map(|i| a.get(&d, i).unwrap())
                .collect::<Vec<_>>(),
            (0..b.len())
                .map(|i| b.get(&decoded, i).unwrap())
                .collect::<Vec<_>>()
        );
    }
    for n in [0, 1, 8, 35, 36, 100, bytes.len() - 1] {
        assert!(Dictionary::from_binary(&bytes[..n]).is_err());
    }
    for n in [0, 8, 12, 20, 28, 32, 36, bytes.len() - 1] {
        let mut broken = bytes.clone();
        broken[n] ^= 0x80;
        assert!(Dictionary::from_binary(&broken).is_err());
    }
    // Recompute CRC after malicious structural edits: checksum alone is insufficient.
    let mut corrupt = bytes.clone();
    corrupt[36..40].copy_from_slice(&u32::MAX.to_le_bytes());
    let mut crc = !0u32;
    for &b in &corrupt[36..] {
        crc ^= u32::from(b);
        for _ in 0..8 {
            crc = (crc >> 1) ^ if crc & 1 != 0 { 0xedb88320 } else { 0 };
        }
    }
    corrupt[32..36].copy_from_slice(&(!crc).to_le_bytes());
    assert!(Dictionary::from_binary(&corrupt).is_err());
    // A per-entry budget, so the guard keeps its meaning as the lexicon grows:
    // 384 bytes/entry is what the previous fixed 32 MiB bound allowed at 87,540
    // entries.
    assert!(d.estimated_heap_bytes() <= d.entry_count() * 384);
}
#[test]
fn daily_vocabulary_composes_phrases_absent_from_the_source() {
    let d = Arc::new(Dictionary::from_tsv(include_str!("../../../data/daily.tsv")).unwrap());
    let mut s = Session::new(d);
    for (spelling, wanted) in [
        ("woxihuanzhongwen", "我喜欢中文"),
        ("jintiantianqihenhao", "今天天气很好"),
        ("womenmingtianjian", "我们明天见"),
    ] {
        input(&mut s, spelling);
        let first = s.candidate(0).unwrap().text.to_owned();
        assert_eq!(first, wanted, "{spelling}");
        press(&mut s, Key::Space);
        assert_eq!(s.commit(), wanted);
    }
}

#[test]
fn sentence_order_matches_independent_exhaustive_lexical_oracle() {
    let entries = [
        ("a", "阿", 100.0f64),
        ("a", "啊", 10.0),
        ("b", "吧", 70.0),
        ("b", "八", 5.0),
        ("c", "此", 90.0),
        ("c", "次", 20.0),
        ("a'b", "阿吧", 20.0),
        ("b'c", "吧此", 10.0),
    ];
    let source = entries
        .iter()
        .map(|(p, t, f)| format!("{p}\t{t}\t{f}\n"))
        .collect::<String>();
    let total: f64 = entries.iter().map(|e| e.2).sum();
    fn enumerate(
        entries: &[(&str, &str, f64)],
        total: f64,
        remaining: &str,
        prefix: String,
        previous: Option<&str>,
        cost: f64,
        output: &mut Vec<(f64, String)>,
    ) {
        if remaining.is_empty() {
            output.push((cost, prefix));
            return;
        }
        for &(key, text, freq) in entries {
            let plain = key.replace('\'', "");
            if let Some(rest) = remaining.strip_prefix(&plain) {
                if let Some(left) = previous {
                    // Exhaustively check witnesses crossing every boundary that
                    // touches a character. This fixture has no authored frames.
                    if left.chars().count() == 1 || text.chars().count() == 1 {
                        let a: Vec<_> = left.chars().collect();
                        let b: Vec<_> = text.chars().collect();
                        let mut witnessed = false;
                        for i in 1..=a.len().min(3) {
                            for j in 1..=b.len().min(3) {
                                let join: String = a[a.len() - i..].iter().chain(&b[..j]).collect();
                                witnessed |= entries.iter().any(|e| e.1 == join);
                            }
                        }
                        if !witnessed {
                            continue;
                        }
                    }
                }
                enumerate(
                    entries,
                    total,
                    rest,
                    format!("{prefix}{text}"),
                    Some(text),
                    cost + total.ln() - freq.ln() + 1.0,
                    output,
                );
            }
        }
    }
    let mut expected = Vec::new();
    enumerate(
        &entries,
        total,
        "abc",
        String::new(),
        None,
        0.0,
        &mut expected,
    );
    expected.sort_by(|a, b| a.0.total_cmp(&b.0).then(a.1.cmp(&b.1)));
    let mut seen = std::collections::BTreeSet::new();
    expected.retain(|e| seen.insert(e.1.clone()));
    let mut s = Session::new(Arc::new(Dictionary::from_tsv(&source).unwrap()));
    input(&mut s, "abc");
    let mut actual = Vec::new();
    loop {
        for i in 0..s.candidate_count() {
            if s.candidate_consumed(i) == Some(3) {
                actual.push(s.candidate(i).unwrap().text.to_owned());
            }
        }
        if !s.has_next_page() {
            break;
        }
        press(&mut s, Key::PageDown);
    }
    assert_eq!(
        actual,
        expected
            .iter()
            .take(16)
            .map(|e| e.1.clone())
            .collect::<Vec<_>>()
    );
}

#[test]
fn unfinished_last_syllable_and_unknown_tail_keep_all_raw_input() {
    let d = Arc::new(
        Dictionary::from_tsv("wo\t我\t100\nxi'huan\t喜欢\t50\nzhong'wen\t中文\t50\n").unwrap(),
    );
    let mut s = Session::new(d);
    input(&mut s, "woxihuanzhongw");
    assert_eq!(s.candidate(0).unwrap().text, "我喜欢中文");
    press(&mut s, Key::Escape);
    input(&mut s, "wovvvv");
    assert_eq!(s.candidate(0).unwrap().text, "我");
    press(&mut s, Key::Space);
    assert_eq!(s.preedit(), "我vvvv");
    assert!(s.commit().is_empty());
    press(&mut s, Key::Space);
    assert_eq!(s.commit(), "我vvvv");
    assert!(s.preedit().is_empty());
}

#[test]
fn unlocking_cannot_overflow_the_utf8_preedit_contract() {
    let d = Arc::new(
        Dictionary::from_tsv(&format!(
            "aa\t{}\t10\nbbbbbbbbbbbbbbbbbbbb\t中\t10\n",
            "长".repeat(67)
        ))
        .unwrap(),
    );
    let mut s = Session::new(d);
    input(&mut s, "aabbbbbbbbbbbbbbbbbbbbz");
    press(&mut s, Key::Space);
    press(&mut s, Key::Space);
    assert_eq!(s.preedit(), format!("{}中z", "长".repeat(67)));
    input(&mut s, &"v".repeat(40));
    press(&mut s, Key::Home);
    let previous = s.preedit().to_owned();
    let cursor = s.preedit_cursor();
    let result = s.process(Key::Backspace, Modifiers::default());
    assert!(result.handled && result.limited);
    assert_eq!(s.preedit(), previous);
    assert_eq!(s.preedit_cursor(), cursor);
    assert!(s.preedit().len() <= MAX_TEXT_BYTES);
}

/// I05: the shipped lexicon must stay at least twice the I04 size, stay inside the
/// v2 format limits, and actually cover words the previous lexicon lacked.
#[test]
fn shipped_lexicon_is_at_least_double_the_i04_size_and_covers_new_words() {
    let d = Dictionary::from_tsv(include_str!("../../../data/daily.tsv")).unwrap();
    assert!(
        d.entry_count() >= 175_080,
        "lexicon shrank below the I05 target: {}",
        d.entry_count()
    );
    assert!(d.entry_count() < 250_000, "entry limit");
    assert!(d.estimated_heap_bytes() < 64 * 1024 * 1024);
    assert_eq!(
        Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict"))
            .unwrap()
            .entry_count(),
        d.entry_count()
    );
    // Typed letters whose best candidate the I04 lexicon got wrong because it had
    // no entry with that reading. Each now resolves to the real word.
    let d = Arc::new(d);
    for (typed, wanted) in [
        ("alaboyu", "阿拉伯语"),
        ("alishan", "阿里山"),
        ("ajimide", "阿基米德"),
        ("shujukuchaxun", "数据库查询"),
    ] {
        let mut s = Session::new(Arc::clone(&d));
        input(&mut s, typed);
        assert_eq!(s.candidate(0).unwrap().text, wanted, "{typed}");
    }
}
