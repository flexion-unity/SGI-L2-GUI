/*
 * config.h - application settings (~/.sgil2guirc)
 */
#ifndef SGIL2_CONFIG_H
#define SGIL2_CONFIG_H


typedef struct {
    char host[128];
    int  l2port;            /* the L2's command port (8000) */
    char password[64];      /* only if the L2 has one (l2passwd) */
    int  consoleQuiet;      /* L2 Terminal: port 8003 (not logged) instead of 8001 */
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
/* the L2 Terminal's port: 8001, or 8003 with consoleQuiet */
int         cfg_console_port(const AppConfig *c);

#endif
