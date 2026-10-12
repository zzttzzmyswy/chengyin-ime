// SPDX-License-Identifier: GPL-3.0-or-later
//! A2 census: what the sentence DP costs and whether it reaches the first page
//! (I24, `docs/PERFORMANCE.md`).
//!
//! A2 asked whether the sentence stage can be gated or truncated without changing
//! any first page. Deciding that needs three facts, all measured here on one
//! replay per input:
//!
//! * **Where the sentence DP spends time.** `decode` is timed per input, and each
//!   keystroke is attributed to one of three states: a whole-input attested word
//!   exists (`Decoder::compute` returns before building the sentence graph at
//!   all), an exact full composition exists (`render`'s suppression gate already
//!   discards speculative sentences), or neither. A gate can only save time in
//!   the states it actually fires in.
//! * **Whether sentence candidates reach the first page.** A candidate is a
//!   synthesized sentence exactly when `Candidate::frequency` is 0:
//!   `Session::result_candidate` reports 0 for every sentence and a non-zero count
//!   for every lexicon, learned and association row. The census reports sentence
//!   slots in the Top-9, both overall and inside each state, because "the gate
//!   fires here" and "the gate changes output here" are different questions.
//! * **Whether dropping sentence candidates would change a first page.** The
//!   gate ceiling: inputs whose first page holds no sentence at all. Truncating
//!   before the first page is only output-safe where the page is already
//!   sentence-free.
//!
//! It also dumps every first page in a stable order, so `diff` between two
//! revisions *is* the first-page equivalence evidence rather than a re-derivation
//! of it:
//!
//!     cargo run --release -p chengyin-core --example decode_census --locked > before.txt
//!     diff before.txt after.txt
//!
//! Observational only: one fresh [`Session`] per input, key by key, no threads.
//! The timing columns are this machine's numbers, not a portability claim.

use chengyin_core::{Dictionary, Key, Modifiers, Session};
use std::sync::Arc;
use std::time::Instant;

/// Matching configurations the acceptance criteria name. `initials` is not a flag
/// but an input shape, so it is selected by replaying only the initials-mixed
/// inputs under the full mask; see [`Group::configs`].
const NO_RULES: u32 = 0;
const ALL_RULES: u32 = chengyin_core::fuzzy::OPTIONS_MASK;

fn letters(spelling: &str) -> usize {
    spelling.bytes().filter(|&b| b != b'\'').count()
}

/// Whether the lexicon attests a multi-character word covering exactly `input`.
/// This is the condition `Decoder::compute` records in `protected_word` and
/// short-circuits on, and the condition librime gates its sentence stage on.
fn has_whole_input_word(dictionary: &Dictionary, input: &str) -> bool {
    let Ok(words) = dictionary.lookup(input) else {
        return false;
    };
    (0..words.len()).any(|i| {
        let c = words.get(dictionary, i).expect("in range");
        letters(c.pinyin) == letters(input) && c.text.chars().nth(1).is_some()
    })
}

struct Census {
    input: String,
    flags: u32,
    whole_word: bool,
    /// Nanoseconds spent inside `process` for every keystroke of this input.
    key_ns: u64,
    /// The first page: text, pinyin and frequency, in order.
    page: Vec<(String, String, u32)>,
}

impl Census {
    fn sentence_slots(&self) -> usize {
        self.page.iter().filter(|(_, _, f)| *f == 0).count()
    }
}

fn replay(dictionary: &Arc<Dictionary>, input: &str, flags: u32) -> Census {
    let mut s = Session::new(Arc::clone(dictionary));
    assert!(s.configure(9, true, false));
    assert!(s.configure_matching(flags));
    assert!(s.configure_incremental(true));
    let mut key_ns = 0u64;
    for c in input.chars() {
        let start = Instant::now();
        s.process(Key::Character(c), Modifiers::default());
        for i in 0..s.candidate_count() {
            std::hint::black_box(s.candidate_marks(i));
        }
        key_ns += start.elapsed().as_nanos() as u64;
    }
    let page = (0..s.candidate_count())
        .map(|i| {
            let c = s.candidate(i).unwrap();
            (c.text.to_owned(), c.pinyin.to_owned(), c.frequency)
        })
        .collect();
    Census {
        input: input.to_owned(),
        flags,
        whole_word: has_whole_input_word(dictionary, input),
        key_ns,
        page,
    }
}

/// A corpus plus the matching configurations to replay it under.
struct Group {
    label: String,
    inputs: Vec<(String, u32)>,
}

fn read_inputs(path: &str) -> Vec<(String, u32)> {
    let source = std::fs::read_to_string(path).unwrap_or_else(|e| panic!("{path}: {e}"));
    let mut out = Vec::new();
    for line in source.lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<&str> = line.split('\t').collect();
        match fields.len() {
            // quality.tsv: category split flags input expected train bucket note
            8 => out.push((fields[3].to_owned(), fields[2].parse().expect("flags"))),
            // prefix.tsv: one bare input per line.
            1 => out.push((fields[0].to_owned(), 0)),
            n => panic!("{path}: {n} fields in {line:?}"),
        }
    }
    out
}

/// Bench corpora: A2 target scenarios that live in the bench source, not in a
/// corpus file, so they are spelled out here to keep the census self-contained.
fn bench_inputs() -> Vec<(String, u32)> {
    [
        ("zhnag", ALL_RULES),
        ("zhng", ALL_RULES),
        ("zhsng", ALL_RULES),
        ("zhaang", ALL_RULES),
        ("nizhnaghao", ALL_RULES),
        ("jintiantianqihenhao", ALL_RULES),
        ("yingshe", ALL_RULES),
        ("yinshe", ALL_RULES),
        ("yignshe", ALL_RULES),
        ("ssdd", ALL_RULES),
        ("zgrm", ALL_RULES),
        ("zhrm", ALL_RULES),
        ("zg", ALL_RULES),
        ("zzbds", ALL_RULES),
        ("rmyh", ALL_RULES),
        ("bao", ALL_RULES),
        ("shi", ALL_RULES),
        ("hao", ALL_RULES),
        ("an", ALL_RULES),
        ("xian", ALL_RULES),
        ("nihao", NO_RULES),
        ("jintiantianqihenhao", NO_RULES),
        ("womenmingtianjian", NO_RULES),
        ("woxihuanzhongwen", NO_RULES),
        (
            "woxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwen",
            NO_RULES,
        ),
        ("zg", NO_RULES),
        ("zhongg", NO_RULES),
        ("zguo", NO_RULES),
        ("wxhzw", NO_RULES),
        ("jttqhh", NO_RULES),
        ("womenmtj", NO_RULES),
    ]
    .into_iter()
    .map(|(s, f)| (s.to_owned(), f))
    .collect()
}

fn report(label: &str, census: &[Census]) {
    let inputs = census.len();
    let whole_word = census.iter().filter(|c| c.whole_word).count();
    let slots: usize = census.iter().map(|c| c.page.len()).sum();
    let sentences: usize = census.iter().map(Census::sentence_slots).sum();
    let with_sentence = census
        .iter()
        .filter(|c| c.page.iter().any(|(_, _, f)| *f == 0))
        .count();
    // The gate ceiling: inputs whose first page is already sentence-free, so
    // dropping every sentence candidate provably reproduces the page.
    let gate_safe = inputs - with_sentence;
    let total_ns: u64 = census.iter().map(|c| c.key_ns).sum();
    let ww: Vec<&Census> = census.iter().filter(|c| c.whole_word).collect();
    let ww_ns: u64 = ww.iter().map(|c| c.key_ns).sum();
    let ww_slots: usize = ww.iter().map(|c| c.page.len()).sum();
    let ww_sentences: usize = ww.iter().map(|c| c.sentence_slots()).sum();

    println!("\n== {label} ==");
    println!("inputs={inputs}");
    println!(
        "whole-input attested word: {whole_word} inputs ({:.1}%), {:.1}% of this corpus's keystroke time",
        100.0 * whole_word as f64 / inputs as f64,
        100.0 * ww_ns as f64 / total_ns.max(1) as f64,
    );
    println!(
        "first page: slots={slots} (mean {:.2}/input), sentence candidates={sentences} ({:.2}% of slots), inputs with any={with_sentence}",
        slots as f64 / inputs as f64,
        100.0 * sentences as f64 / slots.max(1) as f64,
    );
    println!(
        "gate ceiling: {gate_safe} of {inputs} inputs ({:.1}%) already have a sentence-free first page",
        100.0 * gate_safe as f64 / inputs as f64
    );
    println!(
        "  inside the whole-input-word subset: {} inputs, slots={ww_slots}, sentence candidates={ww_sentences} ({:.2}%)",
        ww.len(),
        100.0 * ww_sentences as f64 / ww_slots.max(1) as f64,
    );
    // Per-keystroke time split by state: the ceiling on what a gate firing in that
    // state could save. Reported as mean microseconds per keystroke.
    let keys = |subset: &[&Census]| -> (usize, u64) {
        let n: usize = subset.iter().map(|c| c.input.chars().count()).sum();
        let ns: u64 = subset.iter().map(|c| c.key_ns).sum();
        (n, ns)
    };
    for (name, subset) in [
        ("whole-input word present", ww.clone()),
        (
            "no whole-input word",
            census.iter().filter(|c| !c.whole_word).collect(),
        ),
    ] {
        let (n, ns) = keys(&subset);
        println!(
            "  {name}: inputs={} keystrokes={n} mean={:.1} µs/key",
            subset.len(),
            ns as f64 / n.max(1) as f64 / 1e3,
        );
    }
}

fn main() {
    let bytes = include_bytes!("../../../data/daily.mswydict");
    let dictionary = Arc::new(Dictionary::from_binary(bytes).unwrap());
    let args: Vec<String> = std::env::args().skip(1).collect();
    let paths: Vec<String> = if args.is_empty() {
        vec![
            "data/eval/quality.tsv".into(),
            "data/eval/prefix.tsv".into(),
        ]
    } else {
        args
    };
    let mut groups: Vec<Group> = paths
        .into_iter()
        .map(|path| Group {
            inputs: read_inputs(&path),
            label: path,
        })
        .collect();
    groups.push(Group {
        label: "benches/latency.rs corpora".into(),
        inputs: bench_inputs(),
    });

    let mut all: Vec<Census> = Vec::new();
    for group in &groups {
        let census: Vec<Census> = group
            .inputs
            .iter()
            .map(|(input, flags)| replay(&dictionary, input, *flags))
            .collect();
        report(&group.label, &census);
        all.extend(census);
    }

    // The three matching configurations the acceptance criteria name. The census
    // above replays each corpus under its own row flags; these two sweeps pin the
    // same inputs under one configuration each, so a change that only shows up
    // under one of them cannot hide.
    println!("\n== configurations ==");
    for (name, flags) in [("no-rules", NO_RULES), ("all-rules", ALL_RULES)] {
        for group in &groups {
            if group.label != "data/eval/prefix.tsv" {
                continue;
            }
            let census: Vec<Census> = group
                .inputs
                .iter()
                .map(|(input, _)| replay(&dictionary, input, flags))
                .collect();
            report(&format!("{} under {name}", group.label), &census);
            all.extend(census);
        }
    }

    println!("\n== first pages ==");
    for c in &all {
        println!(
            "PAGE\t{}\tflags={}\t{}",
            c.input,
            c.flags,
            c.page
                .iter()
                .map(|(t, p, _)| format!("{t}|{p}"))
                .collect::<Vec<_>>()
                .join(" ")
        );
    }
}
