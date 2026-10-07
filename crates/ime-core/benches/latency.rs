use chengyin_core::{demo_dictionary, Dictionary, Key, Modifiers, Profile, Session};
use std::hint::black_box;
use std::sync::Arc;
use std::time::Instant;

fn measure(name: &str, iterations: usize, mut action: impl FnMut()) {
    for _ in 0..200 {
        action();
    }
    let mut samples = Vec::with_capacity(iterations);
    for _ in 0..iterations {
        let start = Instant::now();
        action();
        samples.push(start.elapsed().as_nanos());
    }
    samples.sort_unstable();
    println!(
        "{name}: n={iterations} p50={}ns p95={}ns p99={}ns max={}ns",
        samples[iterations / 2],
        samples[iterations * 95 / 100],
        samples[iterations * 99 / 100],
        samples[iterations - 1]
    );
}

fn main() {
    let bytes = include_bytes!("../../../data/daily.mswydict");
    let start = Instant::now();
    let daily = Arc::new(Dictionary::from_binary(bytes).unwrap());
    println!(
        "daily: entries={} binary={}bytes load={}us estimated_heap={}bytes",
        daily.entry_count(),
        bytes.len(),
        start.elapsed().as_micros(),
        daily.estimated_heap_bytes()
    );
    let mut real = Session::new(Arc::clone(&daily));
    assert!(real.configure_incremental(true));
    println!(
        "session capacity+inline={}bytes",
        real.estimated_heap_bytes() + std::mem::size_of::<Session>()
    );
    let rounds = std::env::var("CHENGYIN_BENCH_ROUNDS")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .filter(|&n| n > 0)
        .unwrap_or(2000);
    let corpus = [
        "nihao",
        "jintiantianqihenhao",
        "womenmingtianjian",
        "woxihuanzhongwen",
        "woxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwenwoxihuanzhongwen",
    ];
    real.configure_matching(chengyin_core::fuzzy::OPTIONS_MASK);
    let mut corrected_samples = Vec::new();
    for _ in 0..rounds {
        for spelling in [
            "zhnag",
            "zhng",
            "zhsng",
            "zhaang",
            "nizhnaghao",
            "jintiantianqihenhao",
        ] {
            real.reset();
            for c in spelling.chars() {
                let start = Instant::now();
                black_box(real.process(Key::Character(c), Modifiers::default()));
                for i in 0..real.candidate_count() {
                    black_box(real.candidate_marks(i));
                }
                corrected_samples.push(start.elapsed().as_nanos());
            }
        }
    }
    corrected_samples.sort_unstable();
    let n = corrected_samples.len();
    println!("all matching flags, 6 inputs, per key + all visible annotations: n={n} p50={}ns p95={}ns p99={}ns max={}ns",corrected_samples[n/2],corrected_samples[n*95/100],corrected_samples[n*99/100],corrected_samples[n-1]);
    let mut syllable_samples = Vec::new();
    for _ in 0..rounds {
        for spelling in ["bao", "shi", "hao", "an", "xian"] {
            real.reset();
            for c in spelling.chars() {
                let start = Instant::now();
                black_box(real.process(Key::Character(c), Modifiers::default()));
                for i in 0..real.candidate_count() {
                    black_box(real.candidate_marks(i));
                }
                syllable_samples.push(start.elapsed().as_nanos());
            }
        }
    }
    syllable_samples.sort_unstable();
    let n = syllable_samples.len();
    println!("all matching flags, 5 complete syllables, per key + visible annotations: n={n} p50={}ns p95={}ns p99={}ns max={}ns",syllable_samples[n/2],syllable_samples[n*95/100],syllable_samples[n*99/100],syllable_samples[n-1]);
    let mut initials_samples = Vec::new();
    let mut initials_final_samples = Vec::new();
    for _ in 0..rounds {
        for spelling in ["ssdd", "zgrm", "zhrm", "zg", "zzbds", "rmyh"] {
            real.reset();
            for (at, c) in spelling.chars().enumerate() {
                let start = Instant::now();
                black_box(real.process(Key::Character(c), Modifiers::default()));
                for i in 0..real.candidate_count() {
                    black_box(real.candidate_marks(i));
                }
                let elapsed = start.elapsed().as_nanos();
                initials_samples.push(elapsed);
                if at + 1 == spelling.len() {
                    initials_final_samples.push(elapsed);
                }
            }
        }
    }
    for (scope, samples) in [
        ("all keys", &mut initials_samples),
        ("final key", &mut initials_final_samples),
    ] {
        samples.sort_unstable();
        let n = samples.len();
        println!("all matching flags, 6 initial-head inputs, {scope} + visible annotations: n={n} p50={}ns p95={}ns p99={}ns max={}ns",samples[n/2],samples[n*95/100],samples[n*99/100],samples[n-1]);
    }
    let mut mapping_samples = Vec::new();
    for _ in 0..rounds {
        for spelling in ["yingshe", "yinshe", "yignshe"] {
            real.reset();
            for c in spelling.chars() {
                let start = Instant::now();
                black_box(real.process(Key::Character(c), Modifiers::default()));
                for i in 0..real.candidate_count() {
                    black_box(real.candidate_marks(i));
                }
                mapping_samples.push(start.elapsed().as_nanos());
            }
        }
    }
    mapping_samples.sort_unstable();
    let n = mapping_samples.len();
    println!("all matching flags, yingshe/yinshe/yignshe, per key + all visible annotations: n={n} p50={}ns p95={}ns p99={}ns max={}ns",mapping_samples[n/2],mapping_samples[n*95/100],mapping_samples[n*99/100],mapping_samples[n-1]);
    real.reset();
    real.configure_matching(0);
    let mut key_samples =
        Vec::with_capacity(rounds * corpus.iter().map(|s| s.len()).sum::<usize>());
    for _ in 0..rounds {
        for replay in corpus {
            real.reset();
            for character in replay.chars() {
                let start = Instant::now();
                black_box(real.process(Key::Character(character), Modifiers::default()));
                key_samples.push(start.elapsed().as_nanos());
            }
        }
    }
    key_samples.sort_unstable();
    let n = key_samples.len();
    println!("daily per-key replay (5 inputs, including 60 bytes): n={n} p50={}ns p95={}ns p99={}ns max={}ns",key_samples[n/2],key_samples[n*95/100],key_samples[n*99/100],key_samples[n-1]);
    measure("daily.lookup(shi)", 20000, || {
        black_box(daily.lookup(black_box("shi")).unwrap());
    });
    measure(
        "daily: woxihuanzhongwen + space (16 events)",
        rounds * 5,
        || {
            real.reset();
            for c in "woxihuanzhongwen".chars() {
                black_box(real.process(Key::Character(c), Modifiers::default()));
            }
            black_box(real.process(Key::Space, Modifiers::default()));
            black_box(real.commit());
        },
    );
    measure("daily: shi + 20 pages (23 events)", rounds * 3, || {
        real.reset();
        for c in "shi".chars() {
            black_box(real.process(Key::Character(c), Modifiers::default()));
        }
        for _ in 0..20 {
            black_box(real.process(Key::PageDown, Modifiers::default()));
        }
    });
    // Mid-composition editing: the review asks for per-key, marking, editing and
    // paging to be measured separately, and editing was the missing one. Each
    // sample is one edit key inside an active composition, with the visible
    // annotations refreshed the way a host would after it.
    let mut editing_samples = Vec::new();
    for _ in 0..rounds {
        real.reset();
        for c in "zhongguoren".chars() {
            black_box(real.process(Key::Character(c), Modifiers::default()));
        }
        for (key, times) in [
            (Key::Left, 3),
            (Key::Right, 1),
            (Key::Backspace, 2),
            (Key::Character('o'), 2),
            (Key::Delete, 1),
            (Key::Home, 1),
            (Key::End, 1),
        ] {
            for _ in 0..times {
                let start = Instant::now();
                black_box(real.process(key, Modifiers::default()));
                for i in 0..real.candidate_count() {
                    black_box(real.candidate_marks(i));
                }
                editing_samples.push(start.elapsed().as_nanos());
            }
        }
    }
    editing_samples.sort_unstable();
    let n = editing_samples.len();
    println!("daily: mid-composition editing (11 edit keys) + visible annotations: n={n} p50={}ns p95={}ns p99={}ns max={}ns",editing_samples[n/2],editing_samples[n*95/100],editing_samples[n*99/100],editing_samples[n-1]);
    let mut modern_samples = Vec::new();
    for _ in 0..rounds {
        for spelling in ["zg", "zhongg", "zguo", "wxhzw", "jttqhh", "womenmtj"] {
            real.reset();
            for c in spelling.chars() {
                let start = Instant::now();
                black_box(real.process(Key::Character(c), Modifiers::default()));
                modern_samples.push(start.elapsed().as_nanos());
            }
        }
    }
    modern_samples.sort_unstable();
    let n = modern_samples.len();
    println!(
        "modern per-key replay (6 inputs): n={n} p50={}ns p95={}ns p99={}ns max={}ns",
        modern_samples[n / 2],
        modern_samples[n * 95 / 100],
        modern_samples[n * 99 / 100],
        modern_samples[n - 1]
    );
    let mut context_samples = Vec::new();
    for _ in 0..rounds {
        real.reset();
        for spelling in ["nihao", "wxhzw", "jttqhh", "zhongg"] {
            for c in spelling.chars() {
                let start = Instant::now();
                black_box(real.process(Key::Character(c), Modifiers::default()));
                context_samples.push(start.elapsed().as_nanos());
            }
            black_box(real.process(Key::Space, Modifiers::default()));
            black_box(real.commit());
        }
    }
    context_samples.sort_unstable();
    let n = context_samples.len();
    println!("continuous per-key with context (4 compositions): n={n} p50={}ns p95={}ns p99={}ns max={}ns",context_samples[n/2],context_samples[n*95/100],context_samples[n*99/100],context_samples[n-1]);
    // Worst bounded profile: many homophones, immutable snapshot shared with host.
    let mut preferences = Profile::default();
    for i in 0..8190 {
        let text = format!("词{}", char::from_u32(0x4e00 + i).unwrap());
        assert!(preferences.record("shi", &text));
    }
    assert!(preferences.record("nihao", "拟好"));
    assert!(preferences.record("wxhzw", "我喜欢中文"));
    println!(
        "profile: records={} binary={}bytes",
        preferences.entry_count(),
        preferences.to_binary().len()
    );
    let preferences = Arc::new(preferences);
    real.reset();
    assert!(real.set_profile(Arc::clone(&preferences)));
    measure(
        "8192-record profile: shi + space (4 events, no persistence)",
        rounds * 3,
        || {
            real.reset();
            for c in "shi".chars() {
                black_box(real.process(Key::Character(c), Modifiers::default()));
            }
            black_box(real.process(Key::Space, Modifiers::default()));
        },
    );
    real.reset();
    real.configure_matching(chengyin_core::fuzzy::OPTIONS_MASK);
    let before = real.history_cache_stats();
    measure(
        "8192-record profile: repeated nihao with all matching flags (no acknowledgment/disk)",
        rounds * 3,
        || {
            real.reset();
            for c in "nihao".chars() {
                black_box(real.process(Key::Character(c), Modifiers::default()));
            }
        },
    );
    let after = real.history_cache_stats();
    let hits = after.hits - before.hits;
    let misses = after.misses - before.misses;
    println!("adaptive history cache on repeated nihao: hits={hits} misses={misses} hit_rate={:.2}% capacity={} entries={}",
        100.0 * hits as f64 / (hits + misses) as f64, after.capacity, after.entries);
    real.reset();
    real.configure_matching(0);
    let mut acknowledgments = Vec::with_capacity(rounds);
    for _ in 0..rounds {
        real.reset();
        assert!(real.set_profile(Arc::clone(&preferences)));
        for c in "nihao".chars() {
            black_box(real.process(Key::Character(c), Modifiers::default()));
        }
        black_box(real.process(Key::Space, Modifiers::default()));
        let start = Instant::now();
        assert!(black_box(real.learn_commit()));
        black_box(real.profile());
        acknowledgments.push(start.elapsed().as_nanos());
    }
    acknowledgments.sort_unstable();
    let n = acknowledgments.len();
    println!("8192-record profile: host acknowledgment + immutable snapshot (allocates, no disk): n={n} p50={}ns p95={}ns p99={}ns max={}ns", acknowledgments[n/2], acknowledgments[n*95/100], acknowledgments[n*99/100], acknowledgments[n-1]);
    real.reset();
    for c in corpus[4].chars() {
        real.process(Key::Character(c), Modifiers::default());
    }
    let mut display = [0u8; 319];
    measure(
        "display_preedit: 60-byte input, separators only",
        20000,
        || {
            black_box(real.display_preedit(&mut display));
        },
    );
    // Large structural stress fixture. NOT a representative Chinese language corpus.
    let mut source = String::new();
    for i in 0..100_000 {
        let mut n = i;
        let mut key = String::from("a");
        for _ in 0..4 {
            key.push((b'a' + (n % 26) as u8) as char);
            n /= 26;
        }
        source.push_str(&format!("{key}\t词{i}\t{}\n", i + 1));
    }
    let start = Instant::now();
    let large = Arc::new(Dictionary::from_tsv(&source).unwrap());
    println!(
        "synthetic: entries={} build={}ms estimated_heap={}bytes",
        large.entry_count(),
        start.elapsed().as_millis(),
        large.estimated_heap_bytes()
    );
    let demo = demo_dictionary();
    println!(
        "demo: entries={} estimated_heap={}bytes",
        demo.entry_count(),
        demo.estimated_heap_bytes()
    );
    measure("demo.lookup(zhongguoren)", 20_000, || {
        black_box(demo.lookup(black_box("zhongguoren")).unwrap());
    });
    measure("synthetic.lookup(a), broad prefix", 20_000, || {
        black_box(large.lookup(black_box("a")).unwrap());
    });
    measure("synthetic.lookup(abcde)", 20_000, || {
        black_box(large.lookup(black_box("abcde")).unwrap());
    });
    let mut session = Session::new(demo);
    measure("session: nihaoshijie + space (12 events)", 20_000, || {
        session.reset();
        for c in "nihaoshijie".chars() {
            black_box(session.process(Key::Character(c), Modifiers::default()));
        }
        black_box(session.process(Key::Space, Modifiers::default()));
        black_box(session.commit());
    });
}
