#include "esphook/app.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include <stdio.h>
typedef struct { lv_obj_t *header,*title,*body,*keys,*ring,*stripe; unsigned selected; bool legacy,seen; const char *feedback; } state_t;
static void enter(app_context_t *ctx)
{
    state_t *s=ctx->state; app_device_info_t device; app_device_info(&device); s->legacy=!device.robot_hardware;
    s->header=app_ui_label(ctx->root,&aihook_font_12,4,0,152,15,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s->stripe=app_ui_rect(ctx->root,0,16,2,45,0x47E6AE,0);
    s->title=app_ui_label(ctx->root,&aihook_font_16,5,16,148,22,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s->body=app_ui_label(ctx->root,&aihook_font_16,5,40,148,22,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s->keys=app_ui_label(ctx->root,&aihook_font_12,4,64,132,16,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s->ring=app_ui_ring(ctx->root,137,62,18);
}
static void render(app_context_t *ctx)
{
    state_t *s=ctx->state; app_hook_t hook; app_hook_read(&hook); if(hook.current) s->seen=true;
    char text[100]; snprintf(text,sizeof text,"%s  %s  未读%u",hook.connected?"LINK":"HTTP",hook.current?hook.client:"Hook",hook.unread);
    app_ui_text(s->header,text); app_ui_text(s->title,hook.current?hook.title:"等待 Hook 通知");
    app_ui_text(s->body,hook.current?hook.body:"短按菜单 / 三击菜单");
    uint32_t color=hook.status==1?0xFDB927:hook.status==2?0xFF6B7A:0x47E6AE;
    lv_obj_set_style_bg_color(s->stripe,app_ui_color(color),LV_PART_MAIN);
    lv_obj_set_style_text_color(s->title,app_ui_color(color),LV_PART_MAIN);
    lv_obj_set_style_text_color(s->body,app_ui_color(color),LV_PART_MAIN);
    char word[16]; app_key_word(s->selected,word,sizeof word);
    snprintf(text,sizeof text,"%s %s",s->feedback?s->feedback:s->legacy?"按键回复":"长按回复",word);
    app_ui_text(s->keys,text); app_ui_progress(s->ring,app_hold_ms(ctx));
}
static void tick(app_context_t *ctx)
{
    state_t *s=ctx->state; app_hook_t hook; app_hook_read(&hook);
    if(s->seen && !hook.current && !ctx->standby) app_idle(ctx);
}
static void event(app_context_t *ctx,const app_event_t *e)
{
    state_t *s=ctx->state;
    if(e->kind==APP_EVENT_SHORT) {
        if(s->legacy) s->feedback=app_hook_reply(e->key)?"已排队":"发送失败";
        else { s->selected=(s->selected+1)%3; s->feedback=NULL; }
    }
    if(e->kind==APP_EVENT_CONFIRM && !s->legacy) s->feedback=app_hook_reply(s->selected)?"已排队":"发送失败";
}
static void icon(void *parent,int x,int y,bool selected)
{
    uint32_t color=selected?APP_COLOR_ACCENT:0xC178FF;
    app_ui_rect(parent,x+3,y+5,22,16,color,3); app_ui_rect(parent,x+6,y+8,16,10,APP_COLOR_BG,1);
    app_ui_rect(parent,x+7,y+20,5,5,color,1);
}
const app_descriptor_t esphook_app_hook={.api_version=APP_API_VERSION,.id="hook",.title="Hook",
    .flags=APP_STANDBY|APP_NOTIFICATIONS,.refresh_ms=80,.state_size=sizeof(state_t),.on_enter=enter,.on_tick=tick,.on_event=event,.render=render,.icon=icon};
