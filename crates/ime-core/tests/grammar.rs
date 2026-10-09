// SPDX-License-Identifier: GPL-3.0-or-later
use chengyin_core::{fuzzy::*, Dictionary, Key, Modifiers, Session};
use std::sync::Arc;
fn replay(d: &Arc<Dictionary>, raw: &str, flags: u32) -> Session {
    let mut s = Session::new(Arc::clone(d));
    s.configure(9, true, false);
    s.configure_incremental(true);
    s.configure_matching(flags);
    for c in raw.chars() {
        s.process(Key::Character(c), Modifiers::default());
    }
    s
}
#[test]
fn productive_exact_phrases_precede_typo_expansions() {
    let d =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in [
        0,
        PHONETIC_MASK,
        SWAP | OMIT | NEIGHBOR | REPEAT,
        OPTIONS_MASK,
    ] {
        for (raw, wanted) in [
            ("kaibukai", "开不开"),
            ("kai'bu'kai", "开不开"),
            ("diannaode", "电脑的"),
            ("dian'nao'de", "电脑的"),
            ("shoujide", "手机的"),
            ("haobuhao", "好不好"),
            ("kanbukan", "看不看"),
        ] {
            let mut s = replay(&d, raw, flags);
            assert_eq!(s.candidate(0).unwrap().text, wanted, "{raw}, flags={flags}");
            assert_eq!(s.candidate_consumed(0), Some(raw.len()));
            for _ in 0..4 {
                for i in 0..s.candidate_count() {
                    assert!(
                        !["堤岸闹得", "卡死不开", "看你不开", "电脑我的", "卡死不来"]
                            .contains(&s.candidate(i).unwrap().text),
                        "{raw}, flags={flags}"
                    );
                }
                if !s.has_next_page() {
                    break;
                }
                s.process(Key::PageDown, Modifiers::default());
            }
        }
    }
}
#[test]
fn grammar_uses_lexical_anchors_not_a_phrase_whitelist() {
    let d = Arc::new(
        Dictionary::from_tsv(
            "deng\t等\t100\nbu'deng\t不等\t100\ndeng\t灯\t1\nxing'zhou\t星舟\t100\nde\t的\t100\n",
        )
        .unwrap(),
    );
    for flags in [0, OPTIONS_MASK] {
        assert_eq!(
            replay(&d, "dengbudeng", flags).candidate(0).unwrap().text,
            "等不等"
        );
        assert_eq!(
            replay(&d, "xingzhoude", flags).candidate(0).unwrap().text,
            "星舟的"
        );
        let s = replay(&d, "dengbudeng", flags);
        assert!(!(0..s.candidate_count()).any(|i| s.candidate(i).unwrap().text == "灯不等"));
    }
}
#[test]
fn lexical_prefix_can_still_commit_before_particle() {
    let d =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    let mut s = replay(&d, "diannaode", OPTIONS_MASK);
    let at = (0..s.candidate_count())
        .find(|&i| s.candidate(i).unwrap().text == "电脑")
        .unwrap();
    assert_eq!(s.candidate_consumed(at), Some(7));
    s.process(Key::Select(at), Modifiers::default());
    assert_eq!(s.commit(), "电脑");
    assert_eq!(s.preedit(), "de");
    assert_eq!(s.candidate(0).unwrap().text, "的");
}
