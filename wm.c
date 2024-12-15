#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>

#define LENGTH(X) (sizeof (X) / sizeof (X)[0])

struct X11
{
    Display *dpy;
    int screen;
    Window root;

    int sw, sh;

    XftDraw* fdraw;
    XftColor fcol_fg, fcol_bg;
};

struct X11 x11;

bool
x11_setup(struct X11 *x11)
{
    x11->dpy = XOpenDisplay(NULL);
    if (x11->dpy == NULL)
    {
        fprintf(stderr, "Cannot open display\n");
        return false;
    }

    x11->screen = DefaultScreen(x11->dpy);
    x11->root = XDefaultRootWindow(x11->dpy);

    x11->sw = DisplayWidth(x11->dpy, x11->screen);
    x11->sh = DisplayHeight(x11->dpy, x11->screen);

    // little trick lifted from dwm
    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, Mod2Mask|LockMask };

    for (unsigned int j = 0; j < LENGTH(modifiers); j++) {
        XGrabKey(x11->dpy, XKeysymToKeycode(x11->dpy, XK_Return), modifiers[j] | Mod1Mask, x11->root, False, GrabModeAsync, GrabModeAsync);
        XGrabKey(x11->dpy, XKeysymToKeycode(x11->dpy, XK_p), modifiers[j] | Mod1Mask, x11->root, False, GrabModeAsync, GrabModeAsync);
        XGrabKey(x11->dpy, XStringToKeysym("F1"), modifiers[j] | Mod1Mask, x11->root, False, GrabModeAsync, GrabModeAsync);
    }

    Cursor cursor = XCreateFontCursor(x11->dpy, XC_left_ptr);
    XDefineCursor(x11->dpy, x11->root, cursor);

    Colormap cmap = DefaultColormap(x11->dpy, x11->screen);

    // init XftColor for use with text
    if (XftColorAllocName(x11->dpy,
                           DefaultVisual(x11->dpy, x11->screen),
                           cmap,
                          "black", &x11->fcol_fg) == False)
    {
        fprintf(stderr, "Could not load font fg color\n");
        return false;
    }

    if (XftColorAllocName(x11->dpy,
                           DefaultVisual(x11->dpy, x11->screen),
                           cmap,
                          "white", &x11->fcol_bg) == False)
    {
        fprintf(stderr, "Could not load font bg color\n");
        return false;
    }

    // init draw for xft drawing
    x11->fdraw = XftDrawCreate(x11->dpy, x11->root,
                               DefaultVisual(x11->dpy, x11->screen), cmap);
    if (x11->fdraw == NULL)
    {
        fprintf(stderr, "Could not create xft draw \n");
        return false;
    }

    XSync(x11->dpy, False);
    return true;
}

void
draw_bar(struct X11 *x11)
{
    XftDrawRect(x11->fdraw, &x11->fcol_bg, 0, 0, x11->sw, 20);
}

void
spawn(const char *cmd)
{
    if (!fork()) {
        execlp(cmd, cmd, (char*) NULL);
    }
}

void
handleKeyPress(XKeyEvent *ev)
{
    KeySym ksym = XkbKeycodeToKeysym(x11.dpy, ev->keycode, 0, 0);

    switch (ksym)
    {
        case XK_Return:
            spawn("vex");
            break;
        case XK_p:
            spawn("dmenu_run");
            break;
        case XK_F1:
            break;
    }
}

int main() {
    if (!x11_setup(&x11))
        return 1;

    draw_bar(&x11);

    XEvent ev;
    while(true) {
      XNextEvent(x11.dpy, &ev);

      switch(ev.type) {
        case KeyPress:
            handleKeyPress(&ev.xkey);
            break;
      }
    }
}
