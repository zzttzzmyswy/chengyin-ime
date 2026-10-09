//! I15 long-input tail-latency optimization: behaviour-preservation regressions.
//!
//! The optimization (see `docs/PERFORMANCE.md`, I15) replaced the boundary-word
//! `HashSet` with a bitset-prefiltered index, dropped the boundary set's
//! randomized hasher, and hoisted a loop-invariant context bonus out of
//! `Decoder`'s per-rank loop. None of that is supposed to be observable, so these
//! tests pin the *observable* candidate sequence against values captured from the
//! unoptimized binary.
//!
//! Expectations were recorded by dumping every candidate on every page (text and
//! pinyin) from commit `0665ac5` before the change and re-dumping after; the two
//! dumps were byte-identical. They are hardcoded here so a future edit that *does*
//! change ranking fails loudly instead of silently.
//!
//! Coverage is chosen to hit the paths the change touches: the 60-byte input (the
//! tail-latency scenario itself), each keyboard-correction lane that drives
//! `matches_tolerant`, and the boundary attestation the prefilter accelerates.
//! Both matching configurations are exercised — `flags = 0` takes the plain path,
//! the mask takes the correction path.
use chengyin_core::{fuzzy, Dictionary, Key, Modifiers, Session};
use std::sync::Arc;

fn daily() -> Arc<Dictionary> {
    Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap())
}

/// Default `Session` configuration plus matching flags and incremental mode:
/// exactly the configuration the recorded expectations were dumped under.
fn session(flags: u32) -> Session {
    let mut s = Session::new(daily());
    assert!(s.configure_matching(flags));
    assert!(s.configure_incremental(true));
    s
}

/// Type `raw` (at most [`chengyin_core::MAX_INPUT_BYTES`] bytes, since a longer
/// composition is capped and the remainder refused), then walk every page
/// collecting `text|pinyin`.
fn every_candidate(s: &mut Session, raw: &str) -> Vec<String> {
    let accepted = raw.len().min(chengyin_core::MAX_INPUT_BYTES);
    for c in raw.chars() {
        assert!(s.process(Key::Character(c), Modifiers::default()).handled);
    }
    // The tail-pinyin scenario types 64 bytes; the session accepts 63 and holds
    // the rest back, which is the documented input cap rather than a regression.
    assert_eq!(s.preedit(), &raw[..accepted]);
    let mut all = Vec::new();
    loop {
        for i in 0..s.candidate_count() {
            let c = s.candidate(i).unwrap();
            all.push(format!("{}|{}", c.text, c.pinyin));
        }
        if !s.has_next_page() {
            break;
        }
        assert!(s.process(Key::PageDown, Modifiers::default()).handled);
    }
    all
}

/// Assert the first `expected.len()` candidates match exactly and that the total
/// count is unchanged: a shorter list would pass a prefix-only check silently.
fn assert_prefix(actual: &[String], expected: &[&str], total: usize) {
    assert_eq!(actual.len(), total, "candidate count changed:\n{actual:#?}");
    assert_eq!(&actual[..expected.len()], expected);
}

/// The 64-byte typed input from the task card.
const LONG: &str = "woxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwen";

#[test]
fn sixty_byte_input_keeps_its_candidate_sequence_without_matching() {
    // `flags = 0` takes the plain lexical graph, whose boundary attestation is
    // exactly what the prefiltered index accelerates.
    assert_eq!(LONG.len(), 64);
    let mut s = session(0);
    let all = every_candidate(&mut s, LONG);
    assert_prefix(
        &all,
        &[
            "我喜欢中文我喜欢中文我喜欢中文我西华能走红卫兵|woxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwe",
            "我喜欢中文我喜欢中文我喜欢中文我锡华能走红卫兵|woxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwe",
            "我喜欢中文我喜欢中文我喜欢中文我细化你最红卫兵|woxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwe",
            "我喜欢中文我喜欢中文我喜欢中文我西化你最红卫兵|woxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwe",
            "我喜欢中文我喜欢中文我喜欢中文我西华你最红卫兵|woxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwe",
        ],
        57,
    );
}

#[test]
fn sixty_byte_input_keeps_its_candidate_sequence_with_all_matching_flags() {
    // The same 64-byte input on the correction path, where the tolerant walker
    // runs over every start position for every key — the work whose cost the
    // optimization attacks.
    let mut s = session(fuzzy::OPTIONS_MASK);
    let all = every_candidate(&mut s, LONG);
    assert_prefix(
        &all,
        &[
            "我喜欢中文我喜欢中文我喜欢中文我喜欢中文|wo'xi'huan'zhong'wen'wo'xi'huan'zhong'wen'wo'xi'huan'zhong'wen'wo'xi'huan'zhong'wen",
            "我|wo",
            "喔|wo",
            "窝|wo",
            "握|wo",
            "卧|wo",
            "沃|wo",
            "硪|wo",
            "涡|wo",
            "倭|wo",
            "渥|wo",
            "蜗|wo",
        ],
        45,
    );
}

#[test]
fn phonetic_and_keyboard_corrections_keep_their_candidate_sequences() {
    // One input per correction lane named in the task card — keyboard neighbour,
    // repeat, omit, and a long mixed correction — each under its own flag, which
    // is the configuration its expectation was recorded under.
    for (raw, flags, head, total) in [
        (
            "zhsng",
            fuzzy::NEIGHBOR,
            vec!["张", "正", "涨", "长", "整", "章", "证", "郑", "挣", "争"],
            100usize,
        ),
        (
            "zhaang",
            fuzzy::REPEAT,
            vec!["张", "涨", "长", "章", "帐", "掌", "账", "杖", "仗", "胀"],
            106,
        ),
        (
            "zhng",
            fuzzy::OMIT,
            vec![
                "战歌", "真个", "战鼓", "朱昂", "紫红", "中", "张", "正", "种",
            ],
            152,
        ),
        (
            "nizhnaghao",
            fuzzy::OPTIONS_MASK,
            vec![
                "你帐号",
                "你账号",
                "你",
                "拟",
                "尼",
                "呢",
                "泥",
                "妳",
                "妮",
                "腻",
                "逆",
                "倪",
            ],
            78,
        ),
    ] {
        let mut s = session(flags);
        let all = every_candidate(&mut s, raw);
        let texts: Vec<&str> = all.iter().map(|c| c.split('|').next().unwrap()).collect();
        assert_eq!(&texts[..head.len()], &head[..], "{raw} ranking");
        assert_eq!(all.len(), total, "{raw} candidate count");
    }
    // The corrected long input's canonical pinyin is displayed too, and it is the
    // output of the very alignment the optimization touches, so pin it as well.
    let mut s = session(fuzzy::OPTIONS_MASK);
    let all = every_candidate(&mut s, "nizhnaghao");
    assert_eq!(all[0], "你帐号|ni'zhang'hao");
    assert_eq!(all[1], "你账号|ni'zhang'hao");
}

#[test]
fn boundary_attested_joins_are_neither_added_nor_removed() {
    // `attests_boundary` is the only caller of the prefiltered index, and it
    // decides whether a two-word join counts as lexical evidence. A false negative
    // would silently degrade sentence composition, so the join-bearing cases are
    // pinned: `yingshe` leads with two attested joins, `zhnag` keeps its exact
    // ranking, and `zhnag`/`yingshe` candidate counts are unchanged.
    let mut s = session(fuzzy::OPTIONS_MASK);
    let all = every_candidate(&mut s, "yingshe");
    assert_eq!(all[0], "映射|ying'she");
    assert_eq!(all[1], "影射|ying'she");
    assert_eq!(all.len(), 566, "yingshe candidate count");

    let mut s = session(fuzzy::SWAP);
    let all = every_candidate(&mut s, "zhnag");
    assert_eq!(all[0], "张|zhang");
    assert_eq!(all.len(), 42, "zhnag candidate count");
}

#[test]
fn page_walking_and_first_page_agree_across_a_fixed_corpus() {
    // Every test above pins one input's full page walk. This one sweeps a broader
    // fixed corpus in both matching configurations and asserts the invariants that
    // must hold for *every* input the optimization could touch: the walk is
    // non-empty, terminates, and its first page equals the candidates a fresh
    // session reports without paging. It is the cheap guard against a change that
    // alters which inputs produce output at all.
    let corpus = [
        "nihao",
        "jintiantianqihenhao",
        "womenmingtianjian",
        "woxihuanzhongwen",
        "zhnag",
        "zhng",
        "zhsng",
        "zhaang",
        "nizhnaghao",
        "yingshe",
        "yinshe",
        "yignshe",
        "ssdd",
        "zgrm",
        "zhongguoren",
        "nihaoshijie",
        LONG,
    ];
    for flags in [0u32, fuzzy::OPTIONS_MASK] {
        for raw in corpus {
            let mut walked = session(flags);
            let all = every_candidate(&mut walked, raw);
            assert!(!all.is_empty(), "{raw} flags={flags} produced nothing");

            let mut fresh = session(flags);
            for c in raw.chars() {
                assert!(
                    fresh
                        .process(Key::Character(c), Modifiers::default())
                        .handled
                );
            }
            let first: Vec<String> = (0..fresh.candidate_count())
                .map(|i| {
                    let c = fresh.candidate(i).unwrap();
                    format!("{}|{}", c.text, c.pinyin)
                })
                .collect();
            assert_eq!(&all[..first.len()], &first[..], "{raw} flags={flags}");
        }
    }
}
