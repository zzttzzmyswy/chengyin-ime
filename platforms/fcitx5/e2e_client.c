/* Headless X11 XIM client used by platforms/fcitx5/e2e.sh.
 *
 * This is the real application side of the chain: a real X connection, a real
 * input context created with XOpenIM/XCreateIC, and the fcitx5 XIM frontend on
 * the other end talking to the loaded chengyin plugin. Keys arrive from xdotool
 * through the X server, so nothing here fabricates a keystroke.
 *
 * XFilterEvent is the XIM entry point: an input method only sees a key if the
 * client offers it there first. Every KeyPress is filtered before it is looked
 * up, so a key the input method consumed never reaches this process as text.
 *
 * Committed text is read back with Xutf8LookupString on the KeyPress that
 * produced it (XLookupChars), so that is the only place a commit is recorded.
 * One COMMIT: <text> line is printed per committed string. The scenario is
 * selected by argv[1].
 */
#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TEXT 1024

static char last_commit[MAX_TEXT];
static int commit_count = 0;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <scenario>\n", argv[0]);
        return 2;
    }
    const char *scenario = argv[1];

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "[client] cannot open display\n");
        return 2;
    }

    /* XMODIFIERS=@im=fcitx makes XOpenIM look the name up in the X server's
     * selection, which is where fcitx5's XIM frontend registers itself. */
    XIM im = XOpenIM(display, NULL, NULL, NULL);
    if (!im) {
        fprintf(stderr, "[client] XOpenIM failed: no input method on this display\n");
        XCloseDisplay(display);
        return 2;
    }

    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, 400, 200, 0, 0, 0);
    XStoreName(display, window, "chengyin-e2e-client");
    XSelectInput(display, window, KeyPressMask | KeyReleaseMask | FocusChangeMask);
    XMapWindow(display, window);
    XFlush(display);

    XIMStyles *styles = NULL;
    if (XGetIMValues(im, XNQueryInputStyle, &styles, NULL) || !styles) {
        fprintf(stderr, "[client] cannot query input styles\n");
        return 2;
    }
    /* Take the first style that carries neither preedit nor status callbacks
     * (the classic Root style). The callback styles require every preedit and
     * status callback to be supplied at XCreateIC time, and a client that
     * supplies only some of them silently gets no input context at all. Root is
     * also what a plain X11 application uses. */
    XIMStyle chosen = 0;
    for (unsigned i = 0; i < styles->count_styles; ++i) {
        if (styles->supported_styles[i] == (XIMPreeditNothing | XIMStatusNothing)) {
            chosen = styles->supported_styles[i];
            break;
        }
    }
    XFree(styles);
    if (!chosen) {
        fprintf(stderr, "[client] no usable input style\n");
        return 2;
    }

    /* XNFocusWindow is not allowed with a Root-style context, so it is left out
     * here; passing it makes XCreateIC fail, which is a silent "no client" for
     * the whole test. */
    XIC ic = XCreateIC(im,
                       XNInputStyle, chosen,
                       XNClientWindow, window,
                       NULL);
    if (!ic) {
        fprintf(stderr, "[client] XCreateIC failed\n");
        return 2;
    }

    /* Injected keys go to whatever holds the X input focus, so take it
     * explicitly: an unfocused window would drop them. */
    XSetICFocus(ic);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XFlush(display);
    printf("READY window=0x%lx\n", (unsigned long)window);
    fflush(stdout);

    /* Poll rather than sleep a fixed time: the run stays short when the input
     * method is quick and still tolerates a slow first load. e2e.sh bounds the
     * whole test, so this only needs to be patient, not authoritative. */
    const int iterations = 300; /* ~15s at 50ms */
    for (int i = 0; i < iterations; ++i) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            /* XFilterEvent must see EVERY event, not just KeyPress:
             * XIM delivers its own protocol through it, and a client that
             * filters only KeyPress never completes the handshake. */
            if (XFilterEvent(&event, None)) { continue; }
            if (event.type != KeyPress) { continue; }
            char buffer[MAX_TEXT];
            KeySym keysym = 0;
            Status status = 0;
            int length = Xutf8LookupString(ic, &event.xkey, buffer, sizeof buffer - 1, &keysym, &status);
            if ((status == XLookupChars || status == XLookupBoth) && length > 0) {
                buffer[length] = '\0';
                snprintf(last_commit, sizeof last_commit, "%s", buffer);
                commit_count++;
                printf("COMMIT: %s\n", last_commit);
                fflush(stdout);
            }
        }
        if (strcmp(scenario, "escape") == 0) {
            /* Nothing may commit here; the script waits out the whole window. */
            if (i > 120) { break; }
        } else if (commit_count > 0) {
            break;
        }
        const struct timespec pause = {0, 50 * 1000 * 1000}; /* 50 ms */
        nanosleep(&pause, NULL);
    }

    printf("RESULT scenario=%s commits=%d last_commit=%s\n",
           scenario, commit_count, last_commit);
    fflush(stdout);
    XDestroyIC(ic);
    XCloseIM(im);
    XCloseDisplay(display);
    return 0;
}
