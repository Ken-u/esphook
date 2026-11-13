#include "robot_ui_protocol.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

robot_ui_t robot_ui_default(void)
{
    return (robot_ui_t){.mode = UI_AUTO, .expression = FACE_AUTO,
        .eye_color = 0xC178FF, .mouth_color = 0xFDB927, .text_color = 0xFFFFFF,
        .range_mm = 6000, .blink = true};
}
const char *robot_ui_mode_name(robot_ui_mode_t mode)
{
    static const char *const names[] = {"auto", "face", "text", "radar"};
    return (unsigned)mode < 4 ? names[mode] : "unknown";
}
static bool fail(char *error, size_t n, const char *message)
{
    if (n) snprintf(error, n, "%s", message);
    return false;
}
/* Check UTF-8, including overlong sequences, surrogates and U+10FFFF. */
static bool utf8(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t value;
        int count;
        if (*p < 0x80) { if (*p < 0x20 && *p != '\n') return false; ++p; continue; }
        if (*p >= 0xC2 && *p <= 0xDF) { value = *p++ & 0x1F; count = 1; }
        else if (*p >= 0xE0 && *p <= 0xEF) { value = *p++ & 0x0F; count = 2; }
        else if (*p >= 0xF0 && *p <= 0xF4) { value = *p++ & 7; count = 3; }
        else return false;
        int total = count;
        while (count--) { if ((*p & 0xC0) != 0x80) return false; value = value << 6 | (*p++ & 0x3F); }
        if ((total == 1 && value < 0x80) || (total == 2 && value < 0x800) ||
            (total == 3 && value < 0x10000) || value > 0x10FFFF ||
            (value >= 0xD800 && value <= 0xDFFF)) return false;
    }
    return true;
}
static bool preflight(const char *s, size_t n)
{
    unsigned depth = 0;
    bool quoted = false, escape = false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!c) return false;
        if (quoted) {
            if (escape) {
                /* cJSON stores C strings: reject encoded NUL before it can
                 * truncate a field name, expression name or text. */
                if (c == 'u' && i + 4 < n && !memcmp(s + i + 1, "0000", 4)) return false;
                escape = false;
            } else if (c == '\\') escape = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 4) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    return !quoted && !depth;
}
static bool integer(const cJSON *v, unsigned max, unsigned *out)
{
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
        v->valuedouble > max || floor(v->valuedouble) != v->valuedouble) return false;
    *out = (unsigned)v->valuedouble;
    return true;
}
static bool color(const cJSON *v, uint32_t *out)
{
    if (!cJSON_IsString(v) || strlen(v->valuestring) != 7 || v->valuestring[0] != '#') return false;
    uint32_t c = 0;
    for (int i = 1; i < 7; ++i) {
        unsigned char x = v->valuestring[i];
        unsigned d;
        if (x >= '0' && x <= '9') d = x - '0';
        else if (x >= 'a' && x <= 'f') d = x - 'a' + 10;
        else if (x >= 'A' && x <= 'F') d = x - 'A' + 10;
        else return false;
        c = c << 4 | d;
    }
    *out = c;
    return true;
}
static bool bitmap(const cJSON *v, unsigned rows, char *out)
{
    if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) != (int)rows) return false;
    for (unsigned row = 0; row < rows; ++row) {
        const cJSON *line = cJSON_GetArrayItem(v, row);
        if (!cJSON_IsString(line) || strlen(line->valuestring) != 5) return false;
        for (int col = 0; col < 5; ++col) {
            char c = line->valuestring[col];
            if (c != '0' && c != '1') return false;
            out[row * 5 + col] = c;
        }
    }
    out[rows * 5] = '\0';
    return true;
}
bool robot_ui_parse(const char *json, size_t len, robot_ui_t *out, char *error, size_t error_len)
{
    if (!json || !out || !len || len > ROBOT_UI_JSON_MAX || !preflight(json, len))
        return fail(error, error_len, "invalid size, nesting or string");
    /* Length includes no implicit terminator; cJSON must consume all bytes. */
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, len, &end, false);
    if (!root) return fail(error, error_len, "invalid JSON");
    const char *message = "invalid display request";
    robot_ui_t next = robot_ui_default();
    if (!cJSON_IsObject(root)) goto bad;
    while (end < json + len && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    if (end != json + len) goto bad;
    static const char *const keys[] = {"v", "mode", "expression", "text", "left", "right", "mouth",
        "eye_color", "mouth_color", "text_color", "blink", "ttl_ms", "range_mm"};
    unsigned seen = 0;
    for (const cJSON *item = root->child; item; item = item->next) {
        unsigned k;
        for (k = 0; k < sizeof keys / sizeof keys[0]; ++k)
            if (item->string && !strcmp(item->string, keys[k])) break;
        if (k == sizeof keys / sizeof keys[0] || (seen & (1U << k))) {
            message = "unknown or duplicate field"; goto bad;
        }
        seen |= 1U << k;
    }
    unsigned v;
    if (!integer(cJSON_GetObjectItemCaseSensitive(root, "v"), 1, &v) || v != 1) {
        message = "v must be 1"; goto bad;
    }
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(root, "mode");
    if (!cJSON_IsString(mode)) { message = "mode is required"; goto bad; }
    bool found = false;
    for (unsigned i = 0; i < 4; ++i)
        if (!strcmp(mode->valuestring, robot_ui_mode_name(i))) { next.mode = i; found = true; }
    if (!found) { message = "unknown mode"; goto bad; }
    if (next.mode == UI_AUTO && (seen & ~3U)) {
        message = "auto only accepts v and mode"; goto bad;
    }
    const cJSON *expr = cJSON_GetObjectItemCaseSensitive(root, "expression");
    if (expr) {
        static const char *const expressions[] = {"auto", "neutral", "happy", "sleepy", "sad", "custom"};
        found = false;
        if (!cJSON_IsString(expr)) goto bad;
        for (unsigned i = 0; i < 6; ++i)
            if (!strcmp(expr->valuestring, expressions[i])) { next.expression = i; found = true; }
        if (!found) { message = "unknown expression"; goto bad; }
    }
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
    if (text) {
        if (!cJSON_IsString(text) || strlen(text->valuestring) > ROBOT_UI_TEXT_MAX || !utf8(text->valuestring)) {
            message = "text must be valid UTF-8, at most 192 bytes"; goto bad;
        }
        strcpy(next.text, text->valuestring);
    }
    if (next.mode == UI_TEXT && !next.text[0]) { message = "text mode needs text"; goto bad; }
    const cJSON *left = cJSON_GetObjectItemCaseSensitive(root, "left");
    const cJSON *right = cJSON_GetObjectItemCaseSensitive(root, "right");
    const cJSON *mouth = cJSON_GetObjectItemCaseSensitive(root, "mouth");
    if (next.expression == FACE_CUSTOM) {
        if (!bitmap(left, 7, next.left) || !bitmap(right, 7, next.right) || !bitmap(mouth, 4, next.mouth)) {
            message = "custom requires left/right 7x5 and mouth 4x5 binary rows"; goto bad;
        }
    } else if (left || right || mouth) { message = "bitmaps require custom expression"; goto bad; }
    const char *const colors[] = {"eye_color", "mouth_color", "text_color"};
    uint32_t *dest[] = {&next.eye_color, &next.mouth_color, &next.text_color};
    for (unsigned i = 0; i < 3; ++i) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, colors[i]);
        if (value && !color(value, dest[i])) { message = "color must be #RRGGBB"; goto bad; }
    }
    const cJSON *blink = cJSON_GetObjectItemCaseSensitive(root, "blink");
    if (blink) { if (!cJSON_IsBool(blink)) goto bad; next.blink = cJSON_IsTrue(blink); }
    const cJSON *ttl = cJSON_GetObjectItemCaseSensitive(root, "ttl_ms");
    if (ttl && !integer(ttl, 600000, &next.ttl_ms)) { message = "ttl_ms must be 0..600000"; goto bad; }
    const cJSON *range = cJSON_GetObjectItemCaseSensitive(root, "range_mm");
    if (range && (!integer(range, 6000, &next.range_mm) || next.range_mm < 1000)) {
        message = "range_mm must be 1000..6000"; goto bad;
    }
    *out = next;
    cJSON_Delete(root);
    if (error_len) error[0] = '\0';
    return true;
bad:
    cJSON_Delete(root);
    return fail(error, error_len, message);
}
