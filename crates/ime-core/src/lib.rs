//! Platform-independent full-pinyin prototype. No I/O, threads or runtime dependencies.
#![forbid(unsafe_code)]

mod decoder;
mod dictionary;
pub mod fuzzy;
mod import;
mod language;
mod profile;
mod profile_cache;
mod session;

pub use dictionary::{
    Candidate, CandidateCursor, Candidates, Dictionary, DictionaryError, LookupError,
    MAX_ACTIVE_STATES, MAX_CANDIDATES, MAX_DICTIONARY_BYTES, MAX_INPUT_BYTES, MAX_PINYIN_BYTES,
    MAX_TEXT_BYTES,
};
pub use profile::{Profile, MAX_PROFILE_BYTES, MAX_PROFILE_RECORDS};
pub use profile_cache::HistoryCacheStats;
pub use session::{Key, Modifiers, ProcessResult, Session};

use std::sync::{Arc, OnceLock};

/// Shared demonstration data; intentionally not a production vocabulary.
pub fn demo_dictionary() -> Arc<Dictionary> {
    static DICTIONARY: OnceLock<Arc<Dictionary>> = OnceLock::new();
    Arc::clone(DICTIONARY.get_or_init(|| {
        Arc::new(
            Dictionary::from_tsv(include_str!("../../../data/demo.tsv"))
                .expect("bundled demo dictionary is validated by tests"),
        )
    }))
}

mod syllables;
