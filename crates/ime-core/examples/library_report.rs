// SPDX-License-Identifier: GPL-3.0-or-later
//! R12 resource report: what a multi-vocabulary custom library costs.
//!
//! A Windows custom-vocabulary library holds up to 64 vocabularies that are merged
//! with the built-in base into one immutable dictionary. This measures the real
//! merge path (`Dictionary::merge_all`) for 1/8/32/64 libraries, plus a single
//! import and an enable-toggle re-merge, so the layout decision rests on
//! measurement rather than on the 64 MiB file limit (review R12).
//!
//! Run: cargo run --release -p chengyin-core --example library_report --locked

use chengyin_core::Dictionary;
use std::hint::black_box;
use std::sync::Arc;
use std::time::Instant;

/// Current resident-set growth across one action, in bytes and microseconds.
/// `VmRSS` (not `VmHWM`) is read: the high-water mark only ever rises, so it
/// cannot show the cost of a later, smaller step. Linux-only; this is an
/// observed RSS delta including allocator retention, not an allocation count.
fn rss_delta(action: impl FnOnce()) -> (i64, u128) {
    let before = rss_bytes();
    let start = Instant::now();
    action();
    let micros = start.elapsed().as_micros();
    (rss_bytes() - before, micros)
}

fn rss_bytes() -> i64 {
    std::fs::read_to_string("/proc/self/status")
        .ok()
        .and_then(|text| {
            text.lines()
                .find(|l| l.starts_with("VmRSS:"))
                .and_then(|l| l.split_whitespace().nth(1)?.parse::<i64>().ok())
        })
        .map(|kib| kib * 1024)
        .unwrap_or(0)
}

fn peak_rss() -> i64 {
    std::fs::read_to_string("/proc/self/status")
        .ok()
        .and_then(|text| {
            text.lines()
                .find(|l| l.starts_with("VmHWM:"))
                .and_then(|l| l.split_whitespace().nth(1)?.parse::<i64>().ok())
        })
        .map(|kib| kib * 1024)
        .unwrap_or(0)
}

fn percentile(samples: &mut [u128], p: usize) -> u128 {
    samples.sort_unstable();
    samples[(samples.len() * p / 100).min(samples.len() - 1)]
}

fn main() {
    let bytes = include_bytes!("../../../data/daily.mswydict");
    let base = Arc::new(Dictionary::from_binary(bytes).unwrap());
    println!(
        "hardware: {} cores, base dictionary entries={} binary={}bytes heap={}bytes initialsTruncated={}",
        std::thread::available_parallelism().map(|n| n.get()).unwrap_or(0),
        base.entry_count(),
        bytes.len(),
        base.estimated_heap_bytes(),
        base.initials_truncated_words()
    );
    // A synthetic library row: a distinct two-character word per vocabulary, so a
    // 64-row library measures merge cost without redistributing third-party data.
    let row = |index: usize| format!("ce'shi\t测{index}\t{}\n", 1000 + index);
    for count in [1usize, 8, 32, 64] {
        let mut parts = vec![base.as_ref()];
        let owned: Vec<Dictionary> = (0..count)
            .map(|i| Dictionary::from_tsv(&row(i)).unwrap())
            .collect();
        parts.extend(owned.iter());
        let mut samples = Vec::new();
        let mut merged = None;
        for _ in 0..5 {
            let start = Instant::now();
            let result = Dictionary::merge_all(parts.iter().copied()).unwrap();
            samples.push(start.elapsed().as_micros());
            black_box(result.entry_count());
            merged = Some(result);
        }
        let (rss, _) = rss_delta(|| {
            black_box(
                Dictionary::merge_all(parts.iter().copied())
                    .unwrap()
                    .entry_count(),
            );
        });
        let result = merged.unwrap();
        println!(
            "libraries={count:>2} entries={} merge p50={}us p95={}us max={}us rssDelta={}KiB heap={}bytes",
            result.entry_count(),
            percentile(&mut samples, 50),
            percentile(&mut samples, 95),
            samples.iter().copied().max().unwrap_or(0),
            rss / 1024,
            result.estimated_heap_bytes()
        );
    }
    // One import: the settings path converts a TSV file into a stored binary row.
    let source = row(0);
    let (rss, micros) = rss_delta(|| {
        let imported = Dictionary::from_tsv(&source).unwrap();
        black_box(imported.to_binary().len());
    });
    println!("single import: {micros}us rssDelta={}KiB", rss / 1024);
    // Enable toggle: the library is reparsed and re-merged from scratch, which is
    // the cost a checkbox click pays today.
    let owned: Vec<Dictionary> = (0..64)
        .map(|i| Dictionary::from_tsv(&row(i)).unwrap())
        .collect();
    let mut parts = vec![base.as_ref()];
    parts.extend(owned.iter());
    let mut samples = Vec::new();
    for _ in 0..5 {
        let start = Instant::now();
        let merged = Dictionary::merge_all(parts.iter().copied()).unwrap();
        samples.push(start.elapsed().as_micros());
        black_box(merged.entry_count());
    }
    println!(
        "enable toggle at 64 libraries: re-merge p50={}us p95={}us",
        percentile(&mut samples, 50),
        percentile(&mut samples, 95)
    );
    // The layout question this report exists for: is merging pair by pair (the
    // former behaviour) worse than compiling the union once? Folding rebuilds the
    // whole accumulated dictionary per library; merge_all touches each row once.
    let mut pairwise = Vec::new();
    for _ in 0..3 {
        let start = Instant::now();
        // Fold one library at a time: the former behaviour, which rebuilds the whole
        // accumulated dictionary on every step.
        let mut accumulated = base.as_ref().merge(&owned[0]).unwrap();
        for library in &owned[1..] {
            accumulated = accumulated.merge(library).unwrap();
        }
        pairwise.push(start.elapsed().as_micros());
        black_box(accumulated.entry_count());
    }
    let mut oneshot = Vec::new();
    for _ in 0..3 {
        let start = Instant::now();
        let merged = Dictionary::merge_all(parts.iter().copied()).unwrap();
        oneshot.push(start.elapsed().as_micros());
        black_box(merged.entry_count());
    }
    println!(
        "pairwise fold at 64 libraries: p50={}us  |  one-shot union: p50={}us  ratio={:.1}x",
        percentile(&mut pairwise, 50),
        percentile(&mut oneshot, 50),
        percentile(&mut pairwise, 50) as f64 / percentile(&mut oneshot, 50).max(1) as f64
    );
    println!(
        "process peak RSS (VmHWM) after all runs: {}KiB",
        peak_rss() / 1024
    );
    println!(
        "note: VmRSS deltas include allocator retention and are single observations, not a steady-state budget claim"
    );
}
