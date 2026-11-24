/*
 * app.h - shared application state and actions
 */
#ifndef SGIL2_APP_H
#define SGIL2_APP_H

#include <X11/Intrinsic.h>
#include "config.h"
#include "l2parse.h"

#define APP_NAME     "SGI-L2-GUI"
#define APP_CLASS    "SgiL2Gui"
#define APP_VERSION  "1.0.0"

extern XtAppContext g_app;
extern Widget       g_toplevel;
extern AppConfig    g_cfg;
extern char         g_cfgpath[512];
extern int          g_demo;

/* main.c */
void app_status(const char *msg, int busy);
void app_refresh_rack(void);
void app_refresh_brick(const char *id);
void app_power(const char *target, int up);         /* target NULL = whole rack */
void app_power_confirm(const char *target, int up);  /* asks first if configured */
void app_set_led(const char *id, int failure, int on);
void app_show_command(const char *title, const char *cmd);
void app_settings_changed(int reconnect);
L2ParseOpts app_parse_opts(void);

/* dialogs.c */
typedef void (*ConfirmFn)(const char *arg, int iarg);
void dlg_confirm(const char *title, const char *msg, ConfirmFn fn, const char *arg, int iarg);
void dlg_error(const char *msg);
void dlg_text(const char *title, const char *text);
void dlg_about(void);
void dlg_settings(void);
void dlg_console(void);
void dlg_rackinfo(void);
void brickdlg_show(const L2Brick *b);
void brickdlg_update(const L2Brick *b);
void brickdlg_busy(const char *id, int busy);

#endif
