#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>

#define LENGTH(X) (sizeof (X) / sizeof (X)[0])

struct X11
{
    Display *dpy;
    Window root;
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

    x11->root = XDefaultRootWindow(x11->dpy);

    // little trick lifted from dwm
    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, Mod2Mask|LockMask };

    for (unsigned int j = 0; j < LENGTH(modifiers); j++) {
        XGrabKey(x11->dpy, XKeysymToKeycode(x11->dpy, XK_Return), modifiers[j] | Mod1Mask, x11->root, False, GrabModeAsync, GrabModeAsync);
        XGrabKey(x11->dpy, XStringToKeysym("F1"), modifiers[j] | Mod1Mask, x11->root, False, GrabModeAsync, GrabModeAsync);
    }

    Cursor cursor = XCreateFontCursor(x11->dpy, XC_left_ptr);
    XDefineCursor(x11->dpy, x11->root, cursor);

    XSync(x11->dpy, False);
    return true;
}

void
handleKeyPress(XKeyEvent *ev)
{
    printf("Obtained some key press!\n");
    KeySym ksym = XkbKeycodeToKeysym(x11.dpy, ev->keycode, 0, 0);

    switch (ksym)
    {
        case XK_Return:
            if (!fork()) {
                execlp("vex", "vex", (char*) NULL);
            }
            break;
        case XK_F1:
            break;
    }
}

int main() {
    if (!x11_setup(&x11))
        return 1;

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
