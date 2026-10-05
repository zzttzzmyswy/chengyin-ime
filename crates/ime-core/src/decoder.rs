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
    corrected: bool,
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
            corrected: false,
        }
    }
}
#[derive(Clone, Copy)]
pub(crate) struct Sentence {
    bytes: [u8; MAX_TEXT_BYTES],
    len: u16,
    pinyin: [u8; 255],
    pinyin_len: u8,
    pub corrected: bool,
    pub exact: bool,
}
impl Default for Sentence {
    fn default() -> Self {
        Self {
            bytes: [0; MAX_TEXT_BYTES],
            len: 0,
            pinyin: [0; 255],
            pinyin_len: 0,
            corrected: false,
            exact: true,
        }
    }
}
impl Sentence {
    pub fn pinyin(&self) -> &str {
        std::str::from_utf8(&self.pinyin[..self.pinyin_len as usize]).expect("ASCII pinyin")
    }
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
    allowed: bool,
}
impl Default for Pair {
    fn default() -> Self {
        Self {
            left: u32::MAX,
            right: u32::MAX,
            bonus: 0.0,
            allowed: true,
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
    fn transition(&mut self, d: &Dictionary, left: u32, right: u32) -> Pair {
        let at = ((left.wrapping_mul(2654435761) ^ right.wrapping_mul(2246822519)) & 31) as usize;
        let old = self.pairs[at];
        if old.left == left && old.right == right {
            return old;
        }
        let a = d.entry(left).text;
        let b = d.entry(right).text;
        let bonus = crate::language::bonus(a, b);
        // Isolated characters are not evidence of a word. Require an attested
        // pair or an authored transition before multiplying their homophones.
        // Phrase-sized edges still support ordinary continuous sentences.
        let allowed = if a.chars().nth(1).is_none() && b.chars().nth(1).is_none() && bonus == 0.0 {
            let mut text = [0u8; 8]; // two Unicode scalar values, no key-path allocation
            text[..a.len()].copy_from_slice(a.as_bytes());
            text[a.len()..a.len() + b.len()].copy_from_slice(b.as_bytes());
            d.contains_text(std::str::from_utf8(&text[..a.len() + b.len()]).expect("copied UTF-8"))
        } else {
            true
        };
        let pair = Pair {
            left,
            right,
            bonus,
            allowed,
        };
        self.pairs[at] = pair;
        pair
    }
    fn insert(&mut self, start: usize, item: Path) {
        let len = self.lengths[start] as usize;
        if self.paths[start][..len].iter().any(|p| {
            p.id == item.id && p.next == item.next && p.rank == item.rank && p.cost <= item.cost
        }) {
            return;
        }
        let compare = |a: &Path, b: &Path| {
            a.corrected
                .cmp(&b.corrected)
                .then(a.cost.total_cmp(&b.cost))
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
        options: u32,
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
                    let pair = if end < size {
                        self.transition(d, id, self.paths[end][rank].id)
                    } else {
                        Pair::default()
                    };
                    if !pair.allowed {
                        continue;
                    }
                    let item = Path {
                        cost: cost + self.paths[end][rank].cost
                            - pair.bonus
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
                        corrected: self.paths[end][rank].corrected,
                    };
                    self.insert(start, item);
                }
            })?;
            limited |= d.matches_tolerant(input, start, options, BEAM, |id, end, penalty| {
                for rank in 0..self.lengths[end] as usize {
                    let pair = if end < size {
                        self.transition(d, id, self.paths[end][rank].id)
                    } else {
                        Pair::default()
                    };
                    if !pair.allowed {
                        continue;
                    }
                    self.insert(
                        start,
                        Path {
                            cost: d.word_cost(id)
                                + f32::from(penalty) * 4.0
                                + self.paths[end][rank].cost
                                - pair.bonus,
                            id,
                            next: end as u8,
                            rank: rank as u8,
                            abbreviated: self.paths[end][rank].abbreviated,
                            predicted: self.paths[end][rank].predicted,
                            corrected: true,
                        },
                    );
                }
            });
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
                        let pair = if end < size {
                            self.transition(d, id, self.paths[end][rank].id)
                        } else {
                            Pair::default()
                        };
                        if !pair.allowed {
                            continue;
                        }
                        self.insert(
                            start,
                            Path {
                                cost: cost + self.paths[end][rank].cost
                                    - pair.bonus
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
                                corrected: self.paths[end][rank].corrected,
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
                                corrected: false,
                            },
                        );
                    }
                }
            }
        }
        self.primary_abbreviated = self.lengths[0] > 0 && self.paths[0][0].abbreviated;
        Ok(limited && self.primary_abbreviated)
    }
    fn render(&mut self, d: &Dictionary, input: &str, base: usize) -> bool {
        let mut limited = false;
        let size = input.len();
        self.count = base;
        for initial in 0..self.lengths[0] as usize {
            let mut sentence = Sentence::default();
            let mut pinyin_overflow = false;
            let (mut pos, mut rank, mut words) = (0, initial, 0);
            while pos < size {
                let path = self.paths[pos][rank];
                let text = d.entry(path.id).text;
                let spelling = d.entry(path.id).pinyin.as_bytes();
                let pstart = sentence.pinyin_len as usize;
                let delimiter = usize::from(pstart > 0);
                if pstart + delimiter + spelling.len() > 255 {
                    pinyin_overflow = true;
                }
                if !pinyin_overflow {
                    if delimiter > 0 {
                        sentence.pinyin[pstart] = b'\'';
                    }
                    sentence.pinyin[pstart + delimiter..pstart + delimiter + spelling.len()]
                        .copy_from_slice(spelling);
                    sentence.pinyin_len = (pstart + delimiter + spelling.len()) as u8;
                }
                sentence.corrected |= path.corrected;
                sentence.exact &= !path.corrected && !path.abbreviated && !path.predicted;
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
            if sentence.corrected && pinyin_overflow {
                limited = true;
                continue;
            }
            // A one/two-syllable word query must not expand into a generated
            // phrase through arbitrary initials or corrected character edges.
            if !sentence.exact && crate::syllables::count_spelling(input).is_some_and(|n| n <= 2) {
                continue;
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
        limited
    }
    pub fn decode(
        &mut self,
        d: &Dictionary,
        input: &str,
        context: &str,
        options: u32,
    ) -> Result<bool, LookupError> {
        self.count = 0;
        self.segment_count = 0;
        self.fast_ready = false;
        self.full_coverage = false;
        let mut limited = self.compute(d, input, context, false, options)?;
        self.full_coverage = self.paths[0][..self.lengths[0] as usize]
            .iter()
            .any(|p| !p.predicted);
        if self.lengths[0] == 0 && !input.is_empty() {
            limited |= self.compute(d, input, context, true, options)?;
            self.fast_ready = true;
        }
        limited |= self.render(d, input, 0);
        if options != 0 {
            return Ok(limited);
        }
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
        options: u32,
    ) -> Result<bool, LookupError> {
        if self.fast_ready {
            return Ok(false);
        }
        let base = self.count;
        let limited = self.compute(d, input, context, true, options)? | self.render(d, input, base);
        self.fast_ready = true;
        Ok(limited)
    }
}
