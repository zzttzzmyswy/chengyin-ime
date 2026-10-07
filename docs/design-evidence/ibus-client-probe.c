#include <ibus.h>
#include <stdio.h>
#include <stdlib.h>
static IBusBus *bus; static IBusInputContext *ctx;
static gboolean on_commit(IBusInputContext *c, IBusText *t, gpointer u) {
    (void)c;(void)u; printf("[client] COMMIT: %s\n", ibus_text_get_text(t)); fflush(stdout); return TRUE; }
static gboolean on_preedit(IBusInputContext *c, IBusText *t, guint cur, gpointer u) {
    (void)c;(void)u;(void)cur; printf("[client] PREEDIT: %s\n", ibus_text_get_text(t)); fflush(stdout); return TRUE; }
static void send(guint kc) {
    ibus_input_context_process_key_event(ctx, kc, 0, 0);
    while (g_main_context_iteration(NULL, FALSE)); }
int main(void) {
    ibus_init();
    bus = ibus_bus_new();
    g_assert(ibus_bus_is_connected(bus));
    ctx = ibus_bus_create_input_context(bus, "myswy-probe-client");
    g_assert(ctx);
    g_signal_connect(ctx, "commit-text", G_CALLBACK(on_commit), NULL);
    g_signal_connect(ctx, "update-preedit-text", G_CALLBACK(on_preedit), NULL);
    ibus_input_context_set_capabilities(ctx, IBUS_CAP_PREEDIT_TEXT | IBUS_CAP_FOCUS);
    ibus_input_context_focus_in(ctx);
    sleep(1);
    GError *e=NULL;
    ibus_bus_set_global_engine(bus, "myswy-probe2");
    fprintf(stderr, "[client] set_global_engine => %s\n", e? e->message : "OK"); fflush(stderr);
    sleep(1);

    const gchar *name = "myswy-probe";
    ibus_input_context_set_engine(ctx, name);
    for (int i = 0; i < 50; i++) { while (g_main_context_iteration(NULL, FALSE)); g_usleep(100000); }
    for (const char *p = "nihao"; *p; p++) {
        guint kc = (guint)*p;
        send(kc);
        for (int i = 0; i < 10; i++) { while (g_main_context_iteration(NULL, FALSE)); g_usleep(20000); }
    }
    send(IBUS_KEY_space);
    for (int i = 0; i < 30; i++) { while (g_main_context_iteration(NULL, FALSE)); g_usleep(50000); }
    printf("[client] done\n"); fflush(stdout);
    return 0;
}
