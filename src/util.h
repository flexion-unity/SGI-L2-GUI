/*
 * util.h - small portable string / buffer helpers (C89, no SUSv2 deps)
 */
#ifndef SGIL2_UTIL_H
#define SGIL2_UTIL_H

#include <stddef.h>

/* bounded copy / append, always NUL terminated */
void  s_copy(char *dst, const char *src, size_t size);
void  s_ncopy(char *dst, const char *src, size_t n, size_t size);
void  s_cat(char *dst, const char *src, size_t size);
char *s_dup(const char *s);
/* trim leading/trailing whitespace in place; returns pointer into s */
char *s_trim(char *s);
void  s_rtrim(char *s);
int   s_startswith(const char *s, const char *prefix);
int   s_endswith(const char *s, const char *suffix);
/* case-insensitive substring search, returns 1 if found */
int   s_contains_ci(const char *hay, const char *needle);
int   s_equal_ci(const char *a, const char *b);
/* remove everything but TAB, LF, CR and 0x20..0x7E in place */
void  s_strip_nonprint(char *s);
/* format an int into a static-free buffer */
void  s_itoa(int v, char *buf, size_t size);

/* growable byte buffer, always NUL terminated */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} DBuf;

void dbuf_init(DBuf *b);
void dbuf_free(DBuf *b);
void dbuf_clear(DBuf *b);
void dbuf_append(DBuf *b, const char *s, size_t n);
void dbuf_appends(DBuf *b, const char *s);
void dbuf_appendc(DBuf *b, char c);

/* malloc that exits on failure */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);

#endif
