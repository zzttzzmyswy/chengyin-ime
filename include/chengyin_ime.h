#ifndef CHENGYIN_IME_H
#define CHENGYIN_IME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI v1, UTF-8 throughout. Session handles require external serialization.
 * Use one session per input context. Dictionaries can be shared read-only.
 * Non-null pointers must be valid and correctly aligned; every allocation
 * is freed exactly once by its matching free function. free(NULL) is safe.
 * All buffers must be initialized/readable or writable for the stated size.
 * Do not alias output buffers with handles or free a handle during an API call.
 * No Rust unwind crosses the ABI; invalid pointer use is still undefined.
 * Allocation failure can abort the process. This API is not a sandbox.
 */
typedef struct ChengyinDictionary ChengyinDictionary;
typedef struct ChengyinSession ChengyinSession;
typedef struct ChengyinProfile ChengyinProfile;

enum {
    CHENGYIN_ABI_VERSION = 1,
    CHENGYIN_MAX_TEXT_BYTES = 256,
    CHENGYIN_MAX_INPUT_BYTES = 63,
    CHENGYIN_HANDLED = 1,
    CHENGYIN_LIMITED = 2,
    CHENGYIN_INVALID = -1,
    CHENGYIN_PANIC = -2,
    CHENGYIN_BUSY = -3,
    CHENGYIN_MOD_CONTROL = 1,
    CHENGYIN_MOD_ALT = 2,
    CHENGYIN_MOD_SUPER = 4,
    CHENGYIN_MOD_RELEASE = 8,
    CHENGYIN_KEY_SPACE = 0x110000,
    CHENGYIN_KEY_BACKSPACE,
    CHENGYIN_KEY_ESCAPE,
    CHENGYIN_KEY_ENTER,
    CHENGYIN_KEY_UP,
    CHENGYIN_KEY_DOWN,
    CHENGYIN_KEY_LEFT,
    CHENGYIN_KEY_RIGHT,
    CHENGYIN_KEY_HOME,
    CHENGYIN_KEY_END,
    CHENGYIN_KEY_DELETE,
    CHENGYIN_KEY_PAGE_UP,
    CHENGYIN_KEY_PAGE_DOWN,
    CHENGYIN_KEY_TAB,
    CHENGYIN_KEY_SELECT_1 = 0x110010, /* add 0..8 */
    CHENGYIN_TEXT_PREEDIT = 0,
    CHENGYIN_TEXT_COMMIT = 1,
    CHENGYIN_TEXT_CANDIDATE = 2,
    CHENGYIN_TEXT_CANDIDATE_PINYIN = 3,
    CHENGYIN_TEXT_DISPLAY_PREEDIT = 4,
    CHENGYIN_TEXT_LEARNING_KEY = 5
};

uint32_t chengyin_ime_abi_version(void);
/* Owned handle to the shared demo dictionary; free with dictionary_free. */
ChengyinDictionary *chengyin_dictionary_new_demo(void);
/* Copies and validates TSV. Returns NULL for invalid UTF-8/data/size or panic. */
ChengyinDictionary *chengyin_dictionary_new_tsv(const uint8_t *data, size_t length);
/* Additive API: copies and validates v1/v2 binary, including structure/CRC. */
ChengyinDictionary *chengyin_dictionary_new_binary(const uint8_t *data, size_t length);
/* Settings path only; accepts classic SCEL 0x44/0x45, binary, UTF-8 or
 * UTF-16LE TSV / Sogou text. All-or-nothing validation; NULL on any error.
 * These functions allocate and must not run on the input key thread.
 */
ChengyinDictionary *chengyin_dictionary_new_import(const uint8_t *data, size_t length);
ChengyinDictionary *chengyin_dictionary_merge(const ChengyinDictionary *base, const ChengyinDictionary *extra);
/* Additive API (ABI v1): one-shot union of `count` live immutable handles.
 * Compiles the union once instead of rebuilding after every pair, keeping the
 * peak bounded when a library holds many vocabularies. `count` is 1..=64; NULL
 * is returned unless every handle is live and the union validates. Allocates. */
ChengyinDictionary *chengyin_dictionary_merge_all(const ChengyinDictionary *const *dictionaries, size_t count);
int32_t chengyin_dictionary_entry_count(const ChengyinDictionary *dictionary);
/* Required binary size (no NUL), or negative error. NULL/small output writes
 * nothing. Copies a deterministic v2 binary when capacity suffices. Allocates. */
int32_t chengyin_dictionary_binary(const ChengyinDictionary *, uint8_t *buffer, size_t capacity);
/* Owned reference to the same immutable data; no data copy. */
ChengyinDictionary *chengyin_dictionary_clone(const ChengyinDictionary *dictionary);
void chengyin_dictionary_free(ChengyinDictionary *dictionary);
/* Local user preferences, ABI v1 additive. Bounded to 8192 spelling/text pairs,
 * 4 MiB, checksummed. Reads MSWYUSR1 and MSWYUSR2, writes MSWYUSR2. Recording/export
 * allocate; callers serialize mutation and run disk persistence outside decoding.
 * Session keeps an immutable snapshot; freeing the profile handle is then safe.
 * Sensitive contexts MUST never train. No platform I/O occurs in this library. */
ChengyinProfile *chengyin_profile_new(const uint8_t *data, size_t length);
void chengyin_profile_free(ChengyinProfile *profile);
int32_t chengyin_profile_record(ChengyinProfile *, const uint8_t *key, size_t key_length,
                             const uint8_t *text, size_t text_length);
/* Host-confirmed Chinese selection feedback, including matching opportunities.
 * Same ownership as record; allocates, no I/O. Invalid flags do not mutate. */
int32_t chengyin_profile_record_selection(ChengyinProfile *, const uint8_t *key, size_t key_length,
                                     const uint8_t *text, size_t text_length, uint32_t matching_flags);
int32_t chengyin_profile_count(const ChengyinProfile *);
/* Required bytes, no NUL; NULL/small output writes nothing. */
int32_t chengyin_profile_binary(const ChengyinProfile *, uint8_t *output, size_t capacity);
/* Idle session only; BUSY leaves state unchanged. */
int32_t chengyin_session_set_profile(ChengyinSession *, const ChengyinProfile *);
/* Owned immutable snapshot; allocates a handle, free with profile_free. */
ChengyinProfile *chengyin_session_profile(const ChengyinSession *);
/* page_size=1..9; flags bit0=learning, bit1=association, other bits invalid. */
int32_t chengyin_session_configure(ChengyinSession *, uint32_t page_size, uint32_t flags);
/* Optional matching, idle only; default 0. Unknown bits INVALID, active BUSY;
 * both leave composition/commit unchanged. Phonetic rules are symmetric. */
enum ChengyinMatching {
    CHENGYIN_FUZZY_ZH_Z=1u<<0, CHENGYIN_FUZZY_CH_C=1u<<1, CHENGYIN_FUZZY_SH_S=1u<<2,
    CHENGYIN_FUZZY_N_L=1u<<3, CHENGYIN_FUZZY_F_H=1u<<4, CHENGYIN_FUZZY_L_R=1u<<5,
    CHENGYIN_FUZZY_AN_ANG=1u<<6, CHENGYIN_FUZZY_EN_ENG=1u<<7, CHENGYIN_FUZZY_IN_ING=1u<<8,
    CHENGYIN_FUZZY_IAN_IANG=1u<<9, CHENGYIN_FUZZY_UAN_UANG=1u<<10,
    CHENGYIN_CORRECT_SWAP=1u<<16, CHENGYIN_CORRECT_OMIT=1u<<17,
    CHENGYIN_CORRECT_NEIGHBOR=1u<<18, CHENGYIN_CORRECT_REPEAT=1u<<19,
    CHENGYIN_MATCHING_MASK=0x000f07ffu
};
int32_t chengyin_session_configure_matching(ChengyinSession *, uint32_t flags);
/* Additive opt-in, idle only: 0=legacy staged segments (default), 1=immediate
 * prefix commit plus remaining preedit. Unknown values INVALID, active BUSY.
 * After a prefix selection COMMIT and PREEDIT can BOTH be nonempty: host must
 * accept COMMIT outside the remaining composition, then acknowledge learning. */
int32_t chengyin_session_configure_incremental(ChengyinSession *, uint32_t enabled);
/* One 0/1 annotation per canonical ASCII pinyin byte, including unmarked '.
 * Required byte count, no NUL; NULL/small output writes nothing; invalid index
 * returns INVALID. Mutation-free and allocation-free, same lifetime as text. */
int32_t chengyin_session_candidate_marks(const ChengyinSession *, size_t index, uint8_t *, size_t capacity);
/* Call once only AFTER successful host SetText, before next process/reset.
 * Learns accepted Chinese selections in memory; returns 1/0, or error. Allocates.
 * LEARNING_KEY + COMMIT can also be copied to a platform's background writer. */
int32_t chengyin_session_learn_commit(ChengyinSession *);
/* new() uses the small bundled demo dictionary, initialized once per library. */
ChengyinSession *chengyin_session_new(void);
/* Session retains a dictionary reference; original handle can then be freed. */
ChengyinSession *chengyin_session_new_with_dictionary(const ChengyinDictionary *dictionary);
/* ABI v1 additive API: switch only while preedit is empty; returns 0 or BUSY.
 * BUSY leaves all state unchanged. Success preserves the last event's commit.
 * Buffers are reused. Retain an old dictionary handle outside the input thread
 * if destruction of its last reference would exceed your latency budget.
 */
int32_t chengyin_session_set_dictionary(ChengyinSession *session, const ChengyinDictionary *dictionary);
void chengyin_session_free(ChengyinSession *session);
int32_t chengyin_session_reset(ChengyinSession *session);
/* Key: Unicode scalar or CHENGYIN_KEY_*; map physical space/enter explicitly.
 * Modifier bits are above; Shift is represented in the Unicode scalar.
 * Return >=0: flags; <0: error. On panic discard/reset this session.
 * Read commit exactly once after EVERY successful process, including an
 * unhandled result. Commit text must reach the host before forwarding the key.
 * Commit persists until the next process/reset; getters do not consume it.
 */
int32_t chengyin_session_process(ChengyinSession *session, uint32_t key, uint32_t modifiers);
/* Idle continuation list: preedit is empty, consumed=0. Only TAB, explicit
 * SELECT/click, navigation and Escape consume a key; ordinary Space, digits,
 * punctuation and editing keys dismiss it and pass to the host. Reset drops
 * context and session-local recency. The core performs no file I/O. Persistent preferences require explicit profile export and host-confirmed learning. */
int32_t chengyin_session_is_association(const ChengyinSession *session);
int32_t chengyin_session_candidate_count(const ChengyinSession *session);
/* Additive getters, no mutation/allocation. Pages are zero-based, each <=9 rows.
 * Candidate indexes are relative to the CURRENT page. Cursor is a UTF-8 byte
 * offset at a character boundary. consumed is the pending ASCII input span;
 * in default mode a prefix selection keeps the remainder in preedit with an
 * empty commit; incremental mode commits the prefix and rematches the remainder.
 * Page enumeration is lazy. Work/result budget exhaustion is explicit.
 */
/* Highlight only, preserves commit/preedit; returns INVALID for an absent row. */
int32_t chengyin_session_set_selected(ChengyinSession *session, size_t index);
int32_t chengyin_session_page(const ChengyinSession *session);
int32_t chengyin_session_has_next_page(const ChengyinSession *session);
int32_t chengyin_session_preedit_cursor(const ChengyinSession *session);
int32_t chengyin_session_candidate_consumed(const ChengyinSession *session, size_t index);
int32_t chengyin_session_budget_limited(const ChengyinSession *session);
/* -1 when there are no candidates. */
int32_t chengyin_session_selected(const ChengyinSession *session);
/* Returns required capacity INCLUDING NUL, or <0 on invalid handle/field/index.
 * NULL buffer is a size query. If capacity is too small, NOTHING is written.
 * No partially truncated UTF-8. Getters do not allocate or mutate session.
 * For preedit/commit/display/learning key, index is ignored. Original preedit
 * and cursor offsets remain raw. Display-only text inserts syllable separators;
 * its maximum required capacity is 320, other text fields remain <=257.
 */
int32_t chengyin_session_text(const ChengyinSession *session, uint32_t field, size_t index,
                         uint8_t *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
