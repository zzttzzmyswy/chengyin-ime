// SPDX-License-Identifier: GPL-3.0-or-later
//! Small explainable offline transition baseline, shared and immutable. Mutable
//! context/recency stays in each Session and is discarded on reset/focus loss.
use std::sync::OnceLock;
pub(crate) struct Link {
    pub context: &'static str,
    pub next: &'static str,
    pub weight: u32,
    pub bonus: f32,
}
/// Rows of one distinct context, kept contiguous in the sorted row table. The
/// former lookup binary-searched all 106 rows per suffix with a `str`
/// comparison; this scans only the rows that can match, keyed by a scalar
/// encoding. The gain is measurable because the table is small but probed once
/// per candidate transition (`docs/QUALITY_BASELINE.md`, I06).
struct Group {
    code: u64,
    start: u32,
    end: u32,
}
struct Index {
    rows: Vec<Link>,
    groups: Vec<Group>,
    /// Longest context in scalars. Zero disables the index path.
    width: usize,
    /// Exact set of scalar pairs `(last scalar of the context suffix, first scalar
    /// of `next`) that the table can match; see [`Gate`].
    ///
    /// A row can only match when `row.next` is a prefix of `next` (so its first
    /// scalar equals `next`'s) and `row.context` is a suffix of the context (so its
    /// last scalar equals the context's). Those two scalars must therefore be one of
    /// the pairs the table actually contains, and a pair outside that set proves the
    /// whole table cannot match: the call returns zero without walking a suffix.
    ///
    /// The gate deliberately keys on *scalars*, not bytes. Every CJK character
    /// starts with one of six UTF-8 lead bytes, so a byte-keyed gate measured
    /// against the decoder's real transition calls rejected 0.00% of them — a gate
    /// that can never fire is worse than none, because it looks like a fast path.
    /// The scalar pair rejects 99.3% of those same calls (`docs/PERFORMANCE.md`, I16).
    ///
    /// `None` disables the gate: an empty `row.next` is a prefix of every probe and
    /// an empty `row.context` a suffix of every context, so either would be silently
    /// dropped by any pair test.
    gate: Option<Gate>,
}
/// Sound prefilter for [`lookup`]; see [`Index::gate`].
///
/// The shipped table has 106 rows, so the *exact* set of admissible scalar pairs is
/// a handful of entries — a sorted `Vec` of `(last context scalar, first next
/// scalar)` costs under a kilobyte against the bitset's 8 KiB and answers with a
/// binary search over data that stays resident. A hashed bitset was the obvious
/// alternative and is worse: it can only trade false positives for size, and the
/// pairs that matter here are drawn from a few thousand CJK scalars, which collide
/// heavily in any array small enough to stay in cache.
struct Gate {
    pairs: Vec<(u32, u32)>,
}
impl Gate {
    fn build(rows: &[Link]) -> Option<Self> {
        let mut pairs = Vec::with_capacity(rows.len());
        for row in rows {
            // An empty side matches every probe, so it cannot be gated away.
            let last = row.context.chars().next_back()?;
            let first = row.next.chars().next()?;
            pairs.push((u32::from(last), u32::from(first)));
        }
        pairs.sort_unstable();
        pairs.dedup();
        Some(Self { pairs })
    }
    /// Sound one-sided test: `false` means no row can match `(context, next)`.
    fn may_match(&self, context: &str, next: &str) -> bool {
        let (Some(last), Some(first)) = (context.chars().next_back(), next.chars().next()) else {
            return false;
        };
        self.scalar_may_match(u32::from(last), u32::from(first))
    }
    /// The same test on raw scalars, for callers that hold per-entry metadata
    /// instead of `&str`. `false` means no row can match.
    fn scalar_may_match(&self, last: u32, first: u32) -> bool {
        self.pairs.binary_search(&(last, first)).is_ok()
    }
}
/// Collision-free encoding of up to three scalars (each digit is scalar + 1).
/// A longer context has no encoding, which disables the index instead of
/// aliasing a truncated key.
fn encode(suffix: &str) -> Option<u64> {
    let mut code = 0u64;
    for (count, c) in suffix.chars().enumerate() {
        if count == 3 {
            return None;
        }
        code = (code << 21) | u64::from(u32::from(c) + 1);
    }
    Some(code)
}
/// Sort rows and derive the group index. Every context must be encodable for the
/// index to be usable; one longer context would silently drop its rows, so the
/// whole index is disabled and the caller keeps the exact linear fallback.
fn build(mut rows: Vec<Link>) -> Index {
    rows.sort_by(|a, b| {
        a.context
            .cmp(b.context)
            .then(b.weight.cmp(&a.weight))
            .then(a.next.cmp(b.next))
    });
    let mut groups: Vec<Group> = Vec::new();
    let encodable = rows.iter().all(|r| encode(r.context).is_some());
    if encodable {
        for (at, row) in rows.iter().enumerate() {
            let code = encode(row.context).expect("checked above");
            match groups.last_mut() {
                Some(last) if last.code == code => last.end = at as u32 + 1,
                _ => groups.push(Group {
                    code,
                    start: at as u32,
                    end: at as u32 + 1,
                }),
            }
        }
        // Row order follows the context *string*; the lookup searches the scalar
        // encoding, which orders differently across lengths, so sort the groups.
        groups.sort_unstable_by_key(|g| g.code);
    }
    let width = if encodable {
        rows.iter()
            .map(|r| r.context.chars().count())
            .max()
            .unwrap_or(0)
    } else {
        0
    };
    // Scalar pair gate. `None` means the gate is disabled (an empty side would
    // match every probe), never that nothing can pass.
    let gate = Gate::build(&rows);
    Index {
        rows,
        groups,
        width,
        gate,
    }
}
fn index() -> &'static Index {
    static INDEX: OnceLock<Index> = OnceLock::new();
    INDEX.get_or_init(|| {
        build(
            include_str!("../../../data/associations.tsv")
                .lines()
                .filter(|l| !l.is_empty() && !l.starts_with('#'))
                .map(|line| {
                    let mut parts = line.split('\t');
                    let context = parts.next().unwrap();
                    let next = parts.next().unwrap();
                    let weight: u32 = parts.next().unwrap().parse().unwrap();
                    // Both sides are indexed: `context` by scalar encoding and `next`
                    // by its first byte, so neither may be empty.
                    assert!(
                        !context.is_empty()
                            && !next.is_empty()
                            && next.len() <= crate::MAX_TEXT_BYTES
                    );
                    Link {
                        context,
                        next,
                        weight,
                        bonus: 2.0 + (weight as f32).ln() * 0.4,
                    }
                })
                .collect(),
        )
    })
}
pub(crate) fn links() -> &'static [Link] {
    &index().rows
}
pub(crate) fn bonus(context: &str, next: &str) -> f32 {
    lookup(index(), context, next)
}
/// Scalar-level form of the [`bonus`] gate, for a caller that has the last scalar
/// of the context and the first scalar of the continuation but not the strings.
///
/// A disabled gate (an empty side in the shipped table) admits everything, so this
/// answers `true` and the caller keeps the exact path; it is only ever a rejection
/// when the gate really is enabled.
pub(crate) fn pair_may_match(last: u32, first: u32) -> bool {
    index()
        .gate
        .as_ref()
        .is_none_or(|gate| gate.scalar_may_match(last, first))
}
/// Longest context suffix the association table can hold, as probed by the
/// original scan. The index covers suffixes up to `width`; anything longer
/// cannot match a row, so it is skipped in both paths.
const SUFFIXES: usize = 4;
fn lookup(index: &Index, context: &str, next: &str) -> f32 {
    if context.is_empty() || next.is_empty() {
        return 0.0;
    }
    // `bonus` runs once per distinct candidate transition and the decoder's profile
    // shows almost every call returning zero. The gate answers those from a sorted
    // handful of scalar pairs instead of walking context suffixes and
    // binary-searching groups. It is keyed on the pair a matching row would *have*
    // to carry, so a miss proves the table cannot match and the result is the same
    // zero. A row whose `next` starts with the probe's first scalar is the only kind
    // that can match, and the same holds for the context's last scalar.
    if let Some(gate) = &index.gate {
        if !gate.may_match(context, next) {
            return 0.0;
        }
    }
    let mut result = 0.0f32;
    if index.width != 0 {
        let first = next.as_bytes()[0];
        for (at, _) in context.char_indices().rev().take(index.width) {
            let code = encode(&context[at..]).expect("suffix no longer than table");
            if let Ok(known) = index.groups.binary_search_by_key(&code, |g| g.code) {
                let group = &index.groups[known];
                for row in &index.rows[group.start as usize..group.end as usize] {
                    if row.next.as_bytes()[0] == first && next.starts_with(row.next) {
                        result = result.max(row.bonus);
                    }
                }
            }
        }
        return result;
    }
    let rows = &index.rows;
    for (at, _) in context.char_indices().rev().take(SUFFIXES) {
        let suffix = &context[at..];
        let start = rows.partition_point(|r| r.context < suffix);
        for row in rows[start..].iter().take_while(|r| r.context == suffix) {
            if next.starts_with(row.next) {
                result = result.max(row.bonus);
            }
        }
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    fn link(context: &'static str, next: &'static str, bonus: f32) -> Link {
        Link {
            context,
            next,
            weight: 1,
            bonus,
        }
    }

    /// The pre-index lookup, kept verbatim: every suffix of the context is
    /// matched by binary-searching the whole sorted table. The index path must
    /// return exactly this for every input.
    fn reference(rows: &[Link], context: &str, next: &str) -> f32 {
        if context.is_empty() || next.is_empty() {
            return 0.0;
        }
        let mut result = 0.0f32;
        for (at, _) in context.char_indices().rev().take(SUFFIXES) {
            let suffix = &context[at..];
            let start = rows.partition_point(|r| r.context < suffix);
            for row in rows[start..].iter().take_while(|r| r.context == suffix) {
                if next.starts_with(row.next) {
                    result = result.max(row.bonus);
                }
            }
        }
        result
    }

    /// The shipped table plus every pair of its rows, checked exhaustively.
    #[test]
    fn index_path_matches_the_whole_table_scan_on_the_shipped_table() {
        let index = index();
        assert_ne!(index.width, 0, "shipped table must take the index path");
        assert_eq!(index.rows.len(), links().len());
        // Every context paired with every row's `next`, plus near-miss keys.
        for row in &index.rows {
            for probe in index.rows.iter().map(|r| r.next).chain([
                "",
                "x",
                "我",
                "我喜欢",
                "你好",
                "世界",
                "没这一行",
                "很喜欢",
            ]) {
                assert_eq!(
                    lookup(index, row.context, probe),
                    reference(&index.rows, row.context, probe),
                    "context={:?} next={probe:?}",
                    row.context
                );
            }
        }
    }

    /// Suffixes that are empty, one scalar long, or longer than any table
    /// context, and contexts that do not appear in the table at all.
    #[test]
    fn index_path_matches_at_the_suffix_boundaries() {
        let index = index();
        for (context, next) in [
            ("", "好"),
            ("我", "喜欢"),
            ("我", ""),
            ("", ""),
            ("我喜欢", "中文"),
            ("我喜欢你", "好"),
            ("不存在", "的上下文"),
            ("a", "b"),
            ("很长的上下文超过四个字", "好"),
            ("你", "好"),
            ("你们", "好"),
        ] {
            assert_eq!(
                lookup(index, context, next),
                reference(&index.rows, context, next),
                "context={context:?} next={next:?}"
            );
        }
    }

    /// A table holding a context longer than three scalars disables the index,
    /// and the fallback must still equal the original scan.
    #[test]
    fn longer_than_three_scalars_falls_back_and_stays_exact() {
        let rows = vec![
            link("我", "喜欢", 3.0),
            link("你", "好", 4.0),
            link("一二三四", "五", 5.0),
        ];
        let index = build(rows);
        assert_eq!(
            index.width, 0,
            "a four-scalar context must disable the index"
        );
        assert!(index.groups.is_empty());
        for (context, next) in [
            ("我", "喜欢"),
            ("你", "好"),
            ("一二三四", "五"),
            ("二三四", "五"),
            ("三四", "五"),
            ("我", "好"),
            ("", "五"),
            ("一二三四五", "六"),
        ] {
            assert_eq!(
                lookup(&index, context, next),
                reference(&index.rows, context, next),
                "context={context:?} next={next:?}"
            );
        }
        // The fallback still returns the real bonus, not a silent zero.
        assert_eq!(lookup(&index, "一二三四", "五"), 5.0);
    }

    /// An encodable table: the index must reproduce the scan for every suffix
    /// length it can hold, including two-row groups sharing one context.
    #[test]
    fn encodable_table_matches_the_scan_for_every_suffix_length() {
        let rows = vec![
            link("我", "喜欢", 3.0),
            link("我", "想你", 2.5),
            link("喜欢", "你", 4.0),
            link("好", "的", 1.5),
        ];
        let index = build(rows);
        assert_eq!(index.width, 2);
        assert_eq!(index.groups.len(), 3);
        for context in [
            "我",
            "喜欢",
            "好",
            "我喜欢",
            "我喜欢你",
            "喜欢我",
            "好喜欢",
            "xyz",
        ] {
            for next in ["喜欢", "想你", "你", "的", "好", "", "喜欢我"] {
                assert_eq!(
                    lookup(&index, context, next),
                    reference(&index.rows, context, next),
                    "context={context:?} next={next:?}"
                );
            }
        }
    }

    /// An empty table keeps the fallback exact and must not panic.
    #[test]
    fn empty_table_is_safe() {
        let index = build(Vec::new());
        assert_eq!(index.width, 0);
        assert_eq!(lookup(&index, "我", "好"), 0.0);
        assert_eq!(lookup(&index, "", ""), 0.0);
    }

    /// A context can only match on a row boundary; a suffix that happens to
    /// start mid-scalar must never be constructed by the index path.
    #[test]
    fn only_scalar_boundaries_are_probed() {
        let rows = vec![link("你好", "世界", 6.0)];
        let index = build(rows);
        assert_eq!(index.width, 2);
        assert_eq!(lookup(&index, "你好", "世界"), 6.0);
        assert_eq!(lookup(&index, "很好你好", "世界"), 6.0);
        assert_eq!(lookup(&index, "你说", "世界"), 0.0);
    }

    /// The scalar-pair gate must reject only continuations no row can match. A
    /// continuation sharing its first scalar with a row but diverging later must
    /// still fall through to the exact scan (and return zero), and one that really
    /// matches must never be gated away.
    ///
    /// The gate keys on *scalars*, not UTF-8 bytes. That distinction is the whole
    /// point: every CJK character begins with one of six lead bytes, so a
    /// byte-keyed gate measured against the decoder's real transition calls
    /// rejected 0.00% of them. The occupancy assertion below is what keeps such a
    /// gate from looking like a fast path while removing no work.
    #[test]
    fn scalar_pair_gate_only_rejects_continuations_no_row_can_match() {
        let index = index();
        let gate = index
            .gate
            .as_ref()
            .expect("shipped table must enable the gate");
        // The gate is worth having only if it admits a small fraction of the scalar
        // pairs a probe could present. Assert the *admitted* set is small in
        // absolute terms; the `pairs` table is the exact admissible set, not a
        // hashed superset, so this is also the true selectivity.
        assert!(
            gate.pairs.len() < 512,
            "gate admits too many pairs to be selective: {}",
            gate.pairs.len()
        );
        assert!(
            gate.pairs.len() * 40 < 0x10000,
            "gate admits a large fraction of the scalar space: {}",
            gate.pairs.len()
        );
        for (context, next, wanted) in [
            // Present row: 我 + 喜欢.
            ("我", "喜欢", Some(2.0f32 + 1000f32.ln() * 0.4)),
            // Same first scalar, longer continuation: still matches 喜欢.
            ("我", "喜欢中文", Some(2.0f32 + 1000f32.ln() * 0.4)),
            // No row continues 我 with this scalar: gated to zero.
            ("我", "zzz", Some(0.0)),
            ("我", "@", Some(0.0)),
            // A context whose last scalar no row's context ends with.
            ("啊", "喜欢", Some(0.0)),
        ] {
            let got = lookup(index, context, next);
            let want = wanted.expect("value");
            assert!(
                (got - want).abs() < 1e-4,
                "context={context:?} next={next:?}: {got} != {want}"
            );
        }
        // A gated call must agree with the ungated table scan, not merely be zero.
        for row in &index.rows {
            for probe in ["zzz", "@@@", "喜欢", "喜欢中文", "好"] {
                assert_eq!(
                    lookup(index, row.context, probe),
                    reference(&index.rows, row.context, probe),
                    "gated lookup disagrees for context={:?} next={probe:?}",
                    row.context
                );
            }
        }
    }

    /// The gate is only worth having if it fires on real traffic. Measured over the
    /// decoder's actual transition pairs it rejects 99.28%; this pins a much looser
    /// bound so the test does not depend on a captured trace, while still failing if
    /// a future table edit turns the gate into a near-constant `true`.
    #[test]
    fn scalar_pair_gate_fires_on_unrelated_context_and_continuation_pairs() {
        let index = index();
        let gate = index
            .gate
            .as_ref()
            .expect("shipped table must enable the gate");
        let (mut rejected, mut total) = (0usize, 0usize);
        for row in &index.rows {
            for row2 in &index.rows {
                // Deliberately mismatch: this combination must usually be absent.
                if row.context.chars().next_back().is_none() || row2.next.chars().next().is_none() {
                    continue;
                }
                total += 1;
                rejected += usize::from(!gate.may_match(row.context, row2.next));
            }
        }
        assert!(total > 5_000, "too few probes: {total}");
        assert!(
            rejected * 2 > total,
            "gate admits nearly every mismatched pair: {rejected}/{total}"
        );
    }
}
