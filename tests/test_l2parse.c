/*
 * test_l2parse.c - host-side unit tests for the L2 output parsers.
 * Builds with any C89 compiler (gcc on Linux, MIPSpro on IRIX):
 *   make test
 */
#include <stdio.h>
#include <string.h>

#include "../src/l2parse.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b)) != 0) { failures++; \
    printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

/* fixtures from SGI-L2-WUI _res/tests/parsertest.js (+ CRLF, junk bytes) */
static const char *DISPLAY_TXT =
    "001c01:\r\n"
    "revision: 2.0\r\n"
    "line 1: \"001c01     \\\"\r\n"
    "line 2: \"Powered Down\"\r\n"
    "leds:  PWR=off   SERVICE=off     FAULT=off\r\n"
    "001r20:\r\n"
    "revision: 2.0\r\n"
    "line 1: \"001r20     |\"\r\n"
    "line 2: \"Powered Up\"\r\n"
    "leds:  PWR=on   SERVICE=on     FAULT=on\r\n"
    "\r\n";

/* NL4R L2 emits stray non printable bytes */
static const char *DISPLAY_NL4R_TXT =
    "\x01" "001r01:\x7f\n"
    "line 1: \"001r01\"\n"
    "line 2: \"\x02Powered Up\"\n"
    "leds:  PWR=on   SERVICE=off     FAULT=off\n";

static const char *BRICK_TXT =
    "001c01:\n"
    "rack: 001, slot: 01, partition: none, type: C (IP45) [2MB flash], serial:NCC120, source: EEPROM\n"
    "\n"
    "001r20:\n"
    "rack: 001, slot: 20, partition: N/A, type: R [1MB flash], serial:NEA799, source: EEPROM\n"
    "\n"
    "001c05:\n"
    "rack: 001, slot: 05, partition: none, type: Chimera Blade [2MB flash], serial:MXX123, source: EEPROM\n"
    "firmware revision 1.2.3\n"
    "001c07:\n"
    "rack: 001, slot: 07, partition: none, type: Opus [2MB flash], serial:A1, source: EEPROM\n"
    "002c01:\n"
    "rack: 002, slot: 01, partition: none, type: C (IP41) [2MB flash], serial:A2, source: EEPROM\n"
    "002i02:\n"
    "rack: 002, slot: 02, partition: none, type: IX [2MB flash], serial:A3, source: EEPROM\n"
    "001r30:\n"
    "rack: 001, slot: 30, partition: N/A, type: NL4R [2MB flash], serial:A4, source: EEPROM\n"
    "001c09:\n"
    "rack: 001, slot: 09, partition: none, type: C (IP53 Rackmount) [2MB flash], serial:A5, source: EEPROM\n"
    "001c11:\n"
    "rack: 001, slot: 11, partition: none, type: Chimera Server [2MB flash], serial:A6, source: EEPROM\n";

static const char *CONFIG_TXT =
    "L2 172.16.9.1: - 001 (LOCAL)\n"
    "L1 172.16.9.1:0:0 - 001c01\n"
    "L1 172.16.9.1:0:1 - 001r20\n"
    "garbage line\n";

static void test_display(void)
{
    L2Display d[8];
    int n = l2_parse_display(DISPLAY_TXT, d, 8);
    CHECK(n == 2);
    CHECK_STR(d[0].id, "001c01");
    CHECK_STR(d[0].revision, "2.0");
    CHECK_STR(d[0].line1, "001c01     \\");
    CHECK_STR(d[0].line2, "Powered Down");
    CHECK(d[0].pwr == 0 && d[0].service == 0 && d[0].fault == 0);
    CHECK_STR(d[1].id, "001r20");
    CHECK_STR(d[1].line1, "001r20     |");
    CHECK(d[1].pwr == 1 && d[1].service == 1 && d[1].fault == 1);

    n = l2_parse_display(DISPLAY_NL4R_TXT, d, 8);
    CHECK(n == 1);
    CHECK_STR(d[0].id, "001r01");
    CHECK_STR(d[0].line2, "Powered Up");
    CHECK(d[0].pwr == 1 && d[0].service == 0);

    CHECK(l2_parse_display("", d, 8) == 0);
    CHECK(l2_parse_display(DISPLAY_TXT, d, 1) == 1);
}

static void test_brick(void)
{
    L2Brick b[16];
    L2ParseOpts o;
    int n;

    o.routerAltix = 0;
    o.altix350Grey = 0;
    n = l2_parse_brick(BRICK_TXT, &o, b, 16);
    CHECK(n == 9);
    CHECK_STR(b[0].id, "001c01");
    CHECK_STR(b[0].rack, "001");
    CHECK_STR(b[0].slot, "01");
    CHECK_STR(b[0].partition, "none");
    CHECK_STR(b[0].type, "C (IP45) [2MB flash]");
    CHECK_STR(b[0].serial, "NCC120");
    CHECK_STR(b[0].source, "EEPROM");
    CHECK(b[0].brickType == 'C');
    CHECK(b[0].skin == SKIN_O300);
    CHECK_STR(b[0].productLabel, "sgi origin 300");

    CHECK(b[1].brickType == 'R');
    CHECK(b[1].skin == SKIN_ROUTER);
    CHECK_STR(b[1].partition, "N/A");

    CHECK(b[2].skin == SKIN_O350);
    CHECK_STR(b[2].productLabel, "sgi onyx 350");
    CHECK(b[3].skin == SKIN_A350_YELLOW);
    CHECK(b[4].skin == SKIN_A3000);
    CHECK(b[5].skin == SKIN_A3000IX);
    CHECK(b[5].brickType == 'I');
    CHECK(b[6].skin == SKIN_NL4R);
    CHECK_STR(b[6].productLabel, "sgi nl4r");
    CHECK(b[7].skin == SKIN_TEZRO);
    CHECK(b[8].skin == SKIN_O350);
    CHECK_STR(b[8].productLabel, "sgi origin 350");

    o.routerAltix = 1;
    o.altix350Grey = 1;
    n = l2_parse_brick(BRICK_TXT, &o, b, 16);
    CHECK(b[1].skin == SKIN_ROUTER_ALTIX);
    CHECK(b[3].skin == SKIN_A350_GREY);
    CHECK(b[6].skin == SKIN_NL4R);  /* NL4R wins over router style */
}

static void test_merge_sort(void)
{
    L2Brick b[16];
    L2Display d[8];
    int nb, nd;

    nb = l2_parse_brick(BRICK_TXT, NULL, b, 16);
    nd = l2_parse_display(DISPLAY_TXT, d, 8);
    l2_merge_display(b, nb, d, nd);
    CHECK(b[0].hasDisplay == 1);
    CHECK_STR(b[0].display.line2, "Powered Down");
    CHECK(b[1].hasDisplay == 1 && b[1].display.fault == 1);
    CHECK(b[2].hasDisplay == 0 && b[2].display.pwr == 0);

    l2_sort(b, nb, L2_SORT_DESC, NULL);
    CHECK_STR(b[0].id, "002i02");   /* 2002 */
    CHECK_STR(b[1].id, "002c01");   /* 2001 */
    CHECK_STR(b[2].id, "001r30");
    CHECK_STR(b[nb - 1].id, "001c01");

    l2_sort(b, nb, L2_SORT_ASC, NULL);
    CHECK_STR(b[0].id, "001c01");
    CHECK_STR(b[nb - 1].id, "002i02");

    l2_sort(b, nb, L2_SORT_CUSTOM, "001r20, 001c05\n002c01");
    CHECK_STR(b[0].id, "001r20");
    CHECK_STR(b[1].id, "001c05");
    CHECK_STR(b[2].id, "002c01");
    CHECK_STR(b[3].id, "001c01");   /* not in list: keeps previous order */
}

static void test_config(void)
{
    L2ConfigEntry c[8];
    int n = l2_parse_config(CONFIG_TXT, c, 8);
    CHECK(n == 3);
    CHECK_STR(c[0].ltype, "L2");
    CHECK_STR(c[0].addr, "172.16.9.1:");
    CHECK_STR(c[0].id, "001");
    CHECK_STR(c[1].ltype, "L1");
    CHECK_STR(c[1].addr, "172.16.9.1:0:0");
    CHECK_STR(c[1].id, "001c01");
    CHECK_STR(c[2].id, "001r20");
}

int main(void)
{
    test_display();
    test_brick();
    test_merge_sort();
    test_config();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
