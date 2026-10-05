use myswy_core::{fuzzy::OPTIONS_MASK, Dictionary, Key, Modifiers, Session};
use std::sync::Arc;
fn press(s: &mut Session, k: Key) {
    assert!(s.process(k, Modifiers::default()).handled);
}
fn input(s: &mut Session, raw: &str) {
    for c in raw.chars() {
        press(s, Key::Character(c));
    }
}
fn session(flags: u32) -> Session {
    let d = Dictionary::from_tsv("zheng'ze'biao'da'shi\t正则表达式\t1000\nzheng'ze\t正则\t800\nzheng\t正\t900\nbiao'da'shi\t表达式\t700\nze\t则\t600\nwo\t我\t1000\nai\t爱\t100\na\t啊\t100\n").unwrap();
    let mut s = Session::new(Arc::new(d));
    assert!(s.configure(9, true, false));
    assert!(s.configure_matching(flags));
    assert!(s.configure_incremental(true));
    s
}
fn find(s: &mut Session, text: &str, consumed: usize) -> usize {
    loop {
        for i in 0..s.candidate_count() {
            if s.candidate(i).unwrap().text == text && s.candidate_consumed(i) == Some(consumed) {
                return i;
            }
        }
        assert!(s.has_next_page(), "missing {text}/{consumed}");
        press(s, Key::PageDown);
    }
}
#[test]
fn complete_match_and_independent_prefix_choices_commit_and_rematch() {
    for flags in [0, OPTIONS_MASK] {
        let mut s = session(flags);
        input(&mut s, "zhengzebiaodashi");
        assert_eq!(s.candidate(0).unwrap().text, "正则表达式");
        let i = find(&mut s, "正则", 7);
        press(&mut s, Key::Select(i));
        assert_eq!(s.commit(), "正则");
        assert_eq!(s.preedit(), "biaodashi");
        assert_eq!(s.learning_key(), "zhengze");
        assert_eq!(s.candidate(0).unwrap().text, "表达式");
        assert!(s.learn_commit());
        assert!(!s.learn_commit());
        assert_eq!(s.profile().entry_count(), 1);
        press(&mut s, Key::Select(0));
        assert_eq!(s.commit(), "表达式");
        assert!(s.preedit().is_empty());
        assert_eq!(s.learning_key(), "biaodashi");
        assert!(s.learn_commit());
        assert_eq!(s.profile().entry_count(), 2);
    }
}
#[test]
fn character_choice_separator_editing_and_cancel_leave_committed_prefix() {
    for flags in [0, OPTIONS_MASK] {
        let mut s = session(flags);
        input(&mut s, "zheng'ze'biaodashi");
        let i = find(&mut s, "正", 6);
        press(&mut s, Key::Select(i));
        assert_eq!(s.commit(), "正");
        assert_eq!(s.preedit(), "ze'biaodashi");
        assert_eq!(s.learning_key(), "zheng");
        press(&mut s, Key::Home);
        assert_eq!(s.preedit_cursor(), 0);
        press(&mut s, Key::Backspace);
        assert_eq!(s.preedit(), "ze'biaodashi");
        assert!(s.commit().is_empty());
        assert!(!s.learn_commit());
        press(&mut s, Key::Escape);
        assert!(s.preedit().is_empty());
    }
}
#[test]
fn unknown_suffix_can_be_edited_or_committed_raw_without_losing_prefix() {
    let mut s = session(OPTIONS_MASK);
    input(&mut s, "wovvvv");
    let i = find(&mut s, "我", 2);
    press(&mut s, Key::Select(i));
    assert_eq!(s.commit(), "我");
    assert_eq!(s.preedit(), "vvvv");
    assert!(s.learn_commit());
    press(&mut s, Key::Enter);
    assert_eq!(s.commit(), "vvvv");
    assert!(s.preedit().is_empty());
    assert!(s.learning_key().is_empty());
    assert!(!s.learn_commit());
}
#[test]
fn prefix_pagination_preserves_every_homophone_and_single_syllable_is_not_split() {
    let mut tsv = String::from("wo'hao\t我好\t1000\nhao\t好\t1000\n");
    for i in 0..137 {
        tsv.push_str(&format!(
            "wo\t我{}\t{}\n",
            char::from_u32(0x4e00 + i).unwrap(),
            500 - i
        ));
    }
    let mut s = Session::new(Arc::new(Dictionary::from_tsv(&tsv).unwrap()));
    s.configure_incremental(true);
    s.configure_matching(OPTIONS_MASK);
    input(&mut s, "wohao");
    let mut count = 0;
    loop {
        for i in 0..s.candidate_count() {
            if s.candidate_consumed(i) == Some(2) {
                count += 1;
            }
        }
        if !s.has_next_page() {
            break;
        }
        press(&mut s, Key::PageDown);
    }
    assert_eq!(count, 137);
    let mut s = session(OPTIONS_MASK);
    input(&mut s, "ai");
    loop {
        for i in 0..s.candidate_count() {
            assert_eq!(s.candidate_consumed(i), Some(2));
        }
        if !s.has_next_page() {
            break;
        }
        press(&mut s, Key::PageDown);
    }
}
#[test]
fn active_mode_changes_are_rejected_without_changing_outputs() {
    let mut s = session(0);
    input(&mut s, "wovvvv");
    assert!(!s.configure_incremental(false));
    assert_eq!(s.preedit(), "wovvvv");
    let i = find(&mut s, "我", 2);
    press(&mut s, Key::Select(i));
    assert!(!s.configure_incremental(false));
    assert_eq!(s.commit(), "我");
    assert_eq!(s.preedit(), "vvvv");
}

#[test]
fn production_dictionary_keeps_term_quality_and_prefixes_in_both_matching_modes() {
    let dictionary =
        Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap());
    for flags in [0, OPTIONS_MASK] {
        let mut s = Session::new(Arc::clone(&dictionary));
        s.configure(9, true, false);
        s.configure_matching(flags);
        s.configure_incremental(true);
        input(&mut s, "zhengzebiaodashi");
        assert_eq!(s.candidate(0).unwrap().text, "正则表达式");
        let mut phrase = false;
        let mut character = false;
        loop {
            for i in 0..s.candidate_count() {
                let c = s.candidate(i).unwrap();
                let n = s.candidate_consumed(i).unwrap();
                if n == 15 {
                    assert_eq!(c.text, "正则表达式");
                } else {
                    assert!(c.frequency > 0, "prefix must be a dictionary word");
                }
                phrase |= c.text == "正则" && n == 7;
                character |= c.text == "正" && n == 5;
            }
            if !s.has_next_page() {
                break;
            }
            press(&mut s, Key::PageDown);
        }
        assert!(phrase && character);
        s.reset();
        input(&mut s, "zhengzebiaodashi");
        let i = find(&mut s, "正", 5);
        press(&mut s, Key::Select(i));
        assert_eq!(s.commit(), "正");
        let i = find(&mut s, "则", 2);
        press(&mut s, Key::Select(i));
        assert_eq!(s.commit(), "则");
        assert_eq!(s.preedit(), "biaodashi");
        assert_eq!(s.candidate(0).unwrap().text, "表达式");
    }
}
