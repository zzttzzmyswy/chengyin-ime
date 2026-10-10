// SPDX-License-Identifier: GPL-3.0-or-later
use crate::decoder::{Decoder, Word};
use crate::dictionary::valid_input;
use crate::{
    Candidate, CandidateCursor, Dictionary, MAX_CANDIDATES, MAX_INPUT_BYTES, MAX_TEXT_BYTES,
};
use std::sync::Arc;

const MAX_RESULTS: usize = 4096;
const LEARNED: u32 = 1 << 31;
const EXACT_WORD: u8 = 0;
const EXACT_SENTENCE: u8 = 1;
const RECALLED_WORD: u8 = 2;
const GENERATED_SENTENCE: u8 = 3;
const SEGMENT: u8 = 4;
const COMPLETION: u8 = 5;
const EXACT_HISTORY: u8 = 0;
const MATCHED_WORD: u8 = 1;
const MATCHED_CHARACTER: u8 = 2;
const RECALLED_HISTORY: u8 = 3;
const FUZZY_WORD: u8 = 4;
const FUZZY_CHARACTER: u8 = 5;
const OTHER_SENTENCE: u8 = 6;
const OTHER_COMPLETION: u8 = 7;
#[derive(Debug, Clone, Copy, Default)]
pub struct Modifiers {
    pub control: bool,
    pub alt: bool,
    pub super_key: bool,
}
#[derive(Debug, Clone, Copy)]
pub enum Key {
    Character(char),
    Space,
    Backspace,
    Delete,
    Escape,
    Enter,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Select(usize),
    Tab,
    Other,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ProcessResult {
    pub handled: bool,
    pub limited: bool,
}
#[derive(Clone, Copy, Default)]
struct ResultRef(u32);
impl ResultRef {
    fn new(id: u32, consumed: u8, sentence: bool, association: bool, class: u8) -> Self {
        debug_assert!(id & !LEARNED < (1 << 18));
        Self(
            (id & 0x8003ffff)
                | (u32::from(consumed) << 18)
                | (u32::from(sentence) << 26)
                | (u32::from(association) << 27)
                | (u32::from(class) << 28),
        )
    }
    fn id(self) -> u32 {
        self.0 & 0x8003ffff
    }
    fn consumed(self) -> u8 {
        (self.0 >> 18) as u8
    }
    fn sentence(self) -> bool {
        self.0 & (1 << 26) != 0
    }
    fn association(self) -> bool {
        self.0 & (1 << 27) != 0
    }
    fn class(self) -> u8 {
        ((self.0 >> 28) & 7) as u8
    }
}
#[derive(Clone, Copy, Default)]
struct Boundary {
    raw: u8,
    text: u16,
}

/// Accumulates what one composition was committed in, segment by segment, so the
/// whole "spelling -> phrase" pair can be learned next to the per-segment rows.
/// Only the incremental adapter needs it: the staged adapter holds the complete
/// spelling in `raw` until its single final commit, so its `learning_key` is
/// already the whole phrase.
///
/// A composition boundary resets the buffer (`process` drops it whenever it is
/// entered with no composition open), so cancelled or abandoned segments cannot
/// leak into the next composition. A pair whose final segment did commit stays
/// `pending` until `learn_commit` consumes it, since the host acknowledges the
/// write after `process` returns.
struct PhraseBuffer {
    key: String,
    text: String,
    segments: u8,
    pending: bool,
    overflow: bool,
}
impl PhraseBuffer {
    fn new() -> Self {
        Self {
            key: String::with_capacity(MAX_INPUT_BYTES),
            text: String::with_capacity(MAX_TEXT_BYTES),
            segments: 0,
            pending: false,
            overflow: false,
        }
    }
    fn clear(&mut self) {
        self.key.clear();
        self.text.clear();
        self.segments = 0;
        self.pending = false;
        self.overflow = false;
    }
    /// One committed segment. Buffers are sized to the profile's own key/text
    /// limits and the pushes are guarded, so an over-long or non-Chinese phrase
    /// is abandoned whole rather than truncated into a mismatched key/text pair.
    fn push(&mut self, key: &str, text: &str) {
        // A finished pair belongs to the composition that produced it. Pushing on
        // top means the host never consumed it, so start the new composition clean.
        if self.pending {
            self.clear();
        }
        self.segments = self.segments.saturating_add(1);
        if !crate::profile::chinese(text)
            || self.key.len() + key.len() > MAX_INPUT_BYTES
            || self.text.len() + text.len() > MAX_TEXT_BYTES
        {
            self.overflow = true;
            return;
        }
        self.key.push_str(key);
        self.text.push_str(text);
    }
    /// The composition reached a clean final segment; hold the pair until the
    /// host acknowledges the write with `learn_commit`.
    fn finish(&mut self) {
        self.pending = true;
    }
    fn learnable(&self) -> bool {
        self.pending && !self.overflow && self.segments >= 2
    }
}

/// One mutable session per input context. Cursor/beam/output buffers are reserved
/// once. Pages are produced lazily, preserving the complete terminal lists.
pub struct Session {
    dictionary: Arc<Dictionary>,
    raw: String,
    preedit: String,
    commit: String,
    completed: String,
    caret: usize,
    offset: usize,
    boundaries: [Boundary; MAX_INPUT_BYTES],
    boundary_count: usize,
    cursor: CandidateCursor,
    decoder: Decoder,
    results: Vec<ResultRef>,
    page: usize,
    selected: usize,
    lexical_matches: bool,
    early_prefix_done: bool,
    sentence_index: usize,
    segment_index: usize,
    phase: u8,
    exhausted: bool,
    budget_limited: bool,
    context: String,
    recent: [u32; 32],
    recent_len: usize,
    profile: Arc<crate::Profile>,
    history_cache: crate::profile_cache::HistoryCache,
    learning_key: String,
    phrase: PhraseBuffer,
    page_size: usize,
    learning_enabled: bool,
    associations_enabled: bool,
    matching_options: u32,
    incremental: bool,
    prefix_ends: u64,
    prefix_end: u8,
    exact_sentence_index: usize,
    exact_history: [u32; 4],
    exact_count: [usize; 2],
    recalled_history: [u32; 4],
    recalled_count: [usize; 2],
    recalled_index: usize,
    word_line: bool,
    character_line: bool,
    initials_index: u32,
    initials_end: u32,
    initials_count: u8,
    single_syllable: bool,
    /// The lexicon-query spelling of the whole `raw`: the bytes the user typed
    /// with their `ü` variants canonicalised (I22). Valid only while
    /// `canonicalized` is set.
    canonical: [u8; MAX_INPUT_BYTES],
    /// Whether `canonical` holds a rewritten spelling of `raw`. Clear for every
    /// input that needs no rewrite — all of them but a `v` variant — and every
    /// reader then slices `raw` itself, so those inputs pay nothing for this.
    canonicalized: bool,
}
/// The spelling to query the lexicon with for `raw[start..end]` (I22): the typed
/// bytes with their `ü` variants canonicalised when the input carries one, and
/// the typed bytes themselves otherwise.
///
/// Free rather than a `&self` method so a caller can hold the result across a
/// `&mut` borrow of another [`Session`] field — `cursor.reset(&dictionary, input)`
/// is exactly that shape — because it borrows only `raw` and `canonical`.
/// Slicing `canonical` at a `raw` offset is sound because every rewrite is one
/// byte for one byte. `raw` is never rewritten: the preedit, the caret, the
/// learning history and the stored composition keep what the user typed.
fn query_spelling<'a>(
    raw: &'a str,
    canonical: &'a [u8; MAX_INPUT_BYTES],
    canonicalized: bool,
    start: usize,
    end: usize,
) -> &'a str {
    if canonicalized {
        std::str::from_utf8(&canonical[start..end]).expect("validated ASCII input")
    } else {
        &raw[start..end]
    }
}
impl Session {
    /// Recompute [`Session::canonical`] from `raw`. One bounded scan that returns
    /// immediately when the input holds no `v`, which is what keeps this support
    /// free for every other keystroke.
    fn recompute_spelling(&mut self) {
        self.canonicalized = crate::syllables::canonicalize(&self.raw, &mut self.canonical);
    }
    /// [`query_spelling`] over the live composition up to `end`, an absolute
    /// offset. Only `&self` callers can use this: it borrows all of `self`, so a
    /// site that also takes `&mut` of another field calls `query_spelling`
    /// directly with the two input fields.
    fn live_spelling_to(&self, end: usize) -> &str {
        query_spelling(
            &self.raw,
            &self.canonical,
            self.canonicalized,
            self.offset,
            end,
        )
    }
    pub fn new(dictionary: Arc<Dictionary>) -> Self {
        Self {
            dictionary,
            raw: String::with_capacity(MAX_INPUT_BYTES),
            preedit: String::with_capacity(MAX_TEXT_BYTES),
            commit: String::with_capacity(MAX_TEXT_BYTES),
            completed: String::with_capacity(MAX_TEXT_BYTES),
            caret: 0,
            offset: 0,
            boundaries: [Boundary::default(); MAX_INPUT_BYTES],
            boundary_count: 0,
            cursor: CandidateCursor::new(),
            decoder: Decoder::new(),
            results: Vec::with_capacity(MAX_RESULTS),
            page: 0,
            selected: 0,
            lexical_matches: false,
            early_prefix_done: false,
            sentence_index: 0,
            segment_index: 0,
            phase: 0,
            exhausted: true,
            budget_limited: false,
            context: String::with_capacity(64),
            recent: [u32::MAX; 32],
            recent_len: 0,
            profile: Arc::new(crate::Profile::default()),
            history_cache: crate::profile_cache::HistoryCache::default(),
            learning_key: String::with_capacity(MAX_INPUT_BYTES),
            phrase: PhraseBuffer::new(),
            page_size: MAX_CANDIDATES,
            learning_enabled: true,
            associations_enabled: true,
            matching_options: 0,
            incremental: false,
            prefix_ends: 0,
            prefix_end: 0,
            exact_sentence_index: 0,
            exact_history: [0; 4],
            exact_count: [0; 2],
            recalled_history: [0; 4],
            recalled_count: [0; 2],
            recalled_index: 0,
            word_line: false,
            character_line: false,
            initials_index: 0,
            initials_end: 0,
            initials_count: 0,
            single_syllable: false,
            canonical: [0; MAX_INPUT_BYTES],
            canonicalized: false,
        }
    }
    pub fn preedit(&self) -> &str {
        &self.preedit
    }
    pub fn preedit_cursor(&self) -> usize {
        self.completed.len() + self.caret - self.offset
    }
    pub fn commit(&self) -> &str {
        &self.commit
    }
    pub fn page(&self) -> usize {
        self.page
    }
    pub fn has_next_page(&self) -> bool {
        self.results.len() > (self.page + 1) * self.page_size
    }
    pub fn budget_limited(&self) -> bool {
        self.budget_limited
    }
    pub fn candidate_count(&self) -> usize {
        self.results
            .len()
            .saturating_sub(self.page * self.page_size)
            .min(self.page_size)
    }
    pub fn selected(&self) -> usize {
        self.selected
    }
    fn result_candidate(&self, item: ResultRef) -> Candidate<'_> {
        if item.id() & LEARNED != 0 {
            let r = &self.profile.rows[(item.id() & !LEARNED) as usize];
            Candidate {
                pinyin: self
                    .dictionary
                    .corrected_pronunciation(
                        &r.text,
                        query_spelling(
                            &self.raw,
                            &self.canonical,
                            self.canonicalized,
                            self.offset,
                            self.raw.len(),
                        ),
                        self.matching_options,
                    )
                    .or_else(|| {
                        if self.matching_options == 0 {
                            return None;
                        }
                        self.decoder.sentences[..self.decoder.count]
                            .iter()
                            .find(|s| s.corrected && s.text() == r.text.as_ref())
                            .map(|s| s.pinyin())
                    })
                    .unwrap_or(if self.matching_options == 0 {
                        &r.key
                    } else {
                        &r.pinyin
                    }),
                text: &r.text,
                frequency: r.count,
            }
        } else if item.association() && !item.sentence() {
            let e = self.dictionary.entry(item.id());
            Candidate {
                pinyin: "",
                text: &e.text[item.consumed() as usize..],
                frequency: e.frequency,
            }
        } else if item.association() {
            Candidate {
                pinyin: "",
                text: crate::language::links()[item.id() as usize].next,
                frequency: crate::language::links()[item.id() as usize].weight,
            }
        } else if item.sentence() {
            Candidate {
                pinyin: if self.decoder.sentences[item.id() as usize].corrected {
                    self.decoder.sentences[item.id() as usize].pinyin()
                } else {
                    query_spelling(
                        &self.raw,
                        &self.canonical,
                        self.canonicalized,
                        self.offset,
                        self.raw.len(),
                    )
                },
                text: self.decoder.sentences[item.id() as usize].text(),
                frequency: 0,
            }
        } else {
            self.dictionary.entry(item.id())
        }
    }
    pub fn candidate(&self, index: usize) -> Option<Candidate<'_>> {
        (index < self.candidate_count())
            .then(|| self.result_candidate(self.results[self.page * self.page_size + index]))
    }
    pub fn configure_matching(&mut self, options: u32) -> bool {
        if !self.preedit.is_empty() || options & !crate::fuzzy::OPTIONS_MASK != 0 {
            return false;
        }
        self.matching_options = options;
        true
    }
    /// Opt in to immediate prefix commits plus a remaining preedit. Legacy
    /// adapters retain staged, reversible segment selection by default.
    pub fn configure_incremental(&mut self, enabled: bool) -> bool {
        if !self.preedit.is_empty() {
            return false;
        }
        self.incremental = enabled;
        true
    }
    fn next_prefix(&mut self) -> Option<ResultRef> {
        loop {
            if self.prefix_end == 0 {
                if self.prefix_ends == 0 {
                    return None;
                }
                self.prefix_end = (63 - self.prefix_ends.leading_zeros()) as u8;
                self.prefix_ends &= !(1u64 << self.prefix_end);
                let prefix = query_spelling(
                    &self.raw,
                    &self.canonical,
                    self.canonicalized,
                    self.offset,
                    self.offset + self.prefix_end as usize,
                );
                self.cursor
                    .reset(&self.dictionary, prefix.trim_end_matches('\''))
                    .expect("validated prefix");
            }
            match self.cursor.next(&self.dictionary) {
                Ok(Some(id)) => {
                    let letters = query_spelling(
                        &self.raw,
                        &self.canonical,
                        self.canonicalized,
                        self.offset,
                        self.offset + self.prefix_end as usize,
                    )
                    .bytes()
                    .filter(|&b| b != b'\'')
                    .count();
                    if self
                        .dictionary
                        .entry(id)
                        .pinyin
                        .bytes()
                        .filter(|&b| b != b'\'')
                        .count()
                        == letters
                    {
                        return Some(ResultRef::new(
                            id,
                            self.prefix_end,
                            false,
                            false,
                            if self.matching_options == 0 {
                                SEGMENT
                            } else {
                                OTHER_COMPLETION
                            },
                        ));
                    }
                    // Exact terminals precede subtree completions; do not scan
                    // the whole subtree for a prefix's remaining homophones.
                    self.prefix_end = 0;
                }
                Err(_) => {
                    self.budget_limited = true;
                    self.prefix_end = 0;
                }
                Ok(None) => self.prefix_end = 0,
            }
        }
    }
    pub fn candidate_marks(&self, index: usize) -> Option<[u64; 4]> {
        let candidate = self.candidate(index)?;
        let item = self.results[self.page * self.page_size + index];
        if item.association()
            || self.initials_count != 0
            || self.matching_options == 0
            || (item.sentence() && !self.decoder.sentences[item.id() as usize].corrected)
        {
            return Some([0; 4]);
        }
        Some(
            crate::fuzzy::annotations(
                self.live_spelling_to(self.offset + item.consumed() as usize)
                    .trim_end_matches('\''),
                candidate.pinyin,
                self.matching_options,
            )
            .unwrap_or([0; 4]),
        )
    }
    fn begin_corrections(&mut self) {
        self.phase = 8;
        self.budget_limited |= self.cursor.reset_tolerant(
            &self.dictionary,
            query_spelling(
                &self.raw,
                &self.canonical,
                self.canonicalized,
                self.offset,
                self.raw.len(),
            ),
            self.matching_options,
        );
    }
    pub fn set_selected(&mut self, index: usize) -> bool {
        if index >= self.candidate_count() {
            return false;
        }
        self.selected = index;
        true
    }
    pub fn candidate_consumed(&self, index: usize) -> Option<usize> {
        (index < self.candidate_count()).then(|| {
            let r = self.results[self.page * self.page_size + index];
            if r.association() {
                0
            } else {
                r.consumed() as usize
            }
        })
    }
    pub fn estimated_heap_bytes(&self) -> usize {
        self.raw.capacity()
            + self.preedit.capacity()
            + self.commit.capacity()
            + self.completed.capacity()
            + self.context.capacity()
            + self.learning_key.capacity()
            + self.phrase.key.capacity()
            + self.phrase.text.capacity()
            + self.results.capacity() * std::mem::size_of::<ResultRef>()
    }
    pub fn set_dictionary(&mut self, dictionary: Arc<Dictionary>) -> bool {
        if !self.preedit.is_empty() {
            return false;
        }
        self.clear_composition();
        self.context.clear();
        self.recent_len = 0;
        self.decoder.clear_cache();
        self.history_cache.invalidate();
        self.dictionary = dictionary;
        true
    }
    fn update_preedit(&mut self) {
        self.preedit.clear();
        self.preedit.push_str(&self.completed);
        self.preedit.push_str(&self.raw[self.offset..]);
    }
    fn clear_composition(&mut self) {
        self.raw.clear();
        self.preedit.clear();
        self.completed.clear();
        self.results.clear();
        self.offset = 0;
        self.caret = 0;
        self.page = 0;
        self.selected = 0;
        self.boundary_count = 0;
        self.exhausted = true;
        self.budget_limited = false;
        self.initials_count = 0;
        self.single_syllable = false;
        self.canonicalized = false;
    }
    pub fn reset(&mut self) {
        self.clear_composition();
        self.commit.clear();
        self.learning_key.clear();
        self.phrase.clear();
        self.context.clear();
        self.recent_len = 0;
        self.decoder.clear_cache();
    }
    fn next_result(&mut self) -> Option<ResultRef> {
        if self.phase >= 14 {
            if let Some(character) = self.next_single_character() {
                return Some(character);
            }
        }
        if self.initials_count != 0 {
            let size = (self.raw.len() - self.offset) as u8;
            if self.matching_options != 0 && self.recalled_index < self.exact_count[0] {
                let id = self.exact_history[self.recalled_index];
                self.recalled_index += 1;
                return Some(ResultRef::new(
                    LEARNED | id,
                    size,
                    false,
                    false,
                    EXACT_HISTORY,
                ));
            }
            if self.initials_index == self.initials_end {
                return None;
            }
            let id = self.dictionary.initials_word(self.initials_index);
            self.initials_index += 1;
            return Some(ResultRef::new(
                id,
                size,
                false,
                false,
                if self.matching_options == 0 {
                    EXACT_WORD
                } else {
                    MATCHED_WORD
                },
            ));
        }
        if self.matching_options != 0 {
            return self.next_matched_result();
        }
        let size = (self.raw.len() - self.offset) as u8;
        loop {
            match self.phase {
                8 => {
                    if self.matching_options != 0 {
                        while self.exact_sentence_index < self.decoder.count {
                            let id = self.exact_sentence_index;
                            self.exact_sentence_index += 1;
                            if self.decoder.sentences[id].exact {
                                return Some(ResultRef::new(
                                    id as u32,
                                    size,
                                    true,
                                    false,
                                    EXACT_SENTENCE,
                                ));
                            }
                        }
                    }
                    self.phase = 7;
                }
                0 => {
                    // Whole-input exact homophones retain their ordinary rank.
                    match self.cursor.next(&self.dictionary) {
                        Ok(Some(id)) => {
                            let word = self.dictionary.entry(id);
                            let input = query_spelling(
                                &self.raw,
                                &self.canonical,
                                self.canonicalized,
                                self.offset,
                                self.raw.len(),
                            );
                            // Canonical/input letter counts distinguish exact from prefix completion.
                            if word.pinyin.bytes().filter(|&b| b != b'\'').count()
                                == input.bytes().filter(|&b| b != b'\'').count()
                            {
                                return Some(ResultRef::new(id, size, false, false, EXACT_WORD));
                            }
                            self.begin_corrections();
                        }
                        Ok(None) => {
                            self.begin_corrections();
                        }
                        Err(_) => {
                            self.budget_limited = true;
                            self.begin_corrections();
                        }
                    }
                }
                7 => {
                    match self.cursor.next(&self.dictionary) {
                        Ok(Some(id)) => {
                            let spelling = self.dictionary.entry(id).pinyin;
                            if crate::fuzzy::annotations(
                                query_spelling(
                                    &self.raw,
                                    &self.canonical,
                                    self.canonicalized,
                                    self.offset,
                                    self.raw.len(),
                                ),
                                spelling,
                                self.matching_options,
                            )
                            .is_some_and(|m| m != [0; 4])
                            {
                                return Some(ResultRef::new(id, size, false, false, RECALLED_WORD));
                            }
                            continue;
                        }
                        Err(_) => self.budget_limited = true,
                        Ok(None) => (),
                    }
                    self.phase = 1;
                    self.budget_limited |= !self.decoder.full_coverage
                        && self
                            .cursor
                            .reset_fast(
                                &self.dictionary,
                                query_spelling(
                                    &self.raw,
                                    &self.canonical,
                                    self.canonicalized,
                                    self.offset,
                                    self.raw.len(),
                                ),
                            )
                            .unwrap_or(true)
                        && self.decoder.primary_abbreviated;
                }
                1 => {
                    if self.decoder.full_coverage {
                        self.phase = 2;
                        continue;
                    }
                    match self.cursor.next(&self.dictionary) {
                        Ok(Some(id)) => {
                            return Some(ResultRef::new(id, size, false, false, RECALLED_WORD))
                        }
                        Ok(None) => self.phase = 2,
                        Err(_) => {
                            self.budget_limited = true;
                            self.phase = 2;
                        }
                    }
                }
                2 => {
                    if self.sentence_index < self.decoder.count {
                        let id = self.sentence_index as u32;
                        self.sentence_index += 1;
                        if self.matching_options != 0 && self.decoder.sentences[id as usize].exact {
                            continue;
                        }
                        return Some(ResultRef::new(id, size, true, false, GENERATED_SENTENCE));
                    }
                    self.phase = 3;
                }
                3 => {
                    if self.incremental {
                        if let Some(prefix) = self.next_prefix() {
                            return Some(prefix);
                        }
                        self.phase = if self.decoder.full_coverage { 4 } else { 5 };
                        if self.decoder.full_coverage {
                            self.budget_limited |= self
                                .cursor
                                .reset_fast(
                                    &self.dictionary,
                                    query_spelling(
                                        &self.raw,
                                        &self.canonical,
                                        self.canonicalized,
                                        self.offset,
                                        self.raw.len(),
                                    ),
                                )
                                .unwrap_or(true);
                        }
                        continue;
                    }
                    // Segmentation is useful for continuous input with a complete decode.
                    if (self.decoder.count > 0 || !self.lexical_matches)
                        && self.segment_index < self.decoder.segment_count
                    {
                        let Word { id, consumed, .. } = self.decoder.segments[self.segment_index];
                        self.segment_index += 1;
                        return Some(ResultRef::new(id, consumed, false, false, SEGMENT));
                    }
                    self.phase = if self.decoder.full_coverage { 4 } else { 5 };
                    if self.decoder.full_coverage {
                        self.budget_limited |= self
                            .cursor
                            .reset_fast(
                                &self.dictionary,
                                query_spelling(
                                    &self.raw,
                                    &self.canonical,
                                    self.canonicalized,
                                    self.offset,
                                    self.raw.len(),
                                ),
                            )
                            .unwrap_or(true);
                    }
                }
                4 => match self.cursor.next(&self.dictionary) {
                    Ok(Some(id)) => {
                        return Some(ResultRef::new(id, size, false, false, RECALLED_WORD))
                    }
                    Ok(None) => self.phase = 5,
                    Err(_) => {
                        self.budget_limited = true;
                        self.phase = 5;
                    }
                },
                5 => {
                    if !self.decoder.fast_ready {
                        self.budget_limited |= self
                            .decoder
                            .decode_alternates(
                                &self.dictionary,
                                query_spelling(
                                    &self.raw,
                                    &self.canonical,
                                    self.canonicalized,
                                    self.offset,
                                    self.raw.len(),
                                ),
                                if self.completed.is_empty() {
                                    &self.context
                                } else {
                                    &self.completed
                                },
                                self.matching_options,
                            )
                            .unwrap_or(true);
                    }
                    if self.sentence_index < self.decoder.count {
                        let id = self.sentence_index as u32;
                        self.sentence_index += 1;
                        return Some(ResultRef::new(id, size, true, false, GENERATED_SENTENCE));
                    }
                    self.phase = 6;
                    self.cursor
                        .reset(
                            &self.dictionary,
                            query_spelling(
                                &self.raw,
                                &self.canonical,
                                self.canonicalized,
                                self.offset,
                                self.raw.len(),
                            ),
                        )
                        .expect("validated input");
                }
                6 => match self.cursor.next(&self.dictionary) {
                    Ok(Some(id)) => {
                        return Some(ResultRef::new(id, size, false, false, COMPLETION))
                    }
                    Err(_) => {
                        self.budget_limited = true;
                        return None;
                    }
                    Ok(None) => return None,
                },
                _ => return None,
            }
        }
    }
    fn next_whole_word(&mut self, single: bool, recalled: bool, class: u8) -> Option<ResultRef> {
        let input = query_spelling(
            &self.raw,
            &self.canonical,
            self.canonicalized,
            self.offset,
            self.raw.len(),
        );
        loop {
            match self.cursor.next(&self.dictionary) {
                Ok(Some(id)) => {
                    let word = self.dictionary.entry(id);
                    if !recalled
                        && word.pinyin.bytes().filter(|&b| b != b'\'').count()
                            != input.bytes().filter(|&b| b != b'\'').count()
                    {
                        return None;
                    }
                    if word.text.chars().nth(1).is_none() != single {
                        continue;
                    }
                    let matches = if recalled {
                        crate::fuzzy::complete_annotations(
                            input,
                            word.pinyin,
                            self.matching_options,
                        )
                        .is_some_and(|marks| marks != [0; 4])
                    } else {
                        word.pinyin.bytes().filter(|&b| b != b'\'').count()
                            == input.bytes().filter(|&b| b != b'\'').count()
                    };
                    if matches {
                        return Some(ResultRef::new(id, input.len() as u8, false, false, class));
                    }
                }
                Err(_) => {
                    self.budget_limited = true;
                    return None;
                }
                Ok(None) => return None,
            }
        }
    }
    fn next_history(&mut self, single: bool, recalled: bool) -> Option<ResultRef> {
        let lane = usize::from(single);
        let count = if recalled {
            self.recalled_count[lane]
        } else {
            self.exact_count[lane]
        };
        if self.recalled_index >= count {
            return None;
        }
        let at = lane * 2 + self.recalled_index;
        self.recalled_index += 1;
        let id = if recalled {
            self.recalled_history[at]
        } else {
            self.exact_history[at]
        };
        if single {
            self.character_line = true;
        } else {
            self.word_line = true;
        }
        Some(ResultRef::new(
            LEARNED | id,
            (self.raw.len() - self.offset) as u8,
            false,
            false,
            if recalled {
                RECALLED_HISTORY
            } else {
                EXACT_HISTORY
            },
        ))
    }
    // A complete single syllable expresses character intent. Exhaust that lane
    // lazily before ambiguous splits, corrected multi-syllable words or completions.
    fn next_single_character(&mut self) -> Option<ResultRef> {
        loop {
            match self.phase {
                14 => {
                    if let Some(row) = self.next_history(true, false) {
                        return Some(row);
                    }
                    self.phase = 15;
                }
                15 => {
                    let class = if self.matching_options == 0 {
                        EXACT_WORD
                    } else {
                        MATCHED_CHARACTER
                    };
                    if let Some(row) = self.next_whole_word(true, false, class) {
                        self.character_line = true;
                        return Some(row);
                    }
                    self.phase = 16;
                    self.recalled_index = 0;
                }
                16 => {
                    if self.matching_options != 0 {
                        if let Some(row) = self.next_history(true, true) {
                            return Some(row);
                        }
                        self.budget_limited |= self.cursor.reset_tolerant(
                            &self.dictionary,
                            query_spelling(
                                &self.raw,
                                &self.canonical,
                                self.canonicalized,
                                self.offset,
                                self.raw.len(),
                            ),
                            self.matching_options,
                        );
                    }
                    self.phase = 17;
                }
                17 => {
                    if self.matching_options != 0 {
                        if let Some(row) = self.next_whole_word(true, true, FUZZY_CHARACTER) {
                            self.character_line = true;
                            return Some(row);
                        }
                    }
                    self.cursor
                        .reset(
                            &self.dictionary,
                            query_spelling(
                                &self.raw,
                                &self.canonical,
                                self.canonicalized,
                                self.offset,
                                self.raw.len(),
                            ),
                        )
                        .expect("validated input");
                    self.recalled_index = 0;
                    self.phase = if self.matching_options == 0 { 18 } else { 0 };
                }
                18 => {
                    // Preserve word preferences after the character lane, including
                    // dictionaries with no single-character entries for this key.
                    if let Some(row) = self.next_history(false, false) {
                        return Some(row);
                    }
                    self.phase = 0;
                }
                _ => return None,
            }
        }
    }
    fn completion_in_line(&self, id: u32) -> bool {
        if !self.word_line && !self.character_line {
            return true;
        }
        if self.dictionary.entry(id).text.chars().nth(1).is_none() {
            self.character_line
        } else {
            self.word_line
        }
    }
    // Independent whole-word and whole-character pipelines. Prefix characters
    // never enter word recall; each pipeline has its own two history slots.
    fn next_matched_result(&mut self) -> Option<ResultRef> {
        let size = (self.raw.len() - self.offset) as u8;
        loop {
            match self.phase {
                0 => {
                    if let Some(r) = self.next_history(false, false) {
                        return Some(r);
                    }
                    self.phase = 1;
                }
                1 => {
                    if let Some(r) = self.next_whole_word(false, false, MATCHED_WORD) {
                        self.word_line = true;
                        return Some(r);
                    }
                    self.phase = 2;
                }
                2 => {
                    while self.exact_sentence_index < self.decoder.count {
                        let id = self.exact_sentence_index;
                        self.exact_sentence_index += 1;
                        if self.decoder.sentences[id].exact {
                            self.word_line = true;
                            return Some(ResultRef::new(
                                id as u32,
                                size,
                                true,
                                false,
                                MATCHED_WORD,
                            ));
                        }
                    }
                    self.phase = 3;
                    self.recalled_index = 0;
                }
                3 => {
                    if let Some(r) = self.next_history(false, true) {
                        return Some(r);
                    }
                    self.budget_limited |= self.cursor.reset_tolerant(
                        &self.dictionary,
                        query_spelling(
                            &self.raw,
                            &self.canonical,
                            self.canonicalized,
                            self.offset,
                            self.raw.len(),
                        ),
                        self.matching_options,
                    );
                    self.phase = 4;
                }
                4 => {
                    if let Some(r) = self.next_whole_word(false, true, FUZZY_WORD) {
                        self.word_line = true;
                        return Some(r);
                    }
                    self.phase = if self.decoder.exact_prefix && !self.early_prefix_done {
                        13
                    } else {
                        5
                    };
                    self.sentence_index = 0;
                }
                5 => {
                    while self.sentence_index < self.decoder.count {
                        let id = self.sentence_index;
                        self.sentence_index += 1;
                        if self.decoder.sentences[id].corrected {
                            self.word_line = true;
                            return Some(ResultRef::new(id as u32, size, true, false, FUZZY_WORD));
                        }
                    }
                    self.phase = 6;
                    self.recalled_index = 0;
                    self.cursor
                        .reset(
                            &self.dictionary,
                            query_spelling(
                                &self.raw,
                                &self.canonical,
                                self.canonicalized,
                                self.offset,
                                self.raw.len(),
                            ),
                        )
                        .expect("validated input");
                }
                6 => {
                    if let Some(r) = self.next_history(true, false) {
                        return Some(r);
                    }
                    self.phase = 7;
                }
                7 => {
                    if let Some(r) = self.next_whole_word(true, false, MATCHED_CHARACTER) {
                        self.character_line = true;
                        return Some(r);
                    }
                    self.phase = 8;
                    self.recalled_index = 0;
                }
                8 => {
                    if let Some(r) = self.next_history(true, true) {
                        return Some(r);
                    }
                    self.budget_limited |= self.cursor.reset_tolerant(
                        &self.dictionary,
                        query_spelling(
                            &self.raw,
                            &self.canonical,
                            self.canonicalized,
                            self.offset,
                            self.raw.len(),
                        ),
                        self.matching_options,
                    );
                    self.phase = 9;
                }
                9 => {
                    if let Some(r) = self.next_whole_word(true, true, FUZZY_CHARACTER) {
                        self.character_line = true;
                        return Some(r);
                    }
                    self.phase = 13;
                }
                13 => {
                    if self.incremental {
                        if let Some(prefix) = self.next_prefix() {
                            return Some(prefix);
                        }
                    }
                    if self.decoder.exact_prefix && !self.early_prefix_done {
                        self.early_prefix_done = true;
                        self.phase = 5;
                        continue;
                    }
                    self.budget_limited |= self
                        .cursor
                        .reset_fast(
                            &self.dictionary,
                            query_spelling(
                                &self.raw,
                                &self.canonical,
                                self.canonicalized,
                                self.offset,
                                self.raw.len(),
                            ),
                        )
                        .unwrap_or(true);
                    self.phase = 10;
                }
                10 => match self.cursor.next(&self.dictionary) {
                    Ok(Some(id)) if self.completion_in_line(id) => {
                        return Some(ResultRef::new(id, size, false, false, OTHER_COMPLETION))
                    }
                    Ok(Some(_)) => (),
                    Err(_) => {
                        self.budget_limited = true;
                        self.phase = 11;
                    }
                    Ok(None) => {
                        self.phase = 11;
                    }
                },
                11 => {
                    if !self.decoder.fast_ready {
                        self.budget_limited |= self
                            .decoder
                            .decode_alternates(
                                &self.dictionary,
                                query_spelling(
                                    &self.raw,
                                    &self.canonical,
                                    self.canonicalized,
                                    self.offset,
                                    self.raw.len(),
                                ),
                                if self.completed.is_empty() {
                                    &self.context
                                } else {
                                    &self.completed
                                },
                                self.matching_options,
                            )
                            .unwrap_or(true);
                        self.sentence_index = 0;
                    }
                    while self.sentence_index < self.decoder.count {
                        let id = self.sentence_index;
                        self.sentence_index += 1;
                        let sentence = self.decoder.sentences[id];
                        if !sentence.exact && !sentence.corrected && !self.character_line {
                            return Some(ResultRef::new(
                                id as u32,
                                size,
                                true,
                                false,
                                OTHER_SENTENCE,
                            ));
                        }
                    }
                    self.cursor
                        .reset(
                            &self.dictionary,
                            query_spelling(
                                &self.raw,
                                &self.canonical,
                                self.canonicalized,
                                self.offset,
                                self.raw.len(),
                            ),
                        )
                        .expect("validated input");
                    self.phase = 12;
                }
                12 => match self.cursor.next(&self.dictionary) {
                    Ok(Some(id)) if self.completion_in_line(id) => {
                        return Some(ResultRef::new(id, size, false, false, OTHER_COMPLETION))
                    }
                    Ok(Some(_)) => (),
                    Err(_) => {
                        self.budget_limited = true;
                        return None;
                    }
                    Ok(None) => return None,
                },
                _ => return None,
            }
        }
    }
    fn fill(&mut self, target: usize) {
        while !self.exhausted && self.results.len() < target {
            if self.results.len() == MAX_RESULTS {
                self.budget_limited = true;
                self.exhausted = true;
                break;
            }
            let Some(item) = self.next_result() else {
                self.exhausted = true;
                break;
            };
            // Exact terminals are revisited by subtree completion. Deduplicate across
            // pronunciations and sentence paths while retaining distinct input spans.
            if self.results.iter().any(|&old| {
                (old.consumed() == item.consumed()
                    || (!self.incremental
                        && self.matching_options != 0
                        && old.consumed() > item.consumed()))
                    && self.result_candidate(old).text == self.result_candidate(item).text
            }) {
                continue;
            }
            if self.completed.len() + self.result_candidate(item).text.len() + self.raw.len()
                - self.offset
                - item.consumed() as usize
                > MAX_TEXT_BYTES
            {
                continue;
            }
            self.results.push(item);
        }
    }
    fn refresh(&mut self) {
        // The single point where the query spelling is produced: every lexicon
        // read below goes through `spelling`, never through `raw` (I22).
        self.recompute_spelling();
        self.single_syllable = crate::syllables::count_spelling(query_spelling(
            &self.raw,
            &self.canonical,
            self.canonicalized,
            self.offset,
            self.raw.len(),
        )) == Some(1);
        (self.initials_index, self.initials_end, self.initials_count) = self
            .dictionary
            .initials_range(query_spelling(
                &self.raw,
                &self.canonical,
                self.canonicalized,
                self.offset,
                self.raw.len(),
            ))
            .unwrap_or((0, 0, 0));
        let input = query_spelling(
            &self.raw,
            &self.canonical,
            self.canonicalized,
            self.offset,
            self.raw.len(),
        );
        self.cursor
            .reset(&self.dictionary, input)
            .expect("validated input");
        self.lexical_matches = self.cursor.has_matches();
        self.prefix_end = 0;
        self.prefix_ends = 0;
        self.early_prefix_done = false;
        let input = query_spelling(
            &self.raw,
            &self.canonical,
            self.canonicalized,
            self.offset,
            self.raw.len(),
        );
        let complete = crate::syllables::count_spelling(input).is_some_and(|n| n >= 3);
        let mut exact_prefix = false;
        let mut prefix_limited = false;
        if self.initials_count == 0 && self.incremental && !self.single_syllable {
            prefix_limited = self
                .dictionary
                .matches(input, 0, 1, |id, end| {
                    if end < input.len() {
                        self.prefix_ends |= 1u64 << end;
                        exact_prefix |= complete
                            && self.dictionary.entry(id).pinyin.contains('\'')
                            && self.dictionary.entry(id).text.chars().nth(1).is_some()
                            && crate::syllables::count_spelling(&input[end..]).is_some();
                    }
                })
                .is_err();
        }
        if self.initials_count != 0 {
            self.decoder.count = 0;
            self.decoder.segment_count = 0;
            self.decoder.exact_prefix = false;
            self.budget_limited = false;
        } else {
            match self.decoder.decode(
                &self.dictionary,
                query_spelling(
                    &self.raw,
                    &self.canonical,
                    self.canonicalized,
                    self.offset,
                    self.raw.len(),
                ),
                if self.completed.is_empty() {
                    &self.context
                } else {
                    &self.completed
                },
                self.matching_options,
                exact_prefix,
            ) {
                Err(_) => {
                    // Full-input trie validation already passed; an unusually ambiguous
                    // suffix only limits sentence suggestions, without corrupting input.
                    self.decoder.count = 0;
                    self.decoder.segment_count = 0;
                    self.budget_limited = true;
                }
                Ok(limited) => self.budget_limited = limited || prefix_limited,
            }
        }
        self.results.clear();
        self.page = 0;
        self.selected = 0;
        self.phase = if self.single_syllable { 14 } else { 0 };
        self.exact_sentence_index = 0;
        self.sentence_index = 0;
        self.segment_index = 0;
        self.exact_count = [0; 2];
        self.recalled_count = [0; 2];
        self.word_line = false;
        self.character_line = false;
        self.recalled_index = 0;

        self.exhausted = self.raw.len() == self.offset;
        if self.learning_enabled {
            let remaining = MAX_TEXT_BYTES - self.completed.len();
            if let Some(cached) = self.history_cache.get(
                query_spelling(
                    &self.raw,
                    &self.canonical,
                    self.canonicalized,
                    self.offset,
                    self.raw.len(),
                ),
                self.matching_options,
                remaining,
            ) {
                self.exact_history = cached.exact;
                self.recalled_history = cached.recalled;
                self.exact_count = cached.exact_count.map(usize::from);
                self.recalled_count = cached.recalled_count.map(usize::from);
            } else {
                let mut exact = [[(0usize, 0u32, 0u32); 2]; 2];
                let mut recalled = exact;
                let mut previous = "";
                let mut matches = false;
                let range = if self.matching_options == 0 {
                    self.profile.matching_range(query_spelling(
                        &self.raw,
                        &self.canonical,
                        self.canonicalized,
                        self.offset,
                        self.raw.len(),
                    ))
                } else {
                    0..self.profile.rows.len()
                };
                for id in range {
                    let row = &self.profile.rows[id];
                    let accurate = row.key.as_ref()
                        == query_spelling(
                            &self.raw,
                            &self.canonical,
                            self.canonicalized,
                            self.offset,
                            self.raw.len(),
                        );
                    if self.initials_count != 0
                        && (!accurate || row.text.chars().count() != self.initials_count as usize)
                    {
                        continue;
                    }
                    if row.key.as_ref() != previous {
                        previous = &row.key;
                        matches = !accurate
                            && self.matching_options != 0
                            && crate::fuzzy::complete_annotations(
                                query_spelling(
                                    &self.raw,
                                    &self.canonical,
                                    self.canonicalized,
                                    self.offset,
                                    self.raw.len(),
                                ),
                                &row.pinyin,
                                self.matching_options,
                            )
                            .is_some_and(|marks| marks != [0; 4]);
                    }
                    if (!accurate && !matches)
                        || self.completed.len() + row.text.len() > MAX_TEXT_BYTES
                    {
                        continue;
                    }
                    if !self.profile.reliable(
                        row,
                        !self.profile.has_repeated_evidence(row)
                            && self
                                .dictionary
                                .attests(&row.text, &row.key, self.matching_options),
                    ) {
                        continue;
                    }
                    let lane = usize::from(row.text.chars().nth(1).is_none());
                    let (list, count) = if accurate {
                        (&mut exact[lane], &mut self.exact_count[lane])
                    } else {
                        (&mut recalled[lane], &mut self.recalled_count[lane])
                    };
                    if let Some(at) = list[..*count]
                        .iter()
                        .position(|&(i, _, _)| self.profile.rows[i].text == row.text)
                    {
                        if (row.count, row.sequence) > (list[at].1, list[at].2) {
                            list[at] = (id, row.count, row.sequence);
                            list[..*count].sort_unstable_by_key(|a| std::cmp::Reverse((a.1, a.2)));
                        }
                        continue;
                    }
                    let at =
                        list[..*count].partition_point(|v| (v.1, v.2) >= (row.count, row.sequence));
                    if at < 2 {
                        list.copy_within(at..(*count).min(1), at + 1);
                        list[at] = (id, row.count, row.sequence);
                        *count = (*count + 1).min(2);
                    }
                }
                for lane in 0..2 {
                    for (at, &(id, _, _)) in
                        exact[lane][..self.exact_count[lane]].iter().enumerate()
                    {
                        self.exact_history[lane * 2 + at] = id as u32;
                    }
                    for (at, &(id, _, _)) in recalled[lane][..self.recalled_count[lane]]
                        .iter()
                        .enumerate()
                    {
                        self.recalled_history[lane * 2 + at] = id as u32;
                    }
                }
                self.history_cache.put(
                    query_spelling(
                        &self.raw,
                        &self.canonical,
                        self.canonicalized,
                        self.offset,
                        self.raw.len(),
                    ),
                    self.matching_options,
                    remaining,
                    crate::profile_cache::HistorySelection {
                        exact: self.exact_history,
                        recalled: self.recalled_history,
                        exact_count: self.exact_count.map(|n| n as u8),
                        recalled_count: self.recalled_count.map(|n| n as u8),
                    },
                );
            }
            if self.matching_options == 0 && !self.single_syllable {
                let mut best = [(0usize, 0u32, 0u32); 4];
                let mut len = 0;
                for lane in 0..2 {
                    for &id in &self.exact_history[lane * 2..lane * 2 + self.exact_count[lane]] {
                        let row = &self.profile.rows[id as usize];
                        best[len] = (id as usize, row.count, row.sequence);
                        len += 1;
                    }
                }
                best[..len].sort_unstable_by_key(|a| std::cmp::Reverse((a.1, a.2)));
                for &(id, _, _) in best[..len.min(2)].iter() {
                    self.results.push(ResultRef::new(
                        LEARNED | id as u32,
                        (self.raw.len() - self.offset) as u8,
                        false,
                        false,
                        EXACT_HISTORY,
                    ));
                }
            }
        }
        self.fill(if self.context.is_empty() && self.recent_len == 0 {
            self.page_size + 1
        } else {
            64
        });
        if !self.context.is_empty() || self.recent_len > 0 || self.matching_options != 0 {
            self.rank_first_results();
        }
        self.update_preedit();
    }
    /// Apply preferences only outside an active composition. Profiles are shared
    /// immutable snapshots; disk I/O belongs to the platform adapter.
    pub fn configure(&mut self, page_size: usize, learning: bool, associations: bool) -> bool {
        if !self.preedit.is_empty() || !(1..=MAX_CANDIDATES).contains(&page_size) {
            return false;
        }
        self.clear_composition();
        self.page_size = page_size;
        self.learning_enabled = learning;
        self.associations_enabled = associations;
        if !learning {
            self.recent_len = 0;
            self.phrase.clear();
        }
        true
    }
    pub fn set_profile(&mut self, profile: Arc<crate::Profile>) -> bool {
        if !self.preedit.is_empty() {
            return false;
        }
        self.clear_composition();
        self.profile = profile;
        self.history_cache.invalidate();
        true
    }
    pub fn history_cache_stats(&self) -> crate::HistoryCacheStats {
        self.history_cache.stats()
    }
    pub fn profile(&self) -> Arc<crate::Profile> {
        Arc::clone(&self.profile)
    }
    pub fn learning_key(&self) -> &str {
        &self.learning_key
    }
    /// Call exactly once after a successful host text write. Allocates; deliberately
    /// separate from process(), so rejected edits cannot train the input method.
    /// A composition that was committed in several segments also trains its whole
    /// spelling/phrase pair here, alongside the final segment's own row.
    pub fn learn_commit(&mut self) -> bool {
        let acknowledged = self.learning_enabled && !self.learning_key.is_empty();
        let segment = acknowledged
            && Arc::make_mut(&mut self.profile).record_selection(
                &self.learning_key,
                &self.commit,
                self.matching_options,
            );
        let phrase = acknowledged && self.phrase.learnable();
        let whole = phrase
            && Arc::make_mut(&mut self.profile).record_selection(
                &self.phrase.key,
                &self.phrase.text,
                self.matching_options,
            );
        if segment || whole {
            self.history_cache.invalidate();
        }
        self.learning_key.clear();
        if phrase {
            self.phrase.clear();
        }
        let success = segment || whole;
        if success && self.incremental && !self.raw.is_empty() {
            self.refresh();
        }
        success
    }
    /// Candidate-window display only; original preedit/caret offsets stay intact.
    pub fn display_preedit(&self, output: &mut [u8]) -> usize {
        let input = &self.raw[self.offset..];
        let mut count = self.completed.len();
        if output.len() >= count {
            output[..count].copy_from_slice(self.completed.as_bytes());
        }
        let mut costs = [u16::MAX; MAX_INPUT_BYTES + 1];
        let mut next = [0usize; MAX_INPUT_BYTES + 1];
        costs[input.len()] = 0;
        for at in (0..input.len()).rev() {
            if input.as_bytes()[at] == b'\'' {
                costs[at] = costs[at + 1];
                next[at] = at + 1;
                continue;
            }
            costs[at] = costs[at + 1].saturating_add(100);
            next[at] = at + 1;
            for end in at + 1..=(at + 6).min(input.len()) {
                if crate::syllables::contains(&input[at..end])
                    && costs[end].saturating_add(1) < costs[at]
                {
                    costs[at] = costs[end] + 1;
                    next[at] = end;
                }
            }
            if (input[at..].starts_with("zh")
                || input[at..].starts_with("ch")
                || input[at..].starts_with("sh"))
                && costs[at + 2].saturating_add(50) < costs[at]
            {
                costs[at] = costs[at + 2] + 50;
                next[at] = at + 2;
            }
        }
        let mut at = 0;
        while at < input.len() {
            let end = next[at];
            for b in input[at..end].bytes() {
                if count < output.len() {
                    output[count] = b;
                }
                count += 1;
            }
            if end < input.len() && input.as_bytes()[at] != b'\'' && input.as_bytes()[end] != b'\''
            {
                if count < output.len() {
                    output[count] = b'\'';
                }
                count += 1;
            }
            at = end;
        }
        count
    }
    pub fn is_association(&self) -> bool {
        self.preedit.is_empty() && self.results.first().is_some_and(|r| r.association())
    }
    fn rank_first_results(&mut self) {
        // Calculate expensive context scores once per row, then sort fixed stack
        // keys. Retain the origin class: completions cannot outrank exact parses.
        let context = if self.completed.is_empty() {
            &self.context
        } else {
            &self.completed
        };
        let mut ranked = [(ResultRef::default(), 0.0f32, 0u8, 0u8); 64];
        let size = self.results.len();
        debug_assert!(size <= 64);
        for (i, &r) in self.results.iter().enumerate() {
            let cost = if r.id() & LEARNED != 0 {
                // Injection already orders preferences by frequency, then recency.
                // Preserve it even after a long history makes sequence numbers large.
                -1_000_000.0 + i as f32
            } else if r.sentence() {
                r.id() as f32
                    + if self.matching_options != 0 {
                        1_000_000.0
                    } else {
                        0.0
                    }
            } else if self.matching_options != 0
                && r.consumed() < (self.raw.len() - self.offset) as u8
            {
                2_000_000.0 - (r.consumed() as f32) * 10000.0 + r.id() as f32 * 0.001
            } else if self.matching_options == 0 && r.class() == SEGMENT {
                -(r.consumed() as f32) * 10000.0 + r.id() as f32 * 0.001
            } else {
                let word = self.dictionary.entry(r.id());
                let recent = self.recent[..self.recent_len]
                    .iter()
                    .position(|&id| id == r.id())
                    .map_or(0.0, |i| 3.0 / (1.0 + i as f32 * 0.2));
                self.dictionary.word_cost(r.id())
                    + if self.matching_options != 0
                        && matches!(r.class(), FUZZY_WORD | FUZZY_CHARACTER)
                    {
                        f32::from(
                            crate::fuzzy::penalty(
                                query_spelling(
                                    &self.raw,
                                    &self.canonical,
                                    self.canonicalized,
                                    self.offset,
                                    self.raw.len(),
                                ),
                                word.pinyin,
                                self.matching_options,
                            )
                            .unwrap_or(12),
                        ) * 1000.0
                    } else {
                        0.0
                    }
                    - self.dictionary.context_bonus(context, word.text)
                    - recent
            };
            let single = if self.incremental && r.consumed() < (self.raw.len() - self.offset) as u8
            {
                2
            } else if self.matching_options == 0 && !self.single_syllable {
                0
            } else {
                let text = if r.id() & LEARNED != 0 {
                    self.profile.rows[(r.id() & !LEARNED) as usize]
                        .text
                        .as_ref()
                } else if r.sentence() {
                    self.decoder.sentences[r.id() as usize].text()
                } else {
                    self.dictionary.entry(r.id()).text
                };
                let single = text.chars().nth(1).is_none();
                u8::from(if self.single_syllable {
                    !single
                } else {
                    single
                })
            };
            // A composition the lexicon attests for these very keys (`你好` from
            // `ni`+`hao`) is lexical evidence, so it keeps its leading rank. An
            // ad-hoc join spelling nothing (`清河里`) — or spelling a word under a
            // reading the user did not type (`落后` from `laohou`) — has no such
            // evidence and must not outrank a word or character the lexicon
            // attests here (review gap 3). Sorted ahead of the origin class below.
            let unsupported_join = u8::from(
                r.sentence()
                    && self.matching_options != 0
                    && !self.dictionary.attests(
                        self.decoder.sentences[r.id() as usize].text(),
                        query_spelling(
                            &self.raw,
                            &self.canonical,
                            self.canonicalized,
                            self.offset,
                            self.raw.len(),
                        ),
                        self.matching_options,
                    ),
            );
            ranked[i] = (r, cost, single, unsupported_join);
        }
        ranked[..size].sort_unstable_by(|a, b| {
            a.2.cmp(&b.2)
                .then(a.3.cmp(&b.3))
                .then(a.0.class().cmp(&b.0.class()))
                .then(a.1.total_cmp(&b.1))
                .then(a.0.id().cmp(&b.0.id()))
        });
        for (target, &(item, _, _, _)) in self.results.iter_mut().zip(&ranked[..size]) {
            *target = item;
        }
    }
    fn remember_commit(&mut self, chosen: Option<u32>) -> bool {
        // Raw/ASCII commits and punctuation cannot supply a Chinese context.
        if self.commit.is_empty()
            || self
                .commit
                .chars()
                .any(|c| !matches!(c as u32,0x3400..=0x9fff|0x20000..=0x3134f))
        {
            self.context.clear();
            return false;
        }
        let mut start = self.commit.len().saturating_sub(63);
        while !self.commit.is_char_boundary(start) {
            start += 1;
        }
        self.context.clear();
        self.context.push_str(&self.commit[start..]);
        if let Some(id) = chosen.filter(|_| self.learning_enabled) {
            let at = self.recent[..self.recent_len]
                .iter()
                .position(|&v| v == id)
                .unwrap_or(self.recent_len.min(31));
            self.recent.copy_within(0..at, 1);
            self.recent[0] = id;
            self.recent_len = (self.recent_len + 1).min(32);
        }
        true
    }
    fn after_commit(&mut self, chosen: Option<u32>) {
        if self.learning_enabled && crate::profile::chinese(&self.commit) && !self.raw.is_empty() {
            self.learning_key.clear();
            self.learning_key.push_str(
                query_spelling(
                    &self.raw,
                    &self.canonical,
                    self.canonicalized,
                    0,
                    self.raw.len(),
                )
                .trim_end_matches('\''),
            );
        }
        self.clear_composition();
        if !self.remember_commit(chosen) {
            return;
        }
        if !self.associations_enabled {
            return;
        }
        for (id, link) in crate::language::links().iter().enumerate() {
            if self.context.ends_with(link.context) {
                self.results
                    .push(ResultRef::new(id as u32, 0, true, true, 0));
            }
        }
        let dictionary = &self.dictionary;
        let results = &mut self.results;
        fn text(d: &Dictionary, r: ResultRef) -> &str {
            if r.sentence() {
                crate::language::links()[r.id() as usize].next
            } else {
                &d.entry(r.id()).text[r.consumed() as usize..]
            }
        }
        fn priority(
            d: &Dictionary,
            r: ResultRef,
        ) -> (std::cmp::Reverse<usize>, std::cmp::Reverse<u32>, u32) {
            if r.sentence() {
                let l = &crate::language::links()[r.id() as usize];
                (
                    std::cmp::Reverse(l.context.len()),
                    std::cmp::Reverse(l.weight.saturating_mul(100)),
                    r.id(),
                )
            } else {
                (
                    std::cmp::Reverse(r.consumed() as usize),
                    std::cmp::Reverse(d.entry(r.id()).frequency),
                    r.id(),
                )
            }
        }
        results.sort_unstable_by_key(|&r| priority(dictionary, r));
        for (start, _) in self.context.char_indices().rev().take(4) {
            self.budget_limited |= dictionary.continuations(&self.context[start..], |id, skip| {
                let item = ResultRef::new(id, skip, false, true, 0);
                let candidate = text(dictionary, item);
                if let Some(at) = results
                    .iter()
                    .position(|&r| text(dictionary, r) == candidate)
                {
                    if priority(dictionary, results[at]) <= priority(dictionary, item) {
                        return;
                    }
                    results.remove(at);
                }
                let at = results
                    .partition_point(|&r| priority(dictionary, r) <= priority(dictionary, item));
                if at < 128 {
                    if results.len() == 128 {
                        results.pop();
                    }
                    results.insert(at, item);
                }
            });
        }
    }
    /// Record one segment of the composition in progress. The segment that closes
    /// the whole input also makes the accumulated pair learnable, so the host's
    /// acknowledgement can train the whole spelling/phrase pair next to the
    /// per-segment rows.
    ///
    /// Only the incremental adapter needs this: the staged adapter keeps the
    /// complete spelling in `raw` until its single final commit, so its
    /// `learning_key` is already the whole phrase. A segment outside the typed
    /// remainder, a non-Chinese one, or a pair past the profile's limits drops the
    /// whole phrase rather than learning a mismatched pair.
    fn record_phrase_segment(&mut self, item: ResultRef) {
        if !self.incremental || !self.learning_enabled || item.association() {
            return;
        }
        let text = if item.id() & LEARNED != 0 {
            self.profile.rows[(item.id() & !LEARNED) as usize]
                .text
                .as_ref()
        } else if item.sentence() {
            self.decoder.sentences[item.id() as usize].text()
        } else {
            self.dictionary.entry(item.id()).text
        };
        let end = self.offset + item.consumed() as usize;
        if end > self.raw.len() {
            return;
        }
        // `end` is absolute, so the span runs from the composition offset to it.
        // The learned key must be the spelling the query matched, which for a `ü`
        // variant is the canonical one; the displayed and committed text above is
        // untouched and stays exactly what the user typed.
        let key = query_spelling(
            &self.raw,
            &self.canonical,
            self.canonicalized,
            self.offset,
            end,
        )
        .trim_end_matches('\'');
        self.phrase.push(key, text);
        if end == self.raw.len() {
            self.phrase.finish();
        }
    }
    fn choose(&mut self, index: usize, force: bool) {
        if index >= self.candidate_count() {
            self.commit.push_str(&self.preedit);
            self.clear_composition();
            return;
        }
        let item = self.results[self.page * self.page_size + index];
        if item.association() {
            if item.sentence() {
                self.commit
                    .push_str(crate::language::links()[item.id() as usize].next);
            } else {
                self.commit
                    .push_str(&self.dictionary.entry(item.id()).text[item.consumed() as usize..]);
            }
            self.after_commit(None);
            return;
        }
        if item.id() & LEARNED != 0 {
            self.commit.push_str(&self.completed);
            self.commit
                .push_str(&self.profile.rows[(item.id() & !LEARNED) as usize].text);
            self.record_phrase_segment(item);
            self.after_commit(None);
            return;
        }
        if !force && (item.consumed() as usize) < self.raw.len() - self.offset {
            if self.incremental {
                let consumed = item.consumed() as usize;
                self.commit.push_str(self.dictionary.entry(item.id()).text);
                self.record_phrase_segment(item);
                if self.learning_enabled {
                    self.learning_key.push_str(
                        query_spelling(&self.raw, &self.canonical, self.canonicalized, 0, consumed)
                            .trim_end_matches('\''),
                    );
                }
                self.remember_commit(Some(item.id()));
                self.raw.drain(..consumed);
                self.offset = 0;
                self.caret = self.raw.len();
                self.refresh();
                return;
            }
            self.boundaries[self.boundary_count] = Boundary {
                raw: self.offset as u8,
                text: self.completed.len() as u16,
            };
            self.boundary_count += 1;
            if item.sentence() {
                self.completed
                    .push_str(self.decoder.sentences[item.id() as usize].text());
            } else {
                self.completed
                    .push_str(self.dictionary.entry(item.id()).text);
            }
            self.offset += item.consumed() as usize;
            self.caret = self.raw.len();
            self.refresh();
            return;
        }
        self.commit.push_str(&self.completed);
        if item.sentence() {
            self.commit
                .push_str(self.decoder.sentences[item.id() as usize].text());
        } else {
            self.commit.push_str(self.dictionary.entry(item.id()).text);
        }
        self.commit
            .push_str(&self.raw[self.offset + item.consumed() as usize..]);
        self.record_phrase_segment(item);
        self.after_commit((!item.sentence()).then_some(item.id()));
    }
    fn edit(&mut self, key: Key) -> bool {
        let (mut bytes, len) = {
            let mut bytes = [0; MAX_INPUT_BYTES + 1];
            bytes[..self.raw.len()].copy_from_slice(self.raw.as_bytes());
            (bytes, self.raw.len())
        };
        let mut new_len = len;
        let mut caret = self.caret;
        match key {
            Key::Character(c) => {
                if len == MAX_INPUT_BYTES {
                    return false;
                }
                bytes.copy_within(caret..len, caret + 1);
                bytes[caret] = c.to_ascii_lowercase() as u8;
                caret += 1;
                new_len += 1;
            }
            Key::Backspace => {
                if caret == self.offset {
                    return true;
                }
                bytes.copy_within(caret..len, caret - 1);
                caret -= 1;
                new_len -= 1;
            }
            Key::Delete => {
                if caret == len {
                    return true;
                }
                bytes.copy_within(caret + 1..len, caret);
                new_len -= 1;
            }
            _ => return true,
        }
        let proposed = std::str::from_utf8(&bytes[..new_len]).expect("ASCII input");
        if !valid_input(proposed)
            || self.completed.len() + new_len - self.offset > MAX_TEXT_BYTES
            || self.dictionary.lookup(&proposed[self.offset..]).is_err()
        {
            return false;
        }
        self.raw.clear();
        self.raw.push_str(proposed);
        self.caret = caret;
        self.refresh();
        true
    }
    pub fn process(&mut self, key: Key, modifiers: Modifiers) -> ProcessResult {
        self.commit.clear();
        self.learning_key.clear();
        // A pair is only ever consumed by the `learn_commit` that follows the
        // write it describes, so any buffer still held once no composition is open
        // belongs to a composition the host abandoned. Segment accumulation itself
        // survives here: mid-composition the preedit is non-empty.
        if self.preedit.is_empty() {
            self.phrase.clear();
        }
        if modifiers.control || modifiers.alt || modifiers.super_key {
            if self.preedit.is_empty() {
                self.clear_composition();
                self.context.clear();
            }
            return ProcessResult::default();
        }
        if self.is_association() {
            match key {
                Key::Select(i) => {
                    if i < self.candidate_count() {
                        self.choose(i, false);
                    }
                    return ProcessResult {
                        handled: true,
                        limited: false,
                    };
                }
                Key::Tab => {
                    self.choose(self.selected, false);
                    return ProcessResult {
                        handled: true,
                        limited: false,
                    };
                }
                Key::Escape => {
                    self.clear_composition();
                    self.context.clear();
                    return ProcessResult {
                        handled: true,
                        limited: false,
                    };
                }
                Key::Up | Key::Down => {
                    let n = self.candidate_count();
                    self.selected = if matches!(key, Key::Up) {
                        (self.selected + n - 1) % n
                    } else {
                        (self.selected + 1) % n
                    };
                    return ProcessResult {
                        handled: true,
                        limited: false,
                    };
                }
                Key::PageUp | Key::PageDown => {
                    let max = self.results.len().saturating_sub(1) / self.page_size;
                    if matches!(key, Key::PageDown) {
                        self.page = (self.page + 1).min(max);
                    } else {
                        self.page = self.page.saturating_sub(1);
                    }
                    self.selected = 0;
                    return ProcessResult {
                        handled: true,
                        limited: false,
                    };
                }
                _ => {
                    self.clear_composition();
                    if !matches!(key,Key::Character(c) if c.is_ascii_alphabetic()) {
                        self.context.clear();
                    }
                }
            }
        }
        let active = !self.preedit.is_empty();
        let mut limited = false;
        match key {
            Key::Character(c) if c.is_ascii_alphabetic() || c == '\'' => {
                if c == '\''
                    && (self.caret == self.offset
                        || self.raw.as_bytes().get(self.caret.wrapping_sub(1)) == Some(&b'\''))
                {
                    return ProcessResult {
                        handled: active,
                        limited: false,
                    };
                }
                limited = !self.edit(key);
            }
            Key::Backspace if active => {
                if self.caret == self.offset && self.boundary_count > 0 {
                    let boundary = self.boundaries[self.boundary_count - 1];
                    if boundary.text as usize + self.raw.len() - boundary.raw as usize
                        > MAX_TEXT_BYTES
                        || self
                            .dictionary
                            .lookup(query_spelling(
                                &self.raw,
                                &self.canonical,
                                self.canonicalized,
                                boundary.raw as usize,
                                self.raw.len(),
                            ))
                            .is_err()
                    {
                        limited = true;
                    } else {
                        self.boundary_count -= 1;
                        self.offset = boundary.raw as usize;
                        self.completed.truncate(boundary.text as usize);
                        self.refresh();
                    }
                } else {
                    limited = !self.edit(key);
                }
            }
            Key::Delete if active => limited = !self.edit(key),
            Key::Left if active => self.caret = self.caret.saturating_sub(1).max(self.offset),
            Key::Right if active => self.caret = (self.caret + 1).min(self.raw.len()),
            Key::Home if active => self.caret = self.offset,
            Key::End if active => self.caret = self.raw.len(),
            Key::Escape if active => self.clear_composition(),
            Key::Enter if active => {
                self.commit.push_str(&self.preedit);
                self.clear_composition();
                self.context.clear();
            }
            Key::Space if active => self.choose(self.selected, false),
            Key::Select(index) if active => {
                if index < self.candidate_count() {
                    self.choose(index, false);
                }
            }
            Key::Character(c @ '1'..='9') if active => {
                let index = c as usize - '1' as usize;
                if index < self.candidate_count() {
                    self.choose(index, false);
                }
            }
            Key::PageUp if active => {
                if self.page > 0 {
                    self.page -= 1;
                    self.selected = 0;
                }
            }
            Key::PageDown if active => {
                if self.has_next_page() {
                    self.page += 1;
                    self.fill((self.page + 1) * self.page_size + 1);
                    self.selected = 0;
                }
                limited = self.budget_limited;
            }
            Key::Up | Key::Down if active && self.candidate_count() > 0 => {
                let count = self.candidate_count();
                self.selected = if matches!(key, Key::Up) {
                    (self.selected + count - 1) % count
                } else {
                    (self.selected + 1) % count
                };
            }
            Key::Character(c) if active && (c.is_ascii_punctuation() || c == '0') => {
                self.choose(self.selected, true);
                self.clear_composition();
                self.context.clear();
                return ProcessResult::default();
            }
            _ => {
                if !active {
                    self.context.clear();
                }
                return ProcessResult::default();
            }
        }
        ProcessResult {
            handled: true,
            limited,
        }
    }
}
