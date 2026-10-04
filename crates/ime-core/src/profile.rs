//! Portable local selection preferences. Loading, recording and serialization
//! allocate; platform adapters call them outside the decoding hot path.
use crate::{MAX_INPUT_BYTES, MAX_TEXT_BYTES};
use std::sync::Arc;

const MAX_RECORDS: usize = 4096;
pub const MAX_PROFILE_BYTES: usize = 2 * 1024 * 1024;
#[derive(Clone, Debug)]
pub(crate) struct Preference {
    pub key: Arc<str>,
    pub text: Arc<str>,
    pub count: u32,
    pub sequence: u32,
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
impl Profile {
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
            for (n, i) in order.into_iter().enumerate() {
                self.rows[i].sequence = n as u32 + 1;
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
            }
            Err(_) => {
                if self.rows.len() == MAX_RECORDS {
                    let i = self
                        .rows
                        .iter()
                        .enumerate()
                        .min_by_key(|(_, r)| (r.sequence, r.count))
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
                        key: key.into(),
                        text: text.into(),
                        count: 1,
                        sequence: self.sequence,
                    },
                );
            }
        }
        true
    }
    pub(crate) fn matching(&self, raw: &str) -> impl Iterator<Item = (usize, &Preference)> {
        let key = raw;
        let start = self.rows.partition_point(|r| r.key.as_ref() < key);
        let end = self.rows[start..].partition_point(|r| r.key.as_ref() == key) + start;
        self.rows[start..end]
            .iter()
            .enumerate()
            .map(move |(i, r)| (start + i, r))
    }
    pub fn to_binary(&self) -> Vec<u8> {
        let mut out = Vec::new();
        out.extend_from_slice(b"MSWYUSR1");
        out.extend_from_slice(&(self.rows.len() as u32).to_le_bytes());
        out.extend_from_slice(&self.sequence.to_le_bytes());
        out.extend_from_slice(&0u32.to_le_bytes());
        for r in &self.rows {
            out.extend_from_slice(&(r.key.len() as u16).to_le_bytes());
            out.extend_from_slice(&(r.text.len() as u16).to_le_bytes());
            out.extend_from_slice(&r.count.to_le_bytes());
            out.extend_from_slice(&r.sequence.to_le_bytes());
            out.extend_from_slice(r.key.as_bytes());
            out.extend_from_slice(r.text.as_bytes());
        }
        let checksum = crate::dictionary::crc32(&out[20..]);
        out[16..20].copy_from_slice(&checksum.to_le_bytes());
        out
    }
    pub fn from_binary(bytes: &[u8]) -> Option<Self> {
        if bytes.len() < 20 || bytes.len() > MAX_PROFILE_BYTES || &bytes[..8] != b"MSWYUSR1" {
            return None;
        }
        let word = |at| u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap());
        let count = word(8) as usize;
        if count > MAX_RECORDS || word(16) != crate::dictionary::crc32(&bytes[20..]) {
            return None;
        }
        let mut result = Self {
            rows: Vec::with_capacity(count),
            sequence: word(12),
        };
        let mut at = 20usize;
        for _ in 0..count {
            let header = bytes.get(at..at + 12)?;
            let k = u16::from_le_bytes(header[0..2].try_into().ok()?) as usize;
            let t = u16::from_le_bytes(header[2..4].try_into().ok()?) as usize;
            let frequency = u32::from_le_bytes(header[4..8].try_into().ok()?);
            let sequence = u32::from_le_bytes(header[8..12].try_into().ok()?);
            at += 12;
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
                key: key.into(),
                text: text.into(),
                count: frequency,
                sequence,
            });
        }
        (at == bytes.len()).then_some(result)
    }
}
