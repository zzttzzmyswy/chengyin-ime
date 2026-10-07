#include "engine.h"
#include <ibus.h>
#include <stdlib.h>
#include <string.h>

#define MYSWY_ROWS 9
/* Every field except the display-only preedit is documented as <=257 bytes
 * including the NUL, so one page worth of rows fits without allocation. */
#define MYSWY_TEXT_CAPACITY (MYSWY_MAX_TEXT_BYTES + 1)

struct MyswyEngine {
    MyswySession *session;
    MyswyEngineSink sink;
    int sensitive;
    /* Reused across keys: this runs on the input thread. */
    MyswyEngineRow rows[MYSWY_ROWS];
    char rowText[MYSWY_ROWS][MYSWY_TEXT_CAPACITY];
    char rowPinyin[MYSWY_ROWS][MYSWY_TEXT_CAPACITY];
};

int myswy_engine_utf8_offset_chars(const char *utf8, size_t offset_bytes) {
    if (!utf8) { return 0; }
    int characters = 0;
    for (size_t i = 0; i < offset_bytes && utf8[i] != '\0'; ++i) {
        if (((unsigned char)utf8[i] & 0xC0) != 0x80) { ++characters; }
    }
    return characters;
}

int myswy_engine_read_text(const MyswySession *session, uint32_t field, size_t index,
                           char *out, size_t capacity) {
    if (!session || !out || capacity == 0) { return -1; }
    out[0] = '\0';
    /* Step one of the protocol: the required capacity INCLUDING the NUL. */
    const int32_t required = myswy_session_text(session, field, index, NULL, 0);
    if (required <= 0) { return -1; }
    if ((size_t)required > capacity) {
        /* Step two would write NOTHING, so a short buffer must be reported
         * rather than passed on as an empty or half-written value. */
        return -1;
    }
    const int32_t written = myswy_session_text(session, field, index, (uint8_t *)out, capacity);
    if (written <= 0) {
        out[0] = '\0';
        return -1;
    }
    return written - 1; /* drop the NUL from the reported length */
}

static uint32_t map_modifiers(uint32_t state) {
    uint32_t modifiers = 0;
    if (state & IBUS_CONTROL_MASK) { modifiers |= MYSWY_MOD_CONTROL; }
    if (state & IBUS_MOD1_MASK) { modifiers |= MYSWY_MOD_ALT; }
    /* Super reaches the engine as either the dedicated mask or Mod4 depending on
     * the XKB layout, and Meta/Hyper behave the same way for shortcuts. */
    if (state & (IBUS_SUPER_MASK | IBUS_META_MASK | IBUS_HYPER_MASK | IBUS_MOD4_MASK)) {
        modifiers |= MYSWY_MOD_SUPER;
    }
    return modifiers;
}

int myswy_engine_map_key(uint32_t keyval, uint32_t state, uint32_t *key, uint32_t *modifiers) {
    if (!key || !modifiers) { return 0; }
    if (state & IBUS_RELEASE_MASK) { return 0; }
    *modifiers = map_modifiers(state);
    switch (keyval) {
    case IBUS_KEY_Tab: *key = MYSWY_KEY_TAB; return 1;
    case IBUS_KEY_space: *key = MYSWY_KEY_SPACE; return 1;
    case IBUS_KEY_BackSpace: *key = MYSWY_KEY_BACKSPACE; return 1;
    case IBUS_KEY_Escape: *key = MYSWY_KEY_ESCAPE; return 1;
    case IBUS_KEY_Return:
    case IBUS_KEY_KP_Enter: *key = MYSWY_KEY_ENTER; return 1;
    case IBUS_KEY_Up: *key = MYSWY_KEY_UP; return 1;
    case IBUS_KEY_Down: *key = MYSWY_KEY_DOWN; return 1;
    case IBUS_KEY_Left: *key = MYSWY_KEY_LEFT; return 1;
    case IBUS_KEY_Right: *key = MYSWY_KEY_RIGHT; return 1;
    case IBUS_KEY_Home: *key = MYSWY_KEY_HOME; return 1;
    case IBUS_KEY_End: *key = MYSWY_KEY_END; return 1;
    case IBUS_KEY_Delete: *key = MYSWY_KEY_DELETE; return 1;
    case IBUS_KEY_Page_Up:
    case IBUS_KEY_minus: *key = MYSWY_KEY_PAGE_UP; return 1;
    case IBUS_KEY_Page_Down:
    case IBUS_KEY_equal: *key = MYSWY_KEY_PAGE_DOWN; return 1;
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

static void publish_preedit(MyswyEngine *engine) {
    char preedit[MYSWY_TEXT_CAPACITY];
    const int length = myswy_engine_read_text(engine->session, MYSWY_TEXT_PREEDIT, 0,
                                              preedit, sizeof preedit);
    int cursor = myswy_session_preedit_cursor(engine->session);
    if (cursor < 0) { cursor = 0; }
    /* The core reports the cursor as a UTF-8 byte offset into this same raw
     * preedit, so the character count is what IBus needs. */
    engine->sink.on_preedit(engine->sink.user, preedit,
                            myswy_engine_utf8_offset_chars(preedit, (size_t)cursor), length > 0);
}

static void publish_candidates(MyswyEngine *engine) {
    int count = myswy_session_candidate_count(engine->session);
    if (count < 0) { count = 0; }
    if (count > MYSWY_ROWS) { count = MYSWY_ROWS; }
    for (int i = 0; i < count; ++i) {
        if (myswy_engine_read_text(engine->session, MYSWY_TEXT_CANDIDATE, (size_t)i,
                                   engine->rowText[i], sizeof engine->rowText[i]) < 0) {
            engine->rowText[i][0] = '\0';
        }
        if (myswy_engine_read_text(engine->session, MYSWY_TEXT_CANDIDATE_PINYIN, (size_t)i,
                                   engine->rowPinyin[i], sizeof engine->rowPinyin[i]) < 0) {
            engine->rowPinyin[i][0] = '\0';
        }
        engine->rows[i].text = engine->rowText[i];
        engine->rows[i].pinyin = engine->rowPinyin[i];
        engine->rows[i].consumed = myswy_session_candidate_consumed(engine->session, (size_t)i);
    }
    int selected = count > 0 ? myswy_session_selected(engine->session) : -1;
    if (selected < 0 || selected >= count) { selected = 0; }
    const int association = myswy_session_is_association(engine->session) > 0;
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
static void publish_commit(MyswyEngine *engine, int32_t flags) {
    char commit[MYSWY_TEXT_CAPACITY];
    if (myswy_engine_read_text(engine->session, MYSWY_TEXT_COMMIT, 0,
                              commit, sizeof commit) <= 0) {
        return;
    }
    engine->sink.on_commit(engine->sink.user, commit);
    /* Learning is host-confirmed and only follows a selection the core handled.
     * A key the core forwarded (punctuation) commits the pending candidate but
     * is not a selection, so it must not train. */
    if (flags & MYSWY_HANDLED) {
        char spelling[MYSWY_TEXT_CAPACITY];
        if (myswy_engine_read_text(engine->session, MYSWY_TEXT_LEARNING_KEY, 0,
                                   spelling, sizeof spelling) > 0) {
            myswy_session_learn_commit(engine->session);
        }
    }
}

static void publish_all(MyswyEngine *engine) {
    publish_preedit(engine);
    publish_candidates(engine);
}

static void clear(MyswyEngine *engine) {
    myswy_session_reset(engine->session);
    publish_all(engine);
}

int myswy_engine_process_key(MyswyEngine *engine, uint32_t keyval, uint32_t state) {
    if (!engine) { return 0; }
    if (state & IBUS_RELEASE_MASK) { return 0; }
    /* A sensitive field never composes and never trains; the key goes to the host. */
    if (engine->sensitive) {
        clear(engine);
        return 0;
    }
    uint32_t key = 0;
    uint32_t modifiers = 0;
    if (!myswy_engine_map_key(keyval, state, &key, &modifiers)) { return 0; }
    const int32_t flags = myswy_session_process(engine->session, key, modifiers);
    if (flags < 0) {
        /* The ABI contract says to discard the session after a failure. */
        clear(engine);
        return 0;
    }
    publish_commit(engine, flags);
    publish_all(engine);
    return (flags & MYSWY_HANDLED) ? 1 : 0;
}

void myswy_engine_page(MyswyEngine *engine, int forward) {
    if (!engine || engine->sensitive) { return; }
    if (myswy_session_candidate_count(engine->session) <= 0) { return; }
    const int32_t flags = myswy_session_process(engine->session,
                                                forward ? MYSWY_KEY_PAGE_DOWN : MYSWY_KEY_PAGE_UP, 0);
    if (flags < 0) { clear(engine); return; }
    publish_commit(engine, flags);
    publish_all(engine);
}

void myswy_engine_move(MyswyEngine *engine, int up) {
    if (!engine || engine->sensitive) { return; }
    if (myswy_session_candidate_count(engine->session) <= 0) { return; }
    const int32_t flags = myswy_session_process(engine->session,
                                                up ? MYSWY_KEY_UP : MYSWY_KEY_DOWN, 0);
    if (flags < 0) { clear(engine); return; }
    publish_commit(engine, flags);
    publish_all(engine);
}

void myswy_engine_select(MyswyEngine *engine, size_t index) {
    if (!engine || engine->sensitive) { return; }
    const int count = myswy_session_candidate_count(engine->session);
    if (index >= (size_t)MYSWY_ROWS || (int)index >= count) { return; }
    const int32_t flags = myswy_session_process(engine->session,
                                                MYSWY_KEY_SELECT_1 + (uint32_t)index, 0);
    if (flags < 0) { clear(engine); return; }
    publish_commit(engine, flags);
    publish_all(engine);
}

void myswy_engine_reset(MyswyEngine *engine) {
    if (!engine) { return; }
    clear(engine);
}

void myswy_engine_set_sensitive(MyswyEngine *engine, int sensitive) {
    if (!engine) { return; }
    engine->sensitive = sensitive ? 1 : 0;
    clear(engine);
}

int myswy_engine_sensitive(const MyswyEngine *engine) { return engine ? engine->sensitive : 0; }

const MyswySession *myswy_engine_session(const MyswyEngine *engine) {
    return engine ? engine->session : NULL;
}

MyswyEngine *myswy_engine_new(const MyswyEngineSink *sink) {
    if (!sink || !sink->on_commit || !sink->on_preedit || !sink->on_candidates || !sink->on_auxiliary) {
        return NULL;
    }
    if (myswy_ime_abi_version() != MYSWY_ABI_VERSION) { return NULL; }
    MyswyEngine *engine = (MyswyEngine *)calloc(1, sizeof(MyswyEngine));
    if (!engine) { return NULL; }
    engine->sink = *sink;
    engine->session = myswy_session_new();
    if (!engine->session) {
        free(engine);
        return NULL;
    }
    /* Page size 9 with learning on, matching the Windows and Fcitx platforms. */
    if (myswy_session_configure(engine->session, MYSWY_ROWS, 1) != 0) {
        myswy_session_free(engine->session);
        free(engine);
        return NULL;
    }
    return engine;
}

void myswy_engine_free(MyswyEngine *engine) {
    if (!engine) { return; }
    myswy_session_free(engine->session);
    free(engine);
}
