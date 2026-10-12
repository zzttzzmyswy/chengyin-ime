// SPDX-License-Identifier: GPL-3.0-or-later
//! First-page equivalence gate over the whole I24 measurement corpus.
//!
//! Both I24 candidates — the A2 sentence gate and the A3 incremental reuse — were
//! admissible only if *every* input's first page (the visible Top-9: text, pinyin
//! and order) stayed byte-identical on every prefix. Neither cleared its
//! performance bar, so neither was merged; this test is what pins the pages that
//! judgement was made against, so a later attempt that silently reorders one fails
//! here, naming the input and the configuration that moved.
//!
//! The baseline is `data/eval/first_page.txt`: one line per (configuration, input),
//! holding a digest over every prefix's page of that input. A digest per input
//! rather than the pages themselves, because the pages are ~8 MB of committed text
//! and every one of them would have to be re-read on each review; a digest still
//! localises a regression to one line. That is why the *prefixes* are inside the
//! line and the *inputs* are the lines.
//!
//! Coverage — every prefix of every input of both corpora, under the full matching
//! mask and under no rules:
//!
//! * `data/eval/quality.tsv`, the 580 annotated rows (the quality gate);
//! * `data/eval/prefix.tsv`, 2,000 generated spellings (see
//!   `scripts/generate_prefix_corpus.py`), which is the corpus that is *not* the
//!   quality set judging the change.
//!
//! The third configuration the acceptance criteria name — initials mixed with full
//! spellings — is an input shape rather than a flag, and the prefix corpus already
//! contains those shapes; `Session` picks the initials lane from the input itself.
//!
//! Run:
//!     cargo test -p chengyin-core --test first_page_baseline --locked
//!     cargo test -p chengyin-core --test first_page_baseline --locked -- --ignored regenerate

use chengyin_core::{Dictionary, Key, Modifiers, Session};
use std::fmt::Write as _;
use std::sync::Arc;

/// The matching configurations pinned for every input. The acceptance criteria's
/// third configuration, initials mixed with full spellings, is an input shape the
/// prefix corpus already covers, not a flag.
const CONFIGS: [(&str, u32); 2] = [
    ("no-rules", 0),
    ("all-rules", chengyin_core::fuzzy::OPTIONS_MASK),
];

/// The frozen digest file. One line per (configuration, input):
/// `config<TAB>input<TAB>digest-of-every-prefix's-first-page`.
const BASELINE: &str = include_str!("../../../data/eval/first_page.txt");

/// A 64-bit FNV-1a digest. Not cryptographic: this baseline pins a build against
/// itself, so collision resistance buys nothing, and a hash dependency would be a
/// poor trade in a core crate that has no dependencies and forbids `unsafe`.
fn digest(parts: &[String]) -> u64 {
    let mut hash = 0xcbf2_9ce4_8422_2325u64;
    for part in parts {
        for byte in part.as_bytes() {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
        }
        // A separator, so ("ab","c") and ("a","bc") cannot collide.
        hash ^= 0xff;
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash
}

/// Every input of both corpora. `quality.tsv` rows carry their own matching flags;
/// the sweep below overrides them so each input is pinned under both
/// configurations, which is what makes a one-configuration regression visible.
fn read_corpus() -> Vec<String> {
    let mut out = Vec::new();
    for line in include_str!("../../../data/eval/quality.tsv").lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<&str> = line.split('\t').collect();
        assert_eq!(fields.len(), 8, "quality.tsv row: {line:?}");
        out.push(fields[3].to_owned());
    }
    for line in include_str!("../../../data/eval/prefix.tsv").lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        out.push(line.to_owned());
    }
    assert!(!out.is_empty(), "empty corpus");
    out
}

/// Digest of every prefix's first page for one input under one configuration.
///
/// One session per input, read after every keystroke: one session per *prefix*
/// would create 55,520 dictionary-backed sessions, and reading the page at each
/// prefix is the stronger statement anyway — it pins the page at every prefix, not
/// only at the whole spelling. Each part carries the prefix length, so a page that
/// appears at a different prefix counts as a change.
fn page_digest(dictionary: &Arc<Dictionary>, input: &str, flags: u32) -> u64 {
    let mut session = Session::new(Arc::clone(dictionary));
    assert!(session.configure(9, true, false));
    assert!(session.configure_matching(flags));
    assert!(session.configure_incremental(true));
    let mut parts = Vec::new();
    for (at, c) in input.chars().enumerate() {
        session.process(Key::Character(c), Modifiers::default());
        parts.push(format!("{}#", at + 1));
        for i in 0..session.candidate_count() {
            let candidate = session.candidate(i).unwrap();
            parts.push(format!(
                "{}|{}|{}",
                candidate.text, candidate.pinyin, candidate.frequency
            ));
        }
    }
    session.reset();
    digest(&parts)
}

fn baseline_lines(dictionary: &Arc<Dictionary>, corpus: &[String]) -> Vec<String> {
    let mut lines = Vec::new();
    for (name, flags) in CONFIGS {
        for input in corpus {
            let mut line = String::new();
            let _ = write!(
                line,
                "{name}\t{input}\t{:016x}",
                page_digest(dictionary, input, flags)
            );
            lines.push(line);
        }
    }
    lines
}

fn dictionary() -> Arc<Dictionary> {
    Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap())
}

/// Regenerate the baseline. Run only on the revision being frozen.
#[test]
#[ignore = "regenerates data/eval/first_page.txt"]
fn regenerate() {
    let dictionary = dictionary();
    let corpus = read_corpus();
    let lines = baseline_lines(&dictionary, &corpus);
    let body = format!(
        "# 澄音输入法 I24 首页候选基线。由 tests/first_page_baseline.rs 的 --ignored regenerate 生成，请勿手工编辑。\n\
         # 每行：配置 TAB 输入 TAB 摘要。摘要覆盖该输入的**每一个前缀**的首页（候选文字|拼音|频次，含前缀长度）。\n\
         # 配置 no-rules=0；all-rules=全部模糊音与键盘纠错。语料 data/eval/quality.tsv + data/eval/prefix.tsv。\n{}\n",
        lines.join("\n")
    );
    std::fs::write("../../data/eval/first_page.txt", body).expect("write baseline");
    panic!(
        "regenerated {} lines; re-run without --ignored",
        lines.len()
    );
}

#[test]
fn every_prefix_keeps_its_first_page() {
    let dictionary = dictionary();
    let corpus = read_corpus();
    let expected: Vec<&str> = BASELINE
        .lines()
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .collect();
    let actual = baseline_lines(&dictionary, &corpus);
    assert_eq!(
        actual.len(),
        expected.len(),
        "baseline has {} lines, this build produced {}",
        expected.len(),
        actual.len()
    );
    let moved: Vec<String> = expected
        .iter()
        .zip(actual.iter())
        .filter(|(want, got)| want != got)
        .map(|(want, got)| format!("expected {want}\n     got {got}"))
        .collect();
    assert!(
        moved.is_empty(),
        "{} first pages moved:\n{}",
        moved.len(),
        moved.join("\n")
    );
}
