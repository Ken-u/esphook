#include "app_protocol.h"
#include "robot_ui_protocol.h"
#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
bool app_protocol_parse(const char *json, size_t length, app_request_t *out)
{
    memset(out, 0, sizeof *out);
    if (!length || length > 1024) return false;
    unsigned depth = 0; bool quoted = false, escape = false;
    for (size_t i = 0; i < length; ++i) {
        char c = json[i]; if (!c) return false;
        if (quoted) {
            if (escape) {
                if (c == 'u' && i + 4 < length && !memcmp(json + i + 1, "0000", 4)) return false;
                escape = false;
            } else if (c == '\\') escape = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '[' || c == '{') { if (++depth > 8) return false; }
        else if (c == ']' || c == '}') { if (!depth) return false; --depth; }
    }
    if (quoted || depth) return false;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(json, length, &end, false);
    bool valid = false;
    if (!cJSON_IsObject(j)) goto done;
    while (end < json + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    if (end != json + length) goto done;
    for (const cJSON *a = j->child; a; a = a->next)
        for (const cJSON *b = a->next; b; b = b->next)
            if (!strcmp(a->string, b->string)) goto done;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "v"), *seq = cJSON_GetObjectItemCaseSensitive(j, "seq");
    const cJSON *op = cJSON_GetObjectItemCaseSensitive(j, "op"), *app = cJSON_GetObjectItemCaseSensitive(j, "app");
    if (!cJSON_IsNumber(v) || v->valuedouble != 1 || !cJSON_IsNumber(seq) || !isfinite(seq->valuedouble) ||
        seq->valuedouble < 1 || seq->valuedouble > 2147483647 || floor(seq->valuedouble) != seq->valuedouble || !cJSON_IsString(op)) goto done;
    out->seq = (int)seq->valuedouble;
    const char *names[] = {"list", "status", "open", "menu", "standby", "message"};
    unsigned n = 0; for (; n < 6 && strcmp(op->valuestring, names[n]); ++n) { }
    if (n == 6) goto done;
    out->op = (app_op_t)n;
    if (n == APP_OP_OPEN || n == APP_OP_STANDBY || n == APP_OP_MESSAGE) {
        if (!cJSON_IsString(app) || !app->valuestring[0] || strlen(app->valuestring) > APP_ID_MAX) goto done;
        for (const char *p = app->valuestring; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) goto done;
        strcpy(out->app, app->valuestring);
    }
    if (n == APP_OP_MESSAGE) {
        const cJSON *data = cJSON_GetObjectItemCaseSensitive(j, "data");
        if (!cJSON_IsObject(data)) goto done;
        char *body = cJSON_PrintUnformatted(data);
        if (!body) goto done;
        valid = strlen(body) <= APP_MESSAGE_MAX && robot_ui_valid_utf8(body);
        if (valid) strcpy(out->message, body);
        free(body); if (!valid) goto done;
    }
    valid = true;
done:
    cJSON_Delete(j); return valid;
}
