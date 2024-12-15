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
    XftColor fcol_fg, fcol_bg, fcol_sel;
    int font_width, font_height;
    XftFont* font;
};

struct X11 x11;

static int ccx = 2;
static int ccy = 1;

typedef struct Client Client;
struct Client
{
    int cx, cy;
    Window win;

    Client *next;
};

Client* clients;

typedef struct Cell Cell;
struct Cell
{
    Client* primary;
    Client* secondary;
};

Cell cells[9][9];

bool
load_colors(struct X11 *x11)
{
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

    if (XftColorAllocName(x11->dpy,
                           DefaultVisual(x11->dpy, x11->screen),
                           cmap,
                          "grey", &x11->fcol_sel) == False)
    {
        fprintf(stderr, "Could not load font sel color\n");
        return false;
    }

    return true;
}

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

    KeySym syms[] = { XK_Return, XK_p, XK_Left, XK_Right, XK_Up, XK_Down };

    for (unsigned int j = 0; j < LENGTH(modifiers); j++) {
        for (unsigned int k = 0; k < LENGTH(syms); k++)
            XGrabKey(x11->dpy, XKeysymToKeycode(x11->dpy, syms[k]), modifiers[j] | Mod1Mask, x11->root, False, GrabModeAsync, GrabModeAsync);

        XGrabKey(x11->dpy, XStringToKeysym("F1"), modifiers[j] | Mod1Mask, x11->root, False, GrabModeAsync, GrabModeAsync);
    }

    XSelectInput(x11->dpy, x11->root, SubstructureRedirectMask | SubstructureNotifyMask);

    Cursor cursor = XCreateFontCursor(x11->dpy, XC_left_ptr);
    XDefineCursor(x11->dpy, x11->root, cursor);

    if (!load_colors(x11))
        return false;

    // init draw for xft drawing
    x11->fdraw = XftDrawCreate(x11->dpy, x11->root,
                               DefaultVisual(x11->dpy, x11->screen),
                               DefaultColormap(x11->dpy, x11->screen));
    if (x11->fdraw == NULL)
    {
        fprintf(stderr, "Could not create xft draw \n");
        return false;
    }

    x11->font = XftFontOpenName(x11->dpy, x11->screen,
                                "Monospace:size=22");
    if (x11->font == NULL)
    {
        fprintf(stderr, "Could not load font\n");
        return false;
    }
    x11->font_height = x11->font->height;
    XGlyphInfo ext;
    XftTextExtents8(x11->dpy, x11->font, (FcChar8 *)"m", 1, &ext);
    x11->font_width = ext.width + 2;

    XSync(x11->dpy, False);
    return true;
}

void
draw_bar(struct X11 *x11)
{
    int cellh = 22 + 10;
    XftDrawRect(x11->fdraw, &x11->fcol_bg, 0, 0, x11->sw, cellh);

    char cell;
    int xoff = 0;

    char row[3] = "[0]";
    row[1] = '0' + ccy;
    XftDrawString8(x11->fdraw, &x11->fcol_fg, x11->font,
                xoff,
                x11->font->ascent,
                (XftChar8 *)&row, 3);
    xoff += (x11->font_width*2);

    for (int i = 1; i < 10; i++) {
        cell = '0' + i;
        xoff += (x11->font_width + 8);

        if (i == ccx)
            XftDrawRect(x11->fdraw, &x11->fcol_sel, xoff-4, 0, 8+x11->font_width, cellh);

        XftDrawString8(x11->fdraw, &x11->fcol_fg, x11->font,
                    xoff,
                    x11->font->ascent,
                    (XftChar8 *)&cell, 1);
    }
}

void
spawn(const char *cmd)
{
    if (!fork()) {
        execlp(cmd, cmd, (char*) NULL);
    }
}

int
clip(int n)
{
  if (n < 1)
    return 1;
  else if (n > 9)
    return 9;
  else
    return n;
}

void
update_view(int prevy, int prevx){
    if ((prevx == ccx) && (prevy == ccy))
        return;

    // unmap the windows show previously
    // TODO: deal with layout and secondary win here
    Cell* prev = &cells[prevy][prevx];
    if (prev->primary != NULL)
        XUnmapWindow(x11.dpy, prev->primary->win);

    // map the current cell's window(s) here
    // TODO: deal with secondary and layout here
    Cell* curr = &cells[ccy][ccx];
    if (curr->primary != NULL)
        XMapWindow(x11.dpy, curr->primary->win);

    draw_bar(&x11);
}

void
handleKeyPress(XKeyEvent *ev)
{
    KeySym ksym = XkbKeycodeToKeysym(x11.dpy, ev->keycode, 0, 0);

    int prevx, prevy;
    switch (ksym)
    {
        case XK_Return:
            spawn("vex");
            break;
        case XK_p:
            spawn("dmenu_run");
            break;
        case XK_Left:
            prevx = ccx;
            ccx = clip(ccx-1);
            update_view(ccy, prevx);
            break;
        case XK_Right:
            prevx = ccx;
            ccx = clip(ccx+1);
            update_view(ccy, prevx);
            break;
        case XK_Up:
            prevy = ccy;
            ccy = clip(ccy-1);
            update_view(prevy, ccx);
            break;
        case XK_Down:
            prevy = ccy;
            ccy = clip(ccy+1);
            update_view(prevy, ccx);
            break;
        case XK_F1:
            spawn("xterm");
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

    // resize to our desired shape
    int offy = 22 + 10;
    XMoveResizeWindow(x11.dpy, ev->window, 0, offy,
                      x11.sw, x11.sh - offy);
}

void
handleMapRequest(XMapRequestEvent *ev)
{
    // window may already exist
    bool found = false;
    Client *c;
    for (c = clients; c; c = c->next)
        if (c->win == ev->window) {
            found = true;
            break;
        }

    if (!found) {
        // window does not already exist
        c = (Client *)malloc(sizeof(Client));
        c->win = ev->window;

        // update global store
        c->next = clients;
        clients = c;

        // TODO: need to check if current cell is free
        c->cx = ccx;
        c->cy = ccy;

        // TODO: find the right slot to put it in
        cells[ccy][ccx].primary = c;
    }

    XRaiseWindow(x11.dpy, c->win);
    XMapWindow(x11.dpy, c->win);
}

int main() {
    XInitThreads();

    if (!x11_setup(&x11))
        return 1;

    clients = NULL;

    draw_bar(&x11);

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
      }
    }
}
