// SPDX-License-Identifier: GPL-3.0-or-later
use chengyin_core::{fuzzy::*, Dictionary, Key, Modifiers, Profile, Session};
use std::sync::Arc;

fn replay(d: &Arc<Dictionary>, raw: &str, flags: u32, incremental: bool) -> Session {
    let mut s = Session::new(Arc::clone(d));
    s.configure(9, true, false);
    s.configure_matching(flags);
    s.configure_incremental(incremental);
    for c in raw.chars() {
        s.process(Key::Character(c), Modifiers::default());
    }
    s
}
fn pages(s: &mut Session) -> Vec<(String, usize)> {
    let mut rows = Vec::new();
    loop {
        for i in 0..s.candidate_count() {
            rows.push((
                s.candidate(i).unwrap().text.to_owned(),
                s.candidate_consumed(i).unwrap(),
            ));
        }
        if !s.has_next_page() {
            break;
        }
        assert!(rows.len() < 4096);
        s.process(Key::PageDown, Modifiers::default());
    }
    rows
}
#[test]
fn exact_sentences_with_terminal_particles_precede_local_correction_products() {
    let d =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in [
        0,
        PHONETIC_MASK,
        SWAP | OMIT | NEIGHBOR | REPEAT,
        OPTIONS_MASK,
    ] {
        for incremental in [false, true] {
            for (raw, wanted) in [
                ("xianzaikaishiba", "现在开始吧"),
                ("xian'zai'kai'shi'ba", "现在开始吧"),
                ("mingtiankaishiba", "明天开始吧"),
                ("womenxianzaikaishiba", "我们现在开始吧"),
                ("xianzaikaishima", "现在开始吗"),
                ("mingtianchufaba", "明天出发吧"),
                ("xianzaixuexima", "现在学习吗"),
            ] {
                let mut s = replay(&d, raw, flags, incremental);
                assert_eq!(
                    s.candidate(0).unwrap().text,
                    wanted,
                    "{raw}/{flags}/{incremental}"
                );
                assert_eq!(s.candidate_consumed(0), Some(raw.len()));
                assert_eq!(s.candidate_marks(0), Some([0; 4]));
                assert!(!pages(&mut s).iter().any(|(t, _)| [
                    "想再开时报",
                    "想再开水坝",
                    "明天来时报",
                    "我们想再开时报"
                ]
                .contains(&t.as_str())));
            }
        }
    }
}
#[test]
fn accurate_phrase_prefix_commits_only_its_span_and_rematches_particle() {
    let d =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in [0, OPTIONS_MASK] {
        let mut s = replay(&d, "xianzaikaishiba", flags, true);
        let at = (0..s.candidate_count())
            .find(|&i| s.candidate(i).unwrap().text == "现在开始")
            .unwrap();
        assert_eq!(s.candidate_consumed(at), Some(13));
        s.process(Key::Select(at), Modifiers::default());
        assert_eq!(s.commit(), "现在开始");
        assert_eq!(s.preedit(), "ba");
        assert_eq!(s.learning_key(), "xianzaikaishi");
        assert!(s.learn_commit());
        let at = (0..s.candidate_count())
            .find(|&i| s.candidate(i).unwrap().text == "吧")
            .unwrap();
        s.process(Key::Select(at), Modifiers::default());
        assert_eq!(s.commit(), "吧");
        assert!(s.preedit().is_empty());
    }
}
#[test]
fn terminal_frames_are_generic_exact_and_cannot_bridge_into_another_word() {
    let d = Arc::new(Dictionary::from_tsv("xing'zhou\t星舟\t100\nqian'jin\t前进\t100\nqian'jing\t前景\t999999\nba\t吧\t100\nma\t吗\t100\nne\t呢\t100\na\t啊\t100\nya\t呀\t100\n").unwrap());
    for (tail, text) in [
        ("ba", "吧"),
        ("ma", "吗"),
        ("ne", "呢"),
        ("a", "啊"),
        ("ya", "呀"),
    ] {
        let raw = format!("xingzhouqianjin{tail}");
        let mut s = replay(&d, &raw, OPTIONS_MASK, true);
        assert_eq!(s.candidate(0).unwrap().text, format!("星舟前进{text}"));
        assert!(!pages(&mut s).iter().any(|(t, _)| t.contains("前景")));
    }
    let raw = "xingzhoubaxingzhou";
    let mut s = replay(&d, raw, OPTIONS_MASK, true);
    assert!(!pages(&mut s)
        .iter()
        .any(|(t, n)| *n == raw.len() && t == "星舟吧星舟"));
}
#[test]
fn reliable_prefix_survives_missing_whole_parse_and_context_reranking() {
    // Rare accurate prefix; high-frequency, witnessed corrected sentence would
    // otherwise replace xian->xiang and ba->bao. No terminal particle dictionary.
    let d=Arc::new(Dictionary::from_tsv("xian'zai'kai'shi\t现在开始\t1\nni\t你\t100\nxiang'zai'kai\t想再开\t999999\nshi'bao\t时报\t999999\nkai'shi\t开时\t1\n").unwrap());
    for context in [false, true] {
        let mut s = Session::new(Arc::clone(&d));
        s.configure(9, true, false);
        s.configure_incremental(true);
        s.configure_matching(OPTIONS_MASK);
        if context {
            for c in "ni".chars() {
                s.process(Key::Character(c), Modifiers::default());
            }
            s.process(Key::Select(0), Modifiers::default());
        }
        for c in "xianzaikaishiba".chars() {
            s.process(Key::Character(c), Modifiers::default());
        }
        assert_eq!(s.candidate(0).unwrap().text, "现在开始");
        assert_eq!(s.candidate_consumed(0), Some(13));
        assert!(!pages(&mut s).iter().any(|(t, _)| t == "想再开时报"));
    }
}
#[test]
fn whole_sentence_correction_budget_does_not_restart_at_word_boundaries() {
    let d = Arc::new(Dictionary::from_tsv("ni\t你\t100\nzhang'hao\t账号\t100\n").unwrap());
    let mut one = replay(&d, "nizhnaghao", SWAP | NEIGHBOR, true);
    assert!(pages(&mut one)
        .iter()
        .any(|(t, n)| t == "你账号" && *n == 10));
    let mut two = replay(&d, "nizhnaghai", SWAP | NEIGHBOR, true);
    assert!(!pages(&mut two)
        .iter()
        .any(|(t, n)| t == "你账号" && *n == 10));
}
#[test]
fn exact_history_and_corrected_dictionary_words_keep_their_independent_lanes() {
    let d =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    let mut p = Profile::default();
    for _ in 0..3 {
        p.record("xianzaikaishiba", "现在开始吧");
    }
    let mut s = Session::new(Arc::clone(&d));
    s.configure_incremental(true);
    s.configure_matching(OPTIONS_MASK);
    s.set_profile(Arc::new(p));
    for c in "xianzaikaishiba".chars() {
        s.process(Key::Character(c), Modifiers::default());
    }
    assert_eq!(s.candidate(0).unwrap().text, "现在开始吧");
    for (raw, wanted) in [("zengzebiaodasi", "正则表达式"), ("yignshe", "映射")] {
        let mut s = replay(&d, raw, OPTIONS_MASK, true);
        assert!(pages(&mut s).iter().any(|(t, _)| t == wanted));
    }
}
