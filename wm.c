#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>
#include <pthread.h>

#define LENGTH(X) (sizeof (X) / sizeof (X)[0])

enum ColorType {
    Black,
    White,
    Gray,
    LightBlue,
    NumColors
};
static char* colors[NumColors] = {"black", "white", "gray", "lightblue"};

enum Layout {
    Monocle,
    Tiled
};

struct X11
{
    Display *dpy;
    int screen;
    Window root;

    int sw, sh;

    XftDraw* fdraw;
    XftColor colors[NumColors];
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
Client* hand;

typedef struct Cell Cell;
struct Cell
{
    enum Layout layout;

    Client* primary;
    Client* secondary;
};

Cell cells[9][9];

void
delete_client(Client* clients, Client* cl) {
    // first entry
    if (clients == cl) {
        clients = cl->next;
    } else {
        Client *p = clients;
        for (Client *c = clients->next; c; c = c->next) {
            if (c == cl) {
                p->next = c->next;
                break;
            }
            p = c;
        }
    }
    free(cl);
}

bool
load_colors(struct X11 *x11)
{
    Colormap cmap = DefaultColormap(x11->dpy, x11->screen);

    for (int i = 0; i < NumColors; i++) {
        if (XftColorAllocName(x11->dpy,
                            DefaultVisual(x11->dpy, x11->screen),
                            cmap,
                            colors[i], &x11->colors[i]) == False)
        {
            fprintf(stderr, "Could not load color: %s\n", colors[i]);
            return false;
        }
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

    KeySym syms[] = { XK_Return, XK_p, XK_Left, XK_Right, XK_Up, XK_Down,
                      XK_k, XK_m, XK_t, XK_f };

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
    XftDrawRect(x11->fdraw, &x11->colors[White], 0, 0, x11->sw - 30*x11->font_width, cellh);

    char cell;
    int xoff = 0;

    char row[3] = "[0]";
    row[1] = '0' + ccy;
    XftDrawString8(x11->fdraw, &x11->colors[Black], x11->font,
                xoff,
                x11->font->ascent,
                (XftChar8 *)&row, 3);
    xoff += (x11->font_width*2);

    for (int i = 1; i < 10; i++) {
        cell = '0' + i;
        xoff += (x11->font_width + 8);

        if (i == ccx)
            XftDrawRect(x11->fdraw, &x11->colors[LightBlue], xoff-4, 0, 8+x11->font_width, cellh);
        else if ((cells[ccy][i].primary != NULL) || (cells[ccy][i].secondary != NULL))
            XftDrawRect(x11->fdraw, &x11->colors[Gray], xoff-4, 0, 8+x11->font_width, cellh);

        XftDrawString8(x11->fdraw, &x11->colors[Black], x11->font,
                    xoff,
                    x11->font->ascent,
                    (XftChar8 *)&cell, 1);
    }
    xoff += (x11->font_width + 8);

    // draw layout
    xoff += 5;
    char layout = 'M';
    if (cells[ccy][ccx].layout == Tiled)
        layout = '=';

    XftDrawRect(x11->fdraw, &x11->colors[Black], xoff-4, 0, 8+x11->font_width, cellh);
    XftDrawString8(x11->fdraw, &x11->colors[White], x11->font,
                xoff,
                x11->font->ascent,
                (XftChar8 *)&layout, 1);
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

    // unmap the windows show previously
    Cell* prev = &cells[prevy][prevx];
    if (prev->primary != NULL)
        XUnmapWindow(x11.dpy, prev->primary->win);
    if (prev->secondary != NULL)
        XUnmapWindow(x11.dpy, prev->secondary->win);

    // map the current cell's window(s) here
    Cell* curr = &cells[ccy][ccx];
    if (curr->layout == Tiled) {
        if (curr->primary != NULL)
            XMapWindow(x11.dpy, curr->primary->win);
        if (curr->secondary != NULL)
            XMapWindow(x11.dpy, curr->secondary->win);
    } else {
        // only display one window in Monocle
        if (curr->primary != NULL)
            XMapWindow(x11.dpy, curr->primary->win);
        else if (curr->secondary != NULL)
            XMapWindow(x11.dpy, curr->secondary->win);
    }

    draw_bar(&x11);
}

void
update_cell_layout()
{
    Cell* cell = &cells[ccy][ccx];
    int offy = 22 + 10;

    if (cell->layout == Tiled) {
        if (cell->primary != NULL)
            XMoveResizeWindow(x11.dpy, cell->primary->win, 0, offy,
                                x11.sw/2, x11.sh - offy);
        if (cell->secondary != NULL)
            XMoveResizeWindow(x11.dpy, cell->secondary->win, x11.sw/2, offy,
                                x11.sw/2, x11.sh - offy);
    } else {
        if (cell->primary != NULL)
            XMoveResizeWindow(x11.dpy, cell->primary->win, 0, offy,
                                x11.sw, x11.sh - offy);
        if (cell->secondary != NULL)
            XMoveResizeWindow(x11.dpy, cell->secondary->win, 0, offy,
                                x11.sw, x11.sh - offy);
    }

    update_view(ccy, ccx);
}

void
kill_client(){
    Cell* curr = &cells[ccy][ccx];
    // TODO: lookup the focused client in a better way than this
    // cannot kill secondary window with this logic
    if (curr->primary != NULL) {
        // TODO: send a clean WM_DELETE_WINDOW event
        XKillClient(x11.dpy, curr->primary->win);

        // remove this from the list of clients
        delete_client(clients, curr->primary);

        curr->primary = NULL;
        update_cell_layout();
    }
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
        case XK_k:
            kill_client();
            break;
        case XK_m:
            cells[ccy][ccx].layout = Monocle;
            update_cell_layout();
            break;
        case XK_t:
            cells[ccy][ccx].layout = Tiled;
            update_cell_layout();
            break;
        case XK_f: // flip entries in the cell
            Cell *curr = &cells[ccy][ccx];
            Client *tmp = curr->primary;
            curr->primary = curr->secondary;
            curr->secondary = tmp;
            update_cell_layout();
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

    // resize to our initial desired shape
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

        // assume we can place this client
        c->cx = ccx;
        c->cy = ccy;

        Cell* cc = &cells[ccy][ccx];
        // find the right slot to put it in
        if (cc->primary == NULL) {
            cc->primary = c;
        } else if (cc->secondary == NULL) {
            cc->secondary = c;
        } else {
            // we can't place this window yet
            c->cx = -1;
            c->cy = -1;

            // put it in hand
            // TODO: check if hand is free
            hand = c;
        }
    }

    update_cell_layout();
    update_view(ccy, ccx);
}

void
handleDestroyNotify(XDestroyWindowEvent *ev)
{
    // find the client
    Client *c;
    for(c = clients;c;c = c->next) {
      if (c->win == ev->window)
          break;
    }

    if (c == NULL) {
        // client with window not found
        // for eg, if the wm was the one that killed the client
        // a destroy notify will still be raised for the window
        return;
    }

    // undo the mapping in the cells structure
    if (c == cells[c->cy][c->cx].primary)
        cells[c->cy][c->cx].primary = NULL;
    if (c == cells[c->cy][c->cx].secondary)
        cells[c->cy][c->cx].secondary = NULL;

    delete_client(clients, c);
    update_cell_layout();
    draw_bar(&x11);
}

void*
timer_update(void* arg)
{
    (void)arg;

    time_t t;
    struct tm *tm_info;
    char tstr[20];

    while (true) {
        sleep(30);

        // get current time
        t = time(NULL);
        tm_info = localtime(&t);
        strftime(tstr, 20, "%a %b %e, %H:%M", tm_info);

        // TODO: run pomodoro checks and updates

        int cellh = 22 + 10;
        XftDrawRect(x11.fdraw, &x11.colors[White], x11.sw - 19*x11.font_width, 0,
                    19*x11.font_width, cellh);

        XftDrawString8(x11.fdraw, &x11.colors[Black], x11.font,
                    x11.sw - 18*x11.font_width,
                    x11.font->ascent,
                    (XftChar8 *)&tstr, 20);

        XSync(x11.dpy, False);
    }

    return NULL;
}

int main() {
    XInitThreads();

    if (!x11_setup(&x11))
        return 1;

    clients = NULL;
    hand = NULL;

    draw_bar(&x11);

    pthread_t timer_tid;
    pthread_create(&timer_tid, NULL, &timer_update, NULL);

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
