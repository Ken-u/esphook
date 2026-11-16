#include "esphook/app.h"
#include "esphook/app_ui.h"
#include <stdio.h>
typedef struct { lv_obj_t *icons[3], *titles[3], *ring, *page; size_t page_index, previous; } state_t;
static void arc_x(void *object,int32_t value) { lv_obj_set_x(object,value); }
static void enter(app_context_t *ctx)
{
    state_t *s=ctx->state; s->previous=(size_t)-1; s->page_index=(size_t)-1;
    lv_obj_t *hint=app_ui_label(ctx->root,&aihook_font_12,2,0,126,16,LV_LABEL_LONG_MODE_CLIP);
    app_ui_text(hint,"短按切换 长按进入"); app_ui_center(hint);
    for(unsigned i=0;i<3;++i) {
        s->icons[i]=lv_obj_create(ctx->root); app_ui_plain(s->icons[i]);
        lv_obj_set_pos(s->icons[i],18+i*48,24); lv_obj_set_size(s->icons[i],28,28);
        s->titles[i]=app_ui_label(ctx->root,&aihook_font_12,8+i*48,60,48,16,LV_LABEL_LONG_MODE_CLIP); app_ui_center(s->titles[i]);
    }
    s->ring=app_ui_ring(ctx->root,11,17,42);
    s->page=app_ui_label(ctx->root,LV_FONT_DEFAULT,130,0,30,9,LV_LABEL_LONG_MODE_CLIP);
}
static void render(app_context_t *ctx)
{
    state_t *s=ctx->state; size_t selected=app_selection(ctx), page=selected/3;
    if(s->previous!=selected) {
        for(unsigned i=0;i<3;++i) {
            lv_obj_clean(s->icons[i]); const app_descriptor_t *a=app_at(ctx,page*3+i);
            app_ui_text(s->titles[i],a?a->title:"");
            if(a && a->icon) a->icon(s->icons[i],0,0,page*3+i==selected);
            else if(a) app_ui_rect(s->icons[i],4,4,20,20,APP_COLOR_MUTED,4);
            lv_obj_set_style_text_color(s->titles[i],app_ui_color(page*3+i==selected?APP_COLOR_ACCENT:APP_COLOR_MUTED),LV_PART_MAIN);
        }
        int target=11+(selected%3)*48; lv_anim_delete(s->ring,NULL);
        if(s->page_index!=page || s->previous==(size_t)-1) lv_obj_set_x(s->ring,target);
        else { lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a,s->ring); lv_anim_set_values(&a,lv_obj_get_x(s->ring),target);
            lv_anim_set_exec_cb(&a,arc_x); lv_anim_set_duration(&a,160); lv_anim_set_path_cb(&a,lv_anim_path_ease_out); lv_anim_start(&a); }
        s->page_index=page; s->previous=selected;
        char text[32]; snprintf(text,sizeof text,"%u/%u",(unsigned)(page+1),(unsigned)((app_count(ctx)+2)/3)); app_ui_text(s->page,text);
    }
    app_ui_progress(s->ring,app_hold_ms(ctx));
}
static void event(app_context_t *ctx,const app_event_t *e)
{
    if(e->kind==APP_EVENT_SHORT) app_select_next(ctx);
    if(e->kind==APP_EVENT_CONFIRM) { const app_descriptor_t *a=app_at(ctx,app_selection(ctx)); if(a) (void)app_open(ctx,a->id); }
}
const app_descriptor_t esphook_app_launcher={.api_version=APP_API_VERSION,.id="launcher",.title="菜单",
    .flags=APP_HIDDEN,.refresh_ms=16,.state_size=sizeof(state_t),.on_enter=enter,.on_event=event,.render=render};
