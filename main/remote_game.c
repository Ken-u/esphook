#include "remote_game.h"
#include "cJSON.h"
#include "robot_ui_protocol.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool fail(char *error, size_t n, const char *s) { if (n) snprintf(error, n, "%s", s); return false; }
static bool bounded(const char *s, size_t len)
{
    unsigned depth = 0; bool quote = false, escape = false;
    for (size_t i = 0; i < len; ++i) {
        if (!s[i]) return false;
        if (quote) {
            if (escape) {
                if (s[i] == 'u' && i + 4 < len && !memcmp(s + i + 1, "0000", 4)) return false;
                escape = false;
            } else if (s[i] == '\\') escape = true;
            else if (s[i] == '"') quote = false;
        } else if (s[i] == '"') quote = true;
        else if (s[i] == '{' || s[i] == '[') { if (++depth > 4) return false; }
        else if (s[i] == '}' || s[i] == ']') { if (!depth) return false; --depth; }
    }
    return !quote && !depth;
}
static bool num(const cJSON *v, int32_t min, int32_t max, int32_t *out)
{
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || floor(v->valuedouble) != v->valuedouble ||
        v->valuedouble < min || v->valuedouble > max) return false;
    *out = (int32_t)v->valuedouble; return true;
}
static bool integer(const cJSON *j, const char *key, int32_t lo, int32_t hi, int32_t *out)
{ return num(cJSON_GetObjectItemCaseSensitive(j, key), lo, hi, out); }
static bool text(const cJSON *j, const char *key, char *out, size_t size)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring) >= size || !robot_ui_valid_utf8(v->valuestring)) return false;
    for (const unsigned char *p = (const unsigned char *)v->valuestring; *p; ++p)
        if (*p < 0x20) return false;
    strcpy(out, v->valuestring); return true;
}
bool remote_game_apply(remote_game_t *g, const char *json, size_t len, uint64_t now,
                       int *seq, char *error, size_t error_len)
{
    *seq = 0;
    if (!len || len > REMOTE_GAME_JSON_MAX || !bounded(json, len)) return fail(error, error_len, "invalid size or nesting");
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(json, len, &end, false);
    if (!j) return fail(error, error_len, "invalid JSON");
    const char *why = "invalid game request";
    if (!cJSON_IsObject(j)) goto bad;
    while (end < json + len && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
    if (end != json + len) goto bad;
    /* Reject duplicate fields so packet validation never depends on parser order. */
    for (const cJSON *a = j->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) { why = "duplicate field"; goto bad; }
    int32_t version, id, value;
    if (!integer(j, "v", 1, 1, &version) || !integer(j, "seq", 1, 2147483647, &value)) goto bad;
    *seq = value;
    if (!integer(j, "id", 1, 2147483647, &id)) goto bad;
    const cJSON *op = cJSON_GetObjectItemCaseSensitive(j, "op");
    if (!cJSON_IsString(op)) goto bad;
    if (!strcmp(op->valuestring, "begin")) {
        int32_t count, first;
        char label[49], code[9], date[11];
        if (!integer(j, "count", 1, REMOTE_GAME_MAX_BARS, &count) ||
            !integer(j, "first", 0, count - 1, &first) ||
            !text(j, "label", label, sizeof label) || !text(j, "code", code, sizeof code) ||
            !text(j, "date", date, sizeof date)) goto bad;
        if (g->active && g->data.id == (uint32_t)id) { why = "use a new dataset id"; goto bad; }
        if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') goto bad;
        /* A begin is staging only: the visible session survives partial uploads. */
        memset(&g->pending, 0, sizeof g->pending);
        g->pending.id = id; g->pending.count = count; g->pending.first = first;
        strcpy(g->pending.label, label); strcpy(g->pending.code, code); strcpy(g->pending.date, date);
    } else if (!strcmp(op->valuestring, "bars")) {
        int32_t offset;
        const cJSON *bars = cJSON_GetObjectItemCaseSensitive(j, "bars");
        int n = cJSON_GetArraySize(bars);
        if (g->pending.id != (uint32_t)id || !cJSON_IsArray(bars) || n < 1 || n > 12 ||
            !integer(j, "offset", 0, (int32_t)g->pending.count - n, &offset)) goto bad;
        chart_bar_t parsed[12];
        for (int i = 0; i < n; ++i) {
            const cJSON *row = cJSON_GetArrayItem(bars, i);
            int32_t values[6];
            if (!cJSON_IsArray(row) || cJSON_GetArraySize(row) != 6) goto bad;
            for (int k = 0; k < 6; ++k)
                if (!num(cJSON_GetArrayItem(row, k), 1, 100000000, &values[k])) goto bad;
            parsed[i] = (chart_bar_t){values[0], values[1], values[2], values[3], values[4], values[5]};
            if (values[2] > values[0] || values[2] > values[3] || values[1] < values[0] ||
                values[1] < values[3] || values[2] > values[1]) goto bad;
        }
        for (int i = 0; i < n; ++i) { g->pending.bars[offset + i] = parsed[i]; g->pending.received[offset + i] = true; }
    } else if (!strcmp(op->valuestring, "show")) {
        const remote_dataset_t *data = g->active && g->data.id == (uint32_t)id ? &g->data : &g->pending;
        if (data->id != (uint32_t)id || !data->count) goto bad;
        for (unsigned i = 0; i < data->count; ++i) if (!data->received[i]) { why = "incomplete dataset"; goto bad; }
        int32_t cursor, held, trades, pct, base, pnl;
        char clock[6];
        const cJSON *phase = cJSON_GetObjectItemCaseSensitive(j, "state");
        const cJSON *holding = cJSON_GetObjectItemCaseSensitive(j, "holding");
        if (!cJSON_IsString(phase) || !cJSON_IsBool(holding) ||
            !integer(j, "cursor", data->first, data->count - 1, &cursor) ||
            !integer(j, "held_ms", 0, 500, &held) || !integer(j, "trades", 0, 10000, &trades) ||
            !integer(j, "pct_bps", -1000000, 1000000, &pct) ||
            !integer(j, "baseline_bps", -1000000, 1000000, &base) ||
            !integer(j, "pnl_cents", -2000000000, 2000000000, &pnl) ||
            !text(j, "clock", clock, sizeof clock)) goto bad;
        remote_phase_t p;
        if (!strcmp(phase->valuestring, "play")) p = REMOTE_PLAY;
        else if (!strcmp(phase->valuestring, "result")) p = REMOTE_RESULT;
        else if (!strcmp(phase->valuestring, "loading")) p = REMOTE_LOADING;
        else goto bad;
        if (data == &g->pending) g->data = g->pending;
        g->active = true; g->phase = p; g->cursor = cursor; g->held_ms = held; g->trades = trades;
        g->pct_bps = pct; g->baseline_bps = base; g->pnl_cents = pnl;
        g->holding = cJSON_IsTrue(holding); strcpy(g->clock, clock); g->updated_ms = now;
    } else if (!strcmp(op->valuestring, "heartbeat")) {
        if (!g->active || g->data.id != (uint32_t)id) goto bad;
        g->updated_ms = now;
    } else if (!strcmp(op->valuestring, "close")) {
        if (!g->active || g->data.id != (uint32_t)id) goto bad;
        memset(g, 0, sizeof *g);
    } else { why = "unknown op"; goto bad; }
    cJSON_Delete(j); if (error_len) error[0] = '\0'; return true;
bad:
    cJSON_Delete(j); return fail(error, error_len, why);
}
void remote_game_snapshot(const remote_game_t *g, uint64_t now, remote_game_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->active = g->active;
    if (!g->active) return;
    v->offline = now < g->updated_ms || now - g->updated_ms > REMOTE_GAME_WATCHDOG_MS;
    v->id = g->data.id; v->phase = g->phase; v->cursor = g->cursor; v->first = g->data.first;
    v->held_ms = g->held_ms; v->trades = g->trades; v->pct_bps = g->pct_bps;
    v->baseline_bps = g->baseline_bps; v->pnl_cents = g->pnl_cents; v->holding = g->holding;
    strcpy(v->label, g->data.label); strcpy(v->code, g->data.code); strcpy(v->date, g->data.date); strcpy(v->clock, g->clock);
    v->count = g->cursor + 1 < REMOTE_GAME_WINDOW ? g->cursor + 1 : REMOTE_GAME_WINDOW;
    memcpy(v->bars, g->data.bars + g->cursor + 1 - v->count, v->count * sizeof v->bars[0]);
}
