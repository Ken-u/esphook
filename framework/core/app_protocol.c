#include "app_protocol.h"
#include "robot_ui_protocol.h"
#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static bool map_array(const cJSON *j,const char *name,uint8_t out[3])
{
    const cJSON *a=cJSON_GetObjectItemCaseSensitive(j,name);
    if(!cJSON_IsArray(a) || cJSON_GetArraySize(a)!=3) return false;
    for(unsigned i=0;i<3;++i) {
        const cJSON *v=cJSON_GetArrayItem(a,(int)i);
        if(!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>2 || floor(v->valuedouble)!=v->valuedouble) return false;
        out[i]=(uint8_t)v->valuedouble;
    }
    return true;
}
static bool keymap(const cJSON *j,app_keymap_t *out)
{
    if(!cJSON_IsObject(j) || cJSON_GetArraySize(j)!=3) return false;
    for(const cJSON *a=j->child;a;a=a->next) for(const cJSON *b=a->next;b;b=b->next)
        if(!strcmp(a->string,b->string)) return false;
    if(!map_array(j,"physical",out->physical) || !map_array(j,"usb",out->usb)) return false;
    const cJSON *words=cJSON_GetObjectItemCaseSensitive(j,"words");
    if(!cJSON_IsArray(words) || cJSON_GetArraySize(words)!=3) return false;
    for(int i=0;i<3;++i) {
        const cJSON *v=cJSON_GetArrayItem(words,i);
        if(!cJSON_IsString(v) || strlen(v->valuestring)>=APP_KEY_WORD_SIZE) return false;
        strcpy(out->words[i],v->valuestring);
    }
    return app_keymap_valid(out);
}
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
    const char *names[] = {"list", "status", "open", "menu", "standby", "message", "key", "keys_release", "keymap", "hardware", "time"};
    unsigned n = 0; for (; n < sizeof names/sizeof names[0] && strcmp(op->valuestring, names[n]); ++n) { }
    if (n == sizeof names/sizeof names[0]) goto done;
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
    if(n==APP_OP_KEY) {
        const cJSON *key=cJSON_GetObjectItemCaseSensitive(j,"key"), *pressed=cJSON_GetObjectItemCaseSensitive(j,"pressed");
        if(!cJSON_IsNumber(key) || !isfinite(key->valuedouble) || key->valuedouble<0 || key->valuedouble>2 ||
            floor(key->valuedouble)!=key->valuedouble || !cJSON_IsBool(pressed)) goto done;
        out->key=(unsigned)key->valuedouble;out->pressed=cJSON_IsTrue(pressed);
    }
    if(n==APP_OP_KEYMAP) {
        const cJSON *map=cJSON_GetObjectItemCaseSensitive(j,"mapping");
        if(map) { if(!keymap(map,&out->keymap)) goto done;out->keymap_set=true; }
    }
    if(n==APP_OP_HARDWARE) {
        const cJSON *profile=cJSON_GetObjectItemCaseSensitive(j,"profile"), *reboot=cJSON_GetObjectItemCaseSensitive(j,"reboot");
        if(profile) { if(!cJSON_IsString(profile) || !app_hardware_profile_parse(profile->valuestring,&out->hardware_profile)) goto done;out->hardware_set=true; }
        if(reboot) { if(!cJSON_IsBool(reboot) || !profile) goto done;out->reboot=cJSON_IsTrue(reboot); }
    }
    if(n==APP_OP_TIME) {
        const cJSON *epoch=cJSON_GetObjectItemCaseSensitive(j,"epoch"), *tz=cJSON_GetObjectItemCaseSensitive(j,"tz_min");
        if(epoch || tz) { /* both or neither: a bare time request only reads the clock */
            if(!cJSON_IsNumber(epoch) || !cJSON_IsNumber(tz) || !isfinite(epoch->valuedouble) || !isfinite(tz->valuedouble) ||
               floor(epoch->valuedouble)!=epoch->valuedouble || floor(tz->valuedouble)!=tz->valuedouble ||
               epoch->valuedouble<APP_EPOCH_MIN || epoch->valuedouble>4294967295.0 || !app_time_tz_valid((int)tz->valuedouble)) goto done;
            out->time_set=true;out->epoch=(uint32_t)epoch->valuedouble;out->tz_min=(int)tz->valuedouble;
        }
    }
    valid = true;
done:
    cJSON_Delete(j); return valid;
}
