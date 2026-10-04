//! Optional phonetic equivalences and bounded keyboard-error alignment.
//! No I/O, heap allocation or changes to the caller's raw composition.
pub const PHONETIC_MASK: u32 = (1 << 11) - 1;
pub const SWAP: u32 = 1 << 16;
pub const OMIT: u32 = 1 << 17;
pub const NEIGHBOR: u32 = 1 << 18;
pub const REPEAT: u32 = 1 << 19;
pub const OPTIONS_MASK: u32 = PHONETIC_MASK | SWAP | OMIT | NEIGHBOR | REPEAT;
pub(crate) const RULES: [(&[u8], &[u8], bool); 11] = [
    (b"zh", b"z", true),
    (b"ch", b"c", true),
    (b"sh", b"s", true),
    (b"n", b"l", true),
    (b"f", b"h", true),
    (b"l", b"r", true),
    (b"an", b"ang", false),
    (b"en", b"eng", false),
    (b"in", b"ing", false),
    (b"ian", b"iang", false),
    (b"uan", b"uang", false),
];
pub(crate) fn neighbors(a: u8, b: u8) -> bool {
    fn position(c: u8) -> Option<(i32, i32)> {
        for (row, keys, offset) in [
            (0, b"qwertyuiop".as_slice(), 0),
            (1, b"asdfghjkl".as_slice(), 1),
            (2, b"zxcvbnm".as_slice(), 2),
        ] {
            if let Some(at) = keys.iter().position(|&key| key == c) {
                return Some((at as i32 * 4 + offset, row));
            }
        }
        None
    }
    a != b
        && position(a)
            .zip(position(b))
            .is_some_and(|((x, y), (u, v))| (x - u).abs() <= 4 && (y - v).abs() <= 1)
}
#[derive(Clone, Copy)]
struct Cell {
    cost: u8,
    typo: bool,
    marks: [u64; 4],
}
impl Cell {
    const EMPTY: Self = Self {
        cost: u8::MAX,
        typo: false,
        marks: [0; 4],
    };
    fn marked(mut self, start: usize, end: usize, cost: u8) -> Self {
        self.cost = self.cost.saturating_add(cost);
        self.typo |= cost == 2;
        for at in start..end.min(255) {
            self.marks[at / 64] |= 1 << (at % 64);
        }
        self
    }
}
/// Per canonical ASCII pinyin byte, including zero marks on apostrophes.
/// Prefix completion alone is never labelled as an input error.
pub(crate) fn annotations(input: &str, canonical: &str, flags: u32) -> Option<[u64; 4]> {
    let raw = input.as_bytes();
    let target = canonical.as_bytes();
    if raw.len() > 63 || target.len() > 255 {
        return None;
    }
    // Fixed small alignment table: canonical syllables have at most six letters.
    let mut states = [Cell::EMPTY; 64];
    states[0] = Cell {
        cost: 0,
        typo: false,
        marks: [0; 4],
    };
    let mut result = Cell::EMPTY;
    let mut at = 0;
    for syllable in canonical.split('\'') {
        let word = syllable.as_bytes();
        if word.len() > 6 || word.is_empty() {
            return None;
        }
        let mut next = [Cell::EMPTY; 64];
        for (start, state) in states.iter().enumerate().take(raw.len() + 1) {
            if state.cost == u8::MAX {
                continue;
            }
            let mut base = *state;
            base.typo = false;
            let start = if raw.get(start) == Some(&b'\'') {
                start + 1
            } else {
                start
            };
            if start > raw.len() {
                continue;
            }
            if start == raw.len() {
                if base.cost < result.cost {
                    result = base;
                }
                continue;
            }
            let max = raw[start..]
                .iter()
                .take_while(|&&c| c != b'\'')
                .count()
                .min(8);
            let mut dp = [[Cell::EMPTY; 7]; 9];
            dp[0][0] = base;
            for i in 0..=max {
                for j in 0..=word.len() {
                    let cell = dp[i][j];
                    if cell.cost == u8::MAX {
                        continue;
                    }
                    let mut put = |i: usize, j: usize, value: Cell| {
                        if value.cost < dp[i][j].cost {
                            dp[i][j] = value;
                        }
                    };
                    if i < max && j < word.len() && raw[start + i] == word[j] {
                        put(i + 1, j + 1, cell);
                    }
                    let typo_enabled =
                        !cell.typo && raw.iter().filter(|&&c| c != b'\'').count() >= 3;
                    if typo_enabled
                        && i + 1 < max
                        && j + 1 < word.len()
                        && flags & SWAP != 0
                        && raw[start + i] == word[j + 1]
                        && raw[start + i + 1] == word[j]
                        && word[j] != word[j + 1]
                    {
                        put(i + 2, j + 2, cell.marked(at + j, at + j + 2, 2));
                    }
                    if typo_enabled && i < max && j < word.len() && flags & OMIT != 0 {
                        put(i, j + 1, cell.marked(at + j, at + j + 1, 2));
                    }
                    if typo_enabled
                        && i < max
                        && j < word.len()
                        && flags & NEIGHBOR != 0
                        && neighbors(raw[start + i], word[j])
                    {
                        put(i + 1, j + 1, cell.marked(at + j, at + j + 1, 2));
                    }
                    if typo_enabled
                        && i < max
                        && i > 0
                        && flags & REPEAT != 0
                        && raw[start + i] == raw[start + i - 1]
                    {
                        put(
                            i + 1,
                            j,
                            cell.marked(at + j.saturating_sub(1), at + j.max(1), 2),
                        );
                    }
                    for (rule, (left, right, initial)) in RULES.iter().enumerate() {
                        if flags & (1 << rule) == 0 {
                            continue;
                        }
                        for (can, typed) in [(*left, *right), (*right, *left)] {
                            if i + typed.len() <= max
                                && j + can.len() <= word.len()
                                && (if *initial {
                                    j == 0
                                } else {
                                    j + can.len() == word.len()
                                })
                                && raw[start + i..start + i + typed.len()] == *typed
                                && word[j..j + can.len()] == *can
                            {
                                put(
                                    i + typed.len(),
                                    j + can.len(),
                                    cell.marked(at + j, at + j + can.len(), 1),
                                );
                            }
                        }
                    }
                }
            }
            for i in 1..=max {
                let cell = dp[i][word.len()];
                if cell.cost < next[start + i].cost {
                    next[start + i] = cell;
                }
            }
            if start + max == raw.len() {
                if let Some(cell) = dp[max]
                    .iter()
                    .filter(|c| c.cost != u8::MAX)
                    .min_by_key(|c| c.cost)
                {
                    if cell.cost < result.cost {
                        result = *cell;
                    }
                }
            }
        }
        states = next;
        at += word.len() + 1;
    }
    if states[raw.len()].cost != u8::MAX {
        result = states[raw.len()];
    }
    (result.cost != u8::MAX).then_some(result.marks)
}
