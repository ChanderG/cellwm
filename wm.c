#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>
#include <stdint.h>
#include <sys/timerfd.h>
#include <sys/select.h>

#define LENGTH(X) (sizeof (X) / sizeof (X)[0])

enum ColorType {
    Black,
    White,
    Gray,
    LightBlue,
    Red,
    NumColors
};
static char* colors[NumColors] = {"black", "white", "gray", "lightblue", "red"};

enum Layout {
    Monocle,
    Tiled
};

struct X11
{
    Display *dpy;
    int fd;
    int screen;
    Window root;

    int sw, sh;

    XftDraw* fdraw;
    XftColor colors[NumColors];
    int font_width, font_height;
    XftFont* font;

    Window handwin;
    int hx, hy, hw, hh;
    XftDraw* hdraw;
};

struct X11 x11;

static int ccx = 2;
static int ccy = 1;

typedef struct Client Client;
struct Client
{
    int cx, cy;
    Window win;
    char title[100];

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

enum TimerState {
    OFF,
    ON,
    ELAPSED
};
enum TimerState timer = OFF;
int timer_dur = 20*60;
int timer_elapsed = 0;

int cellh = 22 + 10;

void
delete_client(Client* cl)
{
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

void
get_title(Client *c)
{
    // fallback option
    memcpy(c->title, "unknown", 7);
    c->title[7] = '\0';

	XTextProperty name;
	if (!XGetTextProperty(x11.dpy, c->win, &name, XA_WM_NAME))
        return;

	if (name.encoding == XA_STRING) {
		memcpy(c->title, (char *)name.value, 99);
        c->title[100] = '\0';
    }
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
    x11->fd = ConnectionNumber(x11->dpy);
    x11->sw = DisplayWidth(x11->dpy, x11->screen);
    x11->sh = DisplayHeight(x11->dpy, x11->screen);

    int cw = 150, ch = 250;
    x11->hx = x11->sw/2 - cw/2; x11->hy = x11->sh - ch - 100;
    x11->hw = cw; x11->hh = ch;
    x11->handwin =  XCreateSimpleWindow(x11->dpy, x11->root,
                                        x11->hx, x11->hy, x11->hw, x11->hh,
                                        2, BlackPixel(x11->dpy, x11->screen),
                                        WhitePixel(x11->dpy, x11->screen));


    // little trick lifted from dwm
    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, Mod2Mask|LockMask };

    KeySym syms[] = { XK_Return, XK_p, XK_Left, XK_Right, XK_Up, XK_Down,
                      XK_k, XK_m, XK_t, XK_f, XK_i, XK_l, XK_u, XK_End};
    KeySym numsyms[] = {XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9};

    for (unsigned int j = 0; j < LENGTH(modifiers); j++) {
        for (unsigned int k = 0; k < LENGTH(syms); k++)
            XGrabKey(x11->dpy, XKeysymToKeycode(x11->dpy, syms[k]), modifiers[j] | Mod1Mask, x11->root, False, GrabModeAsync, GrabModeAsync);
        // bind the num keys with shift mask to deal with inverted number row
        for (unsigned int k = 0; k < LENGTH(numsyms); k++)
            XGrabKey(x11->dpy, XKeysymToKeycode(x11->dpy, numsyms[k]), modifiers[j] | Mod1Mask | ShiftMask, x11->root, False, GrabModeAsync, GrabModeAsync);
    }

    XSelectInput(x11->dpy, x11->root, SubstructureRedirectMask | SubstructureNotifyMask);

    Cursor cursor = XCreateFontCursor(x11->dpy, XC_left_ptr);
    XDefineCursor(x11->dpy, x11->root, cursor);

    if (!load_colors(x11))
        return false;

    // init draw for xft drawing onto root
    x11->fdraw = XftDrawCreate(x11->dpy, x11->root,
                               DefaultVisual(x11->dpy, x11->screen),
                               DefaultColormap(x11->dpy, x11->screen));
    x11->hdraw = XftDrawCreate(x11->dpy, x11->handwin,
                               DefaultVisual(x11->dpy, x11->screen),
                               DefaultColormap(x11->dpy, x11->screen));
    if ((x11->fdraw == NULL) || (x11->hdraw == NULL))
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
draw_hand()
{
    if (hand != NULL)
        XftDrawString8(x11.hdraw, &x11.colors[Black], x11.font,
                        30, 30, (XftChar8 *)&hand->title, 10);
}

void
update_hand()
{
    if (hand == NULL)
        XUnmapWindow(x11.dpy, x11.handwin);
    else
        XMapWindow(x11.dpy, x11.handwin);
}

void
draw_bar(struct X11 *x11)
{
    XftDrawRect(x11->fdraw, &x11->colors[White], 0, 0, x11->sw - 30*x11->font_width, cellh);

    char cell;
    int xoff = 0;

    char row[3] = "[0]";
    row[1] = '0' + ccy;
    XftDrawString8(x11->fdraw, &x11->colors[Black], x11->font,
                xoff,
                cellh - 5,
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
                    cellh - 5,
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
                cellh - 5,
                (XftChar8 *)&layout, 1);
    xoff += (x11->font_width + 8);

    // lookup curr cell
    Cell *cc = &cells[ccy][ccx];

    // draw window presence indicator
    char indicator[] = "--";
    if (cc->primary != NULL) indicator[0] = '*';
    if (cc->secondary != NULL) indicator[1] = '*';

    XftDrawString8(x11->fdraw, &x11->colors[Black], x11->font,
                xoff,
                cellh - 5,
                (XftChar8 *)&indicator, 2);
    xoff += 2*(x11->font_width + 8);

    // draw title of primary window
    if (cc->primary != NULL) {
        XftDrawString8(x11->fdraw, &x11->colors[Black], x11->font,
                    xoff,
                    cellh - 5,
                    (XftChar8 *)&cc->primary->title, strlen(cc->primary->title));
    }
    // TODO: draw title of secondary window too?
}

void
spawn(const char *cmd)
{
    if (!fork()) {
        close(x11.fd);
        system(cmd);
    }
}

int
clip(int n)
{
  return (n < 1) ? 9 : (n > 9) ? 1 : n;
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

    if (hand != NULL)
        XRaiseWindow(x11.dpy, x11.handwin);

    draw_bar(&x11);
}

void
update_cell_layout()
{
    Cell* cell = &cells[ccy][ccx];

    if (cell->layout == Tiled) {
        if (cell->primary != NULL)
            XMoveResizeWindow(x11.dpy, cell->primary->win, 0, cellh,
                                x11.sw/2, x11.sh - cellh);
        if (cell->secondary != NULL)
            XMoveResizeWindow(x11.dpy, cell->secondary->win, x11.sw/2, cellh,
                                x11.sw/2, x11.sh - cellh);
    } else {
        if (cell->primary != NULL)
            XMoveResizeWindow(x11.dpy, cell->primary->win, 0, cellh,
                                x11.sw, x11.sh - cellh);
        if (cell->secondary != NULL)
            XMoveResizeWindow(x11.dpy, cell->secondary->win, 0, cellh,
                                x11.sw, x11.sh - cellh);
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
        delete_client(curr->primary);

        curr->primary = NULL;
        update_cell_layout();
    }
}

void
place_hand()
{
    Cell *c = &cells[ccy][ccx];
    if (c->primary == NULL) {
        c->primary = hand;
    } else if (c->secondary == NULL) {
        c->secondary = hand;
    } else {
        // nothing we can do from here
        return;
    }

    // place it in
    hand->cx = ccx;
    hand->cy = ccy;

    // free up the emtpy hand
    hand = NULL;
    update_hand();
    update_cell_layout();
}

void
pickup_hand()
{
    Cell *c = &cells[ccy][ccx];
    // nothing to pick up
    if (c->primary == NULL)
        return;
    // hand is not empty
    if (hand != NULL)
        return;

    hand = c->primary;
    hand->cx = hand->cy = -1;

    // need to manually unmap this window
    // TODO: make this cleaner
    c->primary = NULL;
    XUnmapWindow(x11.dpy, hand->win);

    update_hand();
    update_cell_layout();
}

void
handleKeyPress(XKeyEvent *ev)
{
    KeySym ksym = XKeycodeToKeysym(x11.dpy, ev->keycode, 0);

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
            prevx = ccx; ccx = clip(ccx-1);
            update_view(ccy, prevx); break;
        case XK_Right:
            prevx = ccx; ccx = clip(ccx+1);
            update_view(ccy, prevx); break;
        // actually with the shift mask here, but we are not checking
        case XK_1: prevx = ccx; ccx = 1; update_view(ccy, prevx); break;
        case XK_2: prevx = ccx; ccx = 2; update_view(ccy, prevx); break;
        case XK_3: prevx = ccx; ccx = 3; update_view(ccy, prevx); break;
        case XK_4: prevx = ccx; ccx = 4; update_view(ccy, prevx); break;
        case XK_5: prevx = ccx; ccx = 5; update_view(ccy, prevx); break;
        case XK_6: prevx = ccx; ccx = 6; update_view(ccy, prevx); break;
        case XK_7: prevx = ccx; ccx = 7; update_view(ccy, prevx); break;
        case XK_8: prevx = ccx; ccx = 8; update_view(ccy, prevx); break;
        case XK_9: prevx = ccx; ccx = 9; update_view(ccy, prevx); break;
        case XK_Up:
            prevy = ccy; ccy = clip(ccy-1);
            update_view(prevy, ccx); break;
        case XK_Down:
            prevy = ccy; ccy = clip(ccy+1);
            update_view(prevy, ccx); break;
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
        case XK_i:
            if (timer == OFF) {
                timer = ON;
                XftDrawRect(x11.fdraw, &x11.colors[LightBlue], x11.sw - 19*x11.font_width, 0,
                    19*x11.font_width, cellh/5);
            } else if (timer == ON) {
                timer = OFF;
                XftDrawRect(x11.fdraw, &x11.colors[LightBlue], x11.sw - 19*x11.font_width, 0,
                    19*x11.font_width, cellh/5);
            } else {
                timer = OFF;
                // undo the rectangle
                XftDrawRect(x11.fdraw, &x11.colors[Black], 0, x11.sh/2 - 100,
                            x11.sw, 200);
                update_view(ccy, ccx);
            }
            break;
        case XK_l:
            place_hand();
            break;
        case XK_u:
            pickup_hand();
            break;
        case XK_End:
            exit(0);
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
        get_title(c);

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
            update_hand();
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

    delete_client(c);
    update_cell_layout();
    draw_bar(&x11);
}

void
timer_update()
{
    time_t t;
    struct tm *tm_info;
    char tstr[20];

    // get current battery status
    FILE* fd_power = fopen("/sys/class/power_supply/AC/online", "r");
    FILE* fd_batt = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    int ac, bat;
    fscanf(fd_power, "%d", &ac);
    fscanf(fd_batt, "%d", &bat);
    fclose(fd_power);
    fclose(fd_batt);

    // get current time
    t = time(NULL);
    tm_info = localtime(&t);
    strftime(tstr, 20, "%a %b %e, %H:%M", tm_info);

    // run pomodoro checks and updates
    if (timer == ON) {
        timer_elapsed += 30;

        if (timer_elapsed >= timer_dur) {
            // end of period
            timer = ELAPSED;
            timer_elapsed = 0;
        }

        XftDrawRect(x11.fdraw, &x11.colors[Red], 0, x11.sh/2 - 100,
                    x11.sw, 200);
        // hide window temporarily
        Cell* c = &cells[ccy][ccx];
        if (c->primary != NULL)
            XUnmapWindow(x11.dpy, c->primary->win);
        if (c->secondary != NULL)
            XUnmapWindow(x11.dpy, c->secondary->win);
    }

    // draw battery info onto bar
    XftDrawRect(x11.fdraw, &x11.colors[White], x11.sw - 24*x11.font_width, 0,
                5*x11.font_width, cellh);
    char blvl[4];
    sprintf(blvl, "%3d", bat); blvl[3] = '%';
    if (bat < 20)
        XftDrawRect(x11.fdraw, &x11.colors[Red], x11.sw - 24*x11.font_width, 0,
                    5*x11.font_width, cellh);
    if (ac == 1)
        XftDrawRect(x11.fdraw, &x11.colors[LightBlue], x11.sw - 24*x11.font_width, 0,
                    5*x11.font_width, cellh/5);
    XftDrawString8(x11.fdraw, &x11.colors[Black], x11.font,
                x11.sw - 24*x11.font_width,
                cellh - 5,
                (XftChar8 *)&blvl, 4);

    // draw timer based background onto bar
    int tb_width = 19*x11.font_width;
    XftDrawRect(x11.fdraw, &x11.colors[White], x11.sw - tb_width, 0,
                tb_width, cellh);

    if (timer == ON)
        XftDrawRect(x11.fdraw, &x11.colors[Gray], x11.sw - tb_width, 0,
                    tb_width*timer_elapsed/timer_dur, cellh);

    // write time
    XftDrawString8(x11.fdraw, &x11.colors[Black], x11.font,
                x11.sw - 18*x11.font_width,
                cellh - 5,
                (XftChar8 *)&tstr, 20);

    XSync(x11.dpy, False);

    return;
}

int main() {
    if (!x11_setup(&x11))
        return 1;

    clients = NULL;
    hand = NULL;

    draw_bar(&x11);

    struct itimerspec delta;
    uint64_t exp;
    delta.it_value.tv_sec = 1; // initial timer
    delta.it_value.tv_nsec = 0;
    delta.it_interval.tv_sec = 30; // repeat interval
    delta.it_interval.tv_nsec = 0;    
    int tfd = timerfd_create(CLOCK_REALTIME, 0);
    timerfd_settime(tfd, 0, &delta, NULL);

    int maxfd;
    fd_set readable;
    XEvent ev;

    maxfd = tfd > x11.fd ? tfd : x11.fd;

    while(true) {
        FD_ZERO(&readable);
        FD_SET(tfd, &readable);
        FD_SET(x11.fd, &readable);

        select(maxfd + 1, &readable, NULL, NULL, NULL);

        if (FD_ISSET(tfd, &readable)) {
            read(tfd, &exp, sizeof(uint64_t));
            timer_update();
            continue;
        }

        while (XPending(x11.dpy)) {
            XNextEvent(x11.dpy, &ev);

            // windows on top need continuous drawing
            draw_hand();

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
}
