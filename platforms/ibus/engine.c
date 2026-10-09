// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine.h"
#include <ibus.h>
#include <stdlib.h>
#include <string.h>

#define CHENGYIN_ROWS 9
/* Every field except the display-only preedit is documented as <=257 bytes
 * including the NUL, so one page worth of rows fits without allocation. */
#define CHENGYIN_TEXT_CAPACITY (CHENGYIN_MAX_TEXT_BYTES + 1)

struct ChengyinEngine {
    ChengyinSession *session;
    ChengyinEngineSink sink;
    int sensitive;
    /* Reused across keys: this runs on the input thread. */
    ChengyinEngineRow rows[CHENGYIN_ROWS];
    char rowText[CHENGYIN_ROWS][CHENGYIN_TEXT_CAPACITY];
    char rowPinyin[CHENGYIN_ROWS][CHENGYIN_TEXT_CAPACITY];
};

int chengyin_engine_utf8_offset_chars(const char *utf8, size_t offset_bytes) {
    if (!utf8) { return 0; }
    int characters = 0;
    for (size_t i = 0; i < offset_bytes && utf8[i] != '\0'; ++i) {
        if (((unsigned char)utf8[i] & 0xC0) != 0x80) { ++characters; }
    }
    return characters;
}

int chengyin_engine_read_text(const ChengyinSession *session, uint32_t field, size_t index,
                           char *out, size_t capacity) {
    if (!session || !out || capacity == 0) { return -1; }
    out[0] = '\0';
    /* Step one of the protocol: the required capacity INCLUDING the NUL. */
    const int32_t required = chengyin_session_text(session, field, index, NULL, 0);
    if (required <= 0) { return -1; }
    if ((size_t)required > capacity) {
        /* Step two would write NOTHING, so a short buffer must be reported
         * rather than passed on as an empty or half-written value. */
        return -1;
    }
    const int32_t written = chengyin_session_text(session, field, index, (uint8_t *)out, capacity);
    if (written <= 0) {
        out[0] = '\0';
        return -1;
    }
    return written - 1; /* drop the NUL from the reported length */
}

static uint32_t map_modifiers(uint32_t state) {
    uint32_t modifiers = 0;
    if (state & IBUS_CONTROL_MASK) { modifiers |= CHENGYIN_MOD_CONTROL; }
    if (state & IBUS_MOD1_MASK) { modifiers |= CHENGYIN_MOD_ALT; }
    /* Super reaches the engine as either the dedicated mask or Mod4 depending on
     * the XKB layout, and Meta/Hyper behave the same way for shortcuts. */
    if (state & (IBUS_SUPER_MASK | IBUS_META_MASK | IBUS_HYPER_MASK | IBUS_MOD4_MASK)) {
        modifiers |= CHENGYIN_MOD_SUPER;
    }
    return modifiers;
}

int chengyin_engine_map_key(uint32_t keyval, uint32_t state, uint32_t *key, uint32_t *modifiers) {
    if (!key || !modifiers) { return 0; }
    if (state & IBUS_RELEASE_MASK) { return 0; }
    *modifiers = map_modifiers(state);
    switch (keyval) {
    case IBUS_KEY_Tab: *key = CHENGYIN_KEY_TAB; return 1;
    case IBUS_KEY_space: *key = CHENGYIN_KEY_SPACE; return 1;
    case IBUS_KEY_BackSpace: *key = CHENGYIN_KEY_BACKSPACE; return 1;
    case IBUS_KEY_Escape: *key = CHENGYIN_KEY_ESCAPE; return 1;
    case IBUS_KEY_Return:
    case IBUS_KEY_KP_Enter: *key = CHENGYIN_KEY_ENTER; return 1;
    case IBUS_KEY_Up: *key = CHENGYIN_KEY_UP; return 1;
    case IBUS_KEY_Down: *key = CHENGYIN_KEY_DOWN; return 1;
    case IBUS_KEY_Left: *key = CHENGYIN_KEY_LEFT; return 1;
    case IBUS_KEY_Right: *key = CHENGYIN_KEY_RIGHT; return 1;
    case IBUS_KEY_Home: *key = CHENGYIN_KEY_HOME; return 1;
    case IBUS_KEY_End: *key = CHENGYIN_KEY_END; return 1;
    case IBUS_KEY_Delete: *key = CHENGYIN_KEY_DELETE; return 1;
    case IBUS_KEY_Page_Up:
    case IBUS_KEY_minus: *key = CHENGYIN_KEY_PAGE_UP; return 1;
    case IBUS_KEY_Page_Down:
    case IBUS_KEY_equal: *key = CHENGYIN_KEY_PAGE_DOWN; return 1;
    default: break;
    }
    /* Space and Enter are keyvals too, but the ABI wants the physical key
     * constants: handing over the raw scalar leaves them unhandled. Shift is
     * already folded into the keyval, so no modifier flag is needed for it. */
    const gunichar unicode = ibus_keyval_to_unicode(keyval);
    if (unicode == 0) { return 0; }
    *key = (uint32_t)unicode;
    return 1;
}

static void publish_preedit(ChengyinEngine *engine) {
    char preedit[CHENGYIN_TEXT_CAPACITY];
    const int length = chengyin_engine_read_text(engine->session, CHENGYIN_TEXT_PREEDIT, 0,
                                              preedit, sizeof preedit);
    int cursor = chengyin_session_preedit_cursor(engine->session);
    if (cursor < 0) { cursor = 0; }
    /* The core reports the cursor as a UTF-8 byte offset into this same raw
     * preedit, so the character count is what IBus needs. */
    engine->sink.on_preedit(engine->sink.user, preedit,
                            chengyin_engine_utf8_offset_chars(preedit, (size_t)cursor), length > 0);
}

static void publish_candidates(ChengyinEngine *engine) {
    int count = chengyin_session_candidate_count(engine->session);
    if (count < 0) { count = 0; }
    if (count > CHENGYIN_ROWS) { count = CHENGYIN_ROWS; }
    for (int i = 0; i < count; ++i) {
        if (chengyin_engine_read_text(engine->session, CHENGYIN_TEXT_CANDIDATE, (size_t)i,
                                   engine->rowText[i], sizeof engine->rowText[i]) < 0) {
            engine->rowText[i][0] = '\0';
        }
        if (chengyin_engine_read_text(engine->session, CHENGYIN_TEXT_CANDIDATE_PINYIN, (size_t)i,
                                   engine->rowPinyin[i], sizeof engine->rowPinyin[i]) < 0) {
            engine->rowPinyin[i][0] = '\0';
        }
        engine->rows[i].text = engine->rowText[i];
        engine->rows[i].pinyin = engine->rowPinyin[i];
        engine->rows[i].consumed = chengyin_session_candidate_consumed(engine->session, (size_t)i);
    }
    int selected = count > 0 ? chengyin_session_selected(engine->session) : -1;
    if (selected < 0 || selected >= count) { selected = 0; }
    const int association = chengyin_session_is_association(engine->session) > 0;
    engine->sink.on_candidates(engine->sink.user, engine->rows, count, count > 0 ? selected : -1,
                               association);
    /* The auxiliary bar annotates the highlighted row with the spelling it would
     * consume; an association list has no spelling, so it says how to accept. */
    if (count > 0) {
        engine->sink.on_auxiliary(engine->sink.user, engine->rowPinyin[selected]);
    } else if (association) {
        engine->sink.on_auxiliary(engine->sink.user, "联想 · Tab / 鼠标确认");
    } else {
        engine->sink.on_auxiliary(engine->sink.user, "");
    }
}

/* Publishes a pending COMMIT. This must run after EVERY process call, including
 * one that reports the key as unhandled: the core commits the pending candidate
 * and forwards the key on the same call (ASCII punctuation does exactly that),
 * and the commit is dropped by the next process or reset. */
static void publish_commit(ChengyinEngine *engine, int32_t flags) {
    char commit[CHENGYIN_TEXT_CAPACITY];
    if (chengyin_engine_read_text(engine->session, CHENGYIN_TEXT_COMMIT, 0,
                              commit, sizeof commit) <= 0) {
        return;
    }
    engine->sink.on_commit(engine->sink.user, commit);
    /* Learning is host-confirmed and only follows a selection the core handled.
     * A key the core forwarded (punctuation) commits the pending candidate but
     * is not a selection, so it must not train. */
    if (flags & CHENGYIN_HANDLED) {
        char spelling[CHENGYIN_TEXT_CAPACITY];
        if (chengyin_engine_read_text(engine->session, CHENGYIN_TEXT_LEARNING_KEY, 0,
                                   spelling, sizeof spelling) > 0) {
            chengyin_session_learn_commit(engine->session);
        }
    }
}

static void publish_all(ChengyinEngine *engine) {
    publish_preedit(engine);
    publish_candidates(engine);
}

static void clear(ChengyinEngine *engine) {
    chengyin_session_reset(engine->session);
    publish_all(engine);
}

int chengyin_engine_process_key(ChengyinEngine *engine, uint32_t keyval, uint32_t state) {
    if (!engine) { return 0; }
    if (state & IBUS_RELEASE_MASK) { return 0; }
    /* A sensitive field never composes and never trains; the key goes to the host. */
    if (engine->sensitive) {
        clear(engine);
        return 0;
    }
    uint32_t key = 0;
    uint32_t modifiers = 0;
    if (!chengyin_engine_map_key(keyval, state, &key, &modifiers)) { return 0; }
    const int32_t flags = chengyin_session_process(engine->session, key, modifiers);
    if (flags < 0) {
        /* The ABI contract says to discard the session after a failure. */
        clear(engine);
        return 0;
    }
    publish_commit(engine, flags);
    publish_all(engine);
    return (flags & CHENGYIN_HANDLED) ? 1 : 0;
}

void chengyin_engine_page(ChengyinEngine *engine, int forward) {
    if (!engine || engine->sensitive) { return; }
    if (chengyin_session_candidate_count(engine->session) <= 0) { return; }
    const int32_t flags = chengyin_session_process(engine->session,
                                                forward ? CHENGYIN_KEY_PAGE_DOWN : CHENGYIN_KEY_PAGE_UP, 0);
    if (flags < 0) { clear(engine); return; }
    publish_commit(engine, flags);
    publish_all(engine);
}

void chengyin_engine_move(ChengyinEngine *engine, int up) {
    if (!engine || engine->sensitive) { return; }
    if (chengyin_session_candidate_count(engine->session) <= 0) { return; }
    const int32_t flags = chengyin_session_process(engine->session,
                                                up ? CHENGYIN_KEY_UP : CHENGYIN_KEY_DOWN, 0);
    if (flags < 0) { clear(engine); return; }
    publish_commit(engine, flags);
    publish_all(engine);
}

void chengyin_engine_select(ChengyinEngine *engine, size_t index) {
    if (!engine || engine->sensitive) { return; }
    const int count = chengyin_session_candidate_count(engine->session);
    if (index >= (size_t)CHENGYIN_ROWS || (int)index >= count) { return; }
    const int32_t flags = chengyin_session_process(engine->session,
                                                CHENGYIN_KEY_SELECT_1 + (uint32_t)index, 0);
    if (flags < 0) { clear(engine); return; }
    publish_commit(engine, flags);
    publish_all(engine);
}

void chengyin_engine_reset(ChengyinEngine *engine) {
    if (!engine) { return; }
    clear(engine);
}

void chengyin_engine_set_sensitive(ChengyinEngine *engine, int sensitive) {
    if (!engine) { return; }
    engine->sensitive = sensitive ? 1 : 0;
    clear(engine);
}

int chengyin_engine_sensitive(const ChengyinEngine *engine) { return engine ? engine->sensitive : 0; }

const ChengyinSession *chengyin_engine_session(const ChengyinEngine *engine) {
    return engine ? engine->session : NULL;
}

ChengyinEngine *chengyin_engine_new(const ChengyinEngineSink *sink) {
    if (!sink || !sink->on_commit || !sink->on_preedit || !sink->on_candidates || !sink->on_auxiliary) {
        return NULL;
    }
    if (chengyin_ime_abi_version() != CHENGYIN_ABI_VERSION) { return NULL; }
    ChengyinEngine *engine = (ChengyinEngine *)calloc(1, sizeof(ChengyinEngine));
    if (!engine) { return NULL; }
    engine->sink = *sink;
    engine->session = chengyin_session_new();
    if (!engine->session) {
        free(engine);
        return NULL;
    }
    /* Page size 9 with learning on, matching the Windows and Fcitx platforms. */
    if (chengyin_session_configure(engine->session, CHENGYIN_ROWS, 1) != 0) {
        chengyin_session_free(engine->session);
        free(engine);
        return NULL;
    }
    return engine;
}

void chengyin_engine_free(ChengyinEngine *engine) {
    if (!engine) { return; }
    chengyin_session_free(engine->session);
    free(engine);
}
