#include "esphook/app.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include <stdio.h>
typedef struct { lv_obj_t *title,*word,*hint,*ring; unsigned selected,count; bool three_keys; } state_t;
static void enter(app_context_t *ctx)
{
    state_t *s=ctx->state; app_device_info_t d; app_device_info(&d); s->three_keys=!d.robot_hardware;
    s->title=app_ui_label(ctx->root,&aihook_font_12,4,0,152,16,LV_LABEL_LONG_MODE_CLIP); app_ui_center(s->title);
    s->word=app_ui_label(ctx->root,&aihook_font_16,4,22,152,22,LV_LABEL_LONG_MODE_CLIP); app_ui_center(s->word);
    s->hint=app_ui_label(ctx->root,&aihook_font_12,4,57,128,16,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s->ring=app_ui_ring(ctx->root,135,53,24);
}
static void render(app_context_t *ctx)
{
    state_t *s=ctx->state; char text[100],word[16]; app_key_word(s->selected,word,sizeof word);
    snprintf(text,sizeof text,"按键回传  %u",s->count); app_ui_text(s->title,text); app_ui_text(s->word,word);
    app_ui_text(s->hint,s->three_keys?"K1/K2/K3 直接回传":"短按切换 长按发送"); app_ui_progress(s->ring,app_hold_ms(ctx));
}
static void event(app_context_t *ctx,const app_event_t *e)
{
    state_t *s=ctx->state;
    if(e->kind==APP_EVENT_SHORT && !s->three_keys) { s->selected=(s->selected+1)%3; return; }
    if((s->three_keys && e->kind==APP_EVENT_SHORT) || (!s->three_keys && e->kind==APP_EVENT_CONFIRM)) {
        unsigned key=s->three_keys?e->key:s->selected; char word[16]; app_key_word(key,word,sizeof word);
        (void)app_host_event(ctx,"key",key,word); (void)app_hook_reply(key); s->selected=key; ++s->count;
    }
}
static void icon(void *parent,int x,int y,bool selected)
{
    uint32_t color=selected?APP_COLOR_ACCENT:0x47E6AE;
    app_ui_rect(parent,x+3,y+5,22,18,color,3); app_ui_rect(parent,x+6,y+8,16,12,APP_COLOR_BG,2);
    for(int i=0;i<3;++i) app_ui_rect(parent,x+8+i*5,y+12,3,4,color,0);
}
const app_descriptor_t esphook_app_key_report={.api_version=APP_API_VERSION,.id="key_report",.title="按键",
    .refresh_ms=40,.state_size=sizeof(state_t),.on_enter=enter,.on_event=event,.render=render,.icon=icon};
