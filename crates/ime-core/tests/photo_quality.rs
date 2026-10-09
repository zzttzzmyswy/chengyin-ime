// SPDX-License-Identifier: GPL-3.0-or-later
use chengyin_core::{fuzzy::*, Dictionary, Key, Modifiers, Profile, Session};
use std::sync::Arc;

#[test]
fn common_complete_word_beats_unattested_pairs() {
    let d =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in (0..11)
        .map(|bit| 1 << bit)
        .chain([0, SWAP, OMIT, NEIGHBOR, REPEAT, OPTIONS_MASK])
    {
        for (input, incremental) in [("paizhao", false), ("paizhao", true), ("pai'zhao", true)] {
            let mut s = Session::new(Arc::clone(&d));
            s.configure(5, true, false);
            s.configure_incremental(incremental);
            s.configure_matching(flags);
            for c in input.chars() {
                s.process(Key::Character(c), Modifiers::default());
            }
            assert_eq!(s.candidate(0).unwrap().text, "拍照");
            assert_eq!(s.candidate(1).unwrap().text, "牌照");
            let mut pages = 0;
            loop {
                for i in 0..s.candidate_count() {
                    assert!(
                        !["拍找", "派找", "排找", "牌找", "拍赵"]
                            .contains(&s.candidate(i).unwrap().text),
                        "input={input}, flags={flags}, incremental={incremental}"
                    );
                }
                if !s.has_next_page() {
                    break;
                }
                assert!(pages < 100);
                s.process(Key::PageDown, Modifiers::default());
                pages += 1;
            }
        }
    }
}

// v1 stored lifetime selection counts, but no recent hit/miss evidence.
fn legacy_profile(key: &str, text: &str, count: u32) -> Profile {
    let mut bytes = b"MSWYUSR1".to_vec();
    for n in [1u32, 1, 0] {
        bytes.extend(n.to_le_bytes());
    }
    bytes.extend((key.len() as u16).to_le_bytes());
    bytes.extend((text.len() as u16).to_le_bytes());
    for n in [count, 1] {
        bytes.extend(n.to_le_bytes());
    }
    bytes.extend(key.as_bytes());
    bytes.extend(text.as_bytes());
    let mut crc = !0u32;
    for &byte in &bytes[20..] {
        crc ^= u32::from(byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ if crc & 1 != 0 { 0xedb88320 } else { 0 };
        }
    }
    bytes[16..20].copy_from_slice(&(!crc).to_le_bytes());
    Profile::from_binary(&bytes).unwrap()
}

#[test]
fn legacy_unattested_pairs_need_new_selection_evidence() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in [0, OPTIONS_MASK] {
        for count in [1, 3, 999] {
            let mut history = legacy_profile("paizhao", "拍找", count);
            for recent_selections in 0..=3 {
                let bytes = history.to_binary();
                let mut session = Session::new(Arc::clone(&dictionary));
                session.set_profile(Arc::new(Profile::from_binary(&bytes).unwrap()));
                session.configure_incremental(true);
                session.configure_matching(flags);
                for c in "paizhao".chars() {
                    session.process(Key::Character(c), Modifiers::default());
                }
                assert_eq!(
                    session.candidate(0).unwrap().text,
                    if recent_selections == 3 {
                        "拍找"
                    } else {
                        "拍照"
                    },
                    "flags={flags}, legacy_count={count}, recent={recent_selections}"
                );
                assert_eq!(
                    history.to_binary(),
                    bytes,
                    "lookup must preserve the profile"
                );
                history.record("paizhao", "拍找");
            }
        }
    }
}

#[test]
fn attested_legacy_word_preferences_remain_usable() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in [0, OPTIONS_MASK] {
        let mut session = Session::new(Arc::clone(&dictionary));
        session.set_profile(Arc::new(legacy_profile("paizhao", "牌照", 999)));
        session.configure_matching(flags);
        for c in "paizhao".chars() {
            session.process(Key::Character(c), Modifiers::default());
        }
        assert_eq!(session.candidate(0).unwrap().text, "牌照");
    }
}

#[test]
fn legacy_pairs_cannot_promote_through_typo_recall() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for input in ["paizhoa", "paizho", "paizzhao"] {
        let mut session = Session::new(Arc::clone(&dictionary));
        session.set_profile(Arc::new(legacy_profile("paizhao", "拍找", 999)));
        session.configure_matching(OPTIONS_MASK);
        for c in input.chars() {
            session.process(Key::Character(c), Modifiers::default());
        }
        let rows: Vec<_> = (0..session.candidate_count())
            .map(|i| session.candidate(i).unwrap().text.to_owned())
            .collect();
        assert!(rows.iter().any(|text| text == "拍照"), "{input}: {rows:?}");
        assert!(rows.iter().all(|text| text != "拍找"), "{input}: {rows:?}");
    }
}

#[test]
fn decayed_unknown_history_requires_renewed_hits_but_keeps_data() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    let mut profile = Profile::default();
    for _ in 0..3 {
        profile.record("paizhao", "拍找");
    }
    for _ in 0..256 {
        profile.record("hao", "好");
    }
    let bytes = profile.to_binary();
    for flags in [0, OPTIONS_MASK] {
        let mut session = Session::new(Arc::clone(&dictionary));
        session.set_profile(Arc::new(Profile::from_binary(&bytes).unwrap()));
        session.configure_matching(flags);
        for c in "paizhao".chars() {
            session.process(Key::Character(c), Modifiers::default());
        }
        assert_eq!(session.candidate(0).unwrap().text, "拍照");
        assert_eq!(profile.to_binary(), bytes);
        assert_eq!(profile.entry_count(), 2);
    }
}
