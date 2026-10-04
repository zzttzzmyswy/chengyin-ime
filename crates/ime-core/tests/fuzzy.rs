use myswy_core::{fuzzy::*, Dictionary, Key, Modifiers, Session};
use std::sync::Arc;
fn dictionary() -> Arc<Dictionary> {
    Arc::new(Dictionary::from_tsv("zhang\t张\t1000\nzan\t赞\t100\nzang\t脏\t200\nni\t你\t900\nli\t李\t800\nhao\t好\t800\nzhang'hao\t账号\t700\n").unwrap())
}
fn typed(s: &mut Session, raw: &str) {
    for c in raw.chars() {
        assert!(s.process(Key::Character(c), Modifiers::default()).handled);
    }
    assert_eq!(s.preedit(), raw);
}
fn find(s: &mut Session, text: &str) -> usize {
    for _ in 0..20 {
        if let Some(i) = (0..s.candidate_count()).find(|&i| s.candidate(i).unwrap().text == text) {
            return i;
        }
        if !s.has_next_page() {
            break;
        }
        s.process(Key::PageDown, Modifiers::default());
    }
    panic!("missing {text} in {}", s.preedit())
}
fn marked(s: &Session, i: usize) -> String {
    let c = s.candidate(i).unwrap();
    let m = s.candidate_marks(i).unwrap();
    c.pinyin
        .bytes()
        .enumerate()
        .filter(|(at, _)| m[at / 64] & (1 << (at % 64)) != 0)
        .map(|(_, b)| b as char)
        .collect()
}
#[test]
fn keyboard_errors_are_bounded_and_marked_by_canonical_letter() {
    for (raw, flag, letters) in [
        ("zhnag", SWAP, "an"),
        ("zhng", OMIT, "a"),
        ("zhsng", NEIGHBOR, "a"),
        ("zhaang", REPEAT, "a"),
    ] {
        let mut s = Session::new(dictionary());
        assert!(s.configure_matching(flag));
        typed(&mut s, raw);
        let i = find(&mut s, "张");
        assert_eq!(s.candidate(i).unwrap().pinyin, "zhang");
        assert_eq!(marked(&s, i), letters, "{raw}");
        assert!(!s.configure_matching(0));
        assert_eq!(s.preedit(), raw);
        s.process(Key::Select(i), Modifiers::default());
        assert_eq!(s.commit(), "张");
    }
    let mut s = Session::new(dictionary());
    s.configure_matching(SWAP);
    typed(&mut s, "gnahz");
    assert!(!(0..s.candidate_count()).any(|i| s.candidate(i).unwrap().text == "张"));
}
#[test]
fn learned_typo_preserves_canonical_annotation_and_session_isolation() {
    let d = dictionary();
    let mut s = Session::new(Arc::clone(&d));
    s.configure_matching(SWAP);
    typed(&mut s, "zhnag");
    let i = find(&mut s, "张");
    s.process(Key::Select(i), Modifiers::default());
    assert!(s.learn_commit());
    let profile = s.profile();
    s.reset();
    let mut restored = Session::new(Arc::clone(&d));
    restored.set_profile(profile);
    restored.configure_matching(SWAP);
    typed(&mut restored, "zhnag");
    assert_eq!(restored.candidate(0).unwrap().pinyin, "zhang");
    assert_eq!(marked(&restored, 0), "an");
    let mut independent = Session::new(d);
    typed(&mut independent, "zhnag");
    for i in 0..independent.candidate_count() {
        assert_eq!(marked(&independent, i), "");
    }
}
#[test]
fn phonetic_groups_are_symmetric_anchored_and_exact_first() {
    let mut s = Session::new(dictionary());
    s.configure_matching(1 | 1 << 6);
    typed(&mut s, "zang");
    assert_eq!(s.candidate(0).unwrap().text, "脏");
    assert_eq!(marked(&s, 0), "");
    let i = find(&mut s, "张");
    assert_eq!(marked(&s, i), "zh");
    s.reset();
    typed(&mut s, "zhang");
    let i = find(&mut s, "脏");
    assert_eq!(marked(&s, i), "z");
    s.reset();
    typed(&mut s, "zan");
    let i = find(&mut s, "张");
    assert_eq!(marked(&s, i), "zhang");
    s.reset();
    s.configure_matching(1 << 3);
    typed(&mut s, "ni");
    let i = find(&mut s, "李");
    assert_eq!(marked(&s, i), "l");
    s.reset();
    s.configure_matching(0);
    typed(&mut s, "zhnag");
    let mut original = Session::new(dictionary());
    typed(&mut original, "zhnag");
    assert_eq!(s.candidate_count(), original.candidate_count());
    for i in 0..s.candidate_count() {
        assert_eq!(s.candidate(i), original.candidate(i));
        assert_eq!(marked(&s, i), "");
    }
}
#[test]
fn every_phonetic_switch_and_exact_sentence_priority() {
    for (rule, left, right, letters) in [
        (0, "zhai", "zai", "zh"),
        (1, "chai", "cai", "ch"),
        (2, "shai", "sai", "sh"),
        (3, "nai", "lai", "n"),
        (4, "hai", "fai", "h"),
        (5, "lai", "rai", "l"),
        (6, "bang", "ban", "ang"),
        (7, "beng", "ben", "eng"),
        (8, "bing", "bin", "ing"),
        (9, "jiang", "jian", "iang"),
        (10, "huang", "huan", "uang"),
    ] {
        let source = format!("{left}\t甲\t100\n");
        let mut s = Session::new(Arc::new(Dictionary::from_tsv(&source).unwrap()));
        s.configure_matching(1 << rule);
        typed(&mut s, right);
        let i = find(&mut s, "甲");
        assert_eq!(marked(&s, i), letters, "{left} / {right}");
        s.reset();
        s.configure_matching(0);
        typed(&mut s, right);
        for i in 0..s.candidate_count() {
            assert_eq!(marked(&s, i), "");
        }
    }
    let d =
        Arc::new(Dictionary::from_tsv("ni\t你\t1\nhao\t好\t1\nli'hao\t礼号\t100000\n").unwrap());
    let mut s = Session::new(d);
    s.configure_matching(1 << 3);
    typed(&mut s, "nihao");
    assert_eq!(s.candidate(0).unwrap().text, "你好");
    assert_eq!(marked(&s, 0), "");
}
#[test]
fn corrected_sentence_and_segments_keep_original_spans_and_editing() {
    let d = Arc::new(Dictionary::from_tsv("zhang'fo\t张佛\t1000\n").unwrap());
    let mut pair = Session::new(d);
    pair.configure_matching(SWAP | NEIGHBOR);
    typed(&mut pair, "zhnag'fi");
    let i = find(&mut pair, "张佛");
    assert_eq!(marked(&pair, i), "ano");
    let mut s = Session::new(dictionary());
    s.configure_matching(SWAP);
    typed(&mut s, "nizhnaghao");
    let i = find(&mut s, "你账号");
    assert_eq!(s.candidate(i).unwrap().pinyin, "ni'zhang'hao");
    assert_eq!(marked(&s, i), "an");
    s.process(Key::Select(i), Modifiers::default());
    assert_eq!(s.commit(), "你账号");
    s.reset();
    typed(&mut s, "zhnag'hao");
    let i = find(&mut s, "账号");
    assert_eq!(marked(&s, i), "an");
    s.process(Key::Home, Modifiers::default());
    s.process(Key::Right, Modifiers::default());
    s.process(Key::Right, Modifiers::default());
    s.process(Key::Delete, Modifiers::default());
    s.process(Key::Character('a'), Modifiers::default());
    assert_eq!(s.preedit(), "zhaag'hao");
    s.process(Key::Enter, Modifiers::default());
    assert_eq!(s.commit(), "zhaag'hao");
    assert!(!s.configure_matching(1 << 31));
}
