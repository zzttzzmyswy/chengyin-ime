// SPDX-License-Identifier: GPL-3.0-or-later
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
    errors: u8,
    /// This syllable's first letter was already consumed by a transposition that
    /// crossed the separator in front of it (`guan'ai` typed as `guaani`).
    skip: bool,
    marks: [u64; 4],
}
impl Cell {
    const EMPTY: Self = Self {
        cost: u8::MAX,
        typo: false,
        errors: 0,
        skip: false,
        marks: [0; 4],
    };
    fn marked(mut self, start: usize, end: usize, cost: u8) -> Self {
        self.cost = self.cost.saturating_add(cost);
        self.typo |= cost == 2;
        self.errors += u8::from(cost == 2);
        for at in start..end.min(255) {
            self.marks[at / 64] |= 1 << (at % 64);
        }
        self
    }
}
/// Per canonical ASCII pinyin byte, including zero marks on apostrophes.
/// Prefix completion alone is never labelled as an input error.
pub(crate) fn annotations(input: &str, canonical: &str, flags: u32) -> Option<[u64; 4]> {
    align(input, canonical, flags, true).map(|cell| cell.marks)
}
pub(crate) fn complete_annotations(input: &str, canonical: &str, flags: u32) -> Option<[u64; 4]> {
    align(input, canonical, flags, false).map(|cell| cell.marks)
}
pub(crate) fn penalty(input: &str, canonical: &str, flags: u32) -> Option<u8> {
    align(input, canonical, flags, false).map(|cell| cell.cost)
}
fn align(input: &str, canonical: &str, flags: u32, completion: bool) -> Option<Cell> {
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
        errors: 0,
        skip: false,
        marks: [0; 4],
    };
    let mut result = Cell::EMPTY;
    let mut at = 0;
    let mut syllables = canonical.split('\'').peekable();
    while let Some(syllable) = syllables.next() {
        let word = syllable.as_bytes();
        if word.len() > 6 || word.is_empty() {
            return None;
        }
        // The first canonical letter of the following syllable, if the two are
        // separated. A transposition may exchange it with this syllable's last.
        let next_letter = syllables
            .peek()
            .and_then(|next| next.as_bytes().first().copied());
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
                if completion && base.cost < result.cost {
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
            // A crossing transposition in the previous syllable already matched
            // this syllable's first letter, so matching resumes at the second.
            if base.skip {
                base.skip = false;
                dp[0][1] = base;
            } else {
                dp[0][0] = base;
            }
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
                    let typo_enabled = !cell.typo
                        && cell.errors < 2
                        && raw.iter().filter(|&&c| c != b'\'').count() >= 3;
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
                    // A transposition may straddle the separator: the user types
                    // this syllable's last letter and the next syllable's first in
                    // the wrong order (`guan'ai` -> `guaani`). Match both here and
                    // tell the next syllable its first letter is already consumed.
                    if typo_enabled
                        && i + 1 < max
                        && j + 1 == word.len()
                        && flags & SWAP != 0
                        && word[j] != next_letter.unwrap_or(word[j])
                        && raw[start + i] == next_letter.unwrap_or(0)
                        && raw[start + i + 1] == word[j]
                    {
                        // One error, two letters: this syllable's last and the
                        // next syllable's first, both in canonical positions.
                        let mut value = cell.marked(at + j, at + j + 1, 2);
                        value.marks[(at + word.len() + 1) / 64] |=
                            1 << ((at + word.len() + 1) % 64);
                        value.skip = true;
                        put(i + 2, j + 1, value);
                    }
                    if typo_enabled && j < word.len() && flags & OMIT != 0 {
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
            if completion && start + max == raw.len() {
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
    (result.cost <= 12).then_some(result)
}
