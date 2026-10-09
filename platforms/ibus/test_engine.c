// SPDX-License-Identifier: GPL-3.0-or-later
/* A-level tests for the IBus engine core: no daemon, no D-Bus, no display.
 *
 * Covers the four behaviours the task card calls out: IBus key mapping, the
 * unconditional post-process COMMIT read, the two-step chengyin_session_text
 * protocol, and the UTF-8 preedit cursor conversion. test_negative.c carries the
 * differential control showing the COMMIT assertions can actually fail.
 */
#include <assert.h>
#include <ibus.h>
#include <stdio.h>
#include <string.h>
#include "engine.h"

typedef struct {
    char commit[8][512];
    int commits;
    char preedit[512];
    int cursor_chars;
    int preedit_visible;
    char rows[9][512];
    int row_count;
    int selected;
    int association;
    char auxiliary[512];
} Recorder;

static Recorder recorder;

static void reset_recorder(void) { memset(&recorder, 0, sizeof recorder); }

static void on_commit(void *user, const char *utf8) {
    (void)user;
    assert(recorder.commits < 8);
    snprintf(recorder.commit[recorder.commits], sizeof recorder.commit[0], "%s", utf8);
    ++recorder.commits;
}

static void on_preedit(void *user, const char *utf8, int cursor_chars, int visible) {
    (void)user;
    snprintf(recorder.preedit, sizeof recorder.preedit, "%s", utf8);
    recorder.cursor_chars = cursor_chars;
    recorder.preedit_visible = visible;
}

static void on_candidates(void *user, const ChengyinEngineRow *rows, int count, int selected,
                          int association) {
    (void)user;
    recorder.row_count = count;
    recorder.selected = selected;
    recorder.association = association;
    for (int i = 0; i < count && i < 9; ++i) {
        snprintf(recorder.rows[i], sizeof recorder.rows[i], "%s", rows[i].text);
    }
}

static void on_auxiliary(void *user, const char *utf8) {
    (void)user;
    snprintf(recorder.auxiliary, sizeof recorder.auxiliary, "%s", utf8);
}

static ChengyinEngine *make_engine(void) {
    reset_recorder();
    ChengyinEngineSink sink = {NULL, on_commit, on_preedit, on_candidates, on_auxiliary};
    ChengyinEngine *engine = chengyin_engine_new(&sink);
    assert(engine);
    return engine;
}

static void type(ChengyinEngine *engine, const char *ascii) {
    for (const char *p = ascii; *p; ++p) {
        chengyin_engine_process_key(engine, (uint32_t)(unsigned char)*p, 0);
    }
}

/* --- key mapping --------------------------------------------------------- */

static void test_key_mapping(void) {
    /* Space and Enter must become the ABI's physical key constants. Passing the
     * raw Unicode scalar through leaves the core treating them as text, so this
     * is the difference between " " selecting a candidate and " " being inert. */
    uint32_t key = 0, modifiers = 0;
    assert(chengyin_engine_map_key(IBUS_KEY_space, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_SPACE && modifiers == 0);
    assert(chengyin_engine_map_key(IBUS_KEY_Return, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_ENTER);
    assert(chengyin_engine_map_key(IBUS_KEY_KP_Enter, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_ENTER);

    assert(chengyin_engine_map_key(IBUS_KEY_BackSpace, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_BACKSPACE);
    assert(chengyin_engine_map_key(IBUS_KEY_Escape, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_ESCAPE);
    assert(chengyin_engine_map_key(IBUS_KEY_Tab, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_TAB);
    assert(chengyin_engine_map_key(IBUS_KEY_Up, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_UP);
    assert(chengyin_engine_map_key(IBUS_KEY_Down, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_DOWN);
    assert(chengyin_engine_map_key(IBUS_KEY_Left, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_LEFT);
    assert(chengyin_engine_map_key(IBUS_KEY_Right, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_RIGHT);
    assert(chengyin_engine_map_key(IBUS_KEY_Home, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_HOME);
    assert(chengyin_engine_map_key(IBUS_KEY_End, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_END);
    assert(chengyin_engine_map_key(IBUS_KEY_Delete, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_DELETE);
    assert(chengyin_engine_map_key(IBUS_KEY_Page_Up, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_PAGE_UP);
    assert(chengyin_engine_map_key(IBUS_KEY_Page_Down, 0, &key, &modifiers) == 1);
    assert(key == CHENGYIN_KEY_PAGE_DOWN);

    /* ASCII letters keep their scalar; Shift is already folded into the keyval
     * (IBUS_KEY_A is 'A'), so no modifier flag is invented for it. */
    assert(chengyin_engine_map_key(IBUS_KEY_a, 0, &key, &modifiers) == 1);
    assert(key == 'a' && modifiers == 0);
    assert(chengyin_engine_map_key(IBUS_KEY_A, IBUS_SHIFT_MASK, &key, &modifiers) == 1);
    assert(key == 'A' && modifiers == 0);

    /* Modifiers reach the ABI as the documented bits. */
    assert(chengyin_engine_map_key(IBUS_KEY_x, IBUS_CONTROL_MASK, &key, &modifiers) == 1);
    assert(key == 'x' && modifiers == CHENGYIN_MOD_CONTROL);
    assert(chengyin_engine_map_key(IBUS_KEY_x, IBUS_MOD1_MASK, &key, &modifiers) == 1);
    assert(modifiers == CHENGYIN_MOD_ALT);
    assert(chengyin_engine_map_key(IBUS_KEY_x, IBUS_SUPER_MASK, &key, &modifiers) == 1);
    assert(modifiers == CHENGYIN_MOD_SUPER);
    assert(chengyin_engine_map_key(IBUS_KEY_x, IBUS_MOD4_MASK, &key, &modifiers) == 1);
    assert(modifiers == CHENGYIN_MOD_SUPER);

    /* A release event is never ours: replaying a commit on release would
     * duplicate every character. */
    assert(chengyin_engine_map_key(IBUS_KEY_space, IBUS_RELEASE_MASK, &key, &modifiers) == 0);
    /* An unmappable key (e.g. a pure modifier) is the host's. */
    assert(chengyin_engine_map_key(IBUS_KEY_Control_L, 0, &key, &modifiers) == 0);

    printf("key mapping: OK\n");
}

/* --- unconditional COMMIT read ------------------------------------------- */

static void test_commit_read_is_unconditional(void) {
    ChengyinEngine *engine = make_engine();
    type(engine, "ni");
    assert(recorder.commits == 0);
    assert(strcmp(recorder.preedit, "ni") == 0);
    assert(recorder.row_count == 8);

    /* ASCII punctuation commits the pending candidate AND is forwarded to the
     * host. The core reports it unhandled, so an implementation that only reads
     * COMMIT when the key was handled loses the character. */
    const int consumed = chengyin_engine_process_key(engine, IBUS_KEY_comma, 0);
    assert(consumed == 0); /* forwarded: the comma must still reach the editor */
    assert(recorder.commits == 1);
    assert(strcmp(recorder.commit[0], "你") == 0);
    assert(recorder.preedit[0] == '\0');
    assert(recorder.row_count == 0);

    /* Space commits on a handled key and must be consumed. */
    type(engine, "ni");
    assert(chengyin_engine_process_key(engine, IBUS_KEY_space, 0) == 1);
    assert(recorder.commits == 2);
    assert(strcmp(recorder.commit[1], "你") == 0);

    /* Enter commits the raw spelling and is consumed. */
    type(engine, "ni");
    assert(chengyin_engine_process_key(engine, IBUS_KEY_Return, 0) == 1);
    assert(recorder.commits == 3);
    assert(strcmp(recorder.commit[2], "ni") == 0);

    chengyin_engine_free(engine);
    printf("unconditional commit read: OK\n");
}

static void test_commit_not_repeated(void) {
    /* The commit persists until the next process/reset, so the engine must read
     * it exactly once: re-reading it on an unrelated key would re-commit. */
    ChengyinEngine *engine = make_engine();
    type(engine, "ni");
    assert(chengyin_engine_process_key(engine, IBUS_KEY_space, 0) == 1);
    assert(recorder.commits == 1);
    /* A key with no composition and no candidates changes nothing. */
    assert(chengyin_engine_process_key(engine, IBUS_KEY_Shift_L, 0) == 0);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_Control_L, 0) == 0);
    assert(recorder.commits == 1);
    chengyin_engine_free(engine);
    printf("commit is not repeated: OK\n");
}

static void test_release_events_do_not_commit(void) {
    ChengyinEngine *engine = make_engine();
    type(engine, "ni");
    /* A release of a key that would commit must not consume or commit. */
    assert(chengyin_engine_process_key(engine, IBUS_KEY_space, IBUS_RELEASE_MASK) == 0);
    assert(recorder.commits == 0);
    assert(strcmp(recorder.preedit, "ni") == 0);
    chengyin_engine_free(engine);
    printf("release events: OK\n");
}

/* --- two-step text protocol ---------------------------------------------- */

static void test_read_text_two_step(void) {
    ChengyinSession *session = chengyin_session_new();
    assert(session);
    assert(chengyin_session_configure(session, 9, 1) == 0);
    for (const char *p = "ni"; *p; ++p) { chengyin_session_process(session, (uint32_t)*p, 0); }

    /* Size query: the required capacity INCLUDING the NUL. */
    const int32_t required = chengyin_session_text(session, CHENGYIN_TEXT_PREEDIT, 0, NULL, 0);
    assert(required == 3);

    char exact[3];
    assert(chengyin_engine_read_text(session, CHENGYIN_TEXT_PREEDIT, 0, exact, sizeof exact) == 2);
    assert(strcmp(exact, "ni") == 0);

    /* A buffer one byte short must report failure and come back empty rather
     * than hand the caller a truncated value. */
    char short_buffer[8] = "XXXXXXX";
    assert(chengyin_engine_read_text(session, CHENGYIN_TEXT_PREEDIT, 0, short_buffer,
                                  (size_t)required - 1) == -1);
    assert(short_buffer[0] == '\0');

    /* An empty field reads as zero bytes, not an error: "no text" is a normal
     * state (the COMMIT slot is empty on most keystrokes), and the caller uses
     * the length to decide whether there is anything to publish. */
    assert(chengyin_session_text(session, CHENGYIN_TEXT_COMMIT, 0, NULL, 0) == 1);
    char commit_buffer[8] = "stale";
    assert(chengyin_engine_read_text(session, CHENGYIN_TEXT_COMMIT, 0, commit_buffer,
                                  sizeof commit_buffer) == 0);
    assert(commit_buffer[0] == '\0');

    /* An out-of-range candidate index is rejected by the core, and the helper
     * must not publish whatever happened to be in the buffer. */
    char candidate[64] = "stale";
    assert(chengyin_engine_read_text(session, CHENGYIN_TEXT_CANDIDATE, 99, candidate,
                                  sizeof candidate) == -1);
    assert(candidate[0] == '\0');

    chengyin_session_free(session);
    printf("two-step text protocol: OK\n");
}

static void test_long_composition_needs_the_size_query(void) {
    /* Raw input is capped at CHENGYIN_MAX_INPUT_BYTES (63), but confirming Chinese
     * syllables mixes 3-byte characters into the preedit, so the text outgrows the
     * input by a wide margin. The peak arrives partway through the confirmations,
     * so the longest state is captured as it goes by rather than at the end.
     */
    ChengyinSession *session = chengyin_session_new();
    assert(session);
    assert(chengyin_session_configure(session, 9, 1) == 0);
    for (int i = 0; i < CHENGYIN_MAX_INPUT_BYTES; ++i) {
        chengyin_session_process(session, (uint32_t)'a', 0);
    }

    char longest[CHENGYIN_MAX_TEXT_BYTES + 1];
    longest[0] = '\0';
    size_t longest_length = 0;
    int32_t peak = 0;
    for (int i = 0; i < 200; ++i) {
        if (chengyin_session_candidate_count(session) <= 0) { break; }
        if (chengyin_session_process(session, CHENGYIN_KEY_SELECT_1, 0) < 0) { break; }
        const int32_t required = chengyin_session_text(session, CHENGYIN_TEXT_PREEDIT, 0, NULL, 0);
        if (required > peak) { peak = required; }
        const int length = chengyin_engine_read_text(session, CHENGYIN_TEXT_PREEDIT, 0, longest,
                                                  sizeof longest);
        if (length > 0 && (size_t)length > longest_length) {
            longest_length = (size_t)length;
            /* A buffer sized from the input length is what a fixed-size guess would
             * have used. The helper must refuse it rather than publish a truncated
             * value -- this is the failure the size query exists to prevent. */
            char guessed[CHENGYIN_MAX_INPUT_BYTES + 1] = "stale";
            assert(chengyin_engine_read_text(session, CHENGYIN_TEXT_PREEDIT, 0, guessed,
                                          sizeof guessed) == -1);
            assert(guessed[0] == '\0');
        }
    }

    /* Measured 188 bytes (187 of text) at the peak: 62 three-byte characters plus
     * one raw ASCII byte. Asserting the magnitude rather than the exact figure
     * keeps this a statement about the protocol, not about one dictionary. */
    assert(peak == (int32_t)longest_length + 1);
    assert(peak > 3 * 32);
    assert(peak <= CHENGYIN_MAX_TEXT_BYTES + 1);
    assert(longest_length > CHENGYIN_MAX_INPUT_BYTES * 2);
    /* It really is multibyte text rather than long ASCII: the byte length exceeds
     * the character count, which is the difference the cursor conversion below
     * depends on. */
    assert(chengyin_engine_utf8_offset_chars(longest, longest_length) < (int)longest_length);


    chengyin_session_free(session);
    printf("long composition is carried by the size query: OK\n");
}

/* --- UTF-8 cursor conversion -------------------------------------------- */

static void test_cursor_is_a_character_offset(void) {
    /* The ABI reports the cursor as a UTF-8 BYTE offset; IBus counts characters.
     * With two Chinese characters in the preedit the two disagree by four, and
     * handing IBus the byte offset would place the caret past the end. */
    ChengyinEngine *engine = make_engine();
    type(engine, "zhongguorenmin");
    assert(recorder.cursor_chars == (int)strlen("zhongguorenmin"));

    /* Confirm 中国 (candidate 3), leaving "renmin" raw in the preedit. */
    assert(chengyin_engine_process_key(engine, IBUS_KEY_3, 0) == 1);
    const ChengyinSession *session = chengyin_engine_session(engine);
    assert(strcmp(recorder.preedit, "\xe4\xb8\xad\xe5\x9b\xbdrenmin") == 0);
    assert(chengyin_session_preedit_cursor(session) == 12); /* 6 Chinese + 6 ASCII bytes */
    assert(recorder.cursor_chars == 8);                  /* 2 + 6 characters */
    assert(recorder.cursor_chars != chengyin_session_preedit_cursor(session));

    /* Cursor movement is consumed while composing, and every landing position is
     * a character boundary rather than the middle of a 3-byte sequence. */
    for (int i = 0; i < 6; ++i) {
        assert(chengyin_engine_process_key(engine, IBUS_KEY_Left, 0) == 1);
        assert(recorder.cursor_chars == 8 - (i + 1));
    }
    /* The confirmed segment is a floor: Home cannot move into it. */
    assert(chengyin_engine_process_key(engine, IBUS_KEY_Home, 0) == 1);
    assert(recorder.cursor_chars == 2);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_End, 0) == 1);
    assert(recorder.cursor_chars == 8);
    assert(recorder.preedit_visible == 1);

    chengyin_engine_free(engine);

    /* The conversion itself. It counts characters that BEGIN before the offset,
     * so a byte offset inside a 3-byte sequence counts that character: the core
     * only ever reports boundaries, and counting a started character keeps the
     * caret from being pushed in front of the one the user is editing. */
    assert(chengyin_engine_utf8_offset_chars("", 0) == 0);
    assert(chengyin_engine_utf8_offset_chars("abc", 0) == 0);
    assert(chengyin_engine_utf8_offset_chars("abc", 3) == 3);
    assert(chengyin_engine_utf8_offset_chars("abc", 1) == 1);
    assert(chengyin_engine_utf8_offset_chars("\xe4\xb8\xad", 3) == 1);
    assert(chengyin_engine_utf8_offset_chars("\xe4\xb8\xad", 1) == 1);
    assert(chengyin_engine_utf8_offset_chars("\xe4\xb8\xad\xe5\x9b\xbd", 3) == 1);
    assert(chengyin_engine_utf8_offset_chars("\xe4\xb8\xad\xe5\x9b\xbd", 6) == 2);
    assert(chengyin_engine_utf8_offset_chars("\xe4\xb8\xad\xe5\x9b\xbdrenmin", 12) == 8);
    assert(chengyin_engine_utf8_offset_chars(NULL, 99) == 0);

    printf("UTF-8 cursor conversion: OK\n");
}

/* --- candidates, paging, reset, sensitivity ------------------------------ */

static void test_candidates_and_paging(void) {
    ChengyinEngine *engine = make_engine();
    type(engine, "shi");
    assert(recorder.row_count == 9);
    assert(strcmp(recorder.rows[0], "是") == 0);
    assert(recorder.selected == 0);
    assert(recorder.auxiliary[0] != '\0');

    chengyin_engine_page(engine, 1);
    assert(recorder.row_count == 4);
    assert(strcmp(recorder.rows[0], "诗") == 0);
    chengyin_engine_page(engine, 0);
    assert(strcmp(recorder.rows[0], "是") == 0);

    chengyin_engine_move(engine, 0); /* down */
    assert(recorder.selected == 1);
    chengyin_engine_move(engine, 1); /* up */
    assert(recorder.selected == 0);

    /* Clicking a candidate commits it and is not forwarded. */
    chengyin_engine_select(engine, 0);
    assert(recorder.commits == 1);
    assert(strcmp(recorder.commit[0], "是") == 0);

    chengyin_engine_free(engine);
    printf("candidates and paging: OK\n");
}

static void test_idle_page_keys_are_forwarded(void) {
    /* '-' and '=' map to page up/down, but while nothing is being composed the
     * core reports them unhandled. Swallowing them would break typing a hyphen
     * in a terminal, so the engine must return "not consumed". */
    ChengyinEngine *engine = make_engine();
    assert(chengyin_engine_process_key(engine, IBUS_KEY_minus, 0) == 0);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_equal, 0) == 0);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_Page_Up, 0) == 0);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_Page_Down, 0) == 0);
    assert(recorder.commits == 0);
    /* With a composition open they page the candidate list instead. */
    type(engine, "shi");
    assert(chengyin_engine_process_key(engine, IBUS_KEY_Page_Down, 0) == 1);
    assert(strcmp(recorder.rows[0], "\u8bd7") == 0);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_Page_Up, 0) == 1);
    assert(strcmp(recorder.rows[0], "\u662f") == 0);
    chengyin_engine_free(engine);
    printf("idle page keys are forwarded: OK\n");
}

static void test_number_key_selects(void) {
    ChengyinEngine *engine = make_engine();
    type(engine, "ni");
    assert(chengyin_engine_process_key(engine, IBUS_KEY_2, 0) == 1);
    assert(recorder.commits == 1);
    assert(strcmp(recorder.commit[0], "尼") == 0);
    chengyin_engine_free(engine);
    printf("number key selects: OK\n");
}

static void test_escape_and_backspace(void) {
    ChengyinEngine *engine = make_engine();
    type(engine, "ni");
    assert(chengyin_engine_process_key(engine, IBUS_KEY_BackSpace, 0) == 1);
    assert(strcmp(recorder.preedit, "n") == 0);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_Escape, 0) == 1);
    assert(recorder.preedit[0] == '\0');
    assert(recorder.preedit_visible == 0);
    assert(recorder.row_count == 0);
    chengyin_engine_free(engine);
    printf("escape and backspace: OK\n");
}

static void test_reset_clears_everything(void) {
    ChengyinEngine *engine = make_engine();
    type(engine, "nihao");
    assert(recorder.row_count > 0);
    chengyin_engine_reset(engine);
    assert(recorder.preedit[0] == '\0');
    assert(recorder.preedit_visible == 0);
    assert(recorder.row_count == 0);
    assert(recorder.commits == 0);
    chengyin_engine_free(engine);
    printf("reset: OK\n");
}

static void test_sensitive_context_is_never_composed(void) {
    ChengyinEngine *engine = make_engine();
    type(engine, "ni");
    assert(recorder.row_count > 0);
    /* Entering a password field drops the composition, rejects further input
     * and never commits or learns. */
    chengyin_engine_set_sensitive(engine, 1);
    assert(recorder.preedit[0] == '\0');
    assert(recorder.row_count == 0);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_n, 0) == 0);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_i, 0) == 0);
    assert(chengyin_engine_process_key(engine, IBUS_KEY_space, 0) == 0);
    assert(recorder.commits == 0);
    assert(recorder.preedit[0] == '\0');
    assert(recorder.row_count == 0);
    /* Leaving the field restores normal composition. */
    chengyin_engine_set_sensitive(engine, 0);
    type(engine, "ni");
    assert(chengyin_engine_process_key(engine, IBUS_KEY_space, 0) == 1);
    assert(recorder.commits == 1);
    chengyin_engine_free(engine);
    printf("sensitive context: OK\n");
}

static void test_learning_follows_a_confirmed_commit(void) {
    ChengyinEngine *engine = make_engine();
    const ChengyinSession *session = chengyin_engine_session(engine);
    ChengyinProfile *before = chengyin_session_profile(session);
    assert(before);
    const int32_t learned_before = chengyin_profile_count(before);
    chengyin_profile_free(before);

    type(engine, "ni");
    assert(chengyin_engine_process_key(engine, IBUS_KEY_space, 0) == 1);
    assert(recorder.commits == 1);

    ChengyinProfile *after = chengyin_session_profile(session);
    assert(after);
    assert(chengyin_profile_count(after) == learned_before + 1);
    chengyin_profile_free(after);

    /* A forwarded punctuation key commits but is not a selection, so it must
     * not train. */
    type(engine, "ni");
    assert(chengyin_engine_process_key(engine, IBUS_KEY_comma, 0) == 0);
    ChengyinProfile *punctuated = chengyin_session_profile(session);
    assert(punctuated);
    assert(chengyin_profile_count(punctuated) == learned_before + 1);
    chengyin_profile_free(punctuated);

    chengyin_engine_free(engine);
    printf("learning follows a confirmed commit: OK\n");
}

static void test_engine_instances_are_independent(void) {
    ChengyinEngine *a = make_engine();
    type(a, "ni");
    Recorder recorded_a = recorder;
    ChengyinEngine *b = make_engine();
    type(b, "zhongguo");
    assert(strcmp(recorder.preedit, "zhongguo") == 0);
    /* The first engine keeps its own session. */
    assert(recorder.commits == 0);
    assert(chengyin_engine_process_key(a, IBUS_KEY_space, 0) == 1);
    assert(strcmp(recorder.commit[0], "你") == 0);
    (void)recorded_a;
    chengyin_engine_free(a);
    chengyin_engine_free(b);
    printf("engine instances are independent: OK\n");
}

int main(void) {
    if (chengyin_ime_abi_version() != CHENGYIN_ABI_VERSION) {
        fprintf(stderr, "ABI mismatch\n");
        return 1;
    }
    test_key_mapping();
    test_commit_read_is_unconditional();
    test_commit_not_repeated();
    test_release_events_do_not_commit();
    test_read_text_two_step();
    test_long_composition_needs_the_size_query();
    test_cursor_is_a_character_offset();
    test_candidates_and_paging();
    test_idle_page_keys_are_forwarded();
    test_number_key_selects();
    test_escape_and_backspace();
    test_reset_clears_everything();
    test_sensitive_context_is_never_composed();
    test_learning_follows_a_confirmed_commit();
    test_engine_instances_are_independent();
    printf("all IBus engine tests passed\n");
    return 0;
}
