#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define LENGTH(X) (sizeof (X) / sizeof (X)[0])

struct X11
{
    Display *dpy;
    int screen;
    Window root, win;
};

struct X11 x11;

void
spawn(const char *cmd)
{
    if (!fork()) {
        system(cmd);
    }
}

void kill_client() {
    XKillClient(x11.dpy, x11.win);
    x11.win = -1;
}

void
handleKeyPress(XKeyEvent *ev)
{
    KeySym ksym = XKeycodeToKeysym(x11.dpy, ev->keycode, 0);

    switch (ksym)
    {
        case XK_f:
            spawn("firefox");
            break;
        case XK_c:
            spawn("google-chrome");
            break;
        case XK_t:
            spawn("xterm");
            break;
        case XK_k:
            kill_client();
            break;
    }
}

void
handleConfigureRequest(XConfigureRequestEvent *ev)
{
    XWindowChanges changes;

    // copy in changes as-is from the event
    // trying to make changes here does not work at all
    changes.x = ev->x;
    changes.y = ev->y;
    changes.width = ev->width;
    changes.height = ev->height;
    changes.border_width = ev->border_width;
    changes.sibling = ev->above;
    changes.stack_mode = ev->detail;

    XConfigureWindow(x11.dpy, ev->window, ev->value_mask, &changes);
}

void
handleMapRequest(XMapRequestEvent *ev)
{
    if (x11.win == -1) {
        // window does not already exist
        x11.win = ev->window;
    }

    XUnmapWindow(x11.dpy, x11.win);
    // resize
    XMoveResizeWindow(x11.dpy, x11.win, 0, 0, 800, 800);
    // map
    XMapWindow(x11.dpy, x11.win);
}

void
handleDestroyNotify(XDestroyWindowEvent *ev)
{
    if(x11.win == ev->window) {
        x11.win = -1;
    }
} 

int main() {

    x11.dpy = XOpenDisplay(NULL);
    if (x11.dpy == NULL)
    {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    x11.screen = DefaultScreen(x11.dpy);
    x11.root = XDefaultRootWindow(x11.dpy);
    x11.win = -1;

    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, Mod2Mask|LockMask };
    KeySym syms[] = { XK_f, XK_c, XK_t, XK_k };

    for (unsigned int j = 0; j < LENGTH(modifiers); j++) {
        for (unsigned int k = 0; k < LENGTH(syms); k++)
            XGrabKey(x11.dpy, XKeysymToKeycode(x11.dpy, syms[k]), modifiers[j] | Mod1Mask, x11.root, False, GrabModeAsync, GrabModeAsync);
    }

    XSelectInput(x11.dpy, x11.root, SubstructureRedirectMask | SubstructureNotifyMask);

    XEvent ev;
    while(true) {
      XNextEvent(x11.dpy, &ev);

      switch(ev.type) {
        case KeyPress:
            handleKeyPress(&ev.xkey);
            break;
        case ConfigureRequest:
            handleConfigureRequest(&ev.xconfigurerequest);
            break;
        case MapRequest:
            handleMapRequest(&ev.xmaprequest);
            break;
        case DestroyNotify:
            handleDestroyNotify(&ev.xdestroywindow);
            break;
      }
    }
}
