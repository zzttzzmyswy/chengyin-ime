// SPDX-License-Identifier: GPL-3.0-or-later
//! I24 A/B latency runner for the two target scenarios, plus the corpus census
//! sweep (`docs/PERFORMANCE.md`).
//!
//! `benches/latency.rs` is the project's benchmark and is not the place for a
//! variant-versus-baseline comparison: the two runs must be *separate binaries*
//! built from two revisions, alternated on the same machine, so that code layout
//! and machine drift cannot be mistaken for a change. This example is the half
//! that runs inside one revision; `scripts/bench_ab.sh` builds both revisions and
//! alternates them.
//!
//! It prints one line per scenario: `P50/P95/P99` in nanoseconds over every
//! keystroke of every replay, which is the per-key distribution the project's
//! budgets are stated in. Scenarios:
//!
//! * `target-correction` — the six typo/long inputs under the full matching mask,
//!   the scenario the I17 profile left at P99 ≈ 12 ms;
//! * `target-daily` — the five unseparated daily inputs including the 60-byte one;
//! * `corpus-prefix` — every keystroke of every prefix of `data/eval/prefix.tsv`,
//!   the sweep whose P99 answers "did anything get worse anywhere".
//!
//! Run:
//!     CHENGYIN_BENCH_ROUNDS=300 CHENGYIN_SWEEP_PASSES=8 cargo run --release -p chengyin-core \
//!         --example latency_scenarios --locked

use chengyin_core::{Dictionary, Key, Modifiers, Session};
use std::hint::black_box;
use std::sync::Arc;
use std::time::Instant;

const CORRECTION: &[&str] = &[
    "zhnag",
    "zhng",
    "zhsng",
    "zhaang",
    "nizhnaghao",
    "jintiantianqihenhao",
];
const DAILY: &[&str] = &[
    "nihao",
    "jintiantianqihenhao",
    "womenmingtianjian",
    "woxihuanzhongwen",
    "woxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwen",
];
/// Every-Nth input of `data/eval/prefix.tsv` used for the latency sweep. See the
/// comment at its use site for why the sweep is a slice.
const PREFIX_STRIDE: usize = 8;

/// Whether the named scenario was selected on the command line. No argument means
/// all of them, which is what the recorded A/B runs use; naming one keeps a
/// single-scenario check (during development, or to re-read one table) cheap.
fn selected(name: &str) -> bool {
    let args: Vec<String> = std::env::args().skip(1).collect();
    args.is_empty() || args.iter().any(|a| a == name)
}

fn rounds() -> usize {
    std::env::var("CHENGYIN_BENCH_ROUNDS")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .filter(|&n| n > 0)
        .unwrap_or(300)
}

fn report(name: &str, mut samples: Vec<u64>) {
    samples.sort_unstable();
    let n = samples.len();
    println!(
        "{name}: n={n} p50={}ns p95={}ns p99={}ns max={}ns",
        samples[n / 2],
        samples[n * 95 / 100],
        samples[n * 99 / 100],
        samples[n - 1],
    );
}

/// One replay: type `spelling`, refreshing the visible annotations after every
/// key the way a host does. The sample is the whole per-key refresh.
fn replay(session: &mut Session, spelling: &str, samples: &mut Vec<u64>) {
    session.reset();
    for c in spelling.chars() {
        let start = Instant::now();
        black_box(session.process(Key::Character(c), Modifiers::default()));
        for i in 0..session.candidate_count() {
            black_box(session.candidate_marks(i));
        }
        samples.push(start.elapsed().as_nanos() as u64);
    }
}

fn main() {
    let bytes = include_bytes!("../../../data/daily.mswydict");
    let daily = Arc::new(Dictionary::from_binary(bytes).unwrap());
    let mut session = Session::new(Arc::clone(&daily));
    assert!(session.configure_incremental(true));
    let rounds = rounds();

    if selected("target-correction") {
        session.configure_matching(chengyin_core::fuzzy::OPTIONS_MASK);
        let mut samples = Vec::new();
        for _ in 0..rounds {
            for spelling in CORRECTION {
                replay(&mut session, spelling, &mut samples);
            }
        }
        report("target-correction", samples);
    }

    if selected("target-daily") {
        session.configure_matching(0);
        let mut samples = Vec::new();
        for _ in 0..rounds {
            for spelling in DAILY {
                replay(&mut session, spelling, &mut samples);
            }
        }
        report("target-daily", samples);
    }

    if !selected("corpus-prefix") {
        return;
    }

    // A deterministic every-Nth slice of the prefix corpus, not the whole file:
    // these are unseparated 1..4-word spellings, so decoding one is millisecond
    // scale and the full 2000 would put a single sweep at several minutes, which
    // an alternated A/B cannot afford to repeat. Every 8th input keeps 250
    // spellings and ~6.5k keystrokes per pass — more samples per pass than the two
    // target scenarios produce together — spread evenly over the file's length
    // distribution. The *full* file is still the corpus for the equivalence and
    // census checks.
    //
    // The sweep has its own pass count rather than sharing `CHENGYIN_BENCH_ROUNDS`:
    // the target scenarios are short replays that a high round count makes precise,
    // while one sweep pass already costs seconds, so scaling it by the target
    // scenario's round count would make an alternated run unaffordable without
    // buying precision the target scenarios do not already provide.
    let source =
        std::fs::read_to_string("data/eval/prefix.tsv").expect("read data/eval/prefix.tsv");
    let inputs: Vec<&str> = source
        .lines()
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .step_by(PREFIX_STRIDE)
        .collect();
    let passes = std::env::var("CHENGYIN_SWEEP_PASSES")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .filter(|&n| n > 0)
        .unwrap_or(8);
    let mut samples = Vec::new();
    for _ in 0..passes {
        for input in &inputs {
            replay(&mut session, input, &mut samples);
        }
    }
    report("corpus-prefix", samples);
}
