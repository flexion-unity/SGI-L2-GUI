/*
 * demo.c - offline L2 simulator
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo.h"
#include "util.h"

typedef struct {
    const char *id;
    const char *slot;
    const char *partition;
    const char *type;
    const char *serial;
    int pwr, service, fault;
} DemoBrick;

/* a mixed rack showing every skin */
static DemoBrick demo_bricks[] = {
    { "001r20", "20", "N/A",  "R [1MB flash]",                   "NEA799", 1, 0, 0 },
    { "001c17", "17", "none", "C (IP45) [2MB flash]",            "NCC120", 1, 0, 0 },
    { "001c15", "15", "none", "C (IP45) [2MB flash]",            "NCC121", 0, 0, 0 },
    { "001c13", "13", "none", "Chimera Server [2MB flash]",      "MHX310", 1, 1, 0 },
    { "001c11", "11", "none", "Chimera Blade [2MB flash]",       "MHX402", 1, 0, 0 },
    { "001c09", "09", "none", "C (IP53 Rackmount) [2MB flash]",  "MTZ007", 0, 0, 0 },
    { "001c07", "07", "none", "Opus [2MB flash]",                "OPS123", 1, 0, 1 },
    { "001r05", "05", "N/A",  "NL4R [2MB flash]",                "NLR004", 1, 0, 0 },
    { "001i03", "03", "none", "IX [2MB flash]",                  "IXB001", 1, 0, 0 },
    { "001c01", "01", "none", "C (IP41) [2MB flash]",            "CBR010", 1, 0, 0 }
};
#define NDEMO ((int)(sizeof(demo_bricks) / sizeof(demo_bricks[0])))

static int demo_tick = 0;

void demo_init(void)
{
    demo_tick = 0;
}

static DemoBrick *find(const char *id)
{
    int i;
    for (i = 0; i < NDEMO; i++)
        if (strcmp(demo_bricks[i].id, id) == 0) return &demo_bricks[i];
    return NULL;
}

static void emit_display(DBuf *o, DemoBrick *b)
{
    static const char spin[] = "|/-\\";
    char line[256];
    sprintf(line, "%s:\n", b->id);
    dbuf_appends(o, line);
    dbuf_appends(o, "revision: 2.0\n");
    sprintf(line, "line 1: \"%s     %c\"\n", b->id, b->pwr ? spin[demo_tick % 4] : '|');
    dbuf_appends(o, line);
    sprintf(line, "line 2: \"%s\"\n", b->fault ? "FAULT: PS 1" : (b->pwr ? "Powered Up" : "Powered Down"));
    dbuf_appends(o, line);
    sprintf(line, "leds:  PWR=%s   SERVICE=%s     FAULT=%s\n",
            b->pwr ? "on" : "off", b->service ? "on" : "off", b->fault ? "on" : "off");
    dbuf_appends(o, line);
}

static void emit_brick(DBuf *o, DemoBrick *b)
{
    char line[256];
    sprintf(line, "%s:\n", b->id);
    dbuf_appends(o, line);
    sprintf(line, "rack: 001, slot: %s, partition: %s, type: %s, serial:%s, source: EEPROM\n\n",
            b->slot, b->partition, b->type, b->serial);
    dbuf_appends(o, line);
}

char *demo_exec(const char *cmdin)
{
    DBuf o;
    char cmd[256], *rest, *res;
    DemoBrick *b = NULL;
    int i, n0, n1;
    char line[256];

    dbuf_init(&o);
    s_copy(cmd, cmdin, sizeof(cmd));
    rest = s_trim(cmd);

    /* "<sysid> <command>" when the first word is a known brick id */
    {
        char first[32];
        size_t k = 0;
        while (rest[k] && rest[k] != ' ' && k < sizeof(first) - 1) {
            first[k] = rest[k];
            k++;
        }
        first[k] = '\0';
        b = find(first);
        if (b) rest = s_trim(rest + k);
    }

    if (strcmp(rest, "display") == 0) {
        demo_tick++;
        for (i = 0; i < NDEMO; i++)
            if (!b || &demo_bricks[i] == b) emit_display(&o, &demo_bricks[i]);
    } else if (strcmp(rest, "brick") == 0) {
        for (i = 0; i < NDEMO; i++)
            if (!b || &demo_bricks[i] == b) emit_brick(&o, &demo_bricks[i]);
    } else if (s_startswith(rest, "display attention ") || s_startswith(rest, "display failure ")) {
        int on = s_endswith(rest, " on");
        for (i = 0; i < NDEMO; i++) {
            if (b && &demo_bricks[i] != b) continue;
            if (s_startswith(rest, "display attention ")) demo_bricks[i].service = on;
            else demo_bricks[i].fault = on;
        }
    } else if (strcmp(rest, "power up") == 0 || strcmp(rest, "power down") == 0) {
        int up = strcmp(rest, "power up") == 0;
        for (i = 0; i < NDEMO; i++) {
            if (b && &demo_bricks[i] != b) continue;
            demo_bricks[i].pwr = up;
            sprintf(line, "%s:\npower %s\n", demo_bricks[i].id, up ? "up" : "down");
            dbuf_appends(&o, line);
        }
    } else if (strcmp(rest, "config") == 0) {
        dbuf_appends(&o, "L2 192.168.0.100: - 001 (LOCAL)\n");
        for (i = 0; i < NDEMO; i++) {
            sprintf(line, "L1 192.168.0.100:0:%d - %s\n", i, demo_bricks[i].id);
            dbuf_appends(&o, line);
        }
    } else if (strcmp(rest, "env summary") == 0 || strcmp(rest, "env") == 0) {
        for (i = 0; i < NDEMO; i++) {
            if (b && &demo_bricks[i] != b) continue;
            sprintf(line, "%s:\nEnvironmental monitoring is enabled and running.\n\n",
                    demo_bricks[i].id);
            dbuf_appends(&o, line);
        }
    } else if (strcmp(rest, "fan") == 0) {
        for (i = 0; i < NDEMO; i++) {
            if (b && &demo_bricks[i] != b) continue;
            n0 = 2900 + i * 13;
            n1 = 3050 - i * 7;
            sprintf(line, "%s:\nfan speed:\n  fan 0 %d RPM\n  fan 1 %d RPM\n\n",
                    demo_bricks[i].id, n0, n1);
            dbuf_appends(&o, line);
        }
    } else if (strcmp(rest, "help") == 0) {
        dbuf_appends(&o, "demo L2: display, brick, config, power up|down, env summary,\n"
                         "fan, <sysid> display attention|failure on|off\n");
    } else if (*rest == '\0') {
        /* empty line */
    } else {
        sprintf(line, "L2 demo: '%.200s' not simulated\n", rest);
        dbuf_appends(&o, line);
    }
    res = s_dup(o.data);
    dbuf_free(&o);
    return res;
}
