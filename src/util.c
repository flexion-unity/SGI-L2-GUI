/*
 * util.c - small portable string / buffer helpers
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "util.h"

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "sgil2gui: out of memory\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "sgil2gui: out of memory\n");
        exit(1);
    }
    return q;
}

void s_copy(char *dst, const char *src, size_t size)
{
    size_t n;
    if (size == 0) return;
    if (!src) src = "";
    n = strlen(src);
    if (n >= size) n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void s_ncopy(char *dst, const char *src, size_t n, size_t size)
{
    if (size == 0) return;
    if (n >= size) n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void s_cat(char *dst, const char *src, size_t size)
{
    size_t l = strlen(dst);
    if (l >= size) return;
    s_copy(dst + l, src, size - l);
}

char *s_dup(const char *s)
{
    char *p;
    size_t n;
    if (!s) s = "";
    n = strlen(s);
    p = (char *)xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

void s_rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

char *s_trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    s_rtrim(s);
    return s;
}

int s_startswith(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int s_endswith(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    if (b > a) return 0;
    return strcmp(s + a - b, suffix) == 0;
}

int s_contains_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    const char *h;
    size_t i;
    if (nl == 0) return 1;
    for (h = hay; *h; h++) {
        for (i = 0; i < nl; i++) {
            if (!h[i]) return 0;
            if (tolower((unsigned char)h[i]) != tolower((unsigned char)needle[i]))
                break;
        }
        if (i == nl) return 1;
    }
    return 0;
}

int s_equal_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

void s_strip_nonprint(char *s)
{
    char *w = s;
    unsigned char c;
    for (; *s; s++) {
        c = (unsigned char)*s;
        if (c == '\t' || c == '\n' || c == '\r' || (c >= 0x20 && c <= 0x7e))
            *w++ = (char)c;
    }
    *w = '\0';
}

void s_itoa(int v, char *buf, size_t size)
{
    char tmp[32];
    sprintf(tmp, "%d", v);
    s_copy(buf, tmp, size);
}

void dbuf_init(DBuf *b)
{
    b->cap = 256;
    b->len = 0;
    b->data = (char *)xmalloc(b->cap);
    b->data[0] = '\0';
}

void dbuf_free(DBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

void dbuf_clear(DBuf *b)
{
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

void dbuf_append(DBuf *b, const char *s, size_t n)
{
    if (!b->data) dbuf_init(b);
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->data = (char *)xrealloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void dbuf_appends(DBuf *b, const char *s)
{
    dbuf_append(b, s, strlen(s));
}

void dbuf_appendc(DBuf *b, char c)
{
    dbuf_append(b, &c, 1);
}
