/*
 * image.c - decode embedded PackBits/palette images
 */
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "util.h"

unsigned char *img_decode_rgb(const EmbImage *img)
{
    unsigned long total = (unsigned long)img->width * img->height;
    unsigned long out = 0, i = 0, k, cnt;
    unsigned char *rgb, *dst;
    const unsigned char *pal = img->palette;
    unsigned int n, idx;

    rgb = (unsigned char *)xmalloc(total * 3);
    dst = rgb;
    while (out < total && i < img->datalen) {
        n = img->data[i++];
        if (n < 128) {
            cnt = n + 1;
            if (i + cnt > img->datalen || out + cnt > total) break;
            for (k = 0; k < cnt; k++) {
                idx = img->data[i++] * 3;
                *dst++ = pal[idx];
                *dst++ = pal[idx + 1];
                *dst++ = pal[idx + 2];
            }
            out += cnt;
        } else if (n > 128) {
            cnt = 257 - n;
            if (i >= img->datalen || out + cnt > total) break;
            idx = img->data[i++] * 3;
            for (k = 0; k < cnt; k++) {
                *dst++ = pal[idx];
                *dst++ = pal[idx + 1];
                *dst++ = pal[idx + 2];
            }
            out += cnt;
        }
    }
    if (out != total) {
        free(rgb);
        return NULL;
    }
    return rgb;
}
