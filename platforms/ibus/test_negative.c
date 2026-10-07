/* Differential control for the COMMIT contract.
 *
 * test_engine.c asserts that a commit is read after a process call that reports
 * the key as UNHANDLED: ASCII punctuation commits the pending candidate and is
 * forwarded to the host in the same call. A test like that can pass for the
 * wrong reason, so this file runs the same scenario twice against the real C
 * ABI:
 *
 *   arm A (buggy)   reads COMMIT only when the core reported CHENGYIN_HANDLED
 *   arm B (control) reads COMMIT after every process call
 *
 * Arm A must lose the character and arm B must receive it. The real engine is
 * then required to match arm B, so the assertion in test_engine.c fails if the
 * unconditional read is ever removed. Same inputs, same core, one difference.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine.h"
#include "chengyin_ime.h"

/* Returns 1 when a commit was observed. `unconditional` selects the arm. */
static int punctuate(int unconditional, char *out, size_t capacity) {
    ChengyinSession *session = chengyin_session_new();
    assert(session);
    assert(chengyin_session_configure(session, 9, 1) == 0);
    chengyin_session_process(session, 'n', 0);
    chengyin_session_process(session, 'i', 0);

    /* ',' commits the pending candidate and is forwarded to the host. */
    const int32_t flags = chengyin_session_process(session, ',', 0);
    assert((flags & CHENGYIN_HANDLED) == 0);

    int committed = 0;
    if (unconditional || (flags & CHENGYIN_HANDLED)) {
        const int32_t required = chengyin_session_text(session, CHENGYIN_TEXT_COMMIT, 0, NULL, 0);
        if (required > 0 && (size_t)required <= capacity) {
            const int32_t written =
                chengyin_session_text(session, CHENGYIN_TEXT_COMMIT, 0, (uint8_t *)out, capacity);
            committed = written > 1;
        }
    }
    chengyin_session_free(session);
    return committed;
}

/* The engine under test, driven through the same key sequence. */
typedef struct {
    char commit[256];
    int commits;
} Sink;

static void sink_commit(void *user, const char *utf8) {
    Sink *sink = (Sink *)user;
    snprintf(sink->commit, sizeof sink->commit, "%s", utf8);
    ++sink->commits;
}
static void sink_preedit(void *user, const char *utf8, int cursor, int visible) {
    (void)user; (void)utf8; (void)cursor; (void)visible;
}
static void sink_candidates(void *user, const ChengyinEngineRow *rows, int count, int selected,
                            int association) {
    (void)user; (void)rows; (void)count; (void)selected; (void)association;
}
static void sink_auxiliary(void *user, const char *utf8) { (void)user; (void)utf8; }

int main(void) {
    char buggy[256] = "";
    char control[256] = "";
    const int buggy_committed = punctuate(0, buggy, sizeof buggy);
    const int control_committed = punctuate(1, control, sizeof control);

    /* The whole point of the control: the two arms must disagree. If the core
     * ever stopped committing punctuation on a forwarded key, this test would
     * stop proving anything and must be revised rather than silently pass. */
    if (buggy_committed) {
        fprintf(stderr, "negative control is void: the skipped-read arm committed \"%s\"\n", buggy);
        return 1;
    }
    if (!control_committed || strcmp(control, "\xe4\xbd\xa0") != 0) {
        fprintf(stderr, "negative control is void: the read arm produced \"%s\"\n", control);
        return 1;
    }
    printf("negative control: skipped-read arm lost nothing? no -> buggy=\"\" control=\"%s\"\n", control);

    /* And the shipped engine must behave like the control arm, not the buggy one. */
    Sink sink = {{0}, 0};
    ChengyinEngineSink hooks = {&sink, sink_commit, sink_preedit, sink_candidates, sink_auxiliary};
    ChengyinEngine *engine = chengyin_engine_new(&hooks);
    assert(engine);
    chengyin_engine_process_key(engine, 'n', 0);
    chengyin_engine_process_key(engine, 'i', 0);
    const int consumed = chengyin_engine_process_key(engine, ',', 0);
    assert(consumed == 0); /* still forwarded to the host */
    assert(sink.commits == 1);
    assert(strcmp(sink.commit, control) == 0);
    chengyin_engine_free(engine);

    printf("negative control passed: the unconditional read is what preserves \"%s\"\n", control);
    return 0;
}
