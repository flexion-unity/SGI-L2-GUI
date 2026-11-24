/*
 * skins.h - per brick skin image and overlay geometry
 *
 * Coordinates are taken from the "@media (min-width: 900px)" blocks of
 * SGI-L2-WUI L2-WUI/src/app/components/bricks/<skin>/<skin>.scss, which
 * lay out the bricks at 560 pixels wide - the width png2c.py scales to.
 */
#ifndef SGIL2_SKINS_H
#define SGIL2_SKINS_H

#include "image.h"

/* background the PNG alpha was flattened onto (tools/png2c.py RACK_BG) */
#define RACK_BG_R 40
#define RACK_BG_G 40
#define RACK_BG_B 40

typedef struct {
    short x, y, w, h;
} SRect;

typedef struct {
    const EmbImage *img;
    SRect lcd;          /* h = line height in px (two lines) */
    SRect power;
    SRect attention;
    SRect fault;
    short labelX;       /* product label, -1 = not shown */
    short labelY;
} SkinDef;

const SkinDef *skin_get(int skin);

#endif
