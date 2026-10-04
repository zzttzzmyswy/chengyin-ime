// Compile-time token offsets into the sorted, audited syllable source.
// The display path probes up to six bytes with binary searches; no runtime cache.
const SOURCE: &str = include_str!("../../../data/syllables.txt");
const fn separator(byte: u8) -> bool {
    matches!(byte, b' ' | b'\n' | b'\r' | b'\t')
}
const fn count() -> usize {
    let bytes = SOURCE.as_bytes();
    let mut at = 0;
    let mut total = 0;
    while at < bytes.len() {
        if !separator(bytes[at]) && (at == 0 || separator(bytes[at - 1])) {
            total += 1;
        }
        at += 1;
    }
    total
}
const _: () = assert!(SOURCE.len() <= u16::MAX as usize);
const COUNT: usize = count();
const fn offsets() -> [(u16, u16); COUNT] {
    let bytes = SOURCE.as_bytes();
    let mut table = [(0, 0); COUNT];
    let mut at = 0;
    let mut row = 0;
    while at < bytes.len() {
        if separator(bytes[at]) {
            at += 1;
            continue;
        }
        let start = at;
        while at < bytes.len() && !separator(bytes[at]) {
            at += 1;
        }
        table[row] = (start as u16, at as u16);
        row += 1;
    }
    table
}
const OFFSETS: [(u16, u16); COUNT] = offsets();
pub(crate) fn contains(text: &str) -> bool {
    OFFSETS
        .binary_search_by(|&(start, end)| SOURCE[start as usize..end as usize].cmp(text))
        .is_ok()
}
