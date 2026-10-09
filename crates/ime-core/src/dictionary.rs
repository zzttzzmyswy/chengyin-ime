// SPDX-License-Identifier: GPL-3.0-or-later
use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::fmt;

/// Exact-key set behind [`BoundaryIndex`]. Its keys are the collision-free
/// 126-bit encodings built below from dictionary text, not caller-supplied
/// input, so SipHash's hash-flooding resistance buys nothing here — while its
/// cost is paid on every probe. A deterministic multiplicative mix keeps the
/// same membership answers and the same build-to-build behaviour.
#[derive(Default)]
struct BoundaryHasher(u64);
impl std::hash::Hasher for BoundaryHasher {
    fn finish(&self) -> u64 {
        self.0
    }
    fn write(&mut self, bytes: &[u8]) {
        for &byte in bytes {
            self.write_u64(u64::from(byte));
        }
    }
    fn write_u64(&mut self, value: u64) {
        self.0 = (self.0 ^ value).wrapping_mul(0x9e37_79b9_7f4a_7c15);
        self.0 ^= self.0 >> 29;
    }
    fn write_u128(&mut self, value: u128) {
        // Both halves carry distinct text, so fold them before mixing; dropping
        // the high half would alias six-scalar encodings onto shorter ones.
        self.write_u64(value as u64);
        self.write_u64((value >> 64) as u64);
    }
}
type BoundarySet = HashSet<u128, std::hash::BuildHasherDefault<BoundaryHasher>>;

/// Bits in the [`BoundaryIndex`] prefilter. 64 KiB: small enough to stay
/// cache-resident, large enough that the ~184k dictionary words collide rarely.
const BOUNDARY_FILTER_BITS: usize = 1 << 19;

/// Boundary-word lookup: the exact set of two-to-six-scalar word encodings,
/// fronted by a bitset prefilter over the same keys.
///
/// `attests_boundary` probes up to nine encodings per call, and `Decoder`'s
/// profile shows those probes dominating the long-input tail: each one lands on
/// a random slot of a multi-megabyte `HashSet`, so the cost is cache misses, not
/// comparisons. The prefilter answers the common case — the encoding is simply
/// not a word — from one line of a 64 KiB bitset.
///
/// This cannot change any answer: building the filter sets a bit for every member,
/// so a clear bit proves absence and a set bit still consults the exact set.
/// False positives cost one extra exact lookup; there are no false negatives.
#[derive(Debug)]
struct BoundaryIndex {
    filter: Box<[u64]>,
    exact: BoundarySet,
}
impl BoundaryIndex {
    /// Top bits of a multiplicative mix: the encodings differ only in their low
    /// bits (each scalar contributes 21, so six scalars fill 126 bits), and using
    /// the low bits directly would cluster every short word at the bottom.
    fn bit(code: u128) -> (usize, u64) {
        let mixed = ((code as u64) ^ ((code >> 64) as u64)).wrapping_mul(0x9e37_79b9_7f4a_7c15);
        let at = (mixed >> (64 - 19)) as usize;
        (at >> 6, 1 << (at & 63))
    }
    fn build(codes: impl Iterator<Item = u128>) -> Self {
        let mut filter = vec![0u64; BOUNDARY_FILTER_BITS / 64].into_boxed_slice();
        let mut exact = BoundarySet::default();
        for code in codes {
            let (word, mask) = Self::bit(code);
            filter[word] |= mask;
            exact.insert(code);
        }
        Self { filter, exact }
    }
    fn contains(&self, code: u128) -> bool {
        let (word, mask) = Self::bit(code);
        self.filter[word] & mask != 0 && self.exact.contains(&code)
    }
    fn heap_bytes(&self) -> usize {
        self.filter.len() * 8 + (self.exact.capacity() * 8 / 7 + 1) * 17
    }
}
impl Default for BoundaryIndex {
    fn default() -> Self {
        Self::build(std::iter::empty())
    }
}

pub const MAX_INPUT_BYTES: usize = 63;
pub const MAX_PINYIN_BYTES: usize = 255;
pub const MAX_TEXT_BYTES: usize = 256;
pub const MAX_CANDIDATES: usize = 9;
pub const MAX_ACTIVE_STATES: usize = 256;
pub const MAX_DICTIONARY_BYTES: usize = 64 * 1024 * 1024;
const MAX_ENTRIES: usize = 250_000;
const FRONTIER: usize = 1024;
/// Bound on the distinct one-letter keys derived from a single word. A word whose
/// unseparated spelling admits more same-cardinality parses than this keeps the
/// first `MAX_INITIAL_KEYS` and is counted in `Dictionary::initials_limited`, so
/// the truncation is reported rather than silently losing recall (review R12).
const MAX_INITIAL_KEYS: usize = 8;
const TERMINAL: u32 = 1 << 31;
const ABBREVIATED: u32 = 1 << 31;
const NONE: u32 = u32::MAX;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DictionaryError {
    pub line: usize,
    pub message: &'static str,
}
impl fmt::Display for DictionaryError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "dictionary line {}: {}", self.line, self.message)
    }
}
impl std::error::Error for DictionaryError {}
fn error(message: &'static str) -> DictionaryError {
    DictionaryError { line: 0, message }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LookupError {
    InvalidInput,
    TooAmbiguous,
}

#[derive(Debug, Clone, Copy)]
struct Entry {
    pinyin: u32,
    text: u32,
    frequency: u32,
    cost: f32,
    pinyin_len: u16,
    text_len: u16,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Candidate<'a> {
    pub pinyin: &'a str,
    pub text: &'a str,
    pub frequency: u32,
}
#[derive(Debug, Default)]
struct BuildNode {
    edges: BTreeMap<u8, usize>,
    terminals: Vec<u32>,
}
#[derive(Debug, Clone, Copy)]
struct Node {
    edge_start: u32,
    term_start: u32,
    best: u32,
    edge_len: u16,
    term_len: u16,
    shortcut_start: u32,
    shortcut_len: u16,
}
#[derive(Debug, Clone, Copy)]
struct Edge {
    label: u8,
    target: u32,
}
#[derive(Debug)]
struct InitialGroup {
    key_start: u32,
    word_start: u32,
    word_len: u32,
    key_len: u8,
}
#[derive(Debug, Clone, Copy, Default)]
pub struct Candidates {
    ids: [u32; MAX_CANDIDATES],
    len: usize,
}
impl Candidates {
    pub(crate) fn id(&self, index: usize) -> u32 {
        self.ids[index]
    }
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    pub fn get<'a>(&self, dictionary: &'a Dictionary, index: usize) -> Option<Candidate<'a>> {
        (index < self.len).then(|| dictionary.entry(self.ids[index]))
    }
}

/// Entries are ordered by frequency/text/pronunciation. Rank is thus a compact ID.
/// UTF-8 strings live in one pool; terminals retain ALL homophones, not just nine.
#[derive(Debug)]
pub struct Dictionary {
    entries: Vec<Entry>,
    nodes: Vec<Node>,
    edges: Vec<Edge>,
    terminals: Vec<u32>,
    pool: String,
    total_frequency: f64,
    shortcuts: Vec<u32>,
    text_index: Vec<u32>,
    boundary_words: BoundaryIndex,
    max_letters: usize,
    initials_keys: String,
    initials_groups: Vec<InitialGroup>,
    initials_words: Vec<u32>,
    /// Words whose initial keys had to be truncated by [`MAX_INITIAL_KEYS`]. These
    /// stay reachable through the normal trie/full-pinyin paths; only the strict
    /// one-letter lane is incomplete for them, and this count is the marker that
    /// keeps that loss reportable instead of silent (review R12).
    initials_limited: usize,
}
impl Dictionary {
    /// Every distinct one-letter-per-character key a word can be reached by.
    ///
    /// A spelling with explicit apostrophes has exactly one reading. An
    /// unseparated spelling can have several *same-cardinality* readings whose
    /// first letters differ (`xiangang` -> both `xa` and `xg`), so indexing only
    /// one of them silently drops the word from the other query. Every reading is
    /// indexed here, bounded by [`MAX_INITIAL_KEYS`]; the return value counts how
    /// many words had to be truncated, which is reported instead of hidden.
    ///
    /// Dictionary-initialization only, bounded by the 255-byte canonical spelling
    /// limit rather than the 63-byte input limit.
    fn word_initial_keys(spelling: &str, count: usize, keys: &mut Vec<String>) -> bool {
        keys.clear();
        let bytes = spelling.as_bytes();
        let separated: Vec<&str> = spelling.split('\'').collect();
        if separated.len() == count && separated.iter().all(|p| crate::syllables::contains(p)) {
            let mut key = String::with_capacity(count);
            for part in &separated {
                key.push(char::from(part.as_bytes()[0]));
            }
            keys.push(key);
            return false;
        }
        // Bounded reachability: reachable[r][at] holds when the suffix from `at`
        // parses into exactly `r` syllables. This replaces the former single-path
        // table, which committed to one reading and lost the others.
        let stride = bytes.len() + 1;
        let mut reachable = vec![false; (count + 1) * stride];
        reachable[bytes.len()] = true;
        for remaining in 1..=count {
            for at in (0..bytes.len()).rev() {
                if bytes[at] == b'\'' {
                    continue;
                }
                for end in (at + 1..=(at + 6).min(bytes.len())).rev() {
                    let next = end + usize::from(bytes.get(end) == Some(&b'\''));
                    if reachable[(remaining - 1) * stride + next]
                        && crate::syllables::contains(&spelling[at..end])
                    {
                        reachable[remaining * stride + at] = true;
                        break;
                    }
                }
            }
        }
        if !reachable[count * stride] {
            return false;
        }
        // Depth-first over reachable readings only, so dead ends cost nothing.
        // The visited-state budget keeps the walk linear in the spelling even for a
        // pathological number of readings; spending it is itself a reported
        // truncation rather than a silent one (review R12).
        let mut limited = false;
        let mut budget = MAX_INITIAL_KEYS * 64;
        let mut stack = vec![(0usize, count, String::new())];
        while let Some((at, remaining, key)) = stack.pop() {
            if keys.len() == MAX_INITIAL_KEYS || budget == 0 {
                // The stack may still hold further readings; that is the truncation
                // the caller has to count and report (review R12).
                limited = true;
                break;
            }
            budget -= 1;
            if remaining == 0 {
                if at == bytes.len() && !keys.contains(&key) {
                    keys.push(key);
                }
                continue;
            }
            if bytes.get(at) == Some(&b'\'') {
                continue;
            }
            for end in (at + 1..=(at + 6).min(bytes.len())).rev() {
                let next = end + usize::from(bytes.get(end) == Some(&b'\''));
                if !reachable[(remaining - 1) * stride + next]
                    || !crate::syllables::contains(&spelling[at..end])
                {
                    continue;
                }
                let mut child = key.clone();
                child.push(char::from(bytes[at]));
                stack.push((next, remaining - 1, child));
            }
        }
        limited
    }
    // Immutable, complete terminal groups for one-letter-per-character queries.
    // Unlike the bounded ambiguous trie search, this cannot discard rare words
    // before the session sees them. The serialized dictionary remains unchanged.
    fn build_initials(&mut self) {
        let mut groups: BTreeMap<String, Vec<u32>> = BTreeMap::new();
        let mut keys = Vec::new();
        for id in 0..self.entries.len() as u32 {
            let word = self.entry(id);
            let count = word.text.chars().count();
            if !(2..=MAX_INPUT_BYTES).contains(&count) {
                continue;
            }
            if Self::word_initial_keys(word.pinyin, count, &mut keys) {
                self.initials_limited += 1;
            }
            for key in &keys {
                groups.entry(key.clone()).or_default().push(id);
            }
        }
        self.initials_keys
            .reserve_exact(groups.keys().map(String::len).sum());
        self.initials_groups.reserve_exact(groups.len());
        self.initials_words
            .reserve_exact(groups.values().map(Vec::len).sum());
        for (key, words) in groups {
            self.initials_groups.push(InitialGroup {
                key_start: self.initials_keys.len() as u32,
                key_len: key.len() as u8,
                word_start: self.initials_words.len() as u32,
                word_len: words.len() as u32,
            });
            self.initials_keys.push_str(&key);
            self.initials_words.extend(words);
        }
    }
    pub(crate) fn initials_range(&self, input: &str) -> Option<(u32, u32, u8)> {
        // Explicit separators preserve a digraph syllable such as zh'r'm.
        // Only separated single heads (s's'd'd) qualify for this strict lane.
        let separated = input.trim_end_matches('\'');
        if separated.contains('\'') && separated.split('\'').any(|p| p.len() != 1) {
            return None;
        }
        let syllables = crate::syllables::count_spelling(input);
        if !input
            .bytes()
            .all(|b| b == b'\'' || b"abcdefghjklmnopqrstwxyz".contains(&b))
            || syllables == Some(1)
            // Preserve complete full pinyin; a/e/o are otherwise valid heads
            // for zero-onset syllables (for example ai'qing'dian'ying -> aqdy).
            || (input.bytes().any(|b| b"aeo".contains(&b)) && syllables.is_some())
        {
            return None;
        }
        let index = self
            .initials_groups
            .binary_search_by(|group| {
                let start = group.key_start as usize;
                self.initials_keys.as_bytes()[start..start + group.key_len as usize]
                    .iter()
                    .copied()
                    .cmp(input.bytes().filter(|&b| b != b'\''))
            })
            .ok()?;
        let group = &self.initials_groups[index];
        Some((
            group.word_start,
            group.word_start + group.word_len,
            group.key_len,
        ))
    }
    pub(crate) fn initials_word(&self, index: u32) -> u32 {
        self.initials_words[index as usize]
    }
    pub fn from_tsv(source: &str) -> Result<Self, DictionaryError> {
        if source.len() > MAX_DICTIONARY_BYTES {
            return Err(error("file exceeds 64 MiB"));
        }
        let mut rows = Vec::new();
        let mut seen = BTreeSet::new();
        for (index, line) in source.lines().enumerate() {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let fail = |message| DictionaryError {
                line: index + 1,
                message,
            };
            let fields: Vec<_> = line.split('\t').collect();
            if fields.len() != 3 {
                return Err(fail("expected three tab-separated fields"));
            }
            let pinyin = fields[0];
            let text = fields[1];
            if !valid_pinyin(pinyin)
                || pinyin.is_empty()
                || pinyin.ends_with('\'')
                || pinyin.bytes().any(|b| b.is_ascii_uppercase())
            {
                return Err(fail("invalid canonical pinyin"));
            }
            if text.is_empty() || text.len() > MAX_TEXT_BYTES || text.chars().any(char::is_control)
            {
                return Err(fail(
                    "text must be 1..256 UTF-8 bytes without control characters",
                ));
            }
            let frequency = fields[2]
                .parse::<u32>()
                .ok()
                .filter(|&f| f > 0)
                .ok_or_else(|| fail("frequency must be a positive u32"))?;
            if !seen.insert((pinyin, text)) {
                return Err(fail("duplicate pinyin/text pair"));
            }
            if rows.len() == MAX_ENTRIES {
                return Err(fail("too many entries (maximum 250000)"));
            }
            rows.push((pinyin, text, frequency));
        }
        if rows.is_empty() {
            return Err(error("empty dictionary"));
        }
        rows.sort_unstable_by(|a, b| b.2.cmp(&a.2).then(a.1.cmp(b.1)).then(a.0.cmp(b.0)));
        let pool_bytes: usize = rows.iter().map(|r| r.0.len() + r.1.len()).sum();
        // The v2 image uses 20 bytes per entry (entry + terminal), 28 per
        // node (node + edge + delimiter), pool bytes, and a net 28-byte header.
        let fixed = 28 + rows.len() * 20 + pool_bytes;
        let node_budget = MAX_DICTIONARY_BYTES
            .checked_sub(fixed)
            .ok_or_else(|| error("compiled dictionary exceeds 64 MiB"))?
            / 28;
        if node_budget == 0 {
            return Err(error("compiled dictionary exceeds 64 MiB"));
        }
        let mut build = vec![BuildNode::default()];
        let mut result = Self {
            entries: Vec::with_capacity(rows.len()),
            nodes: Vec::new(),
            edges: Vec::new(),
            terminals: Vec::new(),
            pool: String::new(),
            total_frequency: 0.0,
            shortcuts: Vec::new(),
            text_index: Vec::new(),
            boundary_words: BoundaryIndex::default(),
            max_letters: 0,
            initials_keys: String::new(),
            initials_groups: Vec::new(),
            initials_words: Vec::new(),
            initials_limited: 0,
        };
        result
            .pool
            .reserve_exact(rows.iter().map(|r| r.0.len() + r.1.len()).sum());
        for (id, (pinyin, text, frequency)) in rows.into_iter().enumerate() {
            let pinyin_offset = result.pool.len() as u32;
            result.pool.push_str(pinyin);
            let text_offset = result.pool.len() as u32;
            result.pool.push_str(text);
            result.entries.push(Entry {
                pinyin: pinyin_offset,
                text: text_offset,
                frequency,
                cost: 0.0,
                pinyin_len: pinyin.len() as u16,
                text_len: text.len() as u16,
            });
            result.total_frequency += f64::from(frequency);
            let mut node = 0;
            for label in pinyin.bytes() {
                node = if let Some(&next) = build[node].edges.get(&label) {
                    next
                } else {
                    let next = build.len();
                    if next == node_budget {
                        return Err(error("compiled dictionary exceeds 64 MiB"));
                    }
                    build.push(BuildNode::default());
                    build[node].edges.insert(label, next);
                    next
                };
            }
            build[node].terminals.push(id as u32);
        }
        result.nodes.reserve_exact(build.len());
        result.edges.reserve_exact(build.len() - 1);
        result
            .terminals
            .reserve_exact(result.entries.len() + build.len());
        for node in build {
            if node.terminals.len() > u16::MAX as usize {
                return Err(error("too many homophones"));
            }
            let edge_start = result.edges.len() as u32;
            let edge_len = node.edges.len() as u16;
            result
                .edges
                .extend(node.edges.into_iter().map(|(label, target)| Edge {
                    label,
                    target: target as u32,
                }));
            let term_start = result.terminals.len() as u32;
            let term_len = node.terminals.len() as u16;
            result.terminals.extend(node.terminals);
            result.terminals.push(NONE);
            result.nodes.push(Node {
                edge_start,
                edge_len,
                term_start,
                term_len,
                best: NONE,
                shortcut_start: 0,
                shortcut_len: 0,
            });
        }
        for entry in &mut result.entries {
            entry.cost =
                (result.total_frequency.ln() - f64::from(entry.frequency).ln() + 1.0) as f32;
        }
        for index in (0..result.nodes.len()).rev() {
            let node = result.nodes[index];
            let mut best = result.terminals[node.term_start as usize];
            for edge in result.edges_for(node) {
                best = best.min(result.nodes[edge.target as usize].best);
            }
            result.nodes[index].best = best;
        }
        result.build_shortcuts();
        Ok(result)
    }
    // Derived indices deliberately leave the validated v1 on-disk format intact.
    // Each first-letter node points to legal syllable ends within that syllable.
    fn build_shortcuts(&mut self) {
        let _ = crate::language::links();
        self.build_initials();
        let mut text_index: Vec<_> = (0..self.entries.len() as u32).collect();
        text_index
            .sort_unstable_by(|&a, &b| self.entry(a).text.cmp(self.entry(b).text).then(a.cmp(&b)));
        self.text_index = text_index;
        // Collision-free encoding of up to six Unicode scalars, including their
        // length (each digit is scalar+1). This immutable derived index avoids
        // repeated UTF-8 binary searches for every decoder boundary.
        self.boundary_words =
            BoundaryIndex::build((0..self.entries.len() as u32).filter_map(|id| {
                let mut code = 0u128;
                let mut count = 0;
                for c in self.entry(id).text.chars() {
                    count += 1;
                    if count > 6 {
                        return None;
                    }
                    code = (code << 21) | u128::from(u32::from(c) + 1);
                }
                (count >= 2).then_some(code)
            }));
        self.max_letters = (0..self.entries.len() as u32)
            .map(|id| {
                self.entry(id)
                    .pinyin
                    .bytes()
                    .filter(|&b| b != b'\'')
                    .count()
            })
            .max()
            .unwrap_or(0);
        let syllables: BTreeSet<_> = include_str!("../../../data/syllables.txt")
            .split_ascii_whitespace()
            .collect();
        fn collect(
            d: &Dictionary,
            node: u32,
            key: &mut String,
            syllables: &BTreeSet<&str>,
            out: &mut Vec<u32>,
        ) {
            let n = d.nodes[node as usize];
            if key.len() > 1
                && syllables.contains(key.as_str())
                && (n.term_len > 0 || d.child(node, b'\'').is_some())
            {
                out.push(node);
            }
            if key.len() == 6 {
                return;
            }
            for e in d.edges_for(n) {
                if e.label == b'\'' {
                    continue;
                }
                key.push(e.label as char);
                collect(d, e.target, key, syllables, out);
                key.pop();
            }
        }
        let mut boundaries = vec![0];
        boundaries.extend(
            self.edges
                .iter()
                .filter(|e| e.label == b'\'')
                .map(|e| e.target),
        );
        let mut ends = Vec::new();
        let mut key = String::with_capacity(6);
        for boundary in boundaries {
            let children: Vec<_> = self.edges_for(self.nodes[boundary as usize]).to_vec();
            for edge in children {
                if edge.label == b'\'' {
                    continue;
                }
                ends.clear();
                key.clear();
                key.push(edge.label as char);
                collect(self, edge.target, &mut key, &syllables, &mut ends);
                ends.sort_unstable_by_key(|&n| self.nodes[n as usize].best);
                let node = &mut self.nodes[edge.target as usize];
                node.shortcut_start = self.shortcuts.len() as u32;
                node.shortcut_len = ends.len() as u16;
                self.shortcuts.extend_from_slice(&ends);
                // Also accept the ordinary zh/ch/sh consonant initials.
                if matches!(edge.label, b'z' | b'c' | b's') {
                    if let Some(digraph) = self.child(edge.target, b'h') {
                        ends.clear();
                        key.clear();
                        key.push(edge.label as char);
                        key.push('h');
                        collect(self, digraph, &mut key, &syllables, &mut ends);
                        ends.sort_unstable_by_key(|&n| self.nodes[n as usize].best);
                        let node = &mut self.nodes[digraph as usize];
                        node.shortcut_start = self.shortcuts.len() as u32;
                        node.shortcut_len = ends.len() as u16;
                        self.shortcuts.extend_from_slice(&ends);
                    }
                }
            }
        }
        self.shortcuts.shrink_to_fit();
    }
    fn advance_fast(
        &self,
        active: &[u32],
        label: u8,
        next: &mut [u32; MAX_ACTIVE_STATES],
        capacity: usize,
    ) -> (usize, bool) {
        let mut len = 0;
        let mut limited = false;
        let mut seen = [u32::MAX; 1024];
        let mut insert = |node: u32| {
            let mut slot = (node.wrapping_mul(2654435761) as usize) & 1023;
            let mut probes = 0;
            while seen[slot] != u32::MAX {
                if seen[slot] == node {
                    return;
                }
                slot = (slot + 1) & 1023;
                probes += 1;
                if probes == 1024 {
                    limited = true;
                    return;
                }
            }
            seen[slot] = node;
            let rank = self.nodes[(node & !ABBREVIATED) as usize].best;
            let at = next[..len].partition_point(|&n| {
                (self.nodes[(n & !ABBREVIATED) as usize].best, n) <= (rank, node)
            });
            if len == capacity {
                limited = true;
            }
            if at < capacity {
                next.copy_within(at..len.min(capacity - 1), at + 1);
                next[at] = node;
                len = (len + 1).min(capacity);
            }
        };
        for &state in active {
            let node = state & !ABBREVIATED;
            let direct = if state & ABBREVIATED != 0 && label != b'\'' {
                None
            } else {
                self.child(node, label)
            };
            let skipped = if label != b'\'' {
                self.child(node, b'\'').and_then(|n| self.child(n, label))
            } else {
                None
            };
            for child in direct.into_iter().chain(skipped) {
                insert(child);
                let n = self.nodes[child as usize];
                for &end in &self.shortcuts
                    [n.shortcut_start as usize..n.shortcut_start as usize + n.shortcut_len as usize]
                {
                    insert(end | ABBREVIATED);
                }
            }
        }
        (len, limited)
    }
    pub(crate) fn matches_fast(
        &self,
        input: &str,
        start: usize,
        limit: usize,
        mut emit: impl FnMut(u32, usize),
    ) -> bool {
        let mut active = [0; MAX_ACTIVE_STATES];
        let mut next = [0; MAX_ACTIVE_STATES];
        let mut len = 1;
        let mut limited = false;
        for (offset, label) in input.as_bytes()[start..].iter().copied().enumerate() {
            let (n, l) = self.advance_fast(&active[..len], label, &mut next, 96);
            len = n;
            limited |= l;
            std::mem::swap(&mut active, &mut next);
            if len == 0 {
                break;
            }
            let mut end = start + offset + 1;
            if input.as_bytes().get(end) == Some(&b'\'') {
                end += 1;
            }
            for (i, &state) in active[..len].iter().enumerate() {
                let node = state & !ABBREVIATED;
                if active[..i].iter().any(|&old| old & !ABBREVIATED == node) {
                    continue;
                }
                let n = self.nodes[node as usize];
                for &id in &self.terminals[n.term_start as usize
                    ..n.term_start as usize + (n.term_len as usize).min(limit)]
                {
                    emit(id, end);
                }
            }
        }
        limited
    }
    pub(crate) fn continuations(&self, prefix: &str, mut emit: impl FnMut(u32, u8)) -> bool {
        let at = self
            .text_index
            .partition_point(|&id| self.entry(id).text < prefix);
        for (visited, &id) in self.text_index[at..].iter().enumerate() {
            let text = self.entry(id).text;
            if !text.starts_with(prefix) {
                break;
            }
            if visited == 4096 {
                return true;
            }
            if text.len() > prefix.len() {
                emit(id, prefix.len() as u8);
            }
        }
        false
    }
    pub(crate) fn context_bonus(&self, context: &str, next: &str) -> f32 {
        let mut bonus = crate::language::bonus(context, next);
        let mut bytes = [0u8; MAX_TEXT_BYTES];
        for (start, _) in context.char_indices().rev().take(4) {
            let suffix = &context[start..];
            let size = suffix.len() + next.len();
            if size > MAX_TEXT_BYTES {
                continue;
            }
            bytes[..suffix.len()].copy_from_slice(suffix.as_bytes());
            bytes[suffix.len()..size].copy_from_slice(next.as_bytes());
            let pair = std::str::from_utf8(&bytes[..size]).expect("UTF-8 words");
            let at = self
                .text_index
                .partition_point(|&id| self.entry(id).text < pair);
            if let Some(&id) = self.text_index.get(at) {
                let e = self.entry(id);
                if e.text == pair {
                    bonus = bonus.max(2.0 + (e.frequency as f32).ln() * 0.3);
                }
            }
        }
        bonus
    }
    pub fn entry_count(&self) -> usize {
        self.entries.len()
    }
    /// Number of words whose strict one-letter keys had to be truncated by
    /// [`MAX_INITIAL_KEYS`]. Non-zero means the one-letter lane is incomplete for
    /// those words, so the loss is reportable instead of silent (review R12).
    pub fn initials_truncated_words(&self) -> usize {
        self.initials_limited
    }
    pub fn estimated_heap_bytes(&self) -> usize {
        self.entries.capacity() * std::mem::size_of::<Entry>()
            + self.nodes.capacity() * std::mem::size_of::<Node>()
            + self.edges.capacity() * std::mem::size_of::<Edge>()
            + self.terminals.capacity() * 4
            + self.pool.capacity()
            + self.shortcuts.capacity() * 4
            + self.text_index.capacity() * 4
            + self.initials_keys.capacity()
            + self.initials_groups.capacity() * std::mem::size_of::<InitialGroup>()
            + self.initials_words.capacity() * 4
            + self.boundary_words.heap_bytes()
    }
    pub(crate) fn entry(&self, id: u32) -> Candidate<'_> {
        let e = self.entries[id as usize];
        Candidate {
            pinyin: &self.pool[e.pinyin as usize..e.pinyin as usize + e.pinyin_len as usize],
            text: &self.pool[e.text as usize..e.text as usize + e.text_len as usize],
            frequency: e.frequency,
        }
    }
    /// Evidence for learning promotion must attest both text and pronunciation.
    pub(crate) fn attests(&self, text: &str, input: &str, flags: u32) -> bool {
        if let Some((start, end, _)) = self.initials_range(input) {
            if self.initials_words[start as usize..end as usize]
                .iter()
                .any(|&id| self.entry(id).text == text)
            {
                return true;
            }
        }
        let start = self
            .text_index
            .partition_point(|&id| self.entry(id).text < text);
        self.text_index[start..]
            .iter()
            .take_while(|&&id| self.entry(id).text == text)
            .any(|&id| {
                crate::fuzzy::complete_annotations(input, self.entry(id).pinyin, flags).is_some()
            })
    }
    /// A lexical witness must cross the boundary, not merely occur on one side.
    pub(crate) fn attests_boundary(&self, left: &str, right: &str) -> bool {
        for (start, _) in left.char_indices().rev().take(3) {
            let mut code = left[start..]
                .chars()
                .fold(0u128, |v, c| (v << 21) | u128::from(u32::from(c) + 1));
            for c in right.chars().take(3) {
                code = (code << 21) | u128::from(u32::from(c) + 1);
                if self.boundary_words.contains(code) {
                    return true;
                }
            }
        }
        false
    }
    pub(crate) fn word_cost(&self, id: u32) -> f32 {
        self.entries[id as usize].cost
    }
    pub(crate) fn corrected_pronunciation(
        &self,
        text: &str,
        input: &str,
        flags: u32,
    ) -> Option<&str> {
        if let Some((start, end, _)) = self.initials_range(input) {
            if let Some(&id) = self.initials_words[start as usize..end as usize]
                .iter()
                .find(|&&id| self.entry(id).text == text)
            {
                return Some(self.entry(id).pinyin);
            }
        }
        if flags == 0 {
            return None;
        }
        let start = self
            .text_index
            .partition_point(|&id| self.entry(id).text < text);
        self.text_index[start..]
            .iter()
            .take_while(|&&id| self.entry(id).text == text)
            .take(64)
            .map(|&id| self.entry(id).pinyin)
            .find(|&pinyin| {
                crate::fuzzy::annotations(input, pinyin, flags).is_some_and(|marks| marks != [0; 4])
            })
    }
    fn edges_for(&self, node: Node) -> &[Edge] {
        &self.edges[node.edge_start as usize..node.edge_start as usize + node.edge_len as usize]
    }
    fn child(&self, node: u32, label: u8) -> Option<u32> {
        let edges = self.edges_for(self.nodes[node as usize]);
        edges
            .binary_search_by_key(&label, |e| e.label)
            .ok()
            .map(|i| edges[i].target)
    }
    /// Bounded trie alignment, never a dictionary-wide edit-distance scan.
    /// One keyboard error per syllable, two per word; phonetic rules are anchored.
    pub(crate) fn tolerant(
        &self,
        input: &str,
        start: usize,
        flags: u32,
        mut emit: impl FnMut(u32, usize, u8),
    ) -> bool {
        use crate::fuzzy::*;
        #[derive(Clone, Copy, Default, PartialEq, Eq)]
        struct State {
            node: u32,
            pos: u8,
            depth: u8,
            typo: u8,
            total: u8,
            cost: u8,
        }
        let raw = input.as_bytes();
        if flags == 0 || start >= raw.len() {
            return false;
        }
        let mut queue = [State::default(); 4096];
        queue[0].pos = start as u8;
        let (mut head, mut tail, mut limited) = (0, 1, false);
        // Open-addressed state set: duplicate routes cannot crowd the sentence beam.
        let mut seen = [u64::MAX; 8192];
        while head < tail {
            let s = queue[head];
            head += 1;
            let pos = s.pos as usize;
            let node = self.nodes[s.node as usize];
            if pos > start {
                emit(s.node, pos, s.cost);
            }
            let mut put = |value: State| {
                if tail == queue.len() {
                    limited = true;
                    return;
                }
                let key = u64::from(value.node)
                    | (u64::from(value.pos) << 32)
                    | (u64::from(value.depth) << 40)
                    | (u64::from(value.typo) << 48)
                    | (u64::from(value.total) << 49)
                    | (u64::from(value.cost) << 51);
                let mut at = (key.wrapping_mul(11400714819323198485) >> 51) as usize;
                while seen[at] != u64::MAX && seen[at] != key {
                    at = (at + 1) & 8191;
                }
                if seen[at] == key {
                    return;
                }
                seen[at] = key;
                queue[tail] = value;
                tail += 1;
            };
            // A missing final letter may be immediately before a separator or
            // the end of input. Do not require another typed letter to recall it.
            let typing = s.typo == 0
                && s.total < 2
                && raw[start..].iter().filter(|&&c| c != b'\'').count() >= 3;
            if typing && flags & OMIT != 0 && s.depth < 6 {
                for edge in self.edges_for(node) {
                    if edge.label.is_ascii_lowercase() {
                        put(State {
                            node: edge.target,
                            depth: s.depth + 1,
                            typo: 1,
                            total: s.total + 1,
                            cost: s.cost + 2,
                            ..s
                        });
                    }
                }
            }
            if pos == raw.len() {
                continue;
            }
            if let Some(next) = self.child(s.node, b'\'') {
                if s.depth > 0 {
                    put(State {
                        node: next,
                        pos: s.pos + u8::from(raw[pos] == b'\''),
                        depth: 0,
                        typo: 0,
                        ..s
                    });
                }
            }
            if raw[pos] == b'\'' {
                continue;
            }
            if let Some(next) = self.child(s.node, raw[pos]) {
                put(State {
                    node: next,
                    pos: s.pos + 1,
                    depth: s.depth + 1,
                    ..s
                });
            }
            // Never interpret a one/two-letter initial as a keyboard mistake.
            if typing {
                if flags & SWAP != 0 && pos + 1 < raw.len() && raw[pos] != raw[pos + 1] {
                    // A transposition routinely straddles a syllable boundary
                    // (`guan'ai` typed as `guaani`), so either letter of the pair
                    // may step over the separator the exact path also crosses.
                    // Crossing restarts the within-syllable error budget.
                    let mut crossed = false;
                    let step =
                        |node: u32, label: u8, crossed: &mut bool| match self.child(node, label) {
                            Some(next) => Some(next),
                            None => self
                                .child(node, b'\'')
                                .and_then(|next| self.child(next, label))
                                .inspect(|_| *crossed = true),
                        };
                    let first = step(s.node, raw[pos + 1], &mut crossed);
                    if let Some(next) = first.and_then(|n| step(n, raw[pos], &mut crossed)) {
                        put(State {
                            node: next,
                            pos: s.pos + 2,
                            depth: if crossed { 1 } else { s.depth + 2 },
                            typo: 1,
                            total: s.total + 1,
                            cost: s.cost + 2,
                        });
                    }
                }
                for edge in self.edges_for(node) {
                    if !edge.label.is_ascii_lowercase() || s.depth >= 6 {
                        continue;
                    }
                    if flags & NEIGHBOR != 0 && neighbors(raw[pos], edge.label) {
                        put(State {
                            node: edge.target,
                            pos: s.pos + 1,
                            depth: s.depth + 1,
                            typo: 1,
                            total: s.total + 1,
                            cost: s.cost + 2,
                        });
                    }
                }
                if flags & REPEAT != 0 && s.depth > 0 && pos > start && raw[pos] == raw[pos - 1] {
                    put(State {
                        pos: s.pos + 1,
                        typo: 1,
                        total: s.total + 1,
                        cost: s.cost + 2,
                        ..s
                    });
                }
            }
            if s.cost >= 12 {
                continue;
            }
            for (rule, (left, right, initial)) in RULES.iter().enumerate() {
                if flags & (1 << rule) == 0 || (*initial && s.depth != 0) {
                    continue;
                }
                for (canonical, typed) in [(*left, *right), (*right, *left)] {
                    if !raw[pos..].starts_with(typed) {
                        continue;
                    }
                    let mut next = Some(s.node);
                    for &letter in canonical {
                        next = next.and_then(|n| self.child(n, letter));
                    }
                    if let Some(next) = next {
                        let end = self.nodes[next as usize];
                        if !initial && end.term_len == 0 && self.child(next, b'\'').is_none() {
                            continue;
                        }
                        put(State {
                            node: next,
                            pos: (pos + typed.len()) as u8,
                            depth: s.depth + canonical.len() as u8,
                            cost: s.cost + 1,
                            ..s
                        });
                    }
                }
            }
        }
        limited
    }
    pub(crate) fn matches_tolerant(
        &self,
        input: &str,
        start: usize,
        flags: u32,
        limit: usize,
        mut emit: impl FnMut(u32, usize, u8),
    ) -> bool {
        self.tolerant(input, start, flags, |node, end, cost| {
            if cost == 0 {
                return;
            }
            let n = self.nodes[node as usize];
            let end = end + usize::from(input.as_bytes().get(end) == Some(&b'\''));
            for &id in &self.terminals
                [n.term_start as usize..n.term_start as usize + (n.term_len as usize).min(limit)]
            {
                emit(id, end, cost);
            }
        })
    }
    fn advance(
        &self,
        active: &[u32],
        label: u8,
        next: &mut [u32; MAX_ACTIVE_STATES],
    ) -> Result<usize, LookupError> {
        let mut len = 0;
        for &node in active {
            let direct = self.child(node, label);
            let skipped = if label != b'\'' {
                self.child(node, b'\'').and_then(|n| self.child(n, label))
            } else {
                None
            };
            for child in direct.into_iter().chain(skipped) {
                if len == MAX_ACTIVE_STATES {
                    return Err(LookupError::TooAmbiguous);
                }
                next[len] = child;
                len += 1;
            }
        }
        Ok(len)
    }
    fn trailing_boundary(&self, active: &[u32], next: &mut [u32; MAX_ACTIVE_STATES]) -> usize {
        let mut len = 0;
        for &state in active {
            let node = state & !ABBREVIATED;
            // A real next-syllable edge still means continuation (ni' -> ni'hao).
            // Only a completed terminal without that edge may absorb a trailing
            // separator. Never reinterpret an incomplete syllable as a word.
            if let Some(child) = self.child(node, b'\'') {
                next[len] = child;
            } else if self.nodes[node as usize].term_len != 0 {
                next[len] = state;
            } else {
                continue;
            }
            len += 1;
        }
        len
    }
    /// Exact whole-word full/initial mixtures, ranked by dictionary frequency.
    /// The boolean reports pruning; unlike lookup this does not complete tails.
    pub fn lookup_fast(&self, input: &str) -> Result<(Candidates, bool), LookupError> {
        if !valid_input(input) {
            return Err(LookupError::InvalidInput);
        }
        let mut cursor = CandidateCursor::new();
        let limited = cursor.reset_fast(self, input)?;
        let mut results = Candidates::default();
        while results.len < MAX_CANDIDATES {
            let Some(id) = cursor.next(self)? else {
                break;
            };
            if results.ids[..results.len]
                .iter()
                .any(|&old| self.entry(old).text == self.entry(id).text)
            {
                continue;
            }
            results.ids[results.len] = id;
            results.len += 1;
        }
        Ok((results, limited))
    }
    pub fn lookup(&self, input: &str) -> Result<Candidates, LookupError> {
        let mut cursor = CandidateCursor::new();
        cursor.reset(self, input)?;
        let mut results = Candidates::default();
        while results.len < MAX_CANDIDATES {
            let Some(id) = cursor.next(self)? else {
                break;
            };
            if results.ids[..results.len]
                .iter()
                .any(|&old| self.entry(old).text == self.entry(id).text)
            {
                continue;
            }
            results.ids[results.len] = id;
            results.len += 1;
        }
        Ok(results)
    }
    /// Emit lexical edges of the pronunciation DAG. Separators at word boundaries
    /// belong to the preceding edge. Each terminal supplies its best `limit` words.
    pub(crate) fn matches(
        &self,
        input: &str,
        start: usize,
        limit: usize,
        mut emit: impl FnMut(u32, usize),
    ) -> Result<(), LookupError> {
        let mut active = [0; MAX_ACTIVE_STATES];
        let mut next = [0; MAX_ACTIVE_STATES];
        let mut len = 1;
        for (offset, label) in input.as_bytes()[start..].iter().copied().enumerate() {
            len = self.advance(&active[..len], label, &mut next)?;
            std::mem::swap(&mut active, &mut next);
            if len == 0 {
                break;
            }
            let mut end = start + offset + 1;
            if input.as_bytes().get(end) == Some(&b'\'') {
                end += 1;
            }
            for &node in &active[..len] {
                let node = self.nodes[node as usize];
                for &id in &self.terminals[node.term_start as usize
                    ..node.term_start as usize + (node.term_len as usize).min(limit)]
                {
                    emit(id, end);
                }
            }
        }
        Ok(())
    }

    /// Fixed-width LE arrays and a single UTF-8 pool; deterministic across targets.
    pub fn to_binary(&self) -> Vec<u8> {
        let mut output = Vec::new();
        output.extend_from_slice(b"MSWYDICT");
        for value in [
            2,
            self.entries.len() as u32,
            self.nodes.len() as u32,
            self.edges.len() as u32,
            self.terminals.len() as u32,
            self.pool.len() as u32,
            0,
        ] {
            output.extend_from_slice(&value.to_le_bytes());
        }
        for e in &self.entries {
            for v in [e.pinyin, e.text, e.frequency] {
                output.extend_from_slice(&v.to_le_bytes());
            }
            output.extend_from_slice(&e.pinyin_len.to_le_bytes());
            output.extend_from_slice(&e.text_len.to_le_bytes());
        }
        for n in &self.nodes {
            for v in [n.edge_start, n.term_start, n.best] {
                output.extend_from_slice(&v.to_le_bytes());
            }
            output.extend_from_slice(&n.edge_len.to_le_bytes());
            output.extend_from_slice(&n.term_len.to_le_bytes());
        }
        for e in &self.edges {
            output.extend_from_slice(&u32::from(e.label).to_le_bytes());
            output.extend_from_slice(&e.target.to_le_bytes());
        }
        for t in &self.terminals {
            output.extend_from_slice(&t.to_le_bytes());
        }
        output.extend_from_slice(self.pool.as_bytes());
        let checksum = crc32(&output[36..]);
        output[32..36].copy_from_slice(&checksum.to_le_bytes());
        output
    }
    pub fn from_binary(bytes: &[u8]) -> Result<Self, DictionaryError> {
        if bytes.len() < 36 || bytes.len() > MAX_DICTIONARY_BYTES || &bytes[..8] != b"MSWYDICT" {
            return Err(error("invalid binary header/size"));
        }
        let word = |at: usize| u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap()) as usize;
        if !matches!(word(8), 1 | 2) || word(32) as u32 != crc32(&bytes[36..]) {
            return Err(error("unsupported version or checksum mismatch"));
        }
        let (ec, nc, xc, tc, pc) = (word(12), word(16), word(20), word(24), word(28));
        // u32 sizes promoted to u64 prevent overflow on 32-bit consumers.
        let expected =
            36 + ec as u64 * 16 + nc as u64 * 16 + xc as u64 * 8 + tc as u64 * 4 + pc as u64;
        if ec == 0
            || ec > MAX_ENTRIES
            || nc == 0
            || xc != nc - 1
            || tc != ec + nc
            || expected != bytes.len() as u64
        {
            return Err(error("invalid binary section lengths"));
        }
        let mut d = Self {
            entries: Vec::with_capacity(ec),
            nodes: Vec::with_capacity(nc),
            edges: Vec::with_capacity(xc),
            terminals: Vec::with_capacity(tc),
            pool: String::new(),
            total_frequency: 0.0,
            shortcuts: Vec::new(),
            text_index: Vec::new(),
            boundary_words: BoundaryIndex::default(),
            max_letters: 0,
            initials_keys: String::new(),
            initials_groups: Vec::new(),
            initials_words: Vec::new(),
            initials_limited: 0,
        };
        let half = |at: usize| u16::from_le_bytes(bytes[at..at + 2].try_into().unwrap());
        let mut at = 36;
        for _ in 0..ec {
            d.entries.push(Entry {
                pinyin: word(at) as u32,
                text: word(at + 4) as u32,
                frequency: word(at + 8) as u32,
                cost: 0.0,
                pinyin_len: half(at + 12),
                text_len: half(at + 14),
            });
            at += 16;
        }
        for _ in 0..nc {
            d.nodes.push(Node {
                edge_start: word(at) as u32,
                term_start: word(at + 4) as u32,
                best: word(at + 8) as u32,
                edge_len: half(at + 12),
                term_len: half(at + 14),
                shortcut_start: 0,
                shortcut_len: 0,
            });
            at += 16;
        }
        for _ in 0..xc {
            let label = word(at);
            if label > 255 {
                return Err(error("invalid edge label"));
            }
            d.edges.push(Edge {
                label: label as u8,
                target: word(at + 4) as u32,
            });
            at += 8;
        }
        for _ in 0..tc {
            d.terminals.push(word(at) as u32);
            at += 4;
        }
        d.pool = std::str::from_utf8(&bytes[at..])
            .map_err(|_| error("invalid UTF-8 pool"))?
            .to_owned();
        let mut offset = 0;
        for (id, e) in d.entries.iter().enumerate() {
            let text_end = (e.text as usize)
                .checked_add(e.text_len as usize)
                .ok_or_else(|| error("string range overflow"))?;
            if e.pinyin as usize != offset
                || e.text as usize != offset + e.pinyin_len as usize
                || text_end > pc
                || !d.pool.is_char_boundary(offset)
                || !d.pool.is_char_boundary(e.text as usize)
                || !d.pool.is_char_boundary(text_end)
            {
                return Err(error("invalid string offsets"));
            }
            let c = d.entry(id as u32);
            if c.pinyin.is_empty()
                || !valid_pinyin(c.pinyin)
                || (word(8) == 1 && c.pinyin.len() > MAX_INPUT_BYTES)
                || c.pinyin.ends_with('\'')
                || c.pinyin.bytes().any(|b| b.is_ascii_uppercase())
                || c.text.is_empty()
                || c.text.len() > MAX_TEXT_BYTES
                || c.text.chars().any(char::is_control)
                || c.frequency == 0
            {
                return Err(error("invalid entry"));
            }
            if id > 0 {
                let p = d.entry(id as u32 - 1);
                if (std::cmp::Reverse(p.frequency), p.text, p.pinyin)
                    >= (std::cmp::Reverse(c.frequency), c.text, c.pinyin)
                {
                    return Err(error("invalid entry ordering"));
                }
            }
            d.total_frequency += f64::from(c.frequency);
            offset = text_end;
        }
        for entry in &mut d.entries {
            entry.cost = (d.total_frequency.ln() - f64::from(entry.frequency).ln() + 1.0) as f32;
        }
        if offset != pc {
            return Err(error("unreferenced pool bytes"));
        }
        let (mut edge_offset, mut term_offset) = (0, 0);
        let mut parents = vec![0u8; nc];
        let mut seen = vec![false; ec];
        for (index, &node) in d.nodes.iter().enumerate() {
            let edge_end = edge_offset + node.edge_len as usize;
            let term_end = term_offset + node.term_len as usize;
            if node.edge_start as usize != edge_offset
                || node.term_start as usize != term_offset
                || edge_end > xc
                || term_end >= tc
                || d.terminals[term_end] != NONE
                || node.best as usize >= ec
            {
                return Err(error("invalid node ranges"));
            }
            let mut last = 0;
            for edge in &d.edges[edge_offset..edge_end] {
                if edge.label <= last
                    || !(edge.label.is_ascii_lowercase() || edge.label == b'\'')
                    || edge.target as usize <= index
                    || edge.target as usize >= nc
                {
                    return Err(error("invalid trie edges"));
                }
                if parents[edge.target as usize] != 0 {
                    return Err(error("trie node has multiple parents"));
                }
                parents[edge.target as usize] = 1;
                last = edge.label;
            }
            let mut previous = None;
            for &id in &d.terminals[term_offset..term_end] {
                if id as usize >= ec || previous.is_some_and(|p| p >= id) || seen[id as usize] {
                    return Err(error("invalid terminal IDs"));
                }
                seen[id as usize] = true;
                previous = Some(id);
            }
            edge_offset = edge_end;
            term_offset = term_end + 1;
        }
        if edge_offset != xc
            || term_offset != tc
            || parents[1..].contains(&0)
            || seen.contains(&false)
        {
            return Err(error("incomplete trie"));
        }
        for &node in d.nodes.iter().rev() {
            let mut best = d.terminals[node.term_start as usize];
            for edge in d.edges_for(node) {
                best = best.min(d.nodes[edge.target as usize].best);
            }
            if best != node.best {
                return Err(error("invalid subtree ranking"));
            }
        }
        for id in 0..ec {
            let mut node = 0;
            for label in d.entry(id as u32).pinyin.bytes() {
                node = d
                    .child(node, label)
                    .ok_or_else(|| error("entry/trie mismatch"))?;
            }
            let n = d.nodes[node as usize];
            if d.terminals[n.term_start as usize..n.term_start as usize + n.term_len as usize]
                .binary_search(&(id as u32))
                .is_err()
            {
                return Err(error("entry/terminal mismatch"));
            }
        }
        d.build_shortcuts();
        Ok(d)
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
struct HeapItem {
    rank: u32,
    source: u32,
}
/// Resumable best-first traversal. Heap has an explicit work budget; exhaustion
/// is reported, never mistaken for the end of a homophone list.
#[derive(Clone)]
pub struct CandidateCursor {
    heap: [HeapItem; FRONTIER],
    len: usize,
    roots: [u32; MAX_ACTIVE_STATES],
    root_len: usize,
    completions: bool,
}
impl Default for CandidateCursor {
    fn default() -> Self {
        Self::new()
    }
}
impl CandidateCursor {
    pub fn new() -> Self {
        Self {
            heap: [HeapItem::default(); FRONTIER],
            len: 0,
            roots: [0; MAX_ACTIVE_STATES],
            root_len: 0,
            completions: false,
        }
    }
    fn push(&mut self, item: HeapItem) -> Result<(), LookupError> {
        if self.len == FRONTIER {
            return Err(LookupError::TooAmbiguous);
        }
        let mut at = self.len;
        self.len += 1;
        while at > 0 {
            let parent = (at - 1) / 2;
            if self.heap[parent] <= item {
                break;
            }
            self.heap[at] = self.heap[parent];
            at = parent;
        }
        self.heap[at] = item;
        Ok(())
    }
    fn pop(&mut self) -> Option<HeapItem> {
        if self.len == 0 {
            return None;
        }
        let result = self.heap[0];
        self.len -= 1;
        let item = self.heap[self.len];
        let mut at = 0;
        while at * 2 + 1 < self.len {
            let mut child = at * 2 + 1;
            if child + 1 < self.len && self.heap[child + 1] < self.heap[child] {
                child += 1;
            }
            if item <= self.heap[child] {
                break;
            }
            self.heap[at] = self.heap[child];
            at = child;
        }
        self.heap[at] = item;
        Some(result)
    }
    pub fn reset(&mut self, d: &Dictionary, input: &str) -> Result<(), LookupError> {
        if !valid_input(input) {
            return Err(LookupError::InvalidInput);
        }
        let mut active = [0; MAX_ACTIVE_STATES];
        let mut next = [0; MAX_ACTIVE_STATES];
        let mut len = usize::from(!input.is_empty());
        for (at, label) in input.bytes().map(|b| b.to_ascii_lowercase()).enumerate() {
            len = if label == b'\'' && at + 1 == input.len() {
                d.trailing_boundary(&active[..len], &mut next)
            } else {
                d.advance(&active[..len], label, &mut next)?
            };
            std::mem::swap(&mut active, &mut next);
        }
        self.len = 0;
        self.root_len = len;
        self.roots[..len].copy_from_slice(&active[..len]);
        self.completions = false;
        for &node in &active[..len] {
            let n = d.nodes[node as usize];
            if n.term_len > 0 {
                self.push(HeapItem {
                    rank: d.terminals[n.term_start as usize],
                    source: TERMINAL | n.term_start,
                })?;
            }
        }
        Ok(())
    }
    pub(crate) fn reset_fast(&mut self, d: &Dictionary, input: &str) -> Result<bool, LookupError> {
        self.len = 0;
        self.root_len = 0;
        self.completions = true;
        if input.bytes().filter(|&b| b != b'\'').count() > d.max_letters {
            return Ok(false);
        }
        let mut active = [0; MAX_ACTIVE_STATES];
        let mut next = [0; MAX_ACTIVE_STATES];
        let mut len = usize::from(!input.is_empty());
        let mut limited = false;
        if !valid_input(input) {
            return Err(LookupError::InvalidInput);
        }
        for (at, label) in input.bytes().map(|b| b.to_ascii_lowercase()).enumerate() {
            let (n, l) = if label == b'\'' && at + 1 == input.len() {
                (d.trailing_boundary(&active[..len], &mut next), false)
            } else {
                d.advance_fast(&active[..len], label, &mut next, MAX_ACTIVE_STATES)
            };
            len = n;
            limited |= l;
            std::mem::swap(&mut active, &mut next);
        }
        self.len = 0;
        self.root_len = 0;
        self.completions = true;
        for (i, &state) in active[..len].iter().enumerate() {
            let node = state & !ABBREVIATED;
            if active[..i].iter().any(|&old| old & !ABBREVIATED == node) {
                continue;
            }
            let n = d.nodes[node as usize];
            if n.term_len > 0 {
                self.push(HeapItem {
                    rank: d.terminals[n.term_start as usize],
                    source: TERMINAL | n.term_start,
                })?;
            }
        }
        Ok(limited)
    }
    pub(crate) fn has_matches(&self) -> bool {
        self.root_len > 0
    }
    pub(crate) fn reset_tolerant(&mut self, d: &Dictionary, input: &str, flags: u32) -> bool {
        self.len = 0;
        self.root_len = 0;
        // Correction tiers recall complete dictionary terminals, never their
        // unchecked suffix completions. Ordinary completion is a later tier.
        self.completions = true;
        let mut limited = false;
        let mut penalties = [u8::MAX; MAX_ACTIVE_STATES];
        let traversal = d.tolerant(input, 0, flags, |node, end, cost| {
            if end != input.len() || cost == 0 {
                return;
            }
            if let Some(at) = self.roots[..self.root_len]
                .iter()
                .position(|&old| old == node)
            {
                penalties[at] = penalties[at].min(cost);
                return;
            }
            if self.root_len == MAX_ACTIVE_STATES {
                limited = true;
                return;
            }
            self.roots[self.root_len] = node;
            penalties[self.root_len] = cost;
            self.root_len += 1;
        });
        for (i, &penalty) in penalties.iter().enumerate().take(self.root_len) {
            let n = d.nodes[self.roots[i] as usize];
            if n.term_len > 0 {
                limited |= self
                    .push(HeapItem {
                        rank: d.terminals[n.term_start as usize] | (u32::from(penalty) << 18),
                        source: TERMINAL | n.term_start,
                    })
                    .is_err();
            }
        }
        limited || traversal
    }
    pub fn next(&mut self, d: &Dictionary) -> Result<Option<u32>, LookupError> {
        loop {
            if self.len == 0 && !self.completions {
                self.completions = true;
                for i in 0..self.root_len {
                    let source = self.roots[i];
                    self.push(HeapItem {
                        rank: d.nodes[source as usize].best,
                        source,
                    })?;
                }
            }
            let Some(item) = self.pop() else {
                return Ok(None);
            };
            if item.source & TERMINAL != 0 {
                let next = (item.source & !TERMINAL) + 1;
                let rank = d.terminals[next as usize];
                if rank != NONE {
                    self.push(HeapItem {
                        rank: rank | (item.rank & !0x3ffff),
                        source: TERMINAL | next,
                    })?;
                }
                return Ok(Some(item.rank & 0x3ffff));
            }
            let n = d.nodes[item.source as usize];
            if n.term_len > 0 {
                self.push(HeapItem {
                    rank: d.terminals[n.term_start as usize],
                    source: TERMINAL | n.term_start,
                })?;
            }
            for e in d.edges_for(n) {
                self.push(HeapItem {
                    rank: d.nodes[e.target as usize].best,
                    source: e.target,
                })?;
            }
        }
    }
}
pub(crate) fn valid_pinyin(input: &str) -> bool {
    input.len() <= MAX_PINYIN_BYTES
        && !input.starts_with('\'')
        && !input.contains("''")
        && input.bytes().all(|b| b.is_ascii_alphabetic() || b == b'\'')
}
pub(crate) fn valid_input(input: &str) -> bool {
    input.len() <= MAX_INPUT_BYTES
        && !input.starts_with('\'')
        && !input.contains("''")
        && input.bytes().all(|b| b.is_ascii_alphabetic() || b == b'\'')
}
pub(crate) fn crc32(bytes: &[u8]) -> u32 {
    const TABLE: [u32; 256] = {
        let mut table = [0; 256];
        let mut i = 0;
        while i < 256 {
            let mut x = i as u32;
            let mut j = 0;
            while j < 8 {
                x = (x >> 1) ^ if x & 1 != 0 { 0xedb88320 } else { 0 };
                j += 1;
            }
            table[i] = x;
            i += 1;
        }
        table
    };
    let mut crc = !0u32;
    for &b in bytes {
        crc = (crc >> 8) ^ TABLE[((crc ^ u32::from(b)) & 255) as usize];
    }
    !crc
}

#[cfg(test)]
mod boundary_index_tests {
    use super::*;

    /// The prefilter must never invent or hide a membership. This pins the
    /// prefiltered index against a plain `HashSet` oracle over the real shipped
    /// lexicon, then over codes that are *not* in it (including near-misses that
    /// differ by one scalar, which is where a naive low-bit filter would alias).
    #[test]
    fn prefiltered_index_agrees_with_a_plain_set_on_every_encoding() {
        let source = include_str!("../../../data/daily.tsv");
        let d = Dictionary::from_tsv(source).unwrap();

        // Every dictionary text of two to six scalars, re-encoded independently of
        // the index that was built during construction.
        let mut oracle: HashSet<u128> = HashSet::new();
        let mut probes: Vec<u128> = Vec::new();
        for id in 0..d.entry_count() as u32 {
            let text = d.entry(id).text;
            let mut code = 0u128;
            let mut count = 0;
            for c in text.chars() {
                count += 1;
                code = (code << 21) | u128::from(u32::from(c) + 1);
            }
            if (2..=6).contains(&count) {
                // The same text under several pronunciations yields the same code,
                // so membership is a set question, not a per-entry one.
                oracle.insert(code);
                probes.push(code);
            }
        }
        assert!(!oracle.is_empty());
        for &code in &probes {
            assert!(d.boundary_words.contains(code), "index lost a member");
        }

        // Near-misses: bump one scalar of a real member so the encoding is a
        // different code but adjacent in bit space. None of these may be reported
        // present unless it really is a member.
        let mut false_positives = 0usize;
        for &code in probes.iter().take(4096) {
            for delta in [1u128, 2, 1 << 21, 1 << 42] {
                let probe = code.wrapping_add(delta);
                let expected = oracle.contains(&probe);
                let actual = d.boundary_words.contains(probe);
                assert_eq!(
                    actual, expected,
                    "disagreement on perturbed code {probe:#x}"
                );
                false_positives += usize::from(actual);
            }
        }
        // A prefilter that always said "yes" would pass the loop above while
        // making the prefilter useless; require that it actually rejects.
        assert!(
            false_positives * 8 < probes.len().min(4096),
            "prefilter admits nearly everything ({false_positives} of {})",
            probes.len().min(4096) * 4
        );
        assert_eq!(d.boundary_words.filter.len() * 64, BOUNDARY_FILTER_BITS);
    }

    /// `attests_boundary` is the only caller, and it walks suffixes of `left`
    /// against prefixes of `right`. Pin it against a direct oracle so the
    /// prefilter cannot change which joins count as attested.
    #[test]
    fn boundary_attestation_matches_a_direct_encoding_oracle() {
        let source = include_str!("../../../data/daily.tsv");
        let d = Dictionary::from_tsv(source).unwrap();
        let mut oracle: HashSet<u128> = HashSet::new();
        for id in 0..d.entry_count() as u32 {
            let text = d.entry(id).text;
            let mut code = 0u128;
            let mut count = 0;
            for c in text.chars() {
                count += 1;
                code = (code << 21) | u128::from(u32::from(c) + 1);
            }
            if (2..=6).contains(&count) {
                oracle.insert(code);
            }
        }
        let direct = |left: &str, right: &str| {
            for (start, _) in left.char_indices().rev().take(3) {
                let mut code = left[start..]
                    .chars()
                    .fold(0u128, |v, c| (v << 21) | u128::from(u32::from(c) + 1));
                for c in right.chars().take(3) {
                    code = (code << 21) | u128::from(u32::from(c) + 1);
                    if oracle.contains(&code) {
                        return true;
                    }
                }
            }
            false
        };
        for (left, right) in [
            ("我", "喜欢"),
            ("你", "好"),
            ("映射", "声"),
            ("壅塞", "不通"),
            ("中", "国"),
            ("张", "三"),
            ("完全", "没有"),
            ("啊", "吧"),
            ("数据库", "查询"),
            ("孤", "舟"),
        ] {
            assert_eq!(
                d.attests_boundary(left, right),
                direct(left, right),
                "{left}+{right}"
            );
        }
    }
}
