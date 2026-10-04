//! Small explainable offline transition baseline, shared and immutable. Mutable
//! context/recency stays in each Session and is discarded on reset/focus loss.
use std::sync::OnceLock;
pub(crate) struct Link {
    pub context: &'static str,
    pub next: &'static str,
    pub weight: u32,
    pub bonus: f32,
}
pub(crate) fn links() -> &'static [Link] {
    static LINKS: OnceLock<Vec<Link>> = OnceLock::new();
    LINKS.get_or_init(|| {
        let mut rows: Vec<_> = include_str!("../../../data/associations.tsv")
            .lines()
            .filter(|l| !l.is_empty() && !l.starts_with('#'))
            .map(|line| {
                let mut parts = line.split('\t');
                let context = parts.next().unwrap();
                let next = parts.next().unwrap();
                let weight: u32 = parts.next().unwrap().parse().unwrap();
                assert!(!context.is_empty() && next.len() <= crate::MAX_TEXT_BYTES);
                Link {
                    context,
                    next,
                    weight,
                    bonus: 2.0 + (weight as f32).ln() * 0.4,
                }
            })
            .collect();
        rows.sort_by(|a, b| {
            a.context
                .cmp(b.context)
                .then(b.weight.cmp(&a.weight))
                .then(a.next.cmp(b.next))
        });
        rows
    })
}
pub(crate) fn bonus(context: &str, next: &str) -> f32 {
    if context.is_empty() || next.is_empty() {
        return 0.0;
    }
    let rows = links();
    let mut result = 0.0f32;
    for (at, _) in context.char_indices().rev().take(4) {
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
