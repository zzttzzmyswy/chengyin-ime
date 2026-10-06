use myswy_core::{Dictionary, Key, Modifiers, Session};
use std::sync::Arc;

fn session(dictionary: Arc<Dictionary>) -> Session {
    let mut s = Session::new(dictionary);
    assert!(s.configure_incremental(true));
    s
}
fn type_keys(s: &mut Session, raw: &str) {
    for c in raw.chars() {
        s.process(Key::Character(c), Modifiers::default());
    }
}
fn candidates(s: &Session) -> Vec<String> {
    (0..s.candidate_count())
        .map(|i| s.candidate(i).unwrap().text.to_owned())
        .collect()
}

// R12: an unseparated spelling that admits several *same-cardinality* readings must
// stay reachable from every one of those readings' one-letter keys. The former
// single-path table committed to one reading, so `xg` and `xa` could not both reach
// `xiangang`. Every word below is self-authored for this regression.
#[test]
fn same_cardinality_readings_are_all_indexed() {
    let d = Arc::new(
        Dictionary::from_tsv("xiangang\t先钢\t100\nxi'an'gang\t西安港\t90\nxiang'ang\t香昂\t80\n")
            .unwrap(),
    );
    assert_eq!(d.initials_truncated_words(), 0);
    for (raw, wanted) in [
        ("xg", "先钢"),    // xian'gang
        ("xa", "先钢"),    // xiang'ang
        ("xag", "西安港"), // explicit xi'an'gang
        ("xa", "香昂"),    // explicit xiang'ang
    ] {
        let mut s = session(Arc::clone(&d));
        type_keys(&mut s, raw);
        assert!(
            candidates(&s).iter().any(|text| text == wanted),
            "{raw} must reach {wanted}, got {:?}",
            candidates(&s)
        );
    }
}

// The truncation itself is reported, never silent: a word exceeding the reading
// bound is counted, and it stays reachable through its explicit spelling.
#[test]
fn truncation_is_reported_and_explicit_spellings_survive() {
    let d =
        Arc::new(Dictionary::from_tsv("xiangang\t先钢\t100\nxi'an'gang\t西安港\t90\n").unwrap());
    assert_eq!(
        d.initials_truncated_words(),
        0,
        "explicit spellings never truncate"
    );
    let mut s = session(Arc::clone(&d));
    type_keys(&mut s, "xiangang");
    assert_eq!(s.candidate(0).unwrap().text, "先钢");
    s.reset();
    type_keys(&mut s, "xag");
    assert!(
        candidates(&s).iter().any(|text| text == "西安港"),
        "{:?}",
        candidates(&s)
    );
}

// Bounded multi-parse must not become unbounded work: a long, highly ambiguous
// spelling still initializes, and no per-word bound is spent on distinct words.
#[test]
fn ambiguous_spelling_stays_bounded_and_counted() {
    let mut source = String::new();
    for i in 0..512 {
        source.push_str(&format!("xiangang\t词{i}\t{}\n", 512 - i));
    }
    let d = Dictionary::from_tsv(&source).unwrap();
    assert_eq!(
        d.initials_truncated_words(),
        0,
        "each row is its own word, so none is individually over the bound"
    );
    assert_eq!(d.entry_count(), 512);
    let mut s = session(Arc::new(d));
    type_keys(&mut s, "xg");
    assert!(!candidates(&s).is_empty());
}
