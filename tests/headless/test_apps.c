#include "esphook/app_manager.h"
#include "esphook/app_runtime.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#ifdef ESPHOOK_TEST_STOCK
#include "stock_transport.h"
#endif
#include <assert.h>
#include <math.h>
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
static unsigned restarts;
static app_hardware_profile_t hardware_profile=APP_PROFILE_ROBOT;
app_hardware_profile_t app_hardware_active(void) { return APP_PROFILE_ROBOT; }
void app_hardware_info(app_hardware_info_t *h) { memset(h,0,sizeof *h);h->active=APP_PROFILE_ROBOT;h->configured=hardware_profile;h->drivers_compiled=true;h->restart_required=h->active!=h->configured;h->screen=APP_HW_INITIALIZED;h->radar=APP_HW_DETECTED;h->servo=APP_HW_INITIALIZED;h->physical_keys=1; }
bool app_hardware_set_profile(app_hardware_profile_t p) { hardware_profile=p;return true; }
void app_device_restart(void) { ++restarts; }
void app_device_info(app_device_info_t *d) { memset(d,0,sizeof *d);strcpy(d->version,"0.3.0");strcpy(d->build,"2026-11-16");strcpy(d->daemon_host,"192.168.1.10");strcpy(d->ip,"192.168.1.24");d->daemon_port=18765;d->robot_hardware=true; }
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
/* A glyph missing from the trimmed fixed font renders as blank space on the device: fail loudly instead. */
static void check_glyphs(lv_obj_t *obj)
{
    if(lv_obj_check_type(obj,&lv_label_class)) {
        const lv_font_t *font=lv_obj_get_style_text_font(obj,LV_PART_MAIN);
        for(const unsigned char *p=(const unsigned char *)lv_label_get_text(obj);*p;) {
            uint32_t letter=*p,extra=0;
            if(letter>=0xF0) {letter&=0x07;extra=3;} else if(letter>=0xE0) {letter&=0x0F;extra=2;} else if(letter>=0xC0) {letter&=0x1F;extra=1;}
            ++p;
            for(;extra && *p;--extra,++p) letter=(letter<<6)|(*p&0x3F);
            lv_font_glyph_dsc_t glyph;
            if(letter>=0x20 && !lv_font_get_glyph_dsc(font,&glyph,letter,0)) { fprintf(stderr,"missing glyph U+%04X in \"%s\"\n",letter,lv_label_get_text(obj)); assert(!"label uses a glyph the font does not have"); }
        }
    }
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i) check_glyphs(lv_obj_get_child(obj,(int32_t)i));
}
static unsigned peak_heap;
static void dump(const char *directory,const char *name)
{
    check_glyphs(lv_screen_active());
    lv_mem_monitor_t heap;lv_mem_monitor(&heap);
    unsigned used=(unsigned)(heap.total_size-heap.free_size);
    if(used>peak_heap) peak_heap=used;
    if(getenv("ESPHOOK_HEAP_REPORT")) fprintf(stderr,"heap %6u / %u bytes used after \"%s\"\n",used,(unsigned)heap.total_size,name);
    assert(used<heap.total_size*80/100); /* Leave room for transitions, fragmentation and the display driver. */
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
        for(unsigned i=0;i<12;++i) { /* a wavy synthetic day: candles around a drifting mid price */
            unsigned n=offset+i;int mid=2000+(int)(180*sin(n/9.0))+(int)(n*3),o=mid+(int)(40*sin(n*2.1)),c=mid+(int)(40*cos(n*1.7));
            used+=snprintf(line+used,sizeof line-(size_t)used,"%s[%d,%d,%d,%d,%d,%d]",i?",":"",o,(o>c?o:c)+25,(o<c?o:c)-25,c,mid-15,mid-60); }
        snprintf(line+used,sizeof line-(size_t)used,"]}");assert(stock_transport_command(line));assert(strstr(output,"\"ok\":true"));
    }
    assert(stock_transport_command("game {\"v\":1,\"seq\":3,\"id\":24,\"op\":\"show\",\"cursor\":60,\"state\":\"play\",\"held_ms\":0,\"trades\":0,\"pct_bps\":0,\"baseline_bps\":0,\"pnl_cents\":0,\"holding\":false,\"clock\":\"09:35\"}"));
    assert(strstr(output,"\"ok\":true"));
}
#endif
/* Reads one number out of an app's own "robot <app> status" reply. */
static unsigned game_status(const char *id,const char *line,const char *key)
{
    for(size_t i=0;i<esphook_app_count;++i) if(!strcmp(esphook_apps[i]->id,id)) {
        assert(esphook_apps[i]->command && esphook_apps[i]->command(line));
        const char *at=strstr(output,key);assert(at);return (unsigned)strtoul(at+strlen(key),NULL,10);
    }
    assert(!"unknown app");return 0;
}
#ifdef ESPHOOK_TEST_BASKETBALL
/* Shoots until it has seen a make and a miss, then lets the clock run out. Frames are for eyes only. */
static void play_basketball(const char *out)
{
    assert(app_open(&manager.context,"basketball"));app_manager_commit(&manager);run(100);
    assert(game_status("basketball","robot game status","phase=")==1);dump(out,"basketball-idle");
    unsigned made=0;bool saw_hit=false,saw_miss=false;
    for(unsigned shot=0;shot<60 && !(saw_hit && saw_miss);++shot) {
        event(APP_EVENT_SHORT);run(160);if(shot==0) dump(out,"basketball-flight");
        run(320);
        unsigned now_made=game_status("basketball","robot game status","made=");
        if(now_made>made && !saw_hit) { saw_hit=true;dump(out,"basketball-hit"); }
        else if(now_made==made && !saw_miss) { saw_miss=true;dump(out,"basketball-miss"); }
        made=now_made;run(300);
    }
    assert(saw_hit && saw_miss);
    for(unsigned s=0;s<40 && game_status("basketball","robot game status","phase=")!=2;++s) run(5000);
    assert(game_status("basketball","robot game status","phase=")==2);run(300);dump(out,"basketball-result");
}
#endif
#ifdef ESPHOOK_TEST_DINO
/* Plays a few seconds: jump, run towards the first obstacle without jumping, crash. Frames are for eyes only. */
static void play_dino(const char *out)
{
    assert(app_open(&manager.context,"dino"));app_manager_commit(&manager);run(100);
    assert(game_status("dino","robot dino status","phase=")==0);dump(out,"dino-ready");
    event(APP_EVENT_DOWN);run(250);dump(out,"dino-jump");assert(game_status("dino","robot dino status","phase=")==1);
    event(APP_EVENT_UP);run(650);dump(out,"dino-land");run(1500);dump(out,"dino-run");
    for(unsigned s=0;s<30 && game_status("dino","robot dino status","phase=")!=2;++s) run(500);
    assert(game_status("dino","robot dino status","phase=")==2);run(700);dump(out,"dino-over");
    assert(game_status("dino","robot dino status","best=")>0);
    run(600);event(APP_EVENT_DOWN);event(APP_EVENT_UP);assert(game_status("dino","robot dino status","phase=")==1); /* restart */
}
#endif
#ifdef ESPHOOK_TEST_SKI
static void play_ski(const char *out)
{
    assert(app_open(&manager.context,"ski"));app_manager_commit(&manager);run(100);
    assert(game_status("ski","robot ski status","phase=")==0);dump(out,"ski-ready");
    event(APP_EVENT_DOWN);run(60);dump(out,"ski-takeoff");assert(game_status("ski","robot ski status","phase=")==1);
    run(250);dump(out,"ski-air");event(APP_EVENT_UP);run(1100);dump(out,"ski-run");
    for(unsigned s=0;s<120 && game_status("ski","robot ski status","phase=")!=2;++s) run(500); /* never jumping again meets a rock or a chasm */
    assert(game_status("ski","robot ski status","phase=")==2);run(400);dump(out,"ski-over");
    assert(game_status("ski","robot ski status","best=")>0);
    run(700);event(APP_EVENT_DOWN);event(APP_EVENT_UP);assert(game_status("ski","robot ski status","phase=")==1); /* restart */
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
#ifdef ESPHOOK_TEST_BASKETBALL
    play_basketball(out);
#endif
#ifdef ESPHOOK_TEST_DINO
    play_dino(out);
#endif
#ifdef ESPHOOK_TEST_SKI
    play_ski(out);
#endif
    assert(app_open(&manager.context,"settings"));app_manager_commit(&manager);run(20);
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(standby[0] && strcmp(standby,"face"));dump(out,"settings-standby");
    event(APP_EVENT_SHORT);dump(out,"settings-hardware");event(APP_EVENT_SHORT);dump(out,"settings-profile");
    event(APP_EVENT_CONFIRM);event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);
    assert(hardware_profile==APP_PROFILE_AUTO && restarts==0);
    event(APP_EVENT_CONFIRM);assert(restarts==1);
    assert(app_open(&manager.context,"launcher"));app_manager_commit(&manager);run(20);
    for(size_t i=0;i<app_count(&manager.context);++i) { event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"launcher")); }
#ifdef ESPHOOK_TEST_DEMO
    assert(app_open(&manager.context,"demo"));app_manager_commit(&manager);event(APP_EVENT_SHORT);
    app_event_t message={.kind=APP_EVENT_MESSAGE,.text="{\"text\":\"PC 自定义标题\"}",.ms=manager.context.now_ms};app_manager_event(&manager,&message);run(20);dump(out,"demo-message");
#endif
    assert(app_open(&manager.context,"face"));app_manager_commit(&manager);
    if(manager.context.app->on_leave) manager.context.app->on_leave(&manager.context);
    destroy(manager.context.root,NULL);lv_display_delete(d);lv_deinit();free(font);
    printf("all built-in apps: real LVGL rendering, %u app transitions, 64KB UI heap (peak %u bytes at sampled frames), settings persistence and PC message passed\n",(unsigned)(30*esphook_app_count),peak_heap);
}
