#include <ibus.h>
#include <stdio.h>
#include "myswy_ime.h"

typedef struct _MyswyEngine { IBusEngine parent; MyswySession *session; } MyswyEngine;
typedef struct _MyswyEngineClass { IBusEngineClass parent_class; } MyswyEngineClass;

static gboolean on_key_real(IBusEngine *e, guint kc, guint k, guint st) {
    (void)kc; (void)k; (void)st;
    MyswyEngine *self = (MyswyEngine *)e;
    myswy_session_process(self->session, 'n', 0);
    myswy_session_process(self->session, 'i', 0);
    int n = myswy_session_candidate_count(self->session);
    uint8_t buf[512];
    int r = myswy_session_text(self->session, MYSWY_TEXT_CANDIDATE, 0, buf, sizeof buf);
    fprintf(stderr, "[engine] candidates=%d first=%s\n", n, r > 0 ? (char*)buf : "(none)");
    if (n > 0 && r > 0) {
        int32_t flags = myswy_session_process(self->session, MYSWY_KEY_SPACE, 0);
        fprintf(stderr, "[engine] space flags=%d\n", flags);
        uint8_t cbuf[512];
        int cr = myswy_session_text(self->session, MYSWY_TEXT_COMMIT, 0, cbuf, sizeof cbuf);
        if (cr > 0) {
            IBusText *t = ibus_text_new_from_string((char*)cbuf);
            ibus_engine_commit_text(e, t);
            g_object_unref(t);
            fprintf(stderr, "[engine] committed %s\n", (char*)cbuf);
        }
    }
    return TRUE;
}
static void on_enable(IBusEngine *e){(void)e;fprintf(stderr,"[engine] ENABLE\n");fflush(stderr);}
static void on_focus_in(IBusEngine *e){(void)e;fprintf(stderr,"[engine] FOCUS_IN\n");fflush(stderr);}
static void myswy_engine_init(MyswyEngine *self) {
    self->session = myswy_session_new();
    myswy_session_configure(self->session, 9, 1);
}
static void myswy_engine_class_init(MyswyEngineClass *klass) {
    IBusEngineClass *k = IBUS_ENGINE_CLASS(klass);
    k->process_key_event = on_key_real;
    k->enable = on_enable;
    k->focus_in = on_focus_in;
}
static GType myswy_engine_get_type(void);
G_DEFINE_TYPE(MyswyEngine, myswy_engine, IBUS_TYPE_ENGINE)

static IBusBus *bus; static IBusFactory *factory;
static void on_bus_acquired(IBusBus *b, gpointer u) {
    (void)u;
    IBusComponent *c = ibus_component_new("org.freedesktop.IBus.MyswyProbe2", "Myswy Probe2", "0.1", "MIT", "probe",
        "https://example.invalid", "/usr/bin/true", "ibus-myswy-probe2");
    IBusEngineDesc *d = ibus_engine_desc_new("myswy-probe2", "Myswy Probe2", "probe", "zh_CN", "MIT", "probe", "us", "us");
    ibus_component_add_engine(c, d);
    ibus_bus_register_component(b, c);
    factory = ibus_factory_new(ibus_bus_get_connection(b));
    ibus_factory_add_engine(factory, "myswy-probe2", myswy_engine_get_type());
    ibus_bus_request_name(b, "org.freedesktop.IBus.MyswyProbe2", 0);
    fprintf(stderr, "[engine] registered myswy-probe2\n");
    fflush(stderr);
}
int main(void) {
    ibus_init();
    bus = ibus_bus_new();
    g_signal_connect(bus, "connected", G_CALLBACK(on_bus_acquired), NULL);
    if (ibus_bus_is_connected(bus)) on_bus_acquired(bus, NULL);
    ibus_main();
    return 0;
}
