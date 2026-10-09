// SPDX-License-Identifier: GPL-3.0-or-later
/* Headless IBus client used by platforms/ibus/e2e.sh.
 *
 * Creates a real input context on a real session bus, selects the chengyin engine
 * and types `ni` + space, then reports the preedit/commit signals it received.
 * It is the host side on purpose: the engine under test is the shipped
 * ibus-engine-chengyin process, so observing COMMIT proves the whole chain
 * (client -> ibus-daemon -> engine process -> Rust core -> commit signal).
 *
 * Exits non-zero when the expected commit never arrives, so the shell script
 * can assert on the exit status as well as the log.
 */
#include <ibus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IBusBus *bus = NULL;
static IBusInputContext *context = NULL;
static int commit_seen = 0;
static char committed[256] = "";

static gboolean on_commit(IBusInputContext *ic, IBusText *text, gpointer user_data) {
    (void)ic;
    (void)user_data;
    snprintf(committed, sizeof committed, "%s", ibus_text_get_text(text));
    commit_seen = 1;
    printf("[client] COMMIT: %s\n", committed);
    fflush(stdout);
    return TRUE;
}

static gboolean on_preedit(IBusInputContext *ic, IBusText *text, guint cursor, gpointer user_data) {
    (void)ic;
    (void)user_data;
    printf("[client] PREEDIT: %s (cursor=%u)\n", ibus_text_get_text(text), cursor);
    fflush(stdout);
    return TRUE;
}

static void pump(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(20000);
    }
}

static void send_key(guint keyval) {
    ibus_input_context_process_key_event(context, keyval, 0, 0);
    pump(10);
}

int main(int argc, char **argv) {
    const char *engine = argc > 1 ? argv[1] : "chengyin";
    ibus_init();
    bus = ibus_bus_new();
    if (!ibus_bus_is_connected(bus)) {
        fprintf(stderr, "[client] not connected to a bus\n");
        return 2;
    }
    context = ibus_bus_create_input_context(bus, "chengyin-e2e-client");
    if (!context) {
        fprintf(stderr, "[client] could not create an input context\n");
        return 2;
    }
    g_signal_connect(context, "commit-text", G_CALLBACK(on_commit), NULL);
    g_signal_connect(context, "update-preedit-text", G_CALLBACK(on_preedit), NULL);
    ibus_input_context_set_capabilities(context, IBUS_CAP_PREEDIT_TEXT | IBUS_CAP_FOCUS);
    ibus_input_context_focus_in(context);
    pump(50);

    GError *error = NULL;
    ibus_bus_set_global_engine(bus, engine);
    fprintf(stderr, "[client] set_global_engine(%s) => %s\n", engine,
            error ? error->message : "ok");
    fflush(stderr);
    pump(50);

    ibus_input_context_set_engine(context, engine);
    pump(50);

    /* ni + space must commit 你 through the real daemon. */
    send_key(IBUS_KEY_n);
    send_key(IBUS_KEY_i);
    pump(20);
    send_key(IBUS_KEY_space);
    pump(50);

    printf("[client] done\n");
    fflush(stdout);
    return commit_seen ? 0 : 1;
}
