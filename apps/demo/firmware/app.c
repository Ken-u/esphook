#include "esphook/app.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
typedef struct { lv_obj_t *title,*count,*ball,*status,*ring; unsigned clicks; char text[49]; } state_t;
static void enter(app_context_t *ctx)
{
    state_t *s=ctx->state; strcpy(s->text,"Hello esphook");
    s->title=app_ui_label(ctx->root,&aihook_font_12,3,0,154,16,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR); app_ui_center(s->title);
    s->count=app_ui_label(ctx->root,&aihook_font_16,4,20,152,22,LV_LABEL_LONG_MODE_CLIP); app_ui_center(s->count);
    s->ball=app_ui_rect(ctx->root,0,46,8,8,APP_COLOR_ACCENT,4);
    s->status=app_ui_label(ctx->root,&aihook_font_12,3,62,132,16,LV_LABEL_LONG_MODE_CLIP);
    s->ring=app_ui_ring(ctx->root,139,60,18);
    (void)app_host_event(ctx,"launch",0,NULL);
}
static void event(app_context_t *ctx,const app_event_t *e)
{
    state_t *s=ctx->state;
    if(e->kind==APP_EVENT_SHORT) { ++s->clicks; (void)app_host_event(ctx,"tap",e->key,NULL); }
    if(e->kind==APP_EVENT_CONFIRM) { s->clicks=0; (void)app_host_event(ctx,"reset",e->key,NULL); }
    if(e->kind==APP_EVENT_MESSAGE && e->text) {
        cJSON *j=cJSON_Parse(e->text); const cJSON *text=cJSON_GetObjectItemCaseSensitive(j,"text");
        if(cJSON_IsString(text) && strlen(text->valuestring)<sizeof s->text && robot_ui_valid_utf8(text->valuestring))
            snprintf(s->text,sizeof s->text,"%s",text->valuestring);
        cJSON_Delete(j);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s=ctx->state; char text[80]; app_ui_text(s->title,s->text);
    snprintf(text,sizeof text,"短按 %u 次",s->clicks); app_ui_text(s->count,text);
    unsigned phase=(unsigned)(ctx->now_ms/8)%304; int x=phase<=152?(int)phase:304-(int)phase;
    lv_obj_set_x(s->ball,x); app_ui_progress(s->ring,app_hold_ms(ctx));
    app_radar_t radar; app_radar_read(&radar);
    app_ui_text(s->status,radar.online?"雷达在线 长按清零":"短按计数 长按清零");
}
static void leave(app_context_t *ctx) { (void)app_host_event(ctx,"leave",0,NULL); }
static void icon(void *parent,int x,int y,bool selected)
{ app_ui_rect(parent,x+4,y+4,20,20,selected?APP_COLOR_ACCENT:0xC178FF,5); app_ui_rect(parent,x+11,y+8,6,12,APP_COLOR_BG,2); }
const app_descriptor_t esphook_app_demo={.api_version=APP_API_VERSION,.id="demo",.title="示例",
    .refresh_ms=16,.state_size=sizeof(state_t),.on_enter=enter,.on_leave=leave,.on_event=event,.render=render,.icon=icon};
