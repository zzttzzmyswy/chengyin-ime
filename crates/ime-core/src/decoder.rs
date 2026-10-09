// SPDX-License-Identifier: GPL-3.0-or-later
use crate::{Dictionary, LookupError, MAX_INPUT_BYTES, MAX_TEXT_BYTES};

/// Bounded lexical Viterbi baseline with attested boundary constraints.
/// Each edge retains a word and an explicit input span; no platform I/O.
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
    unsupported: u8,
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
            unsupported: 0,
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
    weak: bool,
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
            weak: false,
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
    unsupported: bool,
    exact_only: bool,
    terminal_only: bool,
}
impl Default for Pair {
    fn default() -> Self {
        Self {
            left: u32::MAX,
            right: u32::MAX,
            bonus: 0.0,
            allowed: true,
            unsupported: false,
            exact_only: false,
            terminal_only: false,
        }
    }
}
pub(crate) struct Decoder {
    paths: [[Path; BEAM]; MAX_INPUT_BYTES + 1],
    pairs: [Pair; 128],
    lengths: [u8; MAX_INPUT_BYTES + 1],
    pub sentences: [Sentence; BEAM * 2],
    pub count: usize,
    pub segments: [Word; 64],
    pub segment_count: usize,
    pub primary_abbreviated: bool,
    pub full_coverage: bool,
    pub fast_ready: bool,
    protected_word: bool,
    pub exact_prefix: bool,
}
impl Decoder {
    pub fn new() -> Self {
        Self {
            paths: [[Path::default(); BEAM]; MAX_INPUT_BYTES + 1],
            pairs: [Pair::default(); 128],
            lengths: [0; MAX_INPUT_BYTES + 1],
            sentences: [Sentence::default(); BEAM * 2],
            count: 0,
            segments: [Word::default(); 64],
            segment_count: 0,
            primary_abbreviated: false,
            full_coverage: false,
            fast_ready: false,
            protected_word: false,
            exact_prefix: false,
        }
    }
    pub fn clear_cache(&mut self) {
        self.pairs.fill(Pair::default());
    }
    fn transition(&mut self, d: &Dictionary, left: u32, right: u32) -> Pair {
        let at = ((left.wrapping_mul(2654435761) ^ right.wrapping_mul(2246822519)) & 127) as usize;
        let old = self.pairs[at];
        if old.left == left && old.right == right {
            return old;
        }
        let a = d.entry(left).text;
        let b = d.entry(right).text;
        let bonus = crate::language::bonus(a, b);
        let a_single = a.chars().nth(1).is_none();
        let b_single = b.chars().nth(1).is_none();
        let lexical = bonus > 0.0 || d.attests_boundary(a, b);
        // A small explicit pronoun frame preserves free sentence composition.
        // It never opens the homophone product of arbitrary single characters.
        let pronoun = |s: &str| {
            matches!(
                s,
                "我" | "你" | "他" | "她" | "它" | "我们" | "你们" | "他们" | "她们" | "它们"
            )
        };
        let pronoun_frame = (pronoun(a) && !b_single) || (!a_single && pronoun(b));
        // Productive exact constructions, anchored by dictionary words on both
        // sides. A / 不A (or A不 / A) requires the same written A, not homophones.
        // 的 attaches to an attested multi-character word, never arbitrary singles.
        let particle = !a_single && matches!(b, "吧" | "吗" | "呢" | "啊" | "呀");
        let grammar = (!a_single && b == "的")
            || particle
            || b.strip_prefix('不') == Some(a)
            || a.strip_suffix('不') == Some(b);
        let frame = pronoun_frame || grammar;
        let unsupported = !lexical && !frame;
        let allowed = !unsupported || (!a_single && !b_single);
        let pair = Pair {
            left,
            right,
            bonus: if lexical {
                bonus
            } else if frame {
                -2.0
            } else {
                -6.0
            },
            allowed,
            unsupported,
            exact_only: grammar && !lexical && !pronoun_frame,
            terminal_only: particle && !lexical,
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
                .then(a.unsupported.cmp(&b.unsupported))
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
        self.protected_word = false;
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
        // Whole lexical matches need no sentence graph. This also avoids paying
        // for the homophone products that would subsequently be discarded.
        d.matches(input, 0, 1, |id, end| {
            if end == size && d.entry(id).text.chars().nth(1).is_some() {
                self.protected_word = true;
                self.paths[0][0] = Path {
                    cost: d.word_cost(id),
                    id,
                    next: size as u8,
                    ..Path::default()
                };
                self.lengths[0] = 1;
            }
        })?;
        if self.protected_word {
            return Ok(false);
        }
        for start in (0..size).rev() {
            if input.as_bytes()[start] == b'\'' {
                continue;
            }
            d.matches(input, start, BEAM, |id, end| {
                let cost = d.word_cost(id);
                // The context bonus depends on this word and `context` only, never
                // on the rank it is reached at, so it is hoisted out of the loop
                // below (which runs up to BEAM times per word).
                let context_bonus = if start == 0 {
                    crate::language::bonus(context, d.entry(id).text)
                } else {
                    0.0
                };
                for rank in 0..self.lengths[end] as usize {
                    let pair = if end < size {
                        self.transition(d, id, self.paths[end][rank].id)
                    } else {
                        Pair::default()
                    };
                    let unsupported =
                        self.paths[end][rank].unsupported + u8::from(pair.unsupported);
                    if !pair.allowed
                        || (pair.terminal_only && self.paths[end][rank].next as usize != size)
                        || (pair.exact_only
                            && (self.paths[end][rank].corrected
                                || self.paths[end][rank].abbreviated
                                || self.paths[end][rank].predicted))
                        || unsupported > 1
                        || (unsupported != 0
                            && (self.paths[end][rank].corrected
                                || self.paths[end][rank].abbreviated
                                || self.paths[end][rank].predicted))
                    {
                        continue;
                    }
                    let item = Path {
                        cost: cost + self.paths[end][rank].cost - pair.bonus - context_bonus,
                        id,
                        next: end as u8,
                        rank: rank as u8,
                        abbreviated: self.paths[end][rank].abbreviated,
                        predicted: self.paths[end][rank].predicted,
                        corrected: self.paths[end][rank].corrected,
                        unsupported,
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
                    if !pair.allowed
                        || pair.exact_only
                        || pair.terminal_only
                        || pair.unsupported
                        || self.paths[end][rank].unsupported != 0
                    {
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
                            unsupported: 0,
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
                    // Same loop-invariant hoist as the lexical branch above.
                    let context_bonus = if start == 0 {
                        crate::language::bonus(context, d.entry(id).text)
                    } else {
                        0.0
                    };
                    for rank in 0..self.lengths[end] as usize {
                        let pair = if end < size {
                            self.transition(d, id, self.paths[end][rank].id)
                        } else {
                            Pair::default()
                        };
                        if !pair.allowed
                            || pair.exact_only
                            || pair.terminal_only
                            || pair.unsupported
                            || self.paths[end][rank].unsupported != 0
                        {
                            continue;
                        }
                        self.insert(
                            start,
                            Path {
                                cost: cost + self.paths[end][rank].cost
                                    - pair.bonus
                                    - context_bonus,
                                id,
                                next: end as u8,
                                rank: rank as u8,
                                abbreviated: true,
                                predicted: self.paths[end][rank].predicted,
                                corrected: self.paths[end][rank].corrected,
                                unsupported: 0,
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
                                unsupported: 0,
                            },
                        );
                    }
                }
            }
        }
        self.primary_abbreviated = self.lengths[0] > 0 && self.paths[0][0].abbreviated;
        Ok(limited && self.primary_abbreviated)
    }
    fn render(&mut self, d: &Dictionary, input: &str, base: usize, options: u32) -> bool {
        let mut limited = false;
        let size = input.len();
        self.count = base;
        // A complete attested word owns its input span. Do not dilute it with
        // alternative homophone products obtained by splitting that same span.
        if self.protected_word {
            return false;
        }
        let supported_exact = self.sentences[..base].iter().any(|s| s.exact && !s.weak)
            || self.paths[0][..self.lengths[0] as usize]
                .iter()
                .any(|p| p.unsupported == 0 && !p.corrected && !p.abbreviated && !p.predicted);
        let mut weak_rendered = self.sentences[..base].iter().any(|s| s.weak);
        let exact_available = self.sentences[..base].iter().any(|s| s.exact)
            || self.paths[0][..self.lengths[0] as usize]
                .iter()
                .any(|p| !p.corrected && !p.abbreviated && !p.predicted);
        let full_syllables = crate::syllables::count_spelling(input);
        for initial in 0..self.lengths[0] as usize {
            // Unknown phrase joins are an exact-input fallback only. Once a
            // completely witnessed parse exists, do not fill pages with them.
            if (supported_exact || weak_rendered) && self.paths[0][initial].unsupported != 0 {
                continue;
            }
            let mut sentence = Sentence {
                weak: self.paths[0][initial].unsupported != 0,
                ..Sentence::default()
            };
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
            // A reliable exact composition suppresses speculative generated sentences;
            // lexical fuzzy words still have their independent recall lane.
            // Fully spelled input must not grow extra syllables through local
            // correction/initial expansion at each independent word boundary.
            if !sentence.exact
                && (exact_available
                    || self.exact_prefix
                    || full_syllables.is_some_and(|n| {
                        n <= 2 || usize::from(n) != sentence.pinyin().split('\'').count()
                    }))
            {
                continue;
            }
            // A word-local alignment budget must not reset at every sentence
            // boundary. Synthesized corrections get one global cost budget;
            // lexical fuzzy words keep the independent dictionary recall lane.
            if sentence.corrected
                && !crate::fuzzy::penalty(input, sentence.pinyin(), options)
                    .is_some_and(|cost| cost <= 2)
            {
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
                weak_rendered |= sentence.weak;
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
        exact_prefix: bool,
    ) -> Result<bool, LookupError> {
        self.count = 0;
        self.segment_count = 0;
        self.fast_ready = false;
        self.full_coverage = false;
        self.exact_prefix = exact_prefix;
        // Protect an accurate multi-syllable lexical prefix before generating
        // locally corrected paths that replace it. Whole-word recall is separate.
        let graph_options = if exact_prefix { 0 } else { options };
        let mut limited = self.compute(d, input, context, false, graph_options)?;
        self.full_coverage = self.paths[0][..self.lengths[0] as usize]
            .iter()
            .any(|p| !p.predicted);
        if self.lengths[0] == 0 && !input.is_empty() {
            limited |= self.compute(d, input, context, true, graph_options)?;
            self.fast_ready = true;
        }
        limited |= self.render(d, input, 0, options);
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
        let graph_options = if self.exact_prefix { 0 } else { options };
        let limited = self.compute(d, input, context, true, graph_options)?
            | self.render(d, input, base, options);
        self.fast_ready = true;
        Ok(limited)
    }
}
