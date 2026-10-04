use crate::{Dictionary, LookupError, MAX_INPUT_BYTES, MAX_TEXT_BYTES};

/// Unigram Viterbi baseline: 16 paths at every raw-input position. No language
/// model, history or I/O. Each edge retains a word and an explicit input span.
pub(crate) const BEAM: usize = 16;
#[derive(Clone, Copy)]
struct Path {
    cost: f32,
    id: u32,
    next: u8,
    rank: u8,
    abbreviated: bool,
    predicted: bool,
}
impl Default for Path {
    fn default() -> Self {
        Self {
            cost: f32::INFINITY,
            id: 0,
            next: 0,
            rank: 0,
            abbreviated: false,
            predicted: false,
        }
    }
}
#[derive(Clone, Copy)]
pub(crate) struct Sentence {
    bytes: [u8; MAX_TEXT_BYTES],
    len: u16,
}
impl Default for Sentence {
    fn default() -> Self {
        Self {
            bytes: [0; MAX_TEXT_BYTES],
            len: 0,
        }
    }
}
impl Sentence {
    pub fn text(&self) -> &str {
        std::str::from_utf8(&self.bytes[..self.len as usize]).expect("copied UTF-8 words")
    }
}
#[derive(Clone, Copy, Default)]
pub(crate) struct Word {
    pub id: u32,
    pub consumed: u8,
}
#[derive(Clone, Copy)]
struct Pair {
    left: u32,
    right: u32,
    bonus: f32,
}
impl Default for Pair {
    fn default() -> Self {
        Self {
            left: u32::MAX,
            right: u32::MAX,
            bonus: 0.0,
        }
    }
}
pub(crate) struct Decoder {
    paths: [[Path; BEAM]; MAX_INPUT_BYTES + 1],
    pairs: [Pair; 32],
    lengths: [u8; MAX_INPUT_BYTES + 1],
    pub sentences: [Sentence; BEAM * 2],
    pub count: usize,
    pub segments: [Word; 64],
    pub segment_count: usize,
    pub primary_abbreviated: bool,
    pub full_coverage: bool,
    pub fast_ready: bool,
}
impl Decoder {
    pub fn new() -> Self {
        Self {
            paths: [[Path::default(); BEAM]; MAX_INPUT_BYTES + 1],
            pairs: [Pair::default(); 32],
            lengths: [0; MAX_INPUT_BYTES + 1],
            sentences: [Sentence::default(); BEAM * 2],
            count: 0,
            segments: [Word::default(); 64],
            segment_count: 0,
            primary_abbreviated: false,
            full_coverage: false,
            fast_ready: false,
        }
    }
    pub fn clear_cache(&mut self) {
        self.pairs.fill(Pair::default());
    }
    fn pair_bonus(&mut self, d: &Dictionary, left: u32, right: u32) -> f32 {
        let at = ((left.wrapping_mul(2654435761) ^ right.wrapping_mul(2246822519)) & 31) as usize;
        let old = self.pairs[at];
        if old.left == left && old.right == right {
            return old.bonus;
        }
        let bonus = crate::language::bonus(d.entry(left).text, d.entry(right).text);
        self.pairs[at] = Pair { left, right, bonus };
        bonus
    }
    fn insert(&mut self, start: usize, item: Path) {
        let len = self.lengths[start] as usize;
        let compare = |a: &Path, b: &Path| {
            a.cost
                .total_cmp(&b.cost)
                .then(a.id.cmp(&b.id))
                .then(a.next.cmp(&b.next))
                .then(a.rank.cmp(&b.rank))
        };
        let at = self.paths[start][..len].partition_point(|p| !compare(&item, p).is_lt());
        if at < BEAM {
            self.paths[start].copy_within(at..len.min(BEAM - 1), at + 1);
            self.paths[start][at] = item;
            self.lengths[start] = (len + 1).min(BEAM) as u8;
        }
    }
    fn compute(
        &mut self,
        d: &Dictionary,
        input: &str,
        context: &str,
        fast: bool,
    ) -> Result<bool, LookupError> {
        self.lengths.fill(0);
        self.primary_abbreviated = false;
        let size = input.len();
        if size == 0 {
            return Ok(false);
        }
        let mut limited = false;
        self.paths[size][0] = Path {
            cost: 0.0,
            ..Path::default()
        };
        self.lengths[size] = 1;
        for start in (0..size).rev() {
            if input.as_bytes()[start] == b'\'' {
                continue;
            }
            d.matches(input, start, BEAM, |id, end| {
                let cost = d.word_cost(id);
                for rank in 0..self.lengths[end] as usize {
                    let pair_bonus = if end < size {
                        self.pair_bonus(d, id, self.paths[end][rank].id)
                    } else {
                        0.0
                    };
                    let item = Path {
                        cost: cost + self.paths[end][rank].cost
                            - pair_bonus
                            - if start == 0 {
                                crate::language::bonus(context, d.entry(id).text)
                            } else {
                                0.0
                            },
                        id,
                        next: end as u8,
                        rank: rank as u8,
                        abbreviated: self.paths[end][rank].abbreviated,
                        predicted: self.paths[end][rank].predicted,
                    };
                    self.insert(start, item);
                }
            })?;
            if fast {
                limited |= d.matches_fast(input, start, BEAM, |id, end| {
                    let word = d.entry(id);
                    if word.pinyin.bytes().filter(|&b| b != b'\'').count()
                        == input[start..end].bytes().filter(|&b| b != b'\'').count()
                    {
                        return;
                    }
                    // Full spellings have priority; abbreviations participate in the
                    // same word graph with an explicit ambiguity penalty per word.
                    let cost = d.word_cost(id) + 6.0;
                    for rank in 0..self.lengths[end] as usize {
                        let pair_bonus = if end < size {
                            self.pair_bonus(d, id, self.paths[end][rank].id)
                        } else {
                            0.0
                        };
                        self.insert(
                            start,
                            Path {
                                cost: cost + self.paths[end][rank].cost
                                    - pair_bonus
                                    - if start == 0 {
                                        crate::language::bonus(context, d.entry(id).text)
                                    } else {
                                        0.0
                                    },
                                id,
                                next: end as u8,
                                rank: rank as u8,
                                abbreviated: true,
                                predicted: self.paths[end][rank].predicted,
                            },
                        );
                    }
                });
            }
            // Last unfinished syllable: complete only the final word. Keep full
            // parses first; the extra 2.0 cost is an explicit prediction penalty.
            if start > 0 && size - start <= 6 && self.lengths[start] == 0 {
                if let Ok(words) = d.lookup(&input[start..]) {
                    for index in 0..words.len() {
                        let id = words.id(index);
                        self.insert(
                            start,
                            Path {
                                cost: d.word_cost(id) + 2.0,
                                id,
                                next: size as u8,
                                rank: 0,
                                abbreviated: false,
                                predicted: true,
                            },
                        );
                    }
                }
            }
        }
        self.primary_abbreviated = self.lengths[0] > 0 && self.paths[0][0].abbreviated;
        Ok(limited && self.primary_abbreviated)
    }
    fn render(&mut self, d: &Dictionary, input: &str, base: usize) {
        let size = input.len();
        self.count = base;
        for initial in 0..self.lengths[0] as usize {
            let mut sentence = Sentence::default();
            let (mut pos, mut rank, mut words) = (0, initial, 0);
            while pos < size {
                let path = self.paths[pos][rank];
                let text = d.entry(path.id).text;
                let end = sentence.len as usize + text.len();
                if end > MAX_TEXT_BYTES {
                    break;
                }
                sentence.bytes[sentence.len as usize..end].copy_from_slice(text.as_bytes());
                sentence.len = end as u16;
                pos = path.next as usize;
                rank = path.rank as usize;
                words += 1;
            }
            if pos == size
                && words > 1
                && !self.sentences[..self.count]
                    .iter()
                    .any(|s| s.text() == sentence.text())
            {
                self.sentences[self.count] = sentence;
                self.count += 1;
            }
        }
    }
    pub fn decode(
        &mut self,
        d: &Dictionary,
        input: &str,
        context: &str,
    ) -> Result<bool, LookupError> {
        self.count = 0;
        self.segment_count = 0;
        self.fast_ready = false;
        self.full_coverage = false;
        let mut limited = self.compute(d, input, context, false)?;
        self.full_coverage = self.paths[0][..self.lengths[0] as usize]
            .iter()
            .any(|p| !p.predicted);
        if self.lengths[0] == 0 && !input.is_empty() {
            limited |= self.compute(d, input, context, true)?;
            self.fast_ready = true;
        }
        self.render(d, input, 0);
        let size = input.len();
        // Offer explicit prefix words after complete sentence/word choices.
        // Descending consumed length gives useful phrase-sized corrections first.
        let mut offer = |id, end| {
            if end >= size {
                return;
            }
            let item = Word {
                id,
                consumed: end as u8,
            };
            let len = self.segment_count;
            let at = self.segments[..len].partition_point(|w| {
                (std::cmp::Reverse(w.consumed), w.id) <= (std::cmp::Reverse(item.consumed), item.id)
            });
            if at < self.segments.len() {
                self.segments.copy_within(at..len.min(63), at + 1);
                self.segments[at] = item;
                self.segment_count = (len + 1).min(self.segments.len());
            }
        };
        if self.fast_ready {
            limited |= d.matches_fast(input, 0, BEAM, &mut offer);
        } else {
            d.matches(input, 0, BEAM, &mut offer)?;
        }
        Ok(limited)
    }
    pub fn decode_alternates(
        &mut self,
        d: &Dictionary,
        input: &str,
        context: &str,
    ) -> Result<bool, LookupError> {
        if self.fast_ready {
            return Ok(false);
        }
        let base = self.count;
        let limited = self.compute(d, input, context, true)?;
        self.render(d, input, base);
        self.fast_ready = true;
        Ok(limited)
    }
}
