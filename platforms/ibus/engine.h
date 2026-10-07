#ifndef MYSWY_IBUS_ENGINE_H
#define MYSWY_IBUS_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include "myswy_ime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Engine core: session lifetime, the post-process COMMIT contract, the
 * two-step myswy_session_text protocol, page/candidate snapshots and the
 * preedit cursor conversion. It holds no D-Bus state, so the CTests drive it
 * directly; ibus_engine.c only translates IBus calls into these.
 *
 * Key symbols are IBus keyvals because a private key enum would need a mapping
 * table of its own to test, and the caller already has them.
 */

typedef struct MyswyEngine MyswyEngine;

/* One lookup-table row of the current page. Strings are borrowed and valid
 * only for the duration of the callback. */
typedef struct {
    const char *text;
    const char *pinyin;
    int consumed;
} MyswyEngineRow;

/* Implemented by the caller. Every callback runs on the input thread. */
typedef struct {
    void *user;
    void (*on_commit)(void *user, const char *utf8);
    void (*on_preedit)(void *user, const char *utf8, int cursor_chars, int visible);
    void (*on_candidates)(void *user, const MyswyEngineRow *rows, int count, int selected, int association);
    void (*on_auxiliary)(void *user, const char *utf8);
} MyswyEngineSink;

MyswyEngine *myswy_engine_new(const MyswyEngineSink *sink);
void myswy_engine_free(MyswyEngine *engine);

/* Maps an IBus keyval plus its modifier state onto the C ABI key/modifier pair.
 * Returns 1 when the key belongs to the core, 0 for a release event, which the
 * host must handle itself. Both outputs are written when it returns 1. */
int myswy_engine_map_key(uint32_t keyval, uint32_t state, uint32_t *key, uint32_t *modifiers);

/* Sends one key to the core and publishes the result. Returns 1 when the key
 * was consumed (the host must not forward it), 0 when it belongs to the host.
 * A COMMIT is read after EVERY process call, including one that reports the key
 * as unhandled, because the core commits pending text while forwarding the key
 * (ASCII punctuation does exactly this). */
int myswy_engine_process_key(MyswyEngine *engine, uint32_t keyval, uint32_t state);

/* Page and cursor actions the panel can trigger without a key event. */
void myswy_engine_page(MyswyEngine *engine, int forward);
void myswy_engine_move(MyswyEngine *engine, int up);

/* Candidate click; index is relative to the current page, 0-based. */
void myswy_engine_select(MyswyEngine *engine, size_t index);

/* Each input context owns one engine instance, so a context switch is a reset. */
void myswy_engine_reset(MyswyEngine *engine);
void myswy_engine_set_sensitive(MyswyEngine *engine, int sensitive);
int myswy_engine_sensitive(const MyswyEngine *engine);

/* The session, so tests can assert against the core directly. */
const MyswySession *myswy_engine_session(const MyswyEngine *engine);

/* The two-step text protocol. myswy_session_text is a size query when the
 * buffer is NULL: it returns the required capacity INCLUDING the NUL, and a
 * short buffer writes NOTHING rather than truncating (no half UTF-8). Callers
 * must therefore ask before copying; a fixed MYSWY_MAX_TEXT_BYTES buffer would
 * silently drop a composition that outgrows it.
 *
 * Returns the length in bytes excluding the NUL, 0 for an empty field, or -1
 * when the index is invalid or the text does not fit `capacity` (in which case
 * `out` is left empty rather than truncated). */
int myswy_engine_read_text(const MyswySession *session, uint32_t field, size_t index,
                           char *out, size_t capacity);

/* Characters that begin before `offset_bytes` in `utf8`. The core reports the
 * preedit cursor as a UTF-8 byte offset; IBus counts characters. */
int myswy_engine_utf8_offset_chars(const char *utf8, size_t offset_bytes);

#ifdef __cplusplus
}
#endif
#endif
