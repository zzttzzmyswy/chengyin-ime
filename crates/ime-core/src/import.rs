// SPDX-License-Identifier: GPL-3.0-or-later
//! Bounded, all-or-nothing local dictionary import. No platform I/O.
use crate::{Dictionary, DictionaryError, MAX_DICTIONARY_BYTES, MAX_PINYIN_BYTES, MAX_TEXT_BYTES};
use std::collections::BTreeMap;

fn fail(message: &'static str) -> DictionaryError {
    DictionaryError { line: 0, message }
}
fn utf16(bytes: &[u8]) -> Result<String, DictionaryError> {
    if bytes.len() % 2 != 0 {
        return Err(fail("odd UTF-16 byte length"));
    }
    let units: Vec<_> = bytes
        .chunks_exact(2)
        .map(|b| u16::from_le_bytes([b[0], b[1]]))
        .collect();
    String::from_utf16(&units).map_err(|_| fail("invalid UTF-16"))
}
struct Reader<'a> {
    bytes: &'a [u8],
    at: usize,
}
impl<'a> Reader<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8], DictionaryError> {
        let end = self
            .at
            .checked_add(n)
            .ok_or_else(|| fail("SCEL range overflow"))?;
        let b = self
            .bytes
            .get(self.at..end)
            .ok_or_else(|| fail("truncated SCEL"))?;
        self.at = end;
        Ok(b)
    }
    fn half(&mut self) -> Result<usize, DictionaryError> {
        let b = self.take(2)?;
        Ok(u16::from_le_bytes([b[0], b[1]]) as usize)
    }
    fn word(&mut self) -> Result<usize, DictionaryError> {
        let b = self.take(4)?;
        Ok(u32::from_le_bytes(b.try_into().unwrap()) as usize)
    }
}
impl Dictionary {
    /// Auto-detect MSWYDICT, classic SCEL (0x44/0x45), UTF-8/UTF-16LE TSV
    /// and Sogou text ('ni'hao 你好). Reject malformed/unsupported files atomically.
    pub fn import(bytes: &[u8]) -> Result<Self, DictionaryError> {
        if bytes.is_empty() || bytes.len() > MAX_DICTIONARY_BYTES {
            return Err(fail("invalid import size"));
        }
        if bytes.starts_with(b"MSWYDICT") {
            return Self::from_binary(bytes);
        }
        if bytes.starts_with(&[0x40, 0x15, 0, 0]) {
            return Self::from_scel(bytes);
        }
        let text = if bytes.starts_with(&[0xff, 0xfe]) {
            utf16(&bytes[2..])?
        } else {
            std::str::from_utf8(bytes)
                .map_err(|_| fail("text must be UTF-8 or UTF-16LE"))?
                .trim_start_matches('\u{feff}')
                .to_owned()
        };
        let mut rows = BTreeMap::new();
        for (line_no, line) in text.lines().enumerate() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
                continue;
            }
            let parts: Vec<_> = line.split_whitespace().collect();
            if !(2..=3).contains(&parts.len()) {
                return Err(DictionaryError {
                    line: line_no + 1,
                    message: "expected pinyin, text and optional frequency",
                });
            }
            let key = parts[0].trim_matches('\'').to_ascii_lowercase();
            let frequency = if parts.len() == 3 {
                parts[2]
                    .parse::<u32>()
                    .map_err(|_| fail("invalid frequency"))?
            } else {
                100
            };
            rows.entry((key, parts[1].to_owned()))
                .and_modify(|f: &mut u32| *f = (*f).max(frequency))
                .or_insert(frequency);
            if rows.len() > 250_000 {
                return Err(fail("too many entries"));
            }
        }
        Self::from_rows(rows)
    }
    fn from_rows(rows: BTreeMap<(String, String), u32>) -> Result<Self, DictionaryError> {
        let mut source = String::new();
        for ((key, text), frequency) in rows {
            if key.len() > MAX_PINYIN_BYTES || text.len() > MAX_TEXT_BYTES {
                return Err(fail("entry exceeds input/text limit"));
            }
            use std::fmt::Write;
            writeln!(source, "{key}\t{text}\t{frequency}").unwrap();
            if source.len() > MAX_DICTIONARY_BYTES {
                return Err(fail("merged dictionary exceeds 64 MiB"));
            }
        }
        Self::from_tsv(&source)
    }
    /// Duplicate pronunciation/text pairs keep the greater weight. Both inputs
    /// remain immutable; limits and full structure validation also apply here.
    pub fn merge(&self, extra: &Self) -> Result<Self, DictionaryError> {
        Self::merge_all(std::iter::once(self).chain(std::iter::once(extra)))
    }
    /// One-shot merge of any number of dictionaries. Merging is a flat union of
    /// rows, so building it once from every source avoids the repeated rebuild
    /// (and repeated peak) of merging pair by pair (review R12).
    pub fn merge_all<'a>(
        dictionaries: impl IntoIterator<Item = &'a Self>,
    ) -> Result<Self, DictionaryError> {
        let mut rows = BTreeMap::new();
        for dictionary in dictionaries {
            for id in 0..dictionary.entry_count() {
                let e = dictionary.entry(id as u32);
                rows.entry((e.pinyin.to_owned(), e.text.to_owned()))
                    .and_modify(|f: &mut u32| *f = (*f).max(e.frequency))
                    .or_insert(e.frequency);
                if rows.len() > 250_000 {
                    return Err(fail("merged dictionary exceeds 250000 entries"));
                }
            }
        }
        if rows.is_empty() {
            return Err(fail("merged dictionary is empty"));
        }
        Self::from_rows(rows)
    }
    pub fn from_scel(bytes: &[u8]) -> Result<Self, DictionaryError> {
        if bytes.len() > MAX_DICTIONARY_BYTES
            || bytes.len() < 0x1544
            || !bytes.starts_with(&[0x40, 0x15, 0, 0])
            || !matches!(bytes[4], 0x44 | 0x45)
            || bytes.get(5..8) != Some(&[0x43, 0x53, 0x01])
        {
            return Err(fail("unsupported SCEL header/version"));
        }
        let groups = u32::from_le_bytes(bytes[0x120..0x124].try_into().unwrap()) as usize;
        if groups > 250_000 {
            return Err(fail("too many SCEL groups"));
        }
        let mut r = Reader { bytes, at: 0x1540 };
        let count = r.word()?;
        if count == 0 || count > 2048 {
            return Err(fail("invalid SCEL pinyin table"));
        }
        let mut syllables = BTreeMap::new();
        for _ in 0..count {
            let id = r.half()?;
            let n = r.half()?;
            if n == 0 || n > 12 {
                return Err(fail("invalid SCEL syllable length"));
            }
            let text = utf16(r.take(n)?)?;
            if !text.bytes().all(|b| b.is_ascii_lowercase()) || syllables.insert(id, text).is_some()
            {
                return Err(fail("invalid/duplicate SCEL syllable"));
            }
        }
        // Classic files reserve a fixed pinyin area; newer producers pack it.
        // Use padding only when it consists entirely of zeroes. Never skip records.
        let legacy = if bytes[4] == 0x44 { 0x2628 } else { 0x26c4 };
        if r.at < legacy
            && bytes
                .get(r.at..legacy)
                .is_some_and(|b| b.iter().all(|&v| v == 0))
        {
            r.at = legacy;
        }
        let mut rows = BTreeMap::new();
        let mut read_groups = 0;
        let mut records = 0;
        while r.at < bytes.len() {
            let homophones = r.half()?;
            let n = r.half()?;
            if homophones == 0 || n == 0 || n % 2 != 0 || n > MAX_PINYIN_BYTES * 2 {
                return Err(fail("invalid SCEL word group"));
            }
            let mut key = String::new();
            for _ in 0..n / 2 {
                let id = r.half()?;
                let syllable = syllables
                    .get(&id)
                    .ok_or_else(|| fail("unknown SCEL syllable ID"))?;
                if !key.is_empty() {
                    key.push('\'');
                }
                key.push_str(syllable);
            }
            if key.len() > MAX_PINYIN_BYTES {
                return Err(fail("SCEL pinyin exceeds 255 bytes"));
            }
            for _ in 0..homophones {
                records += 1;
                if records > 250_000 {
                    return Err(fail("too many SCEL word records"));
                }
                let n = r.half()?;
                if n == 0 || n > MAX_TEXT_BYTES * 2 {
                    return Err(fail("invalid SCEL text length"));
                }
                let text = utf16(r.take(n)?)?;
                let extension = r.half()?;
                if !(2..=4096).contains(&extension) {
                    return Err(fail("invalid SCEL extension length"));
                }
                let extra = r.take(extension)?;
                let frequency = u16::from_le_bytes([extra[0], extra[1]]) as u32;
                rows.entry((key.clone(), text))
                    .and_modify(|f: &mut u32| *f = (*f).max(frequency.max(1)))
                    .or_insert(frequency.max(1));
            }
            read_groups += 1;
        }
        if groups != 0 && groups != read_groups {
            return Err(fail("SCEL group count mismatch"));
        }
        Self::from_rows(rows)
    }
}
