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
    sentence_index: usize,
    segment_index: usize,
    phase: u8,
    exhausted: bool,
    budget_limited: bool,
    context: String,
    recent: [u32; 32],
    recent_len: usize,
    profile: Arc<crate::Profile>,
    learning_key: String,
    page_size: usize,
    learning_enabled: bool,
    associations_enabled: bool,
    matching_options: u32,
    exact_sentence_index: usize,
    exact_history: [u32; 4],
    exact_count: [usize; 2],
    recalled_history: [u32; 4],
    recalled_count: [usize; 2],
    recalled_index: usize,
    word_line: bool,
    character_line: bool,
}
impl Session {
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
            sentence_index: 0,
            segment_index: 0,
            phase: 0,
            exhausted: true,
            budget_limited: false,
            context: String::with_capacity(64),
            recent: [u32::MAX; 32],
            recent_len: 0,
            profile: Arc::new(crate::Profile::default()),
            learning_key: String::with_capacity(MAX_INPUT_BYTES),
            page_size: MAX_CANDIDATES,
            learning_enabled: true,
            associations_enabled: true,
            matching_options: 0,
            exact_sentence_index: 0,
            exact_history: [0; 4],
            exact_count: [0; 2],
            recalled_history: [0; 4],
            recalled_count: [0; 2],
            recalled_index: 0,
            word_line: false,
            character_line: false,
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
                        &self.raw[self.offset..],
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
                    &self.raw[self.offset..]
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
    pub fn candidate_marks(&self, index: usize) -> Option<[u64; 4]> {
        let candidate = self.candidate(index)?;
        let item = self.results[self.page * self.page_size + index];
        if item.association()
            || self.matching_options == 0
            || (item.sentence() && !self.decoder.sentences[item.id() as usize].corrected)
        {
            return Some([0; 4]);
        }
        Some(
            crate::fuzzy::annotations(
                self.raw[self.offset..self.offset + item.consumed() as usize]
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
            &self.raw[self.offset..],
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
    }
    pub fn reset(&mut self) {
        self.clear_composition();
        self.commit.clear();
        self.learning_key.clear();
        self.context.clear();
        self.recent_len = 0;
        self.decoder.clear_cache();
    }
    fn next_result(&mut self) -> Option<ResultRef> {
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
                            let input = &self.raw[self.offset..];
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
                                &self.raw[self.offset..],
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
                            .reset_fast(&self.dictionary, &self.raw[self.offset..])
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
                            .reset_fast(&self.dictionary, &self.raw[self.offset..])
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
                                &self.raw[self.offset..],
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
                        .reset(&self.dictionary, &self.raw[self.offset..])
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
        let input = &self.raw[self.offset..];
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
                        &self.raw[self.offset..],
                        self.matching_options,
                    );
                    self.phase = 4;
                }
                4 => {
                    if let Some(r) = self.next_whole_word(false, true, FUZZY_WORD) {
                        self.word_line = true;
                        return Some(r);
                    }
                    self.phase = 5;
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
                        .reset(&self.dictionary, &self.raw[self.offset..])
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
                        &self.raw[self.offset..],
                        self.matching_options,
                    );
                    self.phase = 9;
                }
                9 => {
                    if let Some(r) = self.next_whole_word(true, true, FUZZY_CHARACTER) {
                        self.character_line = true;
                        return Some(r);
                    }
                    self.budget_limited |= self
                        .cursor
                        .reset_fast(&self.dictionary, &self.raw[self.offset..])
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
                                &self.raw[self.offset..],
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
                        .reset(&self.dictionary, &self.raw[self.offset..])
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
                    || (self.matching_options != 0 && old.consumed() > item.consumed()))
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
        self.cursor
            .reset(&self.dictionary, &self.raw[self.offset..])
            .expect("validated input");
        self.lexical_matches = self.cursor.has_matches();
        match self.decoder.decode(
            &self.dictionary,
            &self.raw[self.offset..],
            if self.completed.is_empty() {
                &self.context
            } else {
                &self.completed
            },
            self.matching_options,
        ) {
            Err(_) => {
                // Full-input trie validation already passed; an unusually ambiguous
                // suffix only limits sentence suggestions, without corrupting input.
                self.decoder.count = 0;
                self.decoder.segment_count = 0;
                self.budget_limited = true;
            }
            Ok(limited) => self.budget_limited = limited,
        }
        self.results.clear();
        self.page = 0;
        self.selected = 0;
        self.phase = 0;
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
            let mut exact = [[(0usize, 0u32, 0u32); 2]; 2];
            let mut recalled = exact;
            let mut previous = "";
            let mut matches = false;
            let range = if self.matching_options == 0 {
                self.profile.matching_range(&self.raw[self.offset..])
            } else {
                0..self.profile.rows.len()
            };
            for id in range {
                let row = &self.profile.rows[id];
                let accurate = row.key.as_ref() == &self.raw[self.offset..];
                if row.key.as_ref() != previous {
                    previous = &row.key;
                    matches = !accurate
                        && self.matching_options != 0
                        && crate::fuzzy::complete_annotations(
                            &self.raw[self.offset..],
                            &row.pinyin,
                            self.matching_options,
                        )
                        .is_some_and(|marks| marks != [0; 4]);
                }
                if (!accurate && !matches) || self.completed.len() + row.text.len() > MAX_TEXT_BYTES
                {
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
                for (at, &(id, _, _)) in exact[lane][..self.exact_count[lane]].iter().enumerate() {
                    self.exact_history[lane * 2 + at] = id as u32;
                }
                for (at, &(id, _, _)) in recalled[lane][..self.recalled_count[lane]]
                    .iter()
                    .enumerate()
                {
                    self.recalled_history[lane * 2 + at] = id as u32;
                }
            }
            if self.matching_options == 0 {
                let mut best = [(0usize, 0u32, 0u32); 4];
                let mut len = 0;
                for lane in 0..2 {
                    for &entry in &exact[lane][..self.exact_count[lane]] {
                        best[len] = entry;
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
        }
        true
    }
    pub fn set_profile(&mut self, profile: Arc<crate::Profile>) -> bool {
        if !self.preedit.is_empty() {
            return false;
        }
        self.clear_composition();
        self.profile = profile;
        true
    }
    pub fn profile(&self) -> Arc<crate::Profile> {
        Arc::clone(&self.profile)
    }
    pub fn learning_key(&self) -> &str {
        &self.learning_key
    }
    /// Call exactly once after a successful host text write. Allocates; deliberately
    /// separate from process(), so rejected edits cannot train the input method.
    pub fn learn_commit(&mut self) -> bool {
        let success = self.learning_enabled
            && !self.learning_key.is_empty()
            && Arc::make_mut(&mut self.profile).record(&self.learning_key, &self.commit);
        self.learning_key.clear();
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
        let mut ranked = [(ResultRef::default(), 0.0f32, 0u8); 64];
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
                                &self.raw[self.offset..],
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
            let single = if self.matching_options == 0 {
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
                u8::from(text.chars().nth(1).is_none())
            };
            ranked[i] = (r, cost, single);
        }
        ranked[..size].sort_unstable_by(|a, b| {
            a.2.cmp(&b.2)
                .then(a.0.class().cmp(&b.0.class()))
                .then(a.1.total_cmp(&b.1))
                .then(a.0.id().cmp(&b.0.id()))
        });
        for (target, &(item, _, _)) in self.results.iter_mut().zip(&ranked[..size]) {
            *target = item;
        }
    }
    fn after_commit(&mut self, chosen: Option<u32>) {
        if self.learning_enabled && crate::profile::chinese(&self.commit) && !self.raw.is_empty() {
            self.learning_key.clear();
            self.learning_key.push_str(&self.raw);
        }
        self.clear_composition();
        // Raw/ASCII commits and punctuation cannot supply a Chinese context.
        if self.commit.is_empty()
            || self
                .commit
                .chars()
                .any(|c| !matches!(c as u32,0x3400..=0x9fff|0x20000..=0x3134f))
        {
            self.context.clear();
            return;
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
            self.after_commit(None);
            return;
        }
        if !force && (item.consumed() as usize) < self.raw.len() - self.offset {
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
                            .lookup(&self.raw[boundary.raw as usize..])
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
