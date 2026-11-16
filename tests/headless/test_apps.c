#include "esphook/app_manager.h"
#include "esphook/app_runtime.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#ifdef ESPHOOK_TEST_STOCK
#include "stock_transport.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static app_manager_t manager;
static robot_ui_t ui;
static char standby[24],output[512];
static bool locked;
static uint16_t framebuffer[160*80],draw_buffer[160*16];
static unsigned roots;
void *xSemaphoreCreateMutex(void) { return &locked; }
int xSemaphoreTake(void *s,unsigned t) { (void)s;(void)t;assert(!locked);locked=true;return 1; }
int xSemaphoreGive(void *s) { (void)s;assert(locked);locked=false;return 1; }
uint64_t app_clock_ms(void) { return manager.context.now_ms; }
uint32_t app_random(void) { return 24; }
bool app_transport_write(const char *s) { snprintf(output,sizeof output,"%s",s);return true; }
bool app_runtime_is_active(const char *id) { return !strcmp(app_active_id(&manager.context),id); }
bool app_runtime_open(const char *id) { return app_open(&manager.context,id); }
bool app_runtime_menu(void) { app_menu(&manager.context);return true; }
void app_radar_read(app_radar_t *r) { memset(r,0,sizeof *r);r->available=r->online=true;r->range_mm=6000;r->frame.targets[0]=(ld2450_target_t){.valid=true,.x_mm=1000,.y_mm=1500}; }
void app_servo_read(app_servo_t *s) { memset(s,0,sizeof *s);s->available=true;s->tracking=ROBOT_IDLE; }
void app_hook_read(app_hook_t *h) { memset(h,0,sizeof *h);h->current=h->connected=true;h->clients=1;h->unread=2;strcpy(h->client,"Codex");strcpy(h->title,"任务已完成");strcpy(h->body,"长按回复继续"); }
bool app_hook_reply(unsigned key) { return key<3; }
void app_key_word(unsigned key,char *s,size_t n) { const char *words[]={"ok","continue","done"};snprintf(s,n,"%s",words[key%3]); }
void app_device_info(app_device_info_t *d) { memset(d,0,sizeof *d);strcpy(d->version,"0.2.0");strcpy(d->build,"2026-11-16");strcpy(d->daemon_host,"192.168.1.10");strcpy(d->ip,"192.168.1.24");d->daemon_port=18765;d->robot_hardware=true; }
void app_display_read(robot_ui_t *v) { *v=ui; }
bool app_display_update(const char *j,size_t n,char *e,size_t z) { return robot_ui_parse(j,n,&ui,e,z); }
bool app_host_event(const app_context_t *c,const char *a,unsigned k,const char *t) { (void)c;(void)a;(void)k;(void)t;return true; }
static void *scene(void *u) { (void)u;assert(!roots);++roots;return app_ui_layer(lv_screen_active()); }
static void destroy(void *r,void *u) { (void)u;lv_obj_delete(r);assert(roots==1);--roots; }
static bool save(const char *id,void *u) { (void)u;snprintf(standby,sizeof standby,"%s",id);return true; }
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *px)
{
    int w=a->x2-a->x1+1;
    for(int y=a->y1;y<=a->y2;++y) memcpy(framebuffer+y*160+a->x1,px+(y-a->y1)*w*2,w*2);
    lv_display_flush_ready(d);
}
static void run(unsigned ms)
{
    for(unsigned i=0;i<ms;i+=5) { lv_tick_inc(5);app_manager_tick(&manager,manager.context.now_ms+5);app_manager_render(&manager);lv_timer_handler(); }
}
static void dump(const char *directory,const char *name)
{
    if(!directory) return;
    lv_refr_now(lv_display_get_default());
    char path[512];snprintf(path,sizeof path,"%s/%s.ppm",directory,name);FILE *f=fopen(path,"wb");assert(f);
    fprintf(f,"P6\n160 80\n255\n");
    for(unsigned i=0;i<160*80;++i) { unsigned c=framebuffer[i];unsigned char rgb[]={(c>>11)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};fwrite(rgb,1,3,f); }
    fclose(f);
}
static void event(app_event_kind_t kind) { app_event_t e={.kind=kind,.ms=manager.context.now_ms};app_manager_event(&manager,&e);run(20); }
#ifdef ESPHOOK_TEST_STOCK
static void stock_data(void)
{
    assert(stock_transport_command("game {\"v\":1,\"seq\":1,\"id\":24,\"op\":\"begin\",\"count\":108,\"first\":60,\"code\":\"600000\",\"date\":\"2026-11-12\",\"label\":\"测试\"}"));
    for(unsigned offset=0;offset<108;offset+=12) {
        char line[900];int used=snprintf(line,sizeof line,"game {\"v\":1,\"seq\":2,\"id\":24,\"op\":\"bars\",\"offset\":%u,\"bars\":[",offset);
        for(unsigned i=0;i<12;++i) used+=snprintf(line+used,sizeof line-(size_t)used,"%s[1000,1100,900,1050,1000,990]",i?",":"");
        snprintf(line+used,sizeof line-(size_t)used,"]}");assert(stock_transport_command(line));assert(strstr(output,"\"ok\":true"));
    }
    assert(stock_transport_command("game {\"v\":1,\"seq\":3,\"id\":24,\"op\":\"show\",\"cursor\":60,\"state\":\"play\",\"held_ms\":0,\"trades\":0,\"pct_bps\":0,\"baseline_bps\":0,\"pnl_cents\":0,\"holding\":false,\"clock\":\"09:35\"}"));
    assert(strstr(output,"\"ok\":true"));
}
#endif
void aihook_font_12_set_bitmap(const uint8_t *);
void aihook_font_16_set_bitmap(const uint8_t *);
int main(int argc,char **argv)
{
    FILE *file=fopen(ESPHOOK_FONT_FILE,"rb");assert(file);fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    uint8_t *font=malloc((size_t)size);assert(font && fread(font,1,(size_t)size,file)==(size_t)size);fclose(file);
    uint32_t *h=(uint32_t *)font;assert(h[0]==0x44464841 && h[4]<(unsigned)size && h[6]<(unsigned)size);
    aihook_font_12_set_bitmap(font+h[4]);aihook_font_16_set_bitmap(font+h[6]);
    lv_init();lv_display_t *d=lv_display_create(160,80);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,draw_buffer,NULL,sizeof draw_buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    for(size_t i=0;i<esphook_app_count;++i) if(esphook_apps[i]->init) assert(esphook_apps[i]->init());
    app_platform_t p={.scene_create=scene,.scene_delete=destroy,.save_standby=save};ui=robot_ui_default();
    assert(app_manager_init(&manager,esphook_apps,esphook_app_count,&p,"face",0));run(100);
#ifdef ESPHOOK_TEST_STOCK
    stock_data();
#endif
    const char *out=argc>1?argv[1]:NULL;
    for(unsigned round=0;round<30;++round) {
        for(size_t i=0;i<esphook_app_count;++i) {
            const app_descriptor_t *a=esphook_apps[i];assert(app_open(&manager.context,a->id));app_manager_commit(&manager);run(100);
            assert(roots==1 && lv_obj_get_child_count(lv_screen_active())==1);
            if(round==0) dump(out,a->id);
            if(round==0 && !strcmp(a->id,"hook")) {
                lv_refr_now(d);unsigned ink=0;
                for(unsigned y=22;y<58;++y) for(unsigned x=5;x<150;++x) if(framebuffer[y*160+x]!=framebuffer[159]) ++ink;
                assert(ink>100); /* Compressed 16px Chinese text must actually render. */
            }
        }
    }
    assert(app_open(&manager.context,"settings"));app_manager_commit(&manager);run(20);
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(standby[0] && strcmp(standby,"face"));dump(out,"settings-standby");
    assert(app_open(&manager.context,"launcher"));app_manager_commit(&manager);run(20);
    for(size_t i=0;i<app_count(&manager.context);++i) { event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"launcher")); }
#ifdef ESPHOOK_TEST_DEMO
    assert(app_open(&manager.context,"demo"));app_manager_commit(&manager);event(APP_EVENT_SHORT);
    app_event_t message={.kind=APP_EVENT_MESSAGE,.text="{\"text\":\"PC 自定义标题\"}",.ms=manager.context.now_ms};app_manager_event(&manager,&message);run(20);dump(out,"demo-message");
#endif
    assert(app_open(&manager.context,"face"));app_manager_commit(&manager);
    if(manager.context.app->on_leave) manager.context.app->on_leave(&manager.context);
    destroy(manager.context.root,NULL);lv_display_delete(d);lv_deinit();free(font);
    printf("all built-in apps: real LVGL rendering, %u app transitions, 64KB UI heap, settings persistence and PC message passed\n",(unsigned)(30*esphook_app_count));
}
