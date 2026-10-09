// SPDX-License-Identifier: GPL-3.0-or-later
/* D-Bus frontend end-to-end client for platforms/fcitx5/e2e.sh (I14).
 *
 * This is the real application side of the chain: a real session bus, a real
 * org.fcitx.Fcitx.InputContext1 created by the real fcitx5 daemon's
 * dbusfrontend, and the daemon's own input-method pipeline behind it. Keys go
 * in through ProcessKeyEvent, so nothing here calls the plugin directly.
 *
 * I13 drove this through X11 XIM instead. That frontend returns committed text
 * inside the X event stream, which a headless client can only read while it
 * owns the X input focus; on this host the commit never came back (see the I13
 * report). The D-Bus frontend delivers it as a signal on the context object,
 * which is what a real D-Bus client observes, and needs no X server at all.
 *
 * Every wait is a poll on an observed event with a deadline, never a fixed
 * sleep: each keystroke must be proven to have reached the engine (the preedit
 * changed) before the next one goes in, and the terminating key only goes in
 * once the full composition is visible. A stage that never happens is reported
 * by name instead of turning into a timeout with no explanation.
 *
 * One COMMIT: <text> line is printed per committed string. The scenario is
 * selected by argv[1]; the exit status is the scenario's own verdict.
 */
#define _POSIX_C_SOURCE 200809L

#include <gio/gio.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IM_NAME "org.fcitx.Fcitx5"
#define IM_PATH "/org/freedesktop/portal/inputmethod"
#define IM_IFACE "org.fcitx.Fcitx.InputMethod1"
#define CTX_IFACE "org.fcitx.Fcitx.InputContext1"
#define CTL_PATH "/controller"
#define CTL_IFACE "org.fcitx.Fcitx.Controller1"

/* CapabilityFlag::Preedit. A client that draws the composition itself sets it,
 * and it is what makes the daemon emit UpdateFormattedPreedit - the signal
 * this client polls to see that a key reached the engine. */
#define CAPABILITY_PREEDIT ((guint64)1 << 1)

/* Per-stage budget. CTest bounds the whole test as well; this is what turns a
 * hang into a named failure instead of a timeout with no explanation. */
#define STAGE_TIMEOUT_MS 10000
#define POLL_MS 10

/* After the Escape scenario's composition has cleared, keep draining signals
 * for this long before ruling a stray commit out: the claim is that nothing was
 * committed, so it must not rest on the order in which two signals happened to
 * arrive. Long enough for any commit the daemon had already queued to land. */
#define SETTLE_MS 250

#define MAX_TEXT 256
#define MAX_FAILURE 256

typedef struct {
    const char *name;
    const char *pinyin;     /* keys typed before the terminating key */
    guint final_key;        /* keysym of the terminating key */
    const char *final_name; /* how the terminating key is named in output */
    gboolean expect_commit; /* terminal condition: commit seen, or preedit cleared */
} Scenario;

static const Scenario SCENARIOS[] = {
    {"commit", "nihao", 0x20, "space", TRUE},
    {"escape", "nihao", 0xff1b, "Escape", FALSE},
    {"second", "nihao", 0x32, "2", TRUE},
};

typedef struct {
    GDBusConnection *bus;
    const Scenario *scenario;
    char *context_path;
    char preedit[MAX_TEXT];
    char last_commit[MAX_TEXT];
    int commits;
    size_t typed;    /* pinyin characters already delivered */
    gboolean final_sent;
    gboolean done;
    gboolean failed;
    char failure[MAX_FAILURE];
    gint64 deadline;     /* µs, current stage */
    gint64 settle_until; /* µs, escape scenario's post-clear drain */
} Client;

static gint64 now_us(void) { return g_get_monotonic_time(); }

static void fail(Client *client, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

static void fail(Client *client, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(client->failure, sizeof client->failure, fmt, args);
    va_end(args);
    client->failed = TRUE;
    client->done = TRUE;
}

/* A poll that ran out of budget is a failure, never a silent pass: a scenario
 * whose condition never fired is exactly what this test exists to catch. */
static gboolean expired(Client *client, const char *what) {
    if (now_us() < client->deadline) { return FALSE; }
    fail(client, "%s did not happen within %d ms (last preedit \"%s\", %d commit(s))",
         what, STAGE_TIMEOUT_MS, client->preedit, client->commits);
    return TRUE;
}

static gboolean send_key(Client *client, guint keyval) {
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        client->bus, IM_NAME, client->context_path, CTX_IFACE, "ProcessKeyEvent",
        g_variant_new("(uuubu)", keyval, 0u, 0u, FALSE, 0u),
        G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
    if (!reply) {
        fail(client, "ProcessKeyEvent(0x%x) failed: %s", keyval,
             error ? error->message : "no reply");
        g_clear_error(&error);
        return FALSE;
    }
    g_variant_unref(reply);
    return TRUE;
}

static void on_commit(GDBusConnection *connection, const gchar *sender,
                      const gchar *object_path, const gchar *interface_name,
                      const gchar *signal_name, GVariant *parameters,
                      gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name;
    (void)signal_name;
    Client *client = user_data;
    const char *text = NULL;
    g_variant_get(parameters, "(&s)", &text);
    snprintf(client->last_commit, sizeof client->last_commit, "%s", text);
    client->commits++;
    /* Printed per commit so the script can assert the exact text the whole
     * chain produced, not just that something arrived. */
    printf("COMMIT: %s\n", client->last_commit);
    fflush(stdout);
}

static void on_preedit(GDBusConnection *connection, const gchar *sender,
                       const gchar *object_path, const gchar *interface_name,
                       const gchar *signal_name, GVariant *parameters,
                       gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name;
    (void)signal_name;
    Client *client = user_data;
    GVariant *segments = NULL;
    gint32 cursor = 0;
    g_variant_get(parameters, "(@a(si)i)", &segments, &cursor);
    (void)cursor;
    /* The formatted preedit is a run list; the composition text is the plain
     * concatenation of its segments. */
    client->preedit[0] = '\0';
    GVariantIter iter;
    g_variant_iter_init(&iter, segments);
    const char *segment = NULL;
    gint32 attribute = 0;
    size_t used = 0;
    while (g_variant_iter_next(&iter, "(&si)", &segment, &attribute)) {
        size_t length = strlen(segment);
        if (used + length >= sizeof client->preedit) { break; }
        memcpy(client->preedit + used, segment, length);
        used += length;
        client->preedit[used] = '\0';
    }
    g_variant_unref(segments);
}

/* Does the engine's composition currently match the first `count` characters
 * of the scripted pinyin? Polling on this is what makes each keystroke's
 * delivery observable instead of assumed. */
static gboolean preedit_matches(const Client *client, size_t count) {
    return strncmp(client->preedit, client->scenario->pinyin, count) == 0 &&
           strlen(client->preedit) == count;
}

static gboolean step(gpointer user_data) {
    Client *client = user_data;
    if (client->done) { return G_SOURCE_REMOVE; }

    const size_t total = strlen(client->scenario->pinyin);

    if (client->typed < total) {
        if (client->typed > 0 && !preedit_matches(client, client->typed)) {
            /* The key is in flight; keep polling until the preedit proves it
             * landed. Nothing else is sent in the meantime, so the engine can
             * never be fed a keystroke it was not waiting for. */
            if (expired(client, "the composition to catch up")) { return G_SOURCE_REMOVE; }
            return G_SOURCE_CONTINUE;
        }
        const guint keyval = (guint)(unsigned char)client->scenario->pinyin[client->typed];
        if (!send_key(client, keyval)) { return G_SOURCE_REMOVE; }
        client->typed++;
        client->deadline = now_us() + STAGE_TIMEOUT_MS * 1000;
        return G_SOURCE_CONTINUE;
    }

    if (!client->final_sent) {
        /* The terminating key only goes in once the full composition is
         * visible: sending it early would test a state the user never reached,
         * and would let the escape scenario "pass" for the wrong reason. */
        if (!preedit_matches(client, total)) {
            if (expired(client, "the full composition")) { return G_SOURCE_REMOVE; }
            return G_SOURCE_CONTINUE;
        }
        printf("PREEDIT: %s\n", client->preedit);
        fflush(stdout);
        if (!send_key(client, client->scenario->final_key)) { return G_SOURCE_REMOVE; }
        client->final_sent = TRUE;
        client->deadline = now_us() + STAGE_TIMEOUT_MS * 1000;
        return G_SOURCE_CONTINUE;
    }

    if (client->scenario->expect_commit) {
        if (client->commits > 0) {
            client->done = TRUE;
            return G_SOURCE_REMOVE;
        }
        if (expired(client, "a commit string")) { return G_SOURCE_REMOVE; }
        return G_SOURCE_CONTINUE;
    }

    /* escape: success is the composition clearing with nothing committed. A
     * commit would be emitted before the cleared preedit, but "nothing was
     * committed" is exactly the claim under test, so it is not inferred from
     * that ordering: once the composition is gone the client keeps pumping for
     * a bounded settle window and only then rules a stray commit out. */
    if (client->preedit[0] == '\0') {
        if (client->settle_until == 0) {
            client->settle_until = now_us() + SETTLE_MS * 1000;
        }
        if (now_us() < client->settle_until) { return G_SOURCE_CONTINUE; }
        if (client->commits > 0) {
            fail(client, "%s committed \"%s\" although the composition was cancelled",
                 client->scenario->final_name, client->last_commit);
        } else {
            client->done = TRUE;
        }
        return G_SOURCE_REMOVE;
    }
    if (expired(client, "the cancelled composition to clear")) { return G_SOURCE_REMOVE; }
    return G_SOURCE_CONTINUE;
}

static gboolean call_void(Client *client, const char *object_path,
                          const char *interface_name, const char *method,
                          GVariant *parameters, const char *format_string) {
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        client->bus, IM_NAME, object_path, interface_name, method, parameters,
        format_string ? G_VARIANT_TYPE(format_string) : NULL,
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
    if (!reply) {
        fail(client, "%s failed: %s", method, error ? error->message : "no reply");
        g_clear_error(&error);
        return FALSE;
    }
    g_variant_unref(reply);
    return TRUE;
}

/* CreateInputContext can race the daemon's own startup. Retry inside a bounded
 * window - a daemon that is not up yet is a "not yet", not a failure - and
 * report the last error if it never succeeds. */
static char *create_context(Client *client) {
    /* Smaller than the failure buffer on purpose: this is embedded in a longer
     * message, and the two together must provably fit at every optimisation
     * level (-Wformat-truncation is level-dependent). */
    char last_error[128] = "";
    for (int attempt = 0; attempt < 100; ++attempt) {
        GError *error = NULL;
        GVariantBuilder args;
        g_variant_builder_init(&args, G_VARIANT_TYPE("a(ss)"));
        g_variant_builder_add(&args, "(ss)", "appname", "chengyin-e2e");
        g_variant_builder_add(&args, "(ss)", "program", "chengyin-e2e-client");
        GVariant *reply = g_dbus_connection_call_sync(
            client->bus, IM_NAME, IM_PATH, IM_IFACE, "CreateInputContext",
            g_variant_new("(a(ss))", &args), G_VARIANT_TYPE("(oay)"),
            G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
        if (reply) {
            const char *path = NULL;
            GVariant *uuid = NULL;
            g_variant_get(reply, "(&o@ay)", &path, &uuid);
            char *result = g_strdup(path);
            g_variant_unref(uuid);
            g_variant_unref(reply);
            return result;
        }
        if (error) {
            snprintf(last_error, sizeof last_error, "%s", error->message);
            g_clear_error(&error);
        }
        g_usleep(100 * 1000);
    }
    snprintf(client->failure, sizeof client->failure,
             "the daemon never handed out an input context: %s", last_error);
    client->failed = TRUE;
    return NULL;
}

/* The activation a user performs with the language-switch hotkey
 * (fcitx5-remote -s chengyin). It cannot be skipped: a fresh input context
 * starts on the group's first entry, so without it the keys would be tested
 * against the wrong engine. The switch is asynchronous, so this waits for the
 * daemon to report the new active input method rather than assuming. */
static gboolean activate_chengyin(Client *client) {
    if (!call_void(client, CTL_PATH, CTL_IFACE, "SetCurrentIM",
                   g_variant_new("(s)", "chengyin"), NULL)) {
        return FALSE;
    }
    for (int attempt = 0; attempt < 200; ++attempt) {
        GError *error = NULL;
        GVariant *reply = g_dbus_connection_call_sync(
            client->bus, IM_NAME, CTL_PATH, CTL_IFACE, "CurrentInputMethod", NULL,
            G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
        if (reply) {
            const char *name = NULL;
            g_variant_get(reply, "(&s)", &name);
            gboolean ok = g_strcmp0(name, "chengyin") == 0;
            g_variant_unref(reply);
            if (ok) { return TRUE; }
        } else {
            g_clear_error(&error);
        }
        g_usleep(10 * 1000);
    }
    fail(client, "chengyin never became the active input method");
    return FALSE;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <scenario>\nscenarios:", argv[0]);
        for (size_t i = 0; i < G_N_ELEMENTS(SCENARIOS); ++i) {
            fprintf(stderr, " %s", SCENARIOS[i].name);
        }
        fprintf(stderr, "\n");
        return 2;
    }

    const Scenario *scenario = NULL;
    for (size_t i = 0; i < G_N_ELEMENTS(SCENARIOS); ++i) {
        if (strcmp(SCENARIOS[i].name, argv[1]) == 0) { scenario = &SCENARIOS[i]; }
    }
    if (!scenario) {
        fprintf(stderr, "unknown scenario: %s\n", argv[1]);
        return 2;
    }

    Client client;
    memset(&client, 0, sizeof client);
    client.scenario = scenario;

    if (!g_getenv("DBUS_SESSION_BUS_ADDRESS")) {
        fprintf(stderr, "DBUS_SESSION_BUS_ADDRESS is not set\n");
        return 2;
    }

    GError *error = NULL;
    client.bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!client.bus) {
        fprintf(stderr, "cannot connect to the session bus: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        return 2;
    }

    client.context_path = create_context(&client);
    if (!client.context_path) {
        fprintf(stderr, "FAIL %s: %s\n", scenario->name,
                client.failure[0] ? client.failure : "no input context");
        return 1;
    }
    printf("CONTEXT: %s\n", client.context_path);
    fflush(stdout);

    /* Subscribe before the first key so no commit can be missed. */
    g_dbus_connection_signal_subscribe(
        client.bus, IM_NAME, CTX_IFACE, "CommitString", client.context_path, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, on_commit, &client, NULL);
    g_dbus_connection_signal_subscribe(
        client.bus, IM_NAME, CTX_IFACE, "UpdateFormattedPreedit", client.context_path,
        NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_preedit, &client, NULL);

    if (!call_void(&client, client.context_path, CTX_IFACE, "SetCapability",
                   g_variant_new("(t)", CAPABILITY_PREEDIT), NULL) ||
        !call_void(&client, client.context_path, CTX_IFACE, "FocusIn", NULL, NULL) ||
        !activate_chengyin(&client)) {
        fprintf(stderr, "FAIL %s: %s\n", scenario->name, client.failure);
        return 1;
    }

    client.deadline = now_us() + STAGE_TIMEOUT_MS * 1000;
    g_timeout_add(POLL_MS, step, &client);
    while (!client.done) {
        g_main_context_iteration(NULL, TRUE);
    }

    printf("RESULT scenario=%s commits=%d last_commit=%s preedit=%s\n", scenario->name,
           client.commits, client.last_commit, client.preedit);
    fflush(stdout);

    if (client.failed) {
        fprintf(stderr, "FAIL %s: %s\n", scenario->name, client.failure);
        return 1;
    }
    printf("PASS %s: %s observed after %s\n", scenario->name,
           scenario->expect_commit ? "a commit string"
                                   : "the cancelled composition clearing",
           scenario->final_name);
    fflush(stdout);
    return 0;
}
