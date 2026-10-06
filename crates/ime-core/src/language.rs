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
    Index {
        rows,
        groups,
        width,
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
/// Longest context suffix the association table can hold, as probed by the
/// original scan. The index covers suffixes up to `width`; anything longer
/// cannot match a row, so it is skipped in both paths.
const SUFFIXES: usize = 4;
fn lookup(index: &Index, context: &str, next: &str) -> f32 {
    if context.is_empty() || next.is_empty() {
        return 0.0;
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
}
