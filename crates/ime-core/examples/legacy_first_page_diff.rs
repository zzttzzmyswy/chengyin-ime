// SPDX-License-Identifier: GPL-3.0-or-later
//! One-off shape report for the legacy first page (MYS-2270 stage 1).
//!
//! The prefix lane was enabled for the non-incremental adapter, which is what
//! Fcitx 5 runs. The acceptance criterion is not "the page is better" but "the page
//! only grew": every input's Top-9 under `configure_incremental(false)` must keep its
//! old Top-1 and its old relative order, with the new prefix words appearing after
//! the whole-input candidates. This program measures exactly that, over both corpora
//! under both matching configurations, against a baseline dump of the previous build.
//!
//! Two steps, because the baseline has to come from the *other* commit:
//!
//!     # on the baseline revision (e.g. `main`)
//!     cargo run --release -p chengyin-core --example legacy_first_page_diff -- dump baseline.txt
//!     # on the revision being measured
//!     cargo run --release -p chengyin-core --example legacy_first_page_diff -- dump candidate.txt
//!     # anywhere: both dumps are read from disk
//!     cargo run --release -p chengyin-core --example legacy_first_page_diff -- compare baseline.txt candidate.txt
//!
//! The dump holds `config<TAB>input<TAB>page` with the page as `|`-joined text, so a
//! `diff` of two dumps shows every page that moved, and `compare` classifies them.

use chengyin_core::fuzzy::OPTIONS_MASK;
use chengyin_core::{Dictionary, Key, Modifiers, Session};
use std::collections::BTreeMap;
use std::sync::Arc;

/// The engine's visible page.
const TOP_N: usize = 9;

/// `data/eval/quality.tsv` column holding the spelling.
const QUALITY_INPUT_FIELD: usize = 3;

fn dictionary() -> Arc<Dictionary> {
    Arc::new(Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap())
}

/// Every input of both corpora, as `first_page_baseline` reads them.
fn corpus() -> Vec<String> {
    let mut out = Vec::new();
    for line in include_str!("../../../data/eval/quality.tsv").lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<&str> = line.split('\t').collect();
        assert!(fields.len() > QUALITY_INPUT_FIELD, "quality row: {line:?}");
        out.push(fields[QUALITY_INPUT_FIELD].to_owned());
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

/// The legacy adapter's visible page for one input under one matching configuration.
fn page(dictionary: &Arc<Dictionary>, input: &str, options: u32) -> String {
    let mut session = Session::new(Arc::clone(dictionary));
    assert!(session.configure(TOP_N, true, false));
    assert!(session.configure_matching(options));
    assert!(session.configure_incremental(false));
    for c in input.chars() {
        session.process(Key::Character(c), Modifiers::default());
    }
    (0..session.candidate_count())
        .map(|i| session.candidate(i).unwrap().text.to_owned())
        .collect::<Vec<_>>()
        .join("|")
}

fn configurations() -> [(u32, &'static str); 2] {
    [(0, "no-rules"), (OPTIONS_MASK, "all-rules")]
}

fn dump() -> Vec<String> {
    let dictionary = dictionary();
    let inputs = corpus();
    let mut lines = Vec::new();
    for (options, name) in configurations() {
        for input in &inputs {
            lines.push(format!(
                "{name}\t{input}\t{}",
                page(&dictionary, input, options)
            ));
        }
    }
    lines
}

/// `dump` restricted to one shard, so a corpus whose slowest inputs cost seconds
/// each can be split across cores and recombined by sorting:
///
///     for i in $(seq 0 19); do
///       legacy_first_page_diff dump-shard 20 $i out.$i.txt &
///     done; wait
///     cat out.*.txt > dump.txt
///
/// Sharding by index keeps the work balanced regardless of input length.
fn dump_shard(shards: usize, shard: usize) -> Vec<String> {
    assert!(shards > 0 && shard < shards, "shard {shard} of {shards}");
    let dictionary = dictionary();
    let inputs = corpus();
    let mut lines = Vec::new();
    for (options, name) in configurations() {
        for (at, input) in inputs.iter().enumerate() {
            if at % shards != shard {
                continue;
            }
            lines.push(format!(
                "{name}\t{input}\t{}",
                page(&dictionary, input, options)
            ));
        }
    }
    lines
}

/// How one input's page moved between the two builds.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum Shape {
    /// Byte-identical.
    Same,
    /// The old page is a prefix of the new one: nothing moved, words were appended.
    Appended,
    /// Same words, different order.
    Reordered,
    /// A different Top-1.
    Top1Changed,
    /// The legacy page was empty and now carries candidates: pure addition.
    Filled,
}

impl Shape {
    fn label(self) -> &'static str {
        match self {
            Shape::Same => "完全相同",
            Shape::Filled => "由空到有",
            Shape::Appended => "仅追加",
            Shape::Reordered => "重排",
            Shape::Top1Changed => "Top-1 改变",
        }
    }
}

fn shape(old: &[&str], new: &[&str]) -> Shape {
    if old == new {
        return Shape::Same;
    }
    // An empty old page has no Top-1 to change: the legacy page used to be blank
    // for these inputs, and every word on the new page is an addition.
    if old.is_empty() {
        return Shape::Filled;
    }
    if old.first() != new.first() {
        return Shape::Top1Changed;
    }
    if new.starts_with(old) {
        return Shape::Appended;
    }
    if old.iter().all(|w| new.contains(w)) && old.len() == new.len() {
        return Shape::Reordered;
    }
    // Same Top-1 and neither a pure append nor a permutation: the page kept its
    // head but lost a word, which is a reorder for reporting purposes.
    Shape::Reordered
}

fn main() {
    let mut args = std::env::args().skip(1);
    let mode = args.next().unwrap_or_else(|| "dump".to_owned());
    match mode.as_str() {
        "dump" => {
            let path = args.next().unwrap_or_else(|| "legacy_page.txt".to_owned());
            let lines = dump();
            std::fs::write(&path, format!("{}\n", lines.join("\n"))).expect("write dump");
            println!("wrote {} lines to {path}", lines.len());
        }
        "dump-shard" => {
            let shards: usize = args
                .next()
                .expect("dump-shard <shards> <shard> <out>")
                .parse()
                .unwrap();
            let shard: usize = args
                .next()
                .expect("dump-shard <shards> <shard> <out>")
                .parse()
                .unwrap();
            let path = args.next().expect("dump-shard <shards> <shard> <out>");
            let lines = dump_shard(shards, shard);
            std::fs::write(&path, format!("{}\n", lines.join("\n"))).expect("write shard");
        }
        "compare" => {
            // Both dumps are read from disk: each one costs a full corpus replay, so
            // the slower side is produced once on its own revision and reused.
            let before_path = args.next().expect("compare <baseline> <candidate>");
            let after_path = args.next().expect("compare <baseline> <candidate>");
            let before = read_dump(&before_path);
            let after = read_dump(&after_path);
            assert_eq!(
                before.len(),
                after.len(),
                "the two dumps cover a different number of (configuration, input) pairs"
            );
            let mut totals: BTreeMap<(&str, Shape), usize> = BTreeMap::new();
            let mut moved = Vec::new();
            for (key, old_words) in &before {
                let new_words = after
                    .get(key)
                    .unwrap_or_else(|| panic!("missing {}/{}", key.0, key.1));
                let shape = shape(
                    &old_words.iter().map(String::as_str).collect::<Vec<_>>(),
                    &new_words.iter().map(String::as_str).collect::<Vec<_>>(),
                );
                *totals.entry((key.0.as_str(), shape)).or_default() += 1;
                if matches!(shape, Shape::Reordered | Shape::Top1Changed) {
                    moved.push((key, shape, old_words, new_words));
                }
            }
            for (key, shape, old_words, new_words) in &moved {
                println!(
                    "{}\t{}\t{}\n  old: {}\n  new: {}",
                    shape.label(),
                    key.0,
                    key.1,
                    old_words.join(" "),
                    new_words.join(" ")
                );
            }
            println!("\n| 配置 | 形状 | 组数 |\n|---|---|---|");
            for ((config, shape), count) in &totals {
                println!("| {config} | {} | {count} |", shape.label());
            }
        }
        other => panic!("unknown mode {other}"),
    }
}

/// `config<TAB>input<TAB>page`, the page `|`-joined.
fn read_dump(path: &str) -> BTreeMap<(String, String), Vec<String>> {
    let text = std::fs::read_to_string(path).expect("read dump");
    text.lines()
        .filter(|line| !line.is_empty())
        .map(|line| {
            let mut fields = line.split('\t');
            let config = fields.next().expect("config").to_owned();
            let input = fields.next().expect("input").to_owned();
            let page = fields.next().unwrap_or_default();
            (
                (config, input),
                page.split('|')
                    .filter(|word| !word.is_empty())
                    .map(str::to_owned)
                    .collect(),
            )
        })
        .collect()
}
