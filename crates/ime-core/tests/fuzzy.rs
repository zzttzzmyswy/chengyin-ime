// SPDX-License-Identifier: GPL-3.0-or-later
use chengyin_core::{fuzzy::*, Dictionary, Key, Modifiers, Session};
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
    let d = Arc::new(
        Dictionary::from_tsv(
            "zhang\t张\t1000\nping\t平\t800\nhao\t好\t800\nshi\t是\t800\nni'hao\t你好\t900\n",
        )
        .unwrap(),
    );
    for (raw, flag, text, canonical, letters) in [
        ("zhnag", SWAP, "张", "zhang", "an"),
        ("zhng", OMIT, "张", "zhang", "a"),
        ("zhsng", NEIGHBOR, "张", "zhang", "a"),
        ("zhaang", REPEAT, "张", "zhang", "a"),
        ("hoa", SWAP, "好", "hao", "ao"),
        ("pign", SWAP, "平", "ping", "ng"),
        ("png", OMIT, "平", "ping", "i"),
        ("hso", NEIGHBOR, "好", "hao", "a"),
        ("pinb", NEIGHBOR, "平", "ping", "g"),
        ("shii", REPEAT, "是", "shi", "i"),
        ("haoo", REPEAT, "好", "hao", "o"),
        ("nihoa", SWAP, "你好", "ni'hao", "ao"),
    ] {
        let mut s = Session::new(Arc::clone(&d));
        assert!(s.configure_matching(flag));
        typed(&mut s, raw);
        let i = find(&mut s, text);
        assert_eq!(s.candidate(i).unwrap().pinyin, canonical);
        assert_eq!(marked(&s, i), letters, "{raw}");
        assert!(!s.configure_matching(0));
        assert_eq!(s.preedit(), raw);
        s.process(Key::Select(i), Modifiers::default());
        assert_eq!(s.commit(), text);
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
    // A composition the lexicon does not attest for these keys must not lead a
    // corrected word it does attest (review gap 3). Here `ni`+`hao` composes
    // 你好, but the only real entry for this key is `li'hao` -> 礼号, so under the
    // n->l rule the corrected word wins; 你好 must stay reachable.
    let d =
        Arc::new(Dictionary::from_tsv("ni\t你\t1\nhao\t好\t1\nli'hao\t礼号\t100000\n").unwrap());
    let mut s = Session::new(d);
    s.configure_matching(1 << 3);
    typed(&mut s, "nihao");
    assert_eq!(s.candidate(0).unwrap().text, "礼号");
    let i = find(&mut s, "你好");
    assert!(i > 0);
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

#[test]
fn transposition_crosses_a_syllable_boundary() {
    // The dominant real-world keyboard slip: the user transposes the last letter
    // of one syllable with the first letter of the next (`guan'ai` -> `guaani`).
    // Together those letters sit on either side of a separator, so the walker and
    // the annotation aligner must both be able to step across it.
    let d = Arc::new(
        Dictionary::from_tsv(
            "guan'ai\t关爱\t1000\ncan'ran\t惨然\t900\nguo'hou\t过后\t900\nshi'nai'de\t施耐德\t800\n",
        )
        .unwrap(),
    );
    for (raw, text, letters) in [
        ("guaani", "关爱", "na"),
        ("carnan", "惨然", "nr"),
        ("guhoou", "过后", "oh"),
        ("shniaide", "施耐德", "in"),
    ] {
        let mut s = Session::new(Arc::clone(&d));
        s.configure(5, true, false);
        assert!(s.configure_matching(SWAP));
        typed(&mut s, raw);
        let i = find(&mut s, text);
        assert_eq!(s.candidate(i).unwrap().text, text, "{raw}");
        // The two canonical letters that were transposed are marked, each on its
        // own side of the syllable separator.
        assert_eq!(marked(&s, i), letters, "{raw}");
        s.process(Key::Select(i), Modifiers::default());
        assert_eq!(s.commit(), text);
    }
}

#[test]
fn unattested_join_does_not_lead_an_attested_word() {
    // Review gap 3: under correction a composition the lexicon does not attest
    // for the typed keys must not outrank a word it does attest. Here the only
    // entry is `qin'he'li` 亲和力; `qingheli` merely splits into other keys whose
    // words are not in this dictionary, so the attested word must lead.
    let d = Arc::new(Dictionary::from_tsv("qin'he'li\t亲和力\t324\n").unwrap());
    let mut s = Session::new(d);
    s.configure(5, true, false);
    assert!(s.configure_matching(1 << 8));
    typed(&mut s, "qingheli");
    assert_eq!(s.candidate(0).unwrap().text, "亲和力");
    assert_eq!(marked(&s, 0), "in");
}
