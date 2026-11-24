/*
 * rack.h - the virtual rack view
 *
 * The Motif UI runs on the default visual (often 8 bit PseudoColor on
 * SGI hardware). The rack itself is drawn into a plain X child window
 * that uses a 24 bit TrueColor visual with its own colormap, so the
 * brick photos render at full colour regardless of the default visual.
 */
#ifndef SGIL2_RACK_H
#define SGIL2_RACK_H

#include <X11/Intrinsic.h>
#include "l2parse.h"

typedef void (*RackClickFn)(const L2Brick *brick);

typedef struct {
    char *lcdFont;
    char *labelFont;
    char *sysidFont;
} RackFonts;

/* find a TrueColor visual; returns 0 (with message in err) if none */
int  rack_init_visual(Display *dpy, int screen, char *err, int errlen);
/* create the scrolled rack view as child of parent; returns the scrolled window */
Widget rack_create(Widget parent, const char *name, Arg *args, Cardinal nargs,
                   const RackFonts *fonts);
/* call once after the toplevel shell was realized */
void rack_realize(Widget shell);

void rack_set_bricks(const L2Brick *bricks, int n);
void rack_update_brick(const L2Brick *brick);   /* matched by id */
int  rack_count(void);
const L2Brick *rack_find(const char *id);
void rack_set_selected(const char *id);         /* NULL = none */
void rack_set_click_callback(RackClickFn fn);
void rack_set_message(const char *msg);         /* shown when the rack is empty */
/* preferred size of the scrolled window for the current content */
void rack_preferred_size(int *w, int *h);

#endif
