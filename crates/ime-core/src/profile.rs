// SPDX-License-Identifier: GPL-3.0-or-later
//! Portable local selection preferences. Loading, recording and serialization
//! allocate; platform adapters call them outside the decoding hot path.
use crate::{MAX_INPUT_BYTES, MAX_TEXT_BYTES};
use std::sync::Arc;

pub const MAX_PROFILE_RECORDS: usize = 8192;
pub const MAX_PROFILE_BYTES: usize = 4 * 1024 * 1024;
#[derive(Clone, Debug)]
pub(crate) struct Preference {
    pub key: Arc<str>,
    pub pinyin: Arc<str>,
    pub text: Arc<str>,
    pub count: u32,
    pub sequence: u32,
    hits: u16,
    trials: u16,
    epoch: u32,
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn recent_hit_rate_beats_recency_when_full() {
        let mut p = Profile::default();
        for i in 0..MAX_PROFILE_RECORDS {
            p.record(
                "shi",
                &format!("词{}", char::from_u32(0x4e00 + i as u32).unwrap()),
            );
        }
        let epoch = p.sequence / 256;
        for row in &mut p.rows {
            row.hits = 10;
            row.trials = 20;
            row.epoch = epoch;
        }
        let hot = p.rows[0].text.clone();
        let cold = p.rows[1].text.clone();
        p.rows[0].hits = 30;
        p.rows[0].trials = 32;
        p.rows[0].sequence = 1;
        p.rows[1].hits = 1;
        p.rows[1].trials = 12;
        p.rows[1].sequence = p.sequence;
        assert!(p.record("xin", "新"));
        assert_eq!(p.entry_count(), 8192);
        assert!(p.rows.iter().any(|r| r.text == hot));
        assert!(!p.rows.iter().any(|r| r.text == cold));
    }
    #[test]
    fn failed_opportunities_forget_but_selected_and_fuzzy_hits_survive() {
        let mut p = Profile::default();
        p.record("shi", "士");
        for _ in 0..16 {
            assert!(p.record_selection("shi", "是", 0));
        }
        assert!(!p.rows.iter().any(|r| r.text.as_ref() == "士"));
        assert_eq!(p.rows[0].hits, 16);
        assert_eq!(p.rows[0].trials, 16);
        p.record("yingshe", "映射");
        for _ in 0..20 {
            p.record_selection("yinshe", "映射", 1 << 8);
        }
        let row = p.rows.iter().find(|r| r.key.as_ref() == "yingshe").unwrap();
        assert_eq!((row.hits, row.trials), (21, 21));
        let before = p.to_binary();
        assert!(!p.record_selection("shi", "是", 1 << 31));
        assert_eq!(before, p.to_binary());
        let restored = Profile::from_binary(&before).unwrap();
        assert_eq!(restored.to_binary(), before);
    }
    #[test]
    fn decay_and_v1_migration_preserve_validated_training_data() {
        let mut legacy = b"MSWYUSR1".to_vec();
        legacy.extend_from_slice(&1u32.to_le_bytes());
        legacy.extend_from_slice(&10u32.to_le_bytes());
        legacy.extend_from_slice(&0u32.to_le_bytes());
        legacy.extend_from_slice(&3u16.to_le_bytes());
        legacy.extend_from_slice(&3u16.to_le_bytes());
        legacy.extend_from_slice(&10u32.to_le_bytes());
        legacy.extend_from_slice(&10u32.to_le_bytes());
        legacy.extend_from_slice("shi是".as_bytes());
        let crc = crate::dictionary::crc32(&legacy[20..]);
        legacy[16..20].copy_from_slice(&crc.to_le_bytes());
        let mut p = Profile::from_binary(&legacy).unwrap();
        assert_eq!(p.rows[0].count, 10);
        assert_eq!((p.rows[0].hits, p.rows[0].trials), (0, 0));
        assert_eq!(&p.to_binary()[..8], b"MSWYUSR2");
        p.rows[0].hits = 32;
        p.rows[0].trials = 64;
        p.rows[0].epoch = 0;
        assert_eq!(p.rows[0].recent(512), (8, 16));
        let mut invalid = p.to_binary();
        invalid[32..34].copy_from_slice(&65u16.to_le_bytes());
        let crc = crate::dictionary::crc32(&invalid[20..]);
        invalid[16..20].copy_from_slice(&crc.to_le_bytes());
        assert!(Profile::from_binary(&invalid).is_none());
    }
    #[test]
    fn maximum_strings_at_8192_records_round_trip_above_old_byte_limit() {
        let mut p = Profile::default();
        let key = "shi".repeat(21);
        let prefix = "字".repeat(84);
        for i in 0..8192 {
            p.record(
                &key,
                &format!("{prefix}{}", char::from_u32(0x4e00 + i).unwrap()),
            );
        }
        let bytes = p.to_binary();
        assert!(bytes.len() > 2 * 1024 * 1024 && bytes.len() < MAX_PROFILE_BYTES);
        assert_eq!(Profile::from_binary(&bytes).unwrap().entry_count(), 8192);
    }
}
#[derive(Clone, Debug, Default)]
pub struct Profile {
    pub(crate) rows: Vec<Preference>,
    sequence: u32,
}
pub(crate) fn chinese(text: &str) -> bool {
    !text.is_empty()
        && text
            .chars()
            .all(|c| matches!(c as u32, 0x3400..=0x9fff | 0x20000..=0x3134f))
}
fn valid(key: &str, text: &str) -> bool {
    !key.is_empty()
        && key.len() <= MAX_INPUT_BYTES
        && crate::dictionary::valid_input(key)
        && !key.starts_with('\'')
        && !key.ends_with('\'')
        && text.len() <= MAX_TEXT_BYTES
        && chinese(text)
}
impl Preference {
    fn recent(&self, sequence: u32) -> (u16, u16) {
        let shift = (sequence / 256).saturating_sub(self.epoch).min(16);
        (
            self.hits.checked_shr(shift).unwrap_or(0),
            self.trials.checked_shr(shift).unwrap_or(0),
        )
    }
    fn observe(&mut self, sequence: u32, hit: bool) {
        (self.hits, self.trials) = self.recent(sequence);
        self.hits = self.hits.saturating_add(u16::from(hit));
        self.trials = self.trials.saturating_add(1);
        self.epoch = sequence / 256;
    }
}
impl Profile {
    /// Lifetime counts from v1 cannot establish the quality of unattested text:
    /// they may include the old decoder's unsupported character combinations.
    pub(crate) fn has_repeated_evidence(&self, row: &Preference) -> bool {
        row.count >= 3 && row.recent(self.sequence).0 >= 3
    }

    /// Unattested text needs repeated recent selections. Dictionary-attested
    /// legacy habits stay usable; sufficiently observed poor habits lose promotion.
    pub(crate) fn reliable(&self, row: &Preference, attested: bool) -> bool {
        let (hits, trials) = row.recent(self.sequence);
        (attested || self.has_repeated_evidence(row))
            && !(trials >= 8 && (u32::from(hits) + 1) * 2 < u32::from(trials) + 2)
    }
    pub fn entry_count(&self) -> usize {
        self.rows.len()
    }
    pub fn record(&mut self, spelling: &str, text: &str) -> bool {
        let key: String = spelling.to_ascii_lowercase();
        if !valid(&key, text) {
            return false;
        }
        if self.sequence == u32::MAX {
            let mut order: Vec<_> = (0..self.rows.len()).collect();
            order.sort_unstable_by_key(|&i| self.rows[i].sequence);
            let epoch = self.rows.len() as u32 / 256;
            for (n, i) in order.into_iter().enumerate() {
                let (hits, trials) = self.rows[i].recent(self.sequence);
                self.rows[i].hits = hits;
                self.rows[i].trials = trials;
                self.rows[i].sequence = n as u32 + 1;
                self.rows[i].epoch = epoch;
            }
            self.sequence = self.rows.len() as u32;
        }
        self.sequence += 1;
        match self
            .rows
            .binary_search_by(|r| r.key.as_ref().cmp(&key).then(r.text.as_ref().cmp(text)))
        {
            Ok(i) => {
                self.rows[i].count = self.rows[i].count.saturating_add(1);
                self.rows[i].sequence = self.sequence;
                self.rows[i].observe(self.sequence, true);
            }
            Err(_) => {
                if self.rows.len() == MAX_PROFILE_RECORDS {
                    let i = self
                        .rows
                        .iter()
                        .enumerate()
                        .min_by(|(_, a), (_, b)| {
                            let (ah, at) = a.recent(self.sequence);
                            let (bh, bt) = b.recent(self.sequence);
                            // Smoothed recent hit rate; recency only breaks ties.
                            ((u64::from(ah) + 1) * (u64::from(bt) + 2))
                                .cmp(&((u64::from(bh) + 1) * (u64::from(at) + 2)))
                                .then(a.sequence.cmp(&b.sequence))
                                .then(a.count.cmp(&b.count))
                        })
                        .unwrap()
                        .0;
                    self.rows.remove(i);
                }
                let i = self.rows.partition_point(|r| {
                    r.key.as_ref() < key.as_str()
                        || (r.key.as_ref() == key && r.text.as_ref() < text)
                });
                self.rows.insert(
                    i,
                    Preference {
                        pinyin: crate::syllables::profile_spelling(&key).into(),
                        key: key.into(),
                        text: text.into(),
                        count: 1,
                        sequence: self.sequence,
                        hits: 1,
                        trials: 1,
                        epoch: self.sequence / 256,
                    },
                );
            }
        }
        true
    }
    /// One opportunity per host-confirmed Chinese selection, never per keystroke.
    /// All eligible exact/fuzzy records are measured, including non-promoted rows.
    /// Misses with sufficient recent evidence are forgotten; raw/failed edits do not train.
    pub fn record_selection(&mut self, spelling: &str, text: &str, flags: u32) -> bool {
        if flags & !crate::fuzzy::OPTIONS_MASK != 0 || !self.record(spelling, text) {
            return false;
        }
        let key = spelling.to_ascii_lowercase();
        let mut previous = String::new();
        let mut matches = false;
        let sequence = self.sequence;
        self.rows.retain_mut(|row| {
            if row.key.as_ref() != previous {
                previous.clear();
                previous.push_str(&row.key);
                matches = row.key.as_ref() == key
                    || (flags != 0
                        && crate::fuzzy::complete_annotations(&key, &row.pinyin, flags).is_some());
            }
            if matches && !(row.key.as_ref() == key && row.text.as_ref() == text) {
                row.observe(sequence, row.text.as_ref() == text);
                if row.text.as_ref() != text
                    && row.trials >= 16
                    && u32::from(row.hits) * 10 <= u32::from(row.trials)
                {
                    return false;
                }
            }
            true
        });
        true
    }
    pub(crate) fn matching_range(&self, raw: &str) -> std::ops::Range<usize> {
        let key = raw;
        let start = self.rows.partition_point(|r| r.key.as_ref() < key);
        let end = self.rows[start..].partition_point(|r| r.key.as_ref() == key) + start;
        start..end
    }
    pub fn to_binary(&self) -> Vec<u8> {
        let mut out = Vec::new();
        out.extend_from_slice(b"MSWYUSR2");
        out.extend_from_slice(&(self.rows.len() as u32).to_le_bytes());
        out.extend_from_slice(&self.sequence.to_le_bytes());
        out.extend_from_slice(&0u32.to_le_bytes());
        for r in &self.rows {
            out.extend_from_slice(&(r.key.len() as u16).to_le_bytes());
            out.extend_from_slice(&(r.text.len() as u16).to_le_bytes());
            out.extend_from_slice(&r.count.to_le_bytes());
            out.extend_from_slice(&r.sequence.to_le_bytes());
            out.extend_from_slice(&r.hits.to_le_bytes());
            out.extend_from_slice(&r.trials.to_le_bytes());
            out.extend_from_slice(&r.epoch.to_le_bytes());
            out.extend_from_slice(r.key.as_bytes());
            out.extend_from_slice(r.text.as_bytes());
        }
        let checksum = crate::dictionary::crc32(&out[20..]);
        out[16..20].copy_from_slice(&checksum.to_le_bytes());
        out
    }
    pub fn from_binary(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < 20
            || bytes.len() > MAX_PROFILE_BYTES
            || !matches!(&bytes[..8], b"MSWYUSR1" | b"MSWYUSR2")
        {
            return None;
        }
        let version2 = &bytes[..8] == b"MSWYUSR2";
        let row_header = if version2 { 20 } else { 12 };
        let word = |at| u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap());
        let count = word(8) as usize;
        if count > MAX_PROFILE_RECORDS || word(16) != crate::dictionary::crc32(&bytes[20..]) {
            return None;
        }
        let mut result = Self {
            rows: Vec::with_capacity(count),
            sequence: word(12),
        };
        let mut at = 20usize;
        for _ in 0..count {
            let header = bytes.get(at..at + row_header)?;
            let k = u16::from_le_bytes(header[0..2].try_into().ok()?) as usize;
            let t = u16::from_le_bytes(header[2..4].try_into().ok()?) as usize;
            let frequency = u32::from_le_bytes(header[4..8].try_into().ok()?);
            let sequence = u32::from_le_bytes(header[8..12].try_into().ok()?);
            let (hits, trials, epoch) = if version2 {
                (
                    u16::from_le_bytes(header[12..14].try_into().ok()?),
                    u16::from_le_bytes(header[14..16].try_into().ok()?),
                    u32::from_le_bytes(header[16..20].try_into().ok()?),
                )
            } else {
                (0, 0, result.sequence / 256)
            };
            if hits > trials || epoch > result.sequence / 256 {
                return None;
            }
            at += row_header;
            let key = std::str::from_utf8(bytes.get(at..at + k)?).ok()?;
            at += k;
            let text = std::str::from_utf8(bytes.get(at..at + t)?).ok()?;
            at += t;
            if !valid(key, text) || frequency == 0 || sequence == 0 || sequence > result.sequence {
                return None;
            }
            if result
                .rows
                .last()
                .is_some_and(|r| (r.key.as_ref(), r.text.as_ref()) >= (key, text))
            {
                return None;
            }
            result.rows.push(Preference {
                pinyin: crate::syllables::profile_spelling(key).into(),
                key: key.into(),
                text: text.into(),
                count: frequency,
                sequence,
                hits,
                trials,
                epoch,
            });
        }
        (at == bytes.len()).then_some(result)
    }
}
