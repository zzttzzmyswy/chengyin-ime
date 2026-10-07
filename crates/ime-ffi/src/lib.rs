//! Versioned C ABI. Pointer validity, exclusivity and lifetime are caller contracts.
#![deny(unsafe_op_in_unsafe_fn)]
#![allow(clippy::missing_safety_doc)] // The complete caller contract is in include/chengyin_ime.h.

use chengyin_core::{
    demo_dictionary, Dictionary, Key, Modifiers, Profile, Session, MAX_DICTIONARY_BYTES,
    MAX_PROFILE_BYTES,
};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::sync::Arc;

pub struct ChengyinDictionary(Arc<Dictionary>);
pub struct ChengyinSession(Session);
pub struct ChengyinProfile(Arc<Profile>);

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_profile(
    session: *const ChengyinSession,
) -> *mut ChengyinProfile {
    catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: caller retains the session and serializes all access.
        unsafe { session.as_ref() }.map_or(ptr::null_mut(), |s| {
            Box::into_raw(Box::new(ChengyinProfile(s.0.profile())))
        })
    }))
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_profile_new(
    data: *const u8,
    length: usize,
) -> *mut ChengyinProfile {
    if length > MAX_PROFILE_BYTES || (length > 0 && data.is_null()) {
        return ptr::null_mut();
    }
    catch_unwind(|| {
        let profile = if length == 0 {
            Some(Profile::default())
        } else {
            // SAFETY: caller provides initialized readable bytes for length.
            Profile::from_binary(unsafe { std::slice::from_raw_parts(data, length) })
        };
        profile.map_or(ptr::null_mut(), |p| {
            Box::into_raw(Box::new(ChengyinProfile(Arc::new(p))))
        })
    })
    .unwrap_or(ptr::null_mut())
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_profile_free(profile: *mut ChengyinProfile) {
    if !profile.is_null() {
        // SAFETY: caller transfers a live uniquely owned handle, once.
        drop(unsafe { Box::from_raw(profile) });
    }
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_profile_record(
    profile: *mut ChengyinProfile,
    key: *const u8,
    key_len: usize,
    text: *const u8,
    text_len: usize,
) -> i32 {
    guard(|| {
        if profile.is_null() || key.is_null() || text.is_null() || key_len > 63 || text_len > 256 {
            return INVALID;
        }
        // SAFETY: caller provides exclusive profile and readable bounded strings.
        let (profile, key, text) = unsafe {
            (
                &mut *profile,
                std::slice::from_raw_parts(key, key_len),
                std::slice::from_raw_parts(text, text_len),
            )
        };
        let (Ok(key), Ok(text)) = (std::str::from_utf8(key), std::str::from_utf8(text)) else {
            return INVALID;
        };
        if Arc::make_mut(&mut profile.0).record(key, text) {
            0
        } else {
            INVALID
        }
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_profile_record_selection(
    profile: *mut ChengyinProfile,
    key: *const u8,
    key_len: usize,
    text: *const u8,
    text_len: usize,
    flags: u32,
) -> i32 {
    guard(|| {
        if profile.is_null()
            || key.is_null()
            || text.is_null()
            || key_len > 63
            || text_len > 256
            || flags & !chengyin_core::fuzzy::OPTIONS_MASK != 0
        {
            return INVALID;
        }
        // SAFETY: caller grants exclusive profile access and bounded readable strings.
        let (profile, key, text) = unsafe {
            (
                &mut *profile,
                std::slice::from_raw_parts(key, key_len),
                std::slice::from_raw_parts(text, text_len),
            )
        };
        let (Ok(key), Ok(text)) = (std::str::from_utf8(key), std::str::from_utf8(text)) else {
            return INVALID;
        };
        if Arc::make_mut(&mut profile.0).record_selection(key, text, flags) {
            0
        } else {
            INVALID
        }
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_profile_count(profile: *const ChengyinProfile) -> i32 {
    guard(|| unsafe { profile.as_ref() }.map_or(INVALID, |p| p.0.entry_count() as i32))
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_profile_binary(
    profile: *const ChengyinProfile,
    output: *mut u8,
    capacity: usize,
) -> i32 {
    guard(|| {
        // SAFETY: externally retained immutable profile.
        let Some(profile) = (unsafe { profile.as_ref() }) else {
            return INVALID;
        };
        let bytes = profile.0.to_binary();
        if !output.is_null() && capacity >= bytes.len() {
            // SAFETY: caller supplies nonoverlapping writable capacity bytes.
            unsafe {
                ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
            }
        }
        bytes.len() as i32
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_session_set_profile(
    session: *mut ChengyinSession,
    profile: *const ChengyinProfile,
) -> i32 {
    guard(|| {
        // SAFETY: live exclusive session and retained immutable profile.
        let (Some(session), Some(profile)) =
            (unsafe { session.as_mut() }, unsafe { profile.as_ref() })
        else {
            return INVALID;
        };
        if session.0.set_profile(Arc::clone(&profile.0)) {
            0
        } else {
            BUSY
        }
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_session_configure(
    session: *mut ChengyinSession,
    page_size: u32,
    flags: u32,
) -> i32 {
    guard(|| {
        if !(1..=9).contains(&page_size) || flags & !3 != 0 {
            return INVALID;
        }
        // SAFETY: caller serializes exclusive access to the live session.
        let Some(session) = (unsafe { session.as_mut() }) else {
            return INVALID;
        };
        if session
            .0
            .configure(page_size as usize, flags & 1 != 0, flags & 2 != 0)
        {
            0
        } else {
            BUSY
        }
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_session_learn_commit(session: *mut ChengyinSession) -> i32 {
    guard(|| {
        // SAFETY: live exclusive session; host has already accepted its commit.
        unsafe { session.as_mut() }.map_or(INVALID, |s| i32::from(s.0.learn_commit()))
    })
}

const INVALID: i32 = -1;
const PANIC: i32 = -2;
const BUSY: i32 = -3;

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_configure_matching(
    session: *mut ChengyinSession,
    flags: u32,
) -> i32 {
    guard(|| {
        if flags & !chengyin_core::fuzzy::OPTIONS_MASK != 0 {
            return INVALID;
        }
        // SAFETY: exclusive access to a live session, or null.
        let Some(s) = (unsafe { session.as_mut() }) else {
            return INVALID;
        };
        if s.0.configure_matching(flags) {
            0
        } else {
            BUSY
        }
    })
}

/// Enable immediate prefix commits while idle. Default is legacy staged mode.
/// # Safety
/// `session` must be null or exclusively borrowed from a live session handle.
#[no_mangle]
pub unsafe extern "C" fn chengyin_session_configure_incremental(
    session: *mut ChengyinSession,
    enabled: u32,
) -> i32 {
    guard(|| {
        if enabled > 1 {
            return INVALID;
        }
        let Some(s) = (unsafe { session.as_mut() }) else {
            return INVALID;
        };
        if s.0.configure_incremental(enabled != 0) {
            0
        } else {
            BUSY
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_candidate_marks(
    session: *const ChengyinSession,
    index: usize,
    buffer: *mut u8,
    capacity: usize,
) -> i32 {
    guard(|| {
        // SAFETY: externally serialized live session, or null.
        let Some(s) = (unsafe { session.as_ref() }) else {
            return INVALID;
        };
        let (Some(candidate), Some(marks)) = (s.0.candidate(index), s.0.candidate_marks(index))
        else {
            return INVALID;
        };
        let size = candidate.pinyin.len();
        if !buffer.is_null() && capacity >= size {
            // SAFETY: caller supplies capacity writable bytes; ASCII spelling uses byte indices.
            let output = unsafe { std::slice::from_raw_parts_mut(buffer, size) };
            for (at, value) in output.iter_mut().enumerate() {
                *value = u8::from(marks[at / 64] & (1 << (at % 64)) != 0);
            }
        }
        size as i32
    })
}

fn guard(action: impl FnOnce() -> i32) -> i32 {
    catch_unwind(AssertUnwindSafe(action)).unwrap_or(PANIC)
}

#[no_mangle]
pub extern "C" fn chengyin_ime_abi_version() -> u32 {
    1
}

#[no_mangle]
pub extern "C" fn chengyin_dictionary_new_demo() -> *mut ChengyinDictionary {
    catch_unwind(|| Box::into_raw(Box::new(ChengyinDictionary(demo_dictionary()))))
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_dictionary_new_tsv(
    data: *const u8,
    length: usize,
) -> *mut ChengyinDictionary {
    if data.is_null() || length == 0 || length > MAX_DICTIONARY_BYTES {
        return ptr::null_mut();
    }
    catch_unwind(|| {
        // SAFETY: caller provides a readable, initialized region of length bytes.
        let bytes = unsafe { std::slice::from_raw_parts(data, length) };
        let dictionary = std::str::from_utf8(bytes)
            .ok()
            .and_then(|s| Dictionary::from_tsv(s).ok());
        dictionary.map_or(ptr::null_mut(), |d| {
            Box::into_raw(Box::new(ChengyinDictionary(Arc::new(d))))
        })
    })
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_dictionary_new_binary(
    data: *const u8,
    length: usize,
) -> *mut ChengyinDictionary {
    if data.is_null() || length == 0 || length > MAX_DICTIONARY_BYTES {
        return ptr::null_mut();
    }
    catch_unwind(|| {
        // SAFETY: caller supplies initialized readable bytes for this length.
        let bytes = unsafe { std::slice::from_raw_parts(data, length) };
        Dictionary::from_binary(bytes)
            .ok()
            .map_or(ptr::null_mut(), |d| {
                Box::into_raw(Box::new(ChengyinDictionary(Arc::new(d))))
            })
    })
    .unwrap_or(ptr::null_mut())
}

/// Settings/CLI path only: bounded auto-detection and complete validation.
#[no_mangle]
pub unsafe extern "C" fn chengyin_dictionary_new_import(
    data: *const u8,
    length: usize,
) -> *mut ChengyinDictionary {
    if data.is_null() || length == 0 || length > MAX_DICTIONARY_BYTES {
        return ptr::null_mut();
    }
    catch_unwind(|| {
        // SAFETY: caller supplies a readable initialized byte buffer of length.
        let bytes = unsafe { std::slice::from_raw_parts(data, length) };
        Dictionary::import(bytes).ok().map_or(ptr::null_mut(), |d| {
            Box::into_raw(Box::new(ChengyinDictionary(Arc::new(d))))
        })
    })
    .unwrap_or(ptr::null_mut())
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_dictionary_merge(
    base: *const ChengyinDictionary,
    extra: *const ChengyinDictionary,
) -> *mut ChengyinDictionary {
    if base.is_null() || extra.is_null() {
        return ptr::null_mut();
    }
    catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: caller retains both immutable live handles for this call.
        let (base, extra) = unsafe { (&*base, &*extra) };
        base.0.merge(&extra.0).ok().map_or(ptr::null_mut(), |d| {
            Box::into_raw(Box::new(ChengyinDictionary(Arc::new(d))))
        })
    }))
    .unwrap_or(ptr::null_mut())
}
/// One-shot merge of several immutable dictionaries, so a library of many
/// vocabularies compiles once instead of rebuilding after every pair. NULL is
/// returned unless every entry is live and the union validates.
#[no_mangle]
pub unsafe extern "C" fn chengyin_dictionary_merge_all(
    dictionaries: *const *const ChengyinDictionary,
    count: usize,
) -> *mut ChengyinDictionary {
    if dictionaries.is_null() || count == 0 || count > 64 {
        return ptr::null_mut();
    }
    catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: the caller supplies `count` live immutable handles.
        let handles = unsafe { std::slice::from_raw_parts(dictionaries, count) };
        let mut borrowed = Vec::with_capacity(count);
        for &handle in handles {
            match unsafe { handle.as_ref() } {
                Some(dictionary) => borrowed.push(dictionary.0.as_ref()),
                None => return ptr::null_mut(),
            }
        }
        Dictionary::merge_all(borrowed)
            .ok()
            .map_or(ptr::null_mut(), |d| {
                Box::into_raw(Box::new(ChengyinDictionary(Arc::new(d))))
            })
    }))
    .unwrap_or(ptr::null_mut())
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_dictionary_entry_count(
    dictionary: *const ChengyinDictionary,
) -> i32 {
    guard(|| {
        // SAFETY: immutable live handle or null, externally retained.
        unsafe { dictionary.as_ref() }.map_or(INVALID, |d| d.0.entry_count() as i32)
    })
}
/// This exporter allocates; never call it from the key path.
#[no_mangle]
pub unsafe extern "C" fn chengyin_dictionary_binary(
    dictionary: *const ChengyinDictionary,
    buffer: *mut u8,
    capacity: usize,
) -> i32 {
    guard(|| {
        // SAFETY: immutable live handle or null, externally retained.
        let Some(d) = (unsafe { dictionary.as_ref() }) else {
            return INVALID;
        };
        let bytes = d.0.to_binary();
        if bytes.len() > MAX_DICTIONARY_BYTES {
            return INVALID;
        }
        if !buffer.is_null() && capacity >= bytes.len() {
            // SAFETY: caller supplies writable initialized capacity, disjoint from d.
            unsafe {
                ptr::copy_nonoverlapping(bytes.as_ptr(), buffer, bytes.len());
            }
        }
        bytes.len() as i32
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_dictionary_free(dictionary: *mut ChengyinDictionary) {
    if !dictionary.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| {
            // SAFETY: caller transfers a live handle from new_tsv exactly once.
            drop(unsafe { Box::from_raw(dictionary) });
        }));
    }
}

#[no_mangle]
pub extern "C" fn chengyin_session_new() -> *mut ChengyinSession {
    catch_unwind(|| Box::into_raw(Box::new(ChengyinSession(Session::new(demo_dictionary())))))
        .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_new_with_dictionary(
    dictionary: *const ChengyinDictionary,
) -> *mut ChengyinSession {
    if dictionary.is_null() {
        return ptr::null_mut();
    }
    catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: live immutable dictionary handle, not concurrently freed.
        let dictionary = unsafe { &*dictionary };
        Box::into_raw(Box::new(ChengyinSession(Session::new(Arc::clone(
            &dictionary.0,
        )))))
    }))
    .unwrap_or(ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_free(session: *mut ChengyinSession) {
    if !session.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| {
            // SAFETY: caller transfers a live session exactly once, without aliases.
            drop(unsafe { Box::from_raw(session) });
        }));
    }
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_set_dictionary(
    session: *mut ChengyinSession,
    dictionary: *const ChengyinDictionary,
) -> i32 {
    guard(|| {
        // SAFETY: caller provides exclusive session and live immutable dictionary,
        // neither concurrently freed; null handles are rejected.
        let Some(session) = (unsafe { session.as_mut() }) else {
            return INVALID;
        };
        let Some(dictionary) = (unsafe { dictionary.as_ref() }) else {
            return INVALID;
        };
        if session.0.set_dictionary(Arc::clone(&dictionary.0)) {
            0
        } else {
            BUSY
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_reset(session: *mut ChengyinSession) -> i32 {
    guard(|| {
        // SAFETY: caller provides exclusive access to a live handle or null.
        let Some(session) = (unsafe { session.as_mut() }) else {
            return INVALID;
        };
        session.0.reset();
        0
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_process(
    session: *mut ChengyinSession,
    key: u32,
    modifiers: u32,
) -> i32 {
    guard(|| {
        // SAFETY: caller provides exclusive access to a live handle or null.
        let Some(session) = (unsafe { session.as_mut() }) else {
            return INVALID;
        };
        let key = match key {
            0x110000 => Key::Space,
            0x110001 => Key::Backspace,
            0x110002 => Key::Escape,
            0x110003 => Key::Enter,
            0x110004 => Key::Up,
            0x110005 => Key::Down,
            0x110006 => Key::Left,
            0x110007 => Key::Right,
            0x110008 => Key::Home,
            0x110009 => Key::End,
            0x11000a => Key::Delete,
            0x11000b => Key::PageUp,
            0x11000c => Key::PageDown,
            0x11000d => Key::Tab,
            0x110010..=0x110018 => Key::Select((key - 0x110010) as usize),
            // Unknown keys are deliberately forwarded to the host.
            _ => char::from_u32(key).map_or(Key::Other, Key::Character),
        };
        // Unknown modifier bits / release events must never trigger composition.
        let result = session.0.process(
            key,
            Modifiers {
                control: modifiers & 1 != 0 || modifiers & !7 != 0,
                alt: modifiers & 2 != 0,
                super_key: modifiers & 4 != 0,
            },
        );
        i32::from(result.handled) | (i32::from(result.limited) << 1)
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_is_association(session: *const ChengyinSession) -> i32 {
    guard(|| {
        // SAFETY: caller retains a live session with external serialization.
        unsafe { session.as_ref() }.map_or(INVALID, |s| i32::from(s.0.is_association()))
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_candidate_count(session: *const ChengyinSession) -> i32 {
    guard(|| {
        // SAFETY: caller provides a live handle with no concurrent mutation, or null.
        unsafe { session.as_ref() }.map_or(INVALID, |s| s.0.candidate_count() as i32)
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_selected(session: *const ChengyinSession) -> i32 {
    guard(|| {
        // SAFETY: caller provides a live handle with no concurrent mutation, or null.
        unsafe { session.as_ref() }.map_or(INVALID, |s| {
            if s.0.candidate_count() == 0 {
                INVALID
            } else {
                s.0.selected() as i32
            }
        })
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_text(
    session: *const ChengyinSession,
    field: u32,
    index: usize,
    buffer: *mut u8,
    capacity: usize,
) -> i32 {
    guard(|| {
        // SAFETY: caller provides a live handle with no concurrent mutation, or null.
        let Some(session) = (unsafe { session.as_ref() }) else {
            return INVALID;
        };
        let mut display = [0u8; chengyin_core::MAX_TEXT_BYTES + chengyin_core::MAX_INPUT_BYTES];
        let value = match field {
            0 => session.0.preedit(),
            1 => session.0.commit(),
            2 => match session.0.candidate(index) {
                Some(c) => c.text,
                None => return INVALID,
            },
            3 => match session.0.candidate(index) {
                Some(c) => c.pinyin,
                None => return INVALID,
            },
            4 => {
                let length = session.0.display_preedit(&mut display);
                std::str::from_utf8(&display[..length])
                    .expect("display consists of validated UTF-8")
            }
            5 => session.0.learning_key(),
            _ => return INVALID,
        };
        let required = value.len() + 1;
        if !buffer.is_null() && capacity >= required {
            // SAFETY: caller provides writable capacity bytes, nonoverlapping with session.
            unsafe {
                ptr::copy_nonoverlapping(value.as_ptr(), buffer, value.len());
                buffer.add(value.len()).write(0);
            }
        }
        required as i32
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_page(session: *const ChengyinSession) -> i32 {
    guard(|| {
        // SAFETY: caller provides a live serialized handle or null.
        unsafe { session.as_ref() }.map_or(INVALID, |s| s.0.page() as i32)
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_session_has_next_page(session: *const ChengyinSession) -> i32 {
    guard(|| {
        // SAFETY: caller provides a live serialized handle or null.
        unsafe { session.as_ref() }.map_or(INVALID, |s| i32::from(s.0.has_next_page()))
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_session_preedit_cursor(session: *const ChengyinSession) -> i32 {
    guard(|| {
        // SAFETY: caller provides a live serialized handle or null.
        unsafe { session.as_ref() }.map_or(INVALID, |s| s.0.preedit_cursor() as i32)
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_session_candidate_consumed(
    session: *const ChengyinSession,
    index: usize,
) -> i32 {
    guard(|| {
        // SAFETY: caller provides a live serialized handle or null.
        unsafe { session.as_ref() }
            .and_then(|s| s.0.candidate_consumed(index))
            .map_or(INVALID, |n| n as i32)
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_session_budget_limited(session: *const ChengyinSession) -> i32 {
    guard(|| {
        // SAFETY: caller provides a live serialized handle or null.
        unsafe { session.as_ref() }.map_or(INVALID, |s| i32::from(s.0.budget_limited()))
    })
}

#[no_mangle]
pub unsafe extern "C" fn chengyin_session_set_selected(
    session: *mut ChengyinSession,
    index: usize,
) -> i32 {
    guard(|| {
        // SAFETY: caller provides exclusive access to a live serialized handle.
        let Some(s) = (unsafe { session.as_mut() }) else {
            return INVALID;
        };
        if s.0.set_selected(index) {
            0
        } else {
            INVALID
        }
    })
}
#[no_mangle]
pub unsafe extern "C" fn chengyin_dictionary_clone(
    dictionary: *const ChengyinDictionary,
) -> *mut ChengyinDictionary {
    catch_unwind(|| {
        // SAFETY: caller supplies a live immutable dictionary or null.
        unsafe { dictionary.as_ref() }.map_or(ptr::null_mut(), |d| {
            Box::into_raw(Box::new(ChengyinDictionary(Arc::clone(&d.0))))
        })
    })
    .unwrap_or(ptr::null_mut())
}
