/*
 * rack.c - the virtual rack view
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <Xm/Xm.h>
#include <Xm/ScrolledW.h>
#include <Xm/ScrollBar.h>

#include "rack.h"
#include "skins.h"
#include "util.h"

#define MARGIN      12      /* rack border around the bricks */
#define BRICK_W     560
#define BADGE_X     9       /* sysid badge (bricks.scss .sysid) */
#define BADGE_Y     7
#define BADGE_W     62
#define BADGE_H     20

/* X state */
static Display     *dpy;
static Visual      *rvis;
static int          rdepth;
static Colormap     rcmap;
static int          own_cmap;
static unsigned long rmask_shift[3], rmask_bits[3], rmasks[3];

static Widget       sw, area;
static Window       win;
static Pixmap       back;
static int          back_w, back_h;
static GC           gc;
static XFontStruct *lcd_font, *label_font, *sysid_font;
static Pixmap       skin_pm[SKIN_COUNT];
static Cursor       hand_cursor;

/* content */
static L2Brick      bricks[L2_MAX_BRICKS];
static int          nbricks;
static int          brick_y[L2_MAX_BRICKS];
static char         selected[16];
static char         message[256] = "Not connected";
static RackClickFn  click_fn;
static RackFonts    font_names;

/* ------------------------------------------------------------------ */
/* colour helpers                                                      */
/* ------------------------------------------------------------------ */

static void mask_info(unsigned long mask, int i)
{
    unsigned long shift = 0, bits = 0;
    rmasks[i] = mask;
    if (mask) {
        while (!(mask & 1)) { mask >>= 1; shift++; }
        while (mask & 1) { mask >>= 1; bits++; }
    }
    rmask_shift[i] = shift;
    rmask_bits[i] = bits;
}

static unsigned long pack(unsigned int v, int i)
{
    unsigned long bits = rmask_bits[i];
    unsigned long x;
    if (bits >= 8) x = (unsigned long)v << (bits - 8);
    else x = (unsigned long)v >> (8 - bits);
    return (x << rmask_shift[i]) & rmasks[i];
}

static unsigned long rgb(unsigned int r, unsigned int g, unsigned int b)
{
    return pack(r, 0) | pack(g, 1) | pack(b, 2);
}

int rack_init_visual(Display *d, int screen, char *err, int errlen)
{
    XVisualInfo vi;
    Visual *defvis = DefaultVisual(d, screen);
    int found = 0;

    dpy = d;
    if (defvis->class == TrueColor && DefaultDepth(d, screen) >= 24) {
        rvis = defvis;
        rdepth = DefaultDepth(d, screen);
        rcmap = DefaultColormap(d, screen);
        own_cmap = 0;
        found = 1;
    } else if (XMatchVisualInfo(d, screen, 24, TrueColor, &vi)) {
        found = 1;
    } else if (XMatchVisualInfo(d, screen, 32, TrueColor, &vi)) {
        found = 1;
    }
    if (found && !rvis) {
        rvis = vi.visual;
        rdepth = vi.depth;
        rcmap = XCreateColormap(d, RootWindow(d, screen), rvis, AllocNone);
        own_cmap = 1;
    }
    if (!found) {
        s_copy(err, "No 24 bit TrueColor visual available on this display.\n"
                    "sgil2gui needs a 24 bit TrueColor capable X server.", (size_t)errlen);
        return 0;
    }
    mask_info(rvis->red_mask, 0);
    mask_info(rvis->green_mask, 1);
    mask_info(rvis->blue_mask, 2);
    return 1;
}

/* ------------------------------------------------------------------ */
/* skin pixmaps                                                        */
/* ------------------------------------------------------------------ */

/* darken the rounded sysid badge area (rgba(0,0,0,0.5), radius 4) */
static void darken_badge(unsigned char *rgbbuf, int w, int h)
{
    int x, y, dx, dy, r = 4;
    unsigned char *p;
    for (y = BADGE_Y; y < BADGE_Y + BADGE_H && y < h; y++) {
        for (x = BADGE_X; x < BADGE_X + BADGE_W && x < w; x++) {
            dx = 0;
            dy = 0;
            if (x < BADGE_X + r) dx = BADGE_X + r - x;
            else if (x >= BADGE_X + BADGE_W - r) dx = x - (BADGE_X + BADGE_W - r - 1);
            if (y < BADGE_Y + r) dy = BADGE_Y + r - y;
            else if (y >= BADGE_Y + BADGE_H - r) dy = y - (BADGE_Y + BADGE_H - r - 1);
            if (dx * dx + dy * dy > r * r) continue;
            p = rgbbuf + (y * w + x) * 3;
            p[0] = (unsigned char)(p[0] / 2);
            p[1] = (unsigned char)(p[1] / 2);
            p[2] = (unsigned char)(p[2] / 2);
        }
    }
}

static Pixmap skin_pixmap(int skin)
{
    const SkinDef *sd;
    unsigned char *rgbbuf, *p;
    XImage *xi;
    int x, y, w, h;

    if (skin < 0 || skin >= SKIN_COUNT) skin = SKIN_O350;
    if (skin_pm[skin]) return skin_pm[skin];
    sd = skin_get(skin);
    w = sd->img->width;
    h = sd->img->height;
    rgbbuf = img_decode_rgb(sd->img);
    if (!rgbbuf) {
        fprintf(stderr, "sgil2gui: corrupt image for skin %s\n", l2_skin_name(skin));
        return None;
    }
    darken_badge(rgbbuf, w, h);

    xi = XCreateImage(dpy, rvis, (unsigned)rdepth, ZPixmap, 0, NULL,
                      (unsigned)w, (unsigned)h, 32, 0);
    xi->data = (char *)xmalloc((size_t)xi->bytes_per_line * (size_t)h);
    p = rgbbuf;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            XPutPixel(xi, x, y, rgb(p[0], p[1], p[2]));
            p += 3;
        }
    }
    free(rgbbuf);
    skin_pm[skin] = XCreatePixmap(dpy, win, (unsigned)w, (unsigned)h, (unsigned)rdepth);
    XPutImage(dpy, skin_pm[skin], gc, xi, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    XDestroyImage(xi);
    return skin_pm[skin];
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static XFontStruct *load_font(const char *preferred, const char **fallbacks)
{
    XFontStruct *f = NULL;
    int i;
    if (preferred && *preferred) f = XLoadQueryFont(dpy, preferred);
    for (i = 0; !f && fallbacks[i]; i++) f = XLoadQueryFont(dpy, fallbacks[i]);
    if (!f) f = XLoadQueryFont(dpy, "fixed");
    return f;
}

static void fill_round(Drawable d, int x, int y, int w, int h)
{
    int r = (w < h ? w : h) / 2;
    int dd = 2 * r;
    if (r < 1 || w < 3 || h < 3) {
        XFillRectangle(dpy, d, gc, x, y, (unsigned)w, (unsigned)h);
        return;
    }
    if (w - dd > 0) XFillRectangle(dpy, d, gc, x + r, y, (unsigned)(w - dd), (unsigned)h);
    if (h - dd > 0) XFillRectangle(dpy, d, gc, x, y + r, (unsigned)w, (unsigned)(h - dd));
    XFillArc(dpy, d, gc, x, y, (unsigned)dd, (unsigned)dd, 0, 360 * 64);
    XFillArc(dpy, d, gc, x + w - dd, y, (unsigned)dd, (unsigned)dd, 0, 360 * 64);
    XFillArc(dpy, d, gc, x, y + h - dd, (unsigned)dd, (unsigned)dd, 0, 360 * 64);
    XFillArc(dpy, d, gc, x + w - dd, y + h - dd, (unsigned)dd, (unsigned)dd, 0, 360 * 64);
}

/* CSS: 1px black border around a w x h coloured box with round corners */
static void draw_led(Drawable d, int ox, int oy, const SRect *r, unsigned long color)
{
    XSetForeground(dpy, gc, rgb(0, 0, 0));
    fill_round(d, ox + r->x, oy + r->y, r->w + 2, r->h + 2);
    XSetForeground(dpy, gc, color);
    fill_round(d, ox + r->x + 1, oy + r->y + 1, r->w, r->h);
}

static void draw_text(Drawable d, XFontStruct *f, int x, int y, const char *s)
{
    if (!f || !s || !*s) return;
    XSetFont(dpy, gc, f->fid);
    XDrawString(dpy, d, gc, x, y, s, (int)strlen(s));
}

static void draw_brick(Drawable d, const L2Brick *b, int ox, int oy)
{
    const SkinDef *sd = skin_get(b->skin);
    const L2Display *dp = &b->display;
    Pixmap pm = skin_pixmap(b->skin);
    XRectangle clip;
    int w = sd->img->width, h = sd->img->height;
    int lh;

    /* 1px black border, image inside */
    XSetForeground(dpy, gc, rgb(0, 0, 0));
    XFillRectangle(dpy, d, gc, ox - 1, oy - 1, (unsigned)(w + 2), (unsigned)(h + 2));
    if (pm) XCopyArea(dpy, pm, d, gc, 0, 0, (unsigned)w, (unsigned)h, ox, oy);

    /* LCD - two lines, clipped (overflow: hidden) */
    if (lcd_font) {
        lh = sd->lcd.h;
        if (lh < lcd_font->ascent) lh = lcd_font->ascent;
        clip.x = (short)(ox + sd->lcd.x);
        clip.y = (short)(oy + sd->lcd.y);
        clip.width = (unsigned short)sd->lcd.w;
        clip.height = (unsigned short)(2 * lh + lcd_font->descent);
        XSetClipRectangles(dpy, gc, 0, 0, &clip, 1, Unsorted);
        XSetForeground(dpy, gc, rgb(0, 0, 0));
        draw_text(d, lcd_font, clip.x, clip.y + lcd_font->ascent, dp->line1);
        draw_text(d, lcd_font, clip.x, clip.y + lh + lcd_font->ascent, dp->line2);
        XSetClipMask(dpy, gc, None);
    }

    /* LEDs: fault red, attention amber, power green */
    draw_led(d, ox, oy, &sd->power, dp->pwr ? rgb(173, 255, 47) : rgb(0, 121, 11));
    draw_led(d, ox, oy, &sd->attention, dp->service ? rgb(255, 200, 47) : rgb(128, 128, 128));
    draw_led(d, ox, oy, &sd->fault, dp->fault ? rgb(255, 47, 47) : rgb(128, 128, 128));

    /* product label */
    if (sd->labelX >= 0 && label_font) {
        XSetForeground(dpy, gc, rgb(255, 255, 255));
        draw_text(d, label_font, ox + sd->labelX, oy + sd->labelY + label_font->ascent,
                  b->productLabel[0] ? b->productLabel : "sgi origin 350");
    }

    /* sysid badge text (background darkened in the pixmap) */
    if (sysid_font) {
        XSetForeground(dpy, gc, rgb(255, 255, 255));
        draw_text(d, sysid_font, ox + BADGE_X + 6,
                  oy + BADGE_Y + (BADGE_H + sysid_font->ascent - sysid_font->descent) / 2,
                  b->id);
    }

    /* selection outline */
    if (selected[0] && strcmp(selected, b->id) == 0) {
        XSetForeground(dpy, gc, rgb(255, 200, 47));
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, d, gc, ox - 2, oy - 2, (unsigned)(w + 3), (unsigned)(h + 3));
        XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
    }
}

static void layout(int *tw, int *th)
{
    int i, y = MARGIN + 1;
    for (i = 0; i < nbricks; i++) {
        brick_y[i] = y;
        y += skin_get(bricks[i].skin)->img->height + 2;
    }
    *tw = BRICK_W + 2 + 2 * MARGIN;
    *th = nbricks > 0 ? y - 1 + MARGIN : 120;
}

static void render(void)
{
    int i, tw, th;
    if (!win) return;
    layout(&tw, &th);
    if (!back || tw != back_w || th != back_h) {
        if (back) XFreePixmap(dpy, back);
        back = XCreatePixmap(dpy, win, (unsigned)tw, (unsigned)th, (unsigned)rdepth);
        back_w = tw;
        back_h = th;
        XtVaSetValues(area, XmNwidth, (Dimension)tw, XmNheight, (Dimension)th, NULL);
        XResizeWindow(dpy, win, (unsigned)tw, (unsigned)th);
    }
    XSetForeground(dpy, gc, rgb(RACK_BG_R, RACK_BG_G, RACK_BG_B));
    XFillRectangle(dpy, back, gc, 0, 0, (unsigned)tw, (unsigned)th);
    /* rack rails */
    XSetForeground(dpy, gc, rgb(70, 70, 74));
    XFillRectangle(dpy, back, gc, 0, 0, (unsigned)(MARGIN - 3), (unsigned)th);
    XFillRectangle(dpy, back, gc, tw - MARGIN + 3, 0, (unsigned)(MARGIN - 3), (unsigned)th);
    XSetForeground(dpy, gc, rgb(0, 0, 0));
    XDrawRectangle(dpy, back, gc, 0, 0, (unsigned)(tw - 1), (unsigned)(th - 1));

    if (nbricks == 0) {
        XSetForeground(dpy, gc, rgb(200, 200, 200));
        draw_text(back, label_font, MARGIN + 20, th / 2, message);
    }
    for (i = 0; i < nbricks; i++)
        draw_brick(back, &bricks[i], MARGIN + 1, brick_y[i]);

    XCopyArea(dpy, back, win, gc, 0, 0, (unsigned)tw, (unsigned)th, 0, 0);
}

/* ------------------------------------------------------------------ */
/* events                                                              */
/* ------------------------------------------------------------------ */

static void scroll_by(int dir)
{
    Widget vsb = NULL;
    int val, slider, inc, pinc, min, max;
    XtVaGetValues(sw, XmNverticalScrollBar, &vsb, NULL);
    if (!vsb || !XtIsManaged(vsb)) return;
    XmScrollBarGetValues(vsb, &val, &slider, &inc, &pinc);
    XtVaGetValues(vsb, XmNminimum, &min, XmNmaximum, &max, NULL);
    val += dir * 60;
    if (val < min) val = min;
    if (val > max - slider) val = max - slider;
    XmScrollBarSetValues(vsb, val, slider, inc, pinc, True);
}

static void on_event(Widget w, XtPointer cd, XEvent *ev, Boolean *cont)
{
    int i, h;
    (void)w;
    (void)cd;
    (void)cont;
    if (ev->xany.window != win) return;
    if (ev->type == Expose) {
        if (back)
            XCopyArea(dpy, back, win, gc, ev->xexpose.x, ev->xexpose.y,
                      (unsigned)ev->xexpose.width, (unsigned)ev->xexpose.height,
                      ev->xexpose.x, ev->xexpose.y);
    } else if (ev->type == ButtonPress) {
        if (ev->xbutton.button == Button4) {
            scroll_by(-1);
            return;
        }
        if (ev->xbutton.button == Button5) {
            scroll_by(1);
            return;
        }
        if (ev->xbutton.button != Button1) return;
        for (i = 0; i < nbricks; i++) {
            h = skin_get(bricks[i].skin)->img->height;
            if (ev->xbutton.y >= brick_y[i] - 1 && ev->xbutton.y < brick_y[i] + h + 1 &&
                ev->xbutton.x >= MARGIN && ev->xbutton.x < MARGIN + BRICK_W + 2) {
                if (click_fn) click_fn(&bricks[i]);
                return;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

Widget rack_create(Widget parent, const char *name, Arg *args, Cardinal nargs,
                   const RackFonts *fonts)
{
    Arg a[8];
    Cardinal n = 0, i;
    int tw, th;

    for (i = 0; i < nargs && n < 4; i++) a[n++] = args[i];
    XtSetArg(a[n], XmNscrollingPolicy, XmAUTOMATIC); n++;
    XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmAS_NEEDED); n++;
    sw = XmCreateScrolledWindow(parent, (char *)name, a, n);

    layout(&tw, &th);
    /* plain Core widget: no expose method, all events go to our handler */
    area = XtVaCreateManagedWidget("rackArea", widgetClass, sw,
                                   XmNwidth, (Dimension)tw,
                                   XmNheight, (Dimension)th,
                                   XmNborderWidth, 0,
                                   NULL);
    if (fonts) font_names = *fonts;
    XtManageChild(sw);
    return sw;
}

void rack_realize(Widget shell)
{
    XSetWindowAttributes wa;
    Window wins[2];
    int tw, th;
    static const char *lcd_fb[] = {
        "-adobe-helvetica-medium-r-normal--8-*-*-*-p-*-iso8859-1",
        "-*-helvetica-medium-r-normal--8-*-*-*-*-*-*-*",
        "-misc-fixed-medium-r-normal--8-*-*-*-*-*-*-*",
        "5x8", "6x10", NULL
    };
    static const char *label_fb[] = {
        "-adobe-helvetica-bold-r-normal--12-*-*-*-p-*-iso8859-1",
        "-*-helvetica-bold-r-normal--12-*-*-*-*-*-*-*",
        "-*-*-bold-r-normal--12-*-*-*-*-*-*-*",
        "6x13bold", NULL
    };
    static const char *sysid_fb[] = {
        "-adobe-helvetica-bold-r-normal--14-*-*-*-p-*-iso8859-1",
        "-*-helvetica-bold-r-normal--14-*-*-*-*-*-*-*",
        "-*-*-bold-r-normal--14-*-*-*-*-*-*-*",
        "7x13bold", NULL
    };

    layout(&tw, &th);
    wa.colormap = rcmap;
    wa.border_pixel = 0;
    wa.background_pixel = rgb(RACK_BG_R, RACK_BG_G, RACK_BG_B);
    wa.event_mask = ExposureMask | ButtonPressMask;
    win = XCreateWindow(dpy, XtWindow(area), 0, 0, (unsigned)tw, (unsigned)th, 0, rdepth,
                        InputOutput, rvis,
                        CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &wa);
    XtRegisterDrawable(dpy, win, area);
    XtAddRawEventHandler(area, ExposureMask | ButtonPressMask, False, on_event, NULL);

    hand_cursor = XCreateFontCursor(dpy, XC_hand2);
    XDefineCursor(dpy, win, hand_cursor);
    XMapWindow(dpy, win);

    if (own_cmap) {
        wins[0] = win;
        wins[1] = XtWindow(shell);
        XSetWMColormapWindows(dpy, XtWindow(shell), wins, 2);
    }

    gc = XCreateGC(dpy, win, 0, NULL);
    lcd_font = load_font(font_names.lcdFont, lcd_fb);
    label_font = load_font(font_names.labelFont, label_fb);
    sysid_font = load_font(font_names.sysidFont, sysid_fb);
    render();
}

void rack_set_bricks(const L2Brick *b, int n)
{
    if (n > L2_MAX_BRICKS) n = L2_MAX_BRICKS;
    if (n > 0) memcpy(bricks, b, sizeof(L2Brick) * (size_t)n);
    nbricks = n;
    render();
}

void rack_update_brick(const L2Brick *b)
{
    int i;
    for (i = 0; i < nbricks; i++) {
        if (strcmp(bricks[i].id, b->id) == 0) {
            bricks[i] = *b;
            render();
            return;
        }
    }
}

int rack_count(void)
{
    return nbricks;
}

const L2Brick *rack_find(const char *id)
{
    int i;
    for (i = 0; i < nbricks; i++)
        if (strcmp(bricks[i].id, id) == 0) return &bricks[i];
    return NULL;
}

void rack_set_selected(const char *id)
{
    s_copy(selected, id ? id : "", sizeof(selected));
    render();
}

void rack_set_click_callback(RackClickFn fn)
{
    click_fn = fn;
}

void rack_set_message(const char *msg)
{
    s_copy(message, msg ? msg : "", sizeof(message));
    if (nbricks == 0) render();
}

void rack_preferred_size(int *w, int *h)
{
    int tw, th;
    layout(&tw, &th);
    *w = tw + 44;   /* room for the vertical scrollbar, shadows and spacing */
    *h = th + 8;
}
