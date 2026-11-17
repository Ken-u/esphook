#include "app_screen.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static app_screen_cmd_t P(const char *line)
{
    app_screen_cmd_t c;
    assert(app_screen_parse(line, &c));
    return c;
}
static void bad(const char *line)
{
    const app_screen_cmd_t c = P(line);
    assert(c.kind == APP_SCREEN_CMD_BAD && c.error && c.error[0]);
}

int main(void)
{
    /* Only lines addressed to the screen test are ours. */
    app_screen_cmd_t c;
    assert(!app_screen_parse("robot dino status", &c));
    assert(!app_screen_parse("robot screen", &c));
    assert(!app_screen_parse("robot screentesting", &c));
    assert(!app_screen_parse("robot screentest2 plain", &c));
    assert(!app_screen_parse("", &c) && !app_screen_parse(NULL, &c) && !app_screen_parse("robot screentest", NULL));

    assert(P("robot screentest").kind == APP_SCREEN_CMD_STATUS);
    assert(P("robot screentest status").kind == APP_SCREEN_CMD_STATUS);
    assert(P("robot screentest   status ").kind == APP_SCREEN_CMD_STATUS);
    bad("robot screentest status now");

    /* The two flavours. */
    c = P("robot screentest plain");
    assert(c.kind == APP_SCREEN_CMD_SET && c.has_invert && c.has_gap && !c.has_mhz);
    assert(!c.value.invert && c.value.x_gap == 0 && c.value.y_gap == 24);
    c = P("robot screentest plugin");
    assert(c.kind == APP_SCREEN_CMD_SET && c.value.invert && c.value.x_gap == 1 && c.value.y_gap == 26);
    bad("robot screentest plain 1");

    app_screen_t s = {.invert = true, .x_gap = 1, .y_gap = 26, .mhz = 20, .saved_mhz = 20};
    assert(!strcmp(app_screen_variant(&s), "plugin"));
    assert(app_screen_set_variant(&s, "plain") && !strcmp(app_screen_variant(&s), "plain") && !s.invert);
    s.y_gap = 25;
    assert(!strcmp(app_screen_variant(&s), "custom"));
    s.y_gap = 24; s.invert = true;
    assert(!strcmp(app_screen_variant(&s), "custom"));
    assert(!app_screen_set_variant(&s, "other") && !app_screen_set_variant(&s, NULL) && !app_screen_set_variant(NULL, "plain"));
    assert(s.invert && s.y_gap == 24); /* unchanged by the failures */

    /* invert */
    c = P("robot screentest invert 0");
    assert(c.kind == APP_SCREEN_CMD_SET && c.has_invert && !c.has_gap && !c.value.invert);
    c = P("robot screentest invert on");
    assert(c.kind == APP_SCREEN_CMD_SET && c.value.invert);
    c = P("robot screentest invert off");
    assert(!c.value.invert);
    bad("robot screentest invert");
    bad("robot screentest invert 2");
    bad("robot screentest invert 0 1");

    /* gap */
    c = P("robot screentest gap 0 24");
    assert(c.kind == APP_SCREEN_CMD_SET && c.has_gap && !c.has_invert && c.value.x_gap == 0 && c.value.y_gap == 24);
    c = P("robot screentest gap 63 63");
    assert(c.kind == APP_SCREEN_CMD_SET && c.value.x_gap == 63 && c.value.y_gap == 63);
    bad("robot screentest gap 64 0");
    bad("robot screentest gap 0 64");
    bad("robot screentest gap -1 0");
    bad("robot screentest gap +1 0");
    bad("robot screentest gap 1");
    bad("robot screentest gap a b");
    bad("robot screentest gap 1 2 3");
    bad("robot screentest gap 1x 2");

    /* save and the clock */
    c = P("robot screentest save");
    assert(c.kind == APP_SCREEN_CMD_SAVE && !c.has_mhz);
    c = P("robot screentest mhz 10");
    assert(c.kind == APP_SCREEN_CMD_SAVE && c.has_mhz && c.value.mhz == 10);
    c = P("robot screentest mhz 40");
    assert(c.value.mhz == 40);
    bad("robot screentest mhz 0");
    bad("robot screentest mhz 41");
    bad("robot screentest mhz");
    bad("robot screentest mhz 10 20");
    bad("robot screentest save now");

    /* raw */
    c = P("robot screentest raw 21");
    assert(c.kind == APP_SCREEN_CMD_RAW && c.raw_length == 1 && c.raw[0] == 0x21);
    c = P("robot screentest raw 2A 00 00 00 9f");
    assert(c.raw_length == 5 && c.raw[0] == 0x2A && c.raw[4] == 0x9F);
    c = P("robot screentest raw 1 2 3 4 5 6 7 8");
    assert(c.raw_length == 8 && c.raw[7] == 8);
    bad("robot screentest raw");
    bad("robot screentest raw 1 2 3 4 5 6 7 8 9");
    bad("robot screentest raw 100");
    bad("robot screentest raw 0x21");
    bad("robot screentest raw zz");
    bad("robot screentest raw -1");

    /* start / stop */
    assert(P("robot screentest start").kind == APP_SCREEN_CMD_OPEN);
    assert(P("robot screentest stop").kind == APP_SCREEN_CMD_MENU);
    bad("robot screentest start now");
    bad("robot screentest blink");

    /* Too long a line is refused, not truncated into something else. */
    char longer[200] = "robot screentest raw";
    for (size_t i = strlen(longer); i + 4 <= sizeof longer; i += 3) memcpy(longer + i, " 00", 4);
    bad(longer);

    /* Ranges. */
    app_screen_t ok = {.invert = false, .x_gap = 63, .y_gap = 63, .mhz = 40, .saved_mhz = 40};
    assert(app_screen_valid(&ok));
    ok.mhz = 0; assert(!app_screen_valid(&ok));
    ok.mhz = 41; assert(!app_screen_valid(&ok));
    ok.mhz = 20; ok.x_gap = 64; assert(!app_screen_valid(&ok));
    ok.x_gap = 0; ok.y_gap = 64; assert(!app_screen_valid(&ok));
    assert(!app_screen_valid(NULL));

    puts("screen tuning: flavours, command grammar and ranges passed");
    return 0;
}
