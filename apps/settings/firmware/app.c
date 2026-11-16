#include "esphook/app.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include <stdio.h>
#include <string.h>
typedef struct { lv_obj_t *title,*lines[3],*ring; unsigned page; size_t candidate; bool editing; const char *feedback; } state_t;
static const app_descriptor_t *candidate(app_context_t *ctx,state_t *s)
{ return app_at(ctx,s->candidate); }
static void next_candidate(app_context_t *ctx,state_t *s)
{
    size_t count=app_count(ctx);
    for(size_t i=0;i<count;++i) { s->candidate=(s->candidate+1)%count; if(candidate(ctx,s)->flags&APP_STANDBY) return; }
}
static void enter(app_context_t *ctx)
{
    state_t *s=ctx->state;
    s->title=app_ui_label(ctx->root,&aihook_font_12,3,0,154,16,LV_LABEL_LONG_MODE_CLIP); app_ui_center(s->title);
    for(unsigned i=0;i<3;++i) s->lines[i]=app_ui_label(ctx->root,&aihook_font_12,4,19+i*19,i==2?129:152,16,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s->ring=app_ui_ring(ctx->root,138,57,20);
}
static const char *profile_title(app_hardware_profile_t profile)
{ const char *names[]={"自动","仅屏幕","机器人"};return (unsigned)profile<3?names[profile]:"未知"; }
static const char *hardware_title(app_hardware_state_t state)
{
    const char *names[]={"未知","初始化/连接未知","尚未检测到","已检测到","未启用","不可用"};
    return (unsigned)state<6?names[state]:"未知";
}
static void render(app_context_t *ctx)
{
    state_t *s=ctx->state; char a[160],b[160],c[160];
    const char *names[]={"设置 1/6 固件","设置 2/6 应用","设置 3/6 Hook","设置 4/6 待机","设置 5/6 硬件","设置 6/6 配置"};
    app_ui_text(s->title,names[s->page]);
    if(s->page==0) {
        app_device_info_t d; app_device_info(&d);
        snprintf(a,sizeof a,"版本 %s",d.version); snprintf(b,sizeof b,"%s",d.build); snprintf(c,sizeof c,"API %u  短按下一页",APP_API_VERSION);
    } else if(s->page==1) {
        snprintf(a,sizeof a,"应用数量 %u",(unsigned)app_count(ctx));
        snprintf(b,sizeof b,"当前 %s",app_active_id(ctx)); snprintf(c,sizeof c,"短按下一页");
    } else if(s->page==2) {
        app_device_info_t d; app_device_info(&d); app_hook_t h; app_hook_read(&h);
        snprintf(a,sizeof a,"%s 客户%u 未读%u",h.connected?"已连接":"未连接",h.clients,h.unread);
        snprintf(b,sizeof b,"%s:%u",d.daemon_host[0]?d.daemon_host:"未设置主机",d.daemon_port);
        snprintf(c,sizeof c,"IP %s",d.ip[0]?d.ip:"--");
    } else if(s->page==3) {
        const app_descriptor_t *idle=app_find(ctx,app_standby_id(ctx));
        snprintf(a,sizeof a,"当前 %s",idle?idle->title:"表情");
        const app_descriptor_t *choice=candidate(ctx,s);
        snprintf(b,sizeof b,"%s %s",s->editing?"选择":"长按修改",s->editing&&choice?choice->title:"");
        snprintf(c,sizeof c,"%s",s->feedback?s->feedback:s->editing?"短按切换 长按保存":"短按下一页");
    } else if(s->page==4) {
        app_hardware_info_t info;app_hardware_info(&info);
        snprintf(a,sizeof a,"屏幕 %s",hardware_title(info.screen));
        snprintf(b,sizeof b,"雷达 %s",hardware_title(info.radar));
        snprintf(c,sizeof c,"舵机 %s",hardware_title(info.servo));
    } else {
        app_hardware_info_t info;app_hardware_info(&info);
        snprintf(a,sizeof a,"当前 %s",profile_title(info.active));
        snprintf(b,sizeof b,"%s %s",s->editing?"选择":"保存",profile_title(s->editing?(app_hardware_profile_t)s->candidate:info.configured));
        snprintf(c,sizeof c,"%s",s->feedback?s->feedback:info.restart_required&&!s->editing?"长按重启生效":s->editing?"短按切换 长按保存":"长按修改");
    }
    app_ui_text(s->lines[0],a); app_ui_text(s->lines[1],b); app_ui_text(s->lines[2],c);
    app_ui_progress(s->ring,app_hold_ms(ctx));
}
static void event(app_context_t *ctx,const app_event_t *e)
{
    state_t *s=ctx->state;
    if(e->kind==APP_EVENT_SHORT) {
        if(s->editing) {
            if(s->page==5) s->candidate=(s->candidate+1)%3;else next_candidate(ctx,s);
        } else s->page=(s->page+1)%6;
        s->feedback=NULL;
    }
    if(e->kind!=APP_EVENT_CONFIRM) return;
    if(s->page==5) {
        app_hardware_info_t info;app_hardware_info(&info);
        if(!info.drivers_compiled) { s->feedback="仅屏幕固件";return; }
        if(info.restart_required && !s->editing) { app_device_restart();return; }
        if(!s->editing) { s->editing=true;s->candidate=info.configured;s->feedback=NULL; }
        else {
            bool saved=app_hardware_set_profile((app_hardware_profile_t)s->candidate);
            if(saved) s->editing=false;
            s->feedback=saved?NULL:"保存失败";
        }
        return;
    }
    if(s->page!=3) return;
    if(!s->editing) {
        s->editing=true;
        for(size_t i=0;i<app_count(ctx);++i) if(!strcmp(app_at(ctx,i)->id,app_standby_id(ctx))) { s->candidate=i; break; }
    } else {
        const app_descriptor_t *choice=candidate(ctx,s);
        bool saved=choice && app_set_standby(ctx,choice->id);
        s->feedback=saved?"已保存":"保存失败"; if(saved) s->editing=false;
    }
}
static void icon(void *parent,int x,int y,bool selected)
{
    uint32_t color=selected?APP_COLOR_ACCENT:0x9BA9C0;
    app_ui_rect(parent,x+4,y+4,20,20,color,5); app_ui_rect(parent,x+10,y+10,8,8,APP_COLOR_BG,4);
    app_ui_rect(parent,x+11,y,6,5,color,0); app_ui_rect(parent,x+11,y+23,6,5,color,0);
    app_ui_rect(parent,x,y+11,5,6,color,0); app_ui_rect(parent,x+23,y+11,5,6,color,0);
}
const app_descriptor_t esphook_app_settings={.api_version=APP_API_VERSION,.id="settings",.title="设置",
    .refresh_ms=40,.state_size=sizeof(state_t),.on_enter=enter,.on_event=event,.render=render,.icon=icon};
