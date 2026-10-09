// SPDX-License-Identifier: GPL-3.0-or-later
//! Per-session bounded adaptive history-query cache. No shared mutable state.
#[derive(Clone, Copy, Default)]
pub(crate) struct HistorySelection {
    pub exact: [u32; 4],
    pub recalled: [u32; 4],
    pub exact_count: [u8; 2],
    pub recalled_count: [u8; 2],
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn full_key_flags_text_budget_and_snapshot_are_part_of_cache_identity() {
        let mut c = HistoryCache::default();
        assert!(c.get("shi", 0, 256).is_none());
        c.put("shi", 0, 256, HistorySelection::default());
        assert!(c.get("shi", 0, 256).is_some());
        assert!(c.get("shi", 1, 256).is_none());
        assert!(c.get("shi", 0, 255).is_none());
        assert!(c.get("sh", 0, 256).is_none());
        c.invalidate();
        assert!(c.get("shi", 0, 256).is_none());
    }
}
#[derive(Clone, Copy)]
struct Entry {
    key: [u8; 64],
    len: u8,
    remaining: u16,
    flags: u32,
    frequency: u16,
    stamp: u32,
    selection: HistorySelection,
}
impl Default for Entry {
    fn default() -> Self {
        Self {
            key: [0; 64],
            len: 0,
            remaining: 0,
            flags: 0,
            frequency: 0,
            stamp: 0,
            selection: HistorySelection::default(),
        }
    }
}
#[derive(Clone, Copy, Debug, Default)]
pub struct HistoryCacheStats {
    pub hits: u64,
    pub misses: u64,
    pub capacity: usize,
    pub entries: usize,
}
pub(crate) struct HistoryCache {
    entries: [Entry; 16],
    ghosts: [u64; 8],
    ghost_at: usize,
    active: usize,
    clock: u32,
    requests: u8,
    window_hits: u8,
    hits: u64,
    misses: u64,
}
impl Default for HistoryCache {
    fn default() -> Self {
        Self {
            entries: [Entry::default(); 16],
            ghosts: [0; 8],
            ghost_at: 0,
            active: 4,
            clock: 0,
            requests: 0,
            window_hits: 0,
            hits: 0,
            misses: 0,
        }
    }
}
fn hash(key: &[u8], flags: u32, remaining: u16) -> u64 {
    key.iter().fold(
        0xcbf29ce484222325 ^ u64::from(flags) ^ (u64::from(remaining) << 32),
        |h, &b| (h ^ u64::from(b)).wrapping_mul(0x100000001b3),
    )
}
impl HistoryCache {
    pub fn invalidate(&mut self) {
        // Row indices belong to one immutable snapshot only.
        self.entries.fill(Entry::default());
        self.ghosts.fill(0);
        self.requests = 0;
        self.window_hits = 0;
    }
    pub fn stats(&self) -> HistoryCacheStats {
        HistoryCacheStats {
            hits: self.hits,
            misses: self.misses,
            capacity: self.active,
            entries: self.entries[..self.active]
                .iter()
                .filter(|e| e.frequency > 0)
                .count(),
        }
    }
    pub fn get(&mut self, key: &str, flags: u32, remaining: usize) -> Option<HistorySelection> {
        self.clock = self.clock.wrapping_add(1);
        self.requests += 1;
        let entry = self.entries[..self.active].iter_mut().find(|e| {
            e.frequency > 0
                && usize::from(e.len) == key.len()
                && &e.key[..key.len()] == key.as_bytes()
                && e.flags == flags
                && usize::from(e.remaining) == remaining
        });
        let result = entry.map(|e| {
            e.frequency = e.frequency.saturating_add(1);
            e.stamp = self.clock;
            e.selection
        });
        if result.is_some() {
            self.hits += 1;
            self.window_hits += 1;
        } else {
            self.misses += 1;
            // Repeated evicted queries indicate the working set exceeds capacity.
            // Ghost hashes only tune size; cached results always compare full keys.
            if self
                .ghosts
                .contains(&hash(key.as_bytes(), flags, remaining as u16))
            {
                self.active = (self.active + 4).min(16);
            }
        }
        if self.requests == 32 {
            if self.window_hits >= 8 {
                self.active = (self.active + 4).min(16);
            } else if self.window_hits < 4 {
                let next = self.active.saturating_sub(2).max(4);
                self.entries[next..].fill(Entry::default());
                self.active = next;
            }
            for e in &mut self.entries {
                if e.frequency > 0 {
                    e.frequency = (e.frequency / 2).max(1);
                }
            }
            self.requests = 0;
            self.window_hits = 0;
        }
        result
    }
    pub fn put(&mut self, key: &str, flags: u32, remaining: usize, selection: HistorySelection) {
        let at = self.entries[..self.active]
            .iter()
            .enumerate()
            .min_by_key(|(_, e)| (e.frequency, e.stamp))
            .unwrap()
            .0;
        let old = self.entries[at];
        if old.frequency > 0 {
            self.ghosts[self.ghost_at] =
                hash(&old.key[..old.len as usize], old.flags, old.remaining);
            self.ghost_at = (self.ghost_at + 1) % self.ghosts.len();
        }
        let mut entry = Entry {
            len: key.len() as u8,
            flags,
            remaining: remaining as u16,
            frequency: 1,
            stamp: self.clock,
            selection,
            ..Entry::default()
        };
        entry.key[..key.len()].copy_from_slice(key.as_bytes());
        self.entries[at] = entry;
    }
}
