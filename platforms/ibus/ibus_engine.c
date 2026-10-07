/* IBus engine for the Chengyin (myswy) shared core.
 *
 * Unlike the Fcitx5 addon, an IBus engine is a separate process: ibus-daemon
 * spawns this executable and asks it for an IBusEngine per input context. All
 * input handling lives in engine.c, which has no D-Bus dependency and is
 * covered by the CTests; this file only translates IBus vfuncs into those
 * calls.
 *
 * Session lifetime: every IBusEngine instance owns a private MyswySession, so
 * two input contexts never share composition state. focus_out and reset both
 * drop the composition, because a new context means a new session anyway.
 */
#include <ibus.h>
#include <stdlib.h>
#include "engine.h"
#include "myswy_ime.h"

#define MYSWY_ENGINE_NAME "myswy"
#define MYSWY_ENGINE_LONGNAME "Chengyin Pinyin (Prototype)"
#define MYSWY_ENGINE_COMPONENT "org.freedesktop.IBus.Myswy"
/* The ABI pages candidates nine at a time and reports indexes relative to the
 * current page, so a lookup table is always exactly one page. */
#define MYSWY_PAGE_SIZE 9

typedef struct _MyswyIbusEngine {
    IBusEngine parent;
    MyswyEngine *core;
    /* Cached so the SIGUSR-style teardown never touches freed GObjects. */
    gboolean has_preedit;
} MyswyIbusEngine;

typedef struct _MyswyIbusEngineClass {
    IBusEngineClass parent_class;
} MyswyIbusEngineClass;

G_DEFINE_TYPE(MyswyIbusEngine, myswy_ibus_engine, IBUS_TYPE_ENGINE)

/* --- sink callbacks: engine.c -> IBus ------------------------------------ */

/* Ownership note, verified against libibus: every object created by
 * ibus_text_new_from_string / ibus_lookup_table_new comes back FLOATING, and the
 * IBus entry points that accept them (ibus_engine_commit_text,
 * ibus_engine_update_preedit_text, ibus_engine_update_auxiliary_text,
 * ibus_engine_update_lookup_table, ibus_lookup_table_append_candidate) SINK that
 * floating reference -- they take the object over. Unref'ing it afterwards frees
 * it while the engine still holds it, which shows up as
 * "ibus_serializable_serialize_object: assertion failed" and then a crash.
 */
static void sink_commit(void *user, const char *utf8) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)user;
    ibus_engine_commit_text(IBUS_ENGINE(self), ibus_text_new_from_string(utf8));
}

static void sink_preedit(void *user, const char *utf8, int cursor_chars, int visible) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)user;
    if (!visible || utf8[0] == '\0') {
        if (self->has_preedit) {
            ibus_engine_hide_preedit_text(IBUS_ENGINE(self));
            self->has_preedit = FALSE;
        }
        return;
    }
    if (cursor_chars < 0) { cursor_chars = 0; }
    ibus_engine_update_preedit_text(IBUS_ENGINE(self), ibus_text_new_from_string(utf8),
                                    (guint)cursor_chars, TRUE);
    self->has_preedit = TRUE;
}

static void sink_candidates(void *user, const MyswyEngineRow *rows, int count, int selected,
                            int association) {
    (void)association;
    MyswyIbusEngine *self = (MyswyIbusEngine *)user;
    IBusEngine *engine = IBUS_ENGINE(self);
    if (count <= 0) {
        ibus_engine_hide_lookup_table(engine);
        return;
    }
    IBusLookupTable *table = ibus_lookup_table_new((guint)count, 0, TRUE, FALSE);
    for (int i = 0; i < count; ++i) {
        ibus_lookup_table_append_candidate(table, ibus_text_new_from_string(rows[i].text));
    }
    if (selected >= 0 && selected < count) {
        ibus_lookup_table_set_cursor_pos(table, (guint)selected);
    }
    ibus_engine_update_lookup_table(engine, table, TRUE);
}

static void sink_auxiliary(void *user, const char *utf8) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)user;
    IBusEngine *engine = IBUS_ENGINE(self);
    if (utf8[0] == '\0') {
        ibus_engine_hide_auxiliary_text(engine);
        return;
    }
    ibus_engine_update_auxiliary_text(engine, ibus_text_new_from_string(utf8), TRUE);
}

/* --- IBusEngine vfuncs --------------------------------------------------- */

static gboolean myswy_process_key_event(IBusEngine *engine, guint keyval, guint keycode,
                                       guint state) {
    (void)keycode;
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    return myswy_engine_process_key(self->core, (uint32_t)keyval, (uint32_t)state) ? TRUE : FALSE;
}

static void myswy_focus_in(IBusEngine *engine) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_reset(self->core);
}

static void myswy_focus_out(IBusEngine *engine) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_reset(self->core);
}

static void myswy_reset(IBusEngine *engine) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_reset(self->core);
}

static void myswy_enable(IBusEngine *engine) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_reset(self->core);
}

static void myswy_disable(IBusEngine *engine) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_reset(self->core);
}

static void myswy_page_up(IBusEngine *engine) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_page(self->core, 0);
}

static void myswy_page_down(IBusEngine *engine) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_page(self->core, 1);
}

static void myswy_cursor_up(IBusEngine *engine) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_move(self->core, 1);
}

static void myswy_cursor_down(IBusEngine *engine) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_move(self->core, 0);
}

static void myswy_candidate_clicked(IBusEngine *engine, guint index, guint button, guint state) {
    (void)button;
    (void)state;
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    myswy_engine_select(self->core, (size_t)index);
}

/* IBus reports password/private fields here. Composing into one would leak the
 * keystrokes into a hidden field's candidate list and league them into the
 * learning profile, so a sensitive context is never composed into. */
static void myswy_set_content_type(IBusEngine *engine, guint purpose, guint hints) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)engine;
    const gboolean sensitive = purpose == IBUS_INPUT_PURPOSE_PASSWORD ||
                               purpose == IBUS_INPUT_PURPOSE_PIN ||
                               (hints & (IBUS_INPUT_HINT_HIDDEN_TEXT | IBUS_INPUT_HINT_PRIVATE)) != 0;
    myswy_engine_set_sensitive(self->core, sensitive ? 1 : 0);
}

static void myswy_ibus_engine_init(MyswyIbusEngine *self) {
    static const MyswyEngineSink sink = {
        .user = NULL,
        .on_commit = sink_commit,
        .on_preedit = sink_preedit,
        .on_candidates = sink_candidates,
        .on_auxiliary = sink_auxiliary,
    };
    MyswyEngineSink bound = sink;
    bound.user = self;
    self->core = myswy_engine_new(&bound);
    g_assert(self->core != NULL);
    self->has_preedit = FALSE;
}

static void myswy_ibus_engine_finalize(GObject *object) {
    MyswyIbusEngine *self = (MyswyIbusEngine *)object;
    myswy_engine_free(self->core);
    self->core = NULL;
    G_OBJECT_CLASS(myswy_ibus_engine_parent_class)->finalize(object);
}

static void myswy_ibus_engine_class_init(MyswyIbusEngineClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    IBusEngineClass *engine_class = IBUS_ENGINE_CLASS(klass);
    object_class->finalize = myswy_ibus_engine_finalize;
    engine_class->process_key_event = myswy_process_key_event;
    engine_class->focus_in = myswy_focus_in;
    engine_class->focus_out = myswy_focus_out;
    engine_class->reset = myswy_reset;
    engine_class->enable = myswy_enable;
    engine_class->disable = myswy_disable;
    engine_class->page_up = myswy_page_up;
    engine_class->page_down = myswy_page_down;
    engine_class->cursor_up = myswy_cursor_up;
    engine_class->cursor_down = myswy_cursor_down;
    engine_class->candidate_clicked = myswy_candidate_clicked;
    engine_class->set_content_type = myswy_set_content_type;
}

/* --- process bootstrap --------------------------------------------------- */

static IBusBus *bus = NULL;
static IBusFactory *factory = NULL;
/* The component's <exec> must be an absolute path, because ibus-daemon spawns it
 * from an unrelated working directory. Running as `./ibus-engine-myswy` in the
 * build tree would otherwise publish an exec the daemon could not resolve. */
static gchar *executable_path = NULL;

static IBusComponent *build_component(const gchar *exec) {
    IBusComponent *component = ibus_component_new(MYSWY_ENGINE_COMPONENT,
                                                  "Chengyin IME",
                                                  "0.1.0",
                                                  "MIT",
                                                  "Chengyin IME contributors",
                                                  "https://github.com/zzttzzmyswy/myswyIm",
                                                  exec,
                                                  "myswy");
    IBusEngineDesc *description = ibus_engine_desc_new(MYSWY_ENGINE_NAME,
                                                       MYSWY_ENGINE_LONGNAME,
                                                       "Chengyin full pinyin",
                                                       "zh_CN",
                                                       "MIT",
                                                       "Chengyin IME contributors",
                                                       "input-keyboard",
                                                       "us");
    /* add_engine sinks the floating reference, so the component owns the
     * description and the caller must not unref it. */
    ibus_component_add_engine(component, description);
    return component;
}

static void on_bus_acquired(IBusBus *acquired, gpointer user_data) {
    (void)user_data;
    /* register_component sinks the floating reference: the bus takes ownership,
     * so unref'ing here would free a component the bus still holds. */
    IBusComponent *component = build_component(executable_path);
    ibus_bus_register_component(acquired, component);

    factory = ibus_factory_new(ibus_bus_get_connection(acquired));
    ibus_factory_add_engine(factory, MYSWY_ENGINE_NAME, myswy_ibus_engine_get_type());
    ibus_bus_request_name(acquired, MYSWY_ENGINE_COMPONENT, 0);
}

int main(int argc, char **argv) {
    (void)argc;
    g_set_prgname("ibus-engine-myswy");
    ibus_init();
    if (myswy_ime_abi_version() != MYSWY_ABI_VERSION) {
        g_printerr("myswy: C ABI version mismatch\n");
        return 1;
    }
    executable_path = g_file_read_link("/proc/self/exe", NULL);
    if (executable_path == NULL) {
        executable_path = g_strdup(argv[0]);
    }
    bus = ibus_bus_new();
    g_signal_connect(bus, "connected", G_CALLBACK(on_bus_acquired), NULL);
    if (ibus_bus_is_connected(bus)) {
        on_bus_acquired(bus, NULL);
    }
    ibus_main();
    g_free(executable_path);
    return 0;
}
