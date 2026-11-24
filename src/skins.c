/*
 * skins.c - per brick skin image and overlay geometry
 */
#include "skins.h"
#include "l2parse.h"
#include "img/images.h"

/*                img                  lcd x,y,w,lineH       power             attention          fault             label */
static const SkinDef skins[SKIN_COUNT] = {
    /* SKIN_O300 */
    { &img_o300,         { 250, 26, 62, 10 }, { 277, 63, 11, 11 }, { 246, 61, 15, 3 }, { 246, 71, 15, 3 }, -1, -1 },
    /* SKIN_O350 */
    { &img_o350,         { 250, 42, 63, 9 },  { 280, 74, 3, 8 },   { 264, 72, 2, 11 }, { 254, 72, 2, 11 }, 371, 86 },
    /* SKIN_TEZRO */
    { &img_tezro,        { 250, 42, 63, 9 },  { 280, 74, 3, 8 },   { 264, 72, 2, 11 }, { 254, 72, 2, 11 }, 371, 86 },
    /* SKIN_A350_YELLOW */
    { &img_a350_yellow,  { 250, 42, 63, 9 },  { 280, 74, 3, 8 },   { 264, 72, 2, 11 }, { 254, 72, 2, 11 }, -1, -1 },
    /* SKIN_A350_GREY */
    { &img_a350_grey,    { 250, 42, 63, 9 },  { 280, 74, 3, 8 },   { 264, 72, 2, 11 }, { 254, 72, 2, 11 }, -1, -1 },
    /* SKIN_A3000 */
    { &img_a3000,        { 467, 58, 47, 7 },  { 483, 95, 11, 11 }, { 464, 89, 8, 8 },  { 464, 104, 8, 8 }, -1, -1 },
    /* SKIN_A3000IX */
    { &img_a3000ix,      { 467, 110, 47, 7 }, { 483, 147, 11, 11 },{ 464, 141, 8, 8 }, { 464, 157, 8, 8 }, -1, -1 },
    /* SKIN_NL4R */
    { &img_nl4r,         { 250, 42, 63, 9 },  { 280, 74, 3, 8 },   { 264, 72, 2, 11 }, { 254, 72, 2, 11 }, -1, -1 },
    /* SKIN_ROUTER */
    { &img_router,       { 459, 27, 44, 7 },  { 477, 56, 9, 9 },   { 456, 48, 8, 8 },  { 456, 59, 8, 8 },  -1, -1 },
    /* SKIN_ROUTER_ALTIX */
    { &img_router_altix, { 471, 29, 47, 7 },  { 487, 58, 11, 11 }, { 468, 52, 8, 8 },  { 468, 67, 8, 8 },  -1, -1 }
};

const SkinDef *skin_get(int skin)
{
    if (skin < 0 || skin >= SKIN_COUNT) skin = SKIN_O350;
    return &skins[skin];
}
