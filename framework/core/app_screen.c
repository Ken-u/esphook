#include "app_screen.h"
#include <stdlib.h>
#include <string.h>

#define PREFIX "robot screentest"
#define LINE_MAX_CHARS 96U
#define TOKENS_MAX 12U

bool app_screen_valid(const app_screen_t *s)
{
    return s && s->x_gap <= APP_SCREEN_GAP_MAX && s->y_gap <= APP_SCREEN_GAP_MAX
        && s->mhz >= 1U && s->mhz <= APP_SCREEN_MHZ_MAX;
}

bool app_screen_set_variant(app_screen_t *s, const char *name)
{
    if (!s || !name) return false;
    if (!strcmp(name, "plugin")) { s->invert = true; s->x_gap = 1; s->y_gap = 26; return true; }
    if (!strcmp(name, "plain")) { s->invert = false; s->x_gap = 0; s->y_gap = 24; return true; }
    return false;
}

const char *app_screen_variant(const app_screen_t *s)
{
    if (!s) return "custom";
    if (s->invert && s->x_gap == 1 && s->y_gap == 26) return "plugin";
    if (!s->invert && s->x_gap == 0 && s->y_gap == 24) return "plain";
    return "custom";
}

static bool parse_uint(const char *text, unsigned base, unsigned max, unsigned *out)
{
    if (!text[0] || text[0] == '-' || text[0] == '+') return false;
    char *end;
    const unsigned long v = strtoul(text, &end, base);
    if (*end || v > max) return false;
    *out = (unsigned)v;
    return true;
}

static bool bad(app_screen_cmd_t *cmd, const char *why)
{
    cmd->kind = APP_SCREEN_CMD_BAD;
    cmd->error = why;
    return true;
}

bool app_screen_parse(const char *line, app_screen_cmd_t *cmd)
{
    if (!line || !cmd) return false;
    memset(cmd, 0, sizeof *cmd);
    const size_t prefix = sizeof PREFIX - 1U;
    if (strncmp(line, PREFIX, prefix) || (line[prefix] && line[prefix] != ' ')) return false;
    if (strlen(line) >= LINE_MAX_CHARS) return bad(cmd, "line too long");

    char buf[LINE_MAX_CHARS];
    memcpy(buf, line + prefix, strlen(line + prefix) + 1U);
    char *tok[TOKENS_MAX];
    size_t n = 0;
    for (char *p = strtok(buf, " "); p; p = strtok(NULL, " ")) {
        if (n == TOKENS_MAX) return bad(cmd, "too many words");
        tok[n++] = p;
    }
    if (n == 0 || !strcmp(tok[0], "status")) {
        if (n > 1) return bad(cmd, "status takes no argument");
        cmd->kind = APP_SCREEN_CMD_STATUS;
        return true;
    }
    const char *word = tok[0];
    unsigned a, b;
    if (!strcmp(word, "plugin") || !strcmp(word, "plain")) {
        if (n > 1) return bad(cmd, "no argument expected");
        cmd->kind = APP_SCREEN_CMD_SET;
        cmd->has_invert = cmd->has_gap = true;
        (void)app_screen_set_variant(&cmd->value, word);
        return true;
    }
    if (!strcmp(word, "invert")) {
        if (n != 2) return bad(cmd, "usage: invert 0|1");
        if (!strcmp(tok[1], "1") || !strcmp(tok[1], "on")) cmd->value.invert = true;
        else if (!strcmp(tok[1], "0") || !strcmp(tok[1], "off")) cmd->value.invert = false;
        else return bad(cmd, "usage: invert 0|1");
        cmd->kind = APP_SCREEN_CMD_SET;
        cmd->has_invert = true;
        return true;
    }
    if (!strcmp(word, "gap")) {
        if (n != 3 || !parse_uint(tok[1], 10, APP_SCREEN_GAP_MAX, &a) || !parse_uint(tok[2], 10, APP_SCREEN_GAP_MAX, &b))
            return bad(cmd, "usage: gap <x 0..63> <y 0..63>");
        cmd->kind = APP_SCREEN_CMD_SET;
        cmd->has_gap = true;
        cmd->value.x_gap = (uint8_t)a;
        cmd->value.y_gap = (uint8_t)b;
        return true;
    }
    if (!strcmp(word, "save")) {
        if (n > 1) return bad(cmd, "no argument expected");
        cmd->kind = APP_SCREEN_CMD_SAVE;
        return true;
    }
    if (!strcmp(word, "reset")) {
        if (n > 1) return bad(cmd, "no argument expected");
        cmd->kind = APP_SCREEN_CMD_RESET;
        return true;
    }
    if (!strcmp(word, "mhz")) {
        if (n != 2 || !parse_uint(tok[1], 10, APP_SCREEN_MHZ_MAX, &a) || a < 1U) return bad(cmd, "usage: mhz <1..40>");
        cmd->kind = APP_SCREEN_CMD_SAVE;
        cmd->has_mhz = true;
        cmd->value.mhz = (uint8_t)a;
        return true;
    }
    if (!strcmp(word, "raw")) {
        if (n < 2 || n - 1U > APP_SCREEN_RAW_MAX) return bad(cmd, "usage: raw <cmd> [param...] (hex, at most 8 bytes)");
        for (size_t i = 1; i < n; ++i) {
            if (strlen(tok[i]) > 2U || !parse_uint(tok[i], 16, 0xFFU, &a)) return bad(cmd, "raw bytes are 1..2 hex digits");
            cmd->raw[i - 1U] = (uint8_t)a;
        }
        cmd->raw_length = n - 1U;
        cmd->kind = APP_SCREEN_CMD_RAW;
        return true;
    }
    if (!strcmp(word, "start") || !strcmp(word, "stop")) {
        if (n > 1) return bad(cmd, "no argument expected");
        cmd->kind = !strcmp(word, "start") ? APP_SCREEN_CMD_OPEN : APP_SCREEN_CMD_MENU;
        return true;
    }
    return bad(cmd, "unknown word: status plugin plain invert gap save reset mhz raw start stop");
}
