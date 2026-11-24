/*
 * config.h - application settings (~/.sgil2guirc)
 */
#ifndef SGIL2_CONFIG_H
#define SGIL2_CONFIG_H

enum { EOL_LF = 0, EOL_CRLF, EOL_CR };

typedef struct {
    char host[128];
    int  port;
    char prompt[64];        /* empty = auto detect after connect */
    int  eol;               /* line ending sent after commands */
    int  timeout;           /* command timeout in seconds */
    int  sortMode;          /* L2_SORT_* */
    char sortOrder[512];    /* custom order, space separated brick ids */
    int  routerAltix;       /* draw routers with the Altix bezel */
    int  altix350Grey;      /* draw Altix 350 with the grey bezel */
    int  refresh;           /* auto refresh interval in seconds, 0 = off */
    int  confirmPower;      /* ask before power up / down */
} AppConfig;

void        cfg_defaults(AppConfig *c);
/* returns 1 if the file was read, 0 if missing; errors go to stderr */
int         cfg_load(AppConfig *c, const char *path);
int         cfg_save(const AppConfig *c, const char *path);
const char *cfg_default_path(void);
const char *cfg_eol_string(int eol);

#endif
