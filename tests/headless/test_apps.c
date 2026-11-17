#include "esphook/app_manager.h"
#include "esphook/app_runtime.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#ifdef ESPHOOK_TEST_STOCK
#include "stock_transport.h"
#endif
#ifdef ESPHOOK_TEST_STARSHIP
#include "starship_core.h"
#endif
#ifdef ESPHOOK_TEST_RACING
#include "racing_core.h"
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
/* The radar sees one person and the servo is a plain recorder: a game can be steered and its servo requests inspected. */
static ld2450_target_t radar_target={.valid=true,.x_mm=1000,.y_mm=1500};
static bool radar_online=true;
void app_radar_read(app_radar_t *r) { memset(r,0,sizeof *r);r->available=true;r->online=radar_online;r->range_mm=6000;r->frame.targets[0]=radar_target;r->frame.targets[0].x_mm=(int16_t)-radar_target.x_mm; }
/* radar_target.x_mm is where the player stands to their own right; the sensor's +X is the player's left, so the frame is mirrored. */
static app_servo_t servo_state={.available=true,.tracking=ROBOT_IDLE,.limit_deg=30,.speed_dps=35};
static struct { unsigned arm,disarm,angle; int last,lo,hi; } servo_log;
void app_servo_read(app_servo_t *s) { *s=servo_state; }
bool app_servo_request(app_context_t *c,app_servo_action_t a,int v)
{
    (void)c;
    if(a==APP_SERVO_ARM) { ++servo_log.arm;servo_state.armed=true;return true; }
    if(a==APP_SERVO_DISARM) { ++servo_log.disarm;servo_state.armed=false;return true; }
    if(a!=APP_SERVO_ANGLE || !servo_state.armed || v>servo_state.limit_deg || v<-servo_state.limit_deg) return false;
    ++servo_log.angle;servo_log.last=v;if(v<servo_log.lo) servo_log.lo=v;if(v>servo_log.hi) servo_log.hi=v;servo_state.angle_deg=(float)v;return true;
}
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
/* K1 through the framework's gesture layer, and the two ways out of a game that switched the triple off. */
__attribute__((unused)) static void k1(bool down) { app_manager_key(&manager,0,down,manager.context.now_ms);run(20); }
__attribute__((unused)) static bool left(const char *id) { return strcmp(app_active_id(&manager.context),id)!=0; }
/* Three quick presses; true when they went back to the menu. */
__attribute__((unused)) static bool mash_leaves(const char *id) { for(unsigned i=0;i<3;++i) { k1(true);run(60);k1(false);run(60); } return left(id); }
/* Holds K1 for `ms` and lets go; true when the hold itself went back to the menu. */
__attribute__((unused)) static bool hold_leaves(const char *id,unsigned ms) { k1(true);run(ms);const bool out=left(id);k1(false);return out; }
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
/* True when a label with exactly this text is on screen (it and all its parents are not hidden). */
static bool shows_text(lv_obj_t *obj,const char *text)
{
    if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)) return false;
    if(lv_obj_check_type(obj,&lv_label_class) && !strcmp(lv_label_get_text(obj),text)) return true;
    for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i) if(shows_text(lv_obj_get_child(obj,(int32_t)i),text)) return true;
    return false;
}
/* The hint line of a game that switched the triple off alternates with the way out: either may be on screen. */
static bool shows_hint(const char *hint) { return shows_text(lv_screen_active(),hint) || shows_text(lv_screen_active(),"按住2秒退出"); }
static unsigned changed_pixels(const uint16_t *a,const uint16_t *b) { unsigned n=0;for(unsigned i=0;i<160*64;++i) n+=a[i]!=b[i];return n; }
#ifdef ESPHOOK_TEST_BASKETBALL
/* Shoots until it has seen a make and a miss, then lets the clock run out. Frames are for eyes only. */
static void play_basketball(const char *out)
{
    assert(app_open(&manager.context,"basketball"));app_manager_commit(&manager);run(100);
    /* shots come fast: three quick presses are shots, and only holding the key for two seconds leaves mid-game */
    assert(!mash_leaves("basketball") && game_status("basketball","robot game status","shots=")>=1);
    assert(!hold_leaves("basketball",1900) && hold_leaves("basketball",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
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
    /* a hopping game: three quick presses are hops, holding the key for two seconds is the way out */
    k1(true);run(1500);dump(out,"dino-exit-hold");k1(false);
    assert(!mash_leaves("dino") && !hold_leaves("dino",1900) && hold_leaves("dino",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
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
    /* a jumping game: three quick presses are jumps, holding the key for three seconds is the way out */
    assert(!mash_leaves("ski") && !hold_leaves("ski",2900) && hold_leaves("ski",3100) && !strcmp(app_active_id(&manager.context),"launcher"));
}
#endif
#ifdef ESPHOOK_TEST_MUYU
static void play_muyu(const char *out)
{
    assert(app_open(&manager.context,"muyu"));app_manager_commit(&manager);run(300);
    assert(game_status("muyu","robot muyu status","count=")==0 && game_status("muyu","robot muyu status","auto=")==0);
    assert(shows_text(lv_screen_active(),"功德 0") && !shows_text(lv_screen_active(),"功德+1"));dump(out,"muyu-idle");
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(35);dump(out,"muyu-strike"); /* the mallet is on its way down, a faint trail behind it */
    assert(game_status("muyu","robot muyu status","count=")==1 && shows_text(lv_screen_active(),"功德 1") && !shows_text(lv_screen_active(),"功德+1")); /* the counter is instant, the label waits for the landing */
    run(30);dump(out,"muyu-swing");run(55);dump(out,"muyu-impact"); /* ~120 ms: the head has just landed on the fish */
    assert(shows_text(lv_screen_active(),"功德+1"));
    run(200);dump(out,"muyu-float");
    run(500);assert(!shows_text(lv_screen_active(),"功德+1"));dump(out,"muyu-calm"); /* the label has risen and faded away */
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(130);event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(45);dump(out,"muyu-chain"); /* a quick second knock starts from the rebound */
    assert(game_status("muyu","robot muyu status","count=")==3);run(1000);
    for(unsigned i=0;i<4;++i) { event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(400); }
    assert(game_status("muyu","robot muyu status","count=")==7);
    event(APP_EVENT_CONFIRM);assert(game_status("muyu","robot muyu status","auto=")==1 && shows_text(lv_screen_active(),"自动"));
    run(2100);assert(game_status("muyu","robot muyu status","count=")==9);dump(out,"muyu-auto");
    event(APP_EVENT_CONFIRM);assert(game_status("muyu","robot muyu status","auto=")==0);run(2100);
    assert(game_status("muyu","robot muyu status","count=")==9);
    for(unsigned i=9;i<107;++i) { event(APP_EVENT_DOWN);event(APP_EVENT_UP); }
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(190); /* the 107th label has been taken over by the 108th */
    assert(game_status("muyu","robot muyu status","count=")==108 && game_status("muyu","robot muyu status","rounds=")==1);
    assert(shows_text(lv_screen_active(),"功德圆满") && shows_text(lv_screen_active(),"功德 108"));dump(out,"muyu-round");
    assert(game_status("muyu","robot muyu status","frames=")>100);
    event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"muyu")); /* SHORT is the release of a knock: no navigation */
    assert(!mash_leaves("muyu") && !hold_leaves("muyu",1900) && hold_leaves("muyu",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
}
#endif
#ifdef ESPHOOK_TEST_AQUARIUM
static void play_aquarium(const char *out)
{
    assert(app_open(&manager.context,"aquarium"));app_manager_commit(&manager);run(400);
    assert(game_status("aquarium","robot aquarium status","thrown=")==0 && shows_text(lv_screen_active(),"投食 0") && shows_text(lv_screen_active(),"进食 0"));
    dump(out,"aquarium-idle");
    static uint16_t previous[160*80];lv_refr_now(lv_display_get_default());memcpy(previous,framebuffer,sizeof previous);run(300);lv_refr_now(lv_display_get_default());
    assert(changed_pixels(previous,framebuffer)>300); /* water, kelp and fish never stand still */
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(60);dump(out,"aquarium-drop"); /* the pinch leaves the fingers, ripples start */
    assert(game_status("aquarium","robot aquarium status","thrown=")==1 && game_status("aquarium","robot aquarium status","food=")>=2 && shows_text(lv_screen_active(),"投食 1")); /* a hungry fish right below the hand may have taken one already */
    run(250);dump(out,"aquarium-sinking");run(900);dump(out,"aquarium-chase");
    for(unsigned i=0;i<3;++i) { run(900);event(APP_EVENT_DOWN);event(APP_EVENT_UP); }
    run(3000);dump(out,"aquarium-feast");
    run(4000);dump(out,"aquarium-later");
    assert(game_status("aquarium","robot aquarium status","thrown=")==4 && game_status("aquarium","robot aquarium status","eaten=")>=2);
    char text[32];snprintf(text,sizeof text,"进食 %u",game_status("aquarium","robot aquarium status","eaten="));assert(shows_text(lv_screen_active(),text));
    assert(game_status("aquarium","robot aquarium status","frames=")>300);
    event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"aquarium")); /* a press is a feeding, never navigation */
    assert(!mash_leaves("aquarium") && !hold_leaves("aquarium",1900) && hold_leaves("aquarium",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
}
#endif
#ifdef ESPHOOK_TEST_FISHING
/* Plays one fight perfectly from the numbers in the status line (phase 4 is the fight, state 1 a pull on its way,
 * state 2 a hold in progress). Returns the number of pulls answered. */
static unsigned fishing_fight(const char *out,bool frames)
{
    unsigned answered=0;bool ring=false,hold=false,toast=false;
    for(unsigned guard=0;guard<8000 && game_status("fishing","robot fishing status","phase=")==4;++guard) {
        const unsigned state=game_status("fishing","robot fishing status","state=");
        const int to=(int)game_status("fishing","robot fishing status","to=");
        const unsigned kind=game_status("fishing","robot fishing status","kind=");
        if(frames && state==1 && kind==0 && to<330 && to>250 && !ring) { dump(out,"fishing-ring");ring=true; }
        if(state==2 && frames && !hold && to>200) { dump(out,"fishing-hold");hold=true; }
        if(state==1 && to<=0) { event(APP_EVENT_DOWN);if(kind==0) { event(APP_EVENT_UP);++answered; } }
        else if(state==2 && to<=0) { event(APP_EVENT_UP);++answered; }
        else run(5);
        if(frames && !toast && state==3 && shows_text(lv_screen_active(),"完美!")) { dump(out,"fishing-toast");toast=true; }
    }
    return answered;
}
static void fishing_hook(void)
{
    for(unsigned i=0;i<100 && game_status("fishing","robot fishing status","phase=")==5;++i) run(50); /* still landing the last one */
    for(unsigned i=0;i<100 && game_status("fishing","robot fishing status","phase=")==7;++i) run(50);
    const unsigned phase=game_status("fishing","robot fishing status","phase=");
    if(phase==6) run(800);
    if(phase==0 || phase==6) { event(APP_EVENT_DOWN);event(APP_EVENT_UP); }
    for(unsigned i=0;i<200 && game_status("fishing","robot fishing status","phase=")!=3;++i) run(50);
    assert(game_status("fishing","robot fishing status","phase=")==3);
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);assert(game_status("fishing","robot fishing status","phase=")==4);
}
static void play_fishing(const char *out)
{
    assert(app_open(&manager.context,"fishing"));app_manager_commit(&manager);run(300);
    assert(game_status("fishing","robot fishing status","phase=")==0 && game_status("fishing","robot fishing status","servo=")==1);
    assert(shows_hint("按键抛竿 长按启用舵机") && shows_text(lv_screen_active(),"钓0"));dump(out,"fishing-idle");
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(300);
    assert(game_status("fishing","robot fishing status","phase=")==1 && game_status("fishing","robot fishing status","casts=")==1);dump(out,"fishing-cast");
    for(unsigned i=0;i<60 && game_status("fishing","robot fishing status","phase=")!=2;++i) run(50);
    assert(game_status("fishing","robot fishing status","phase=")==2 && shows_hint("盯住鱼漂 等它下沉"));dump(out,"fishing-wait");
    for(unsigned i=0;i<120 && game_status("fishing","robot fishing status","phase=")!=3;++i) run(50);
    assert(game_status("fishing","robot fishing status","phase=")==3 && shows_text(lv_screen_active(),"鱼漂下沉! 快按!"));run(120);dump(out,"fishing-bite");
    assert(servo_log.arm==0 && servo_log.angle==0); /* nothing moves the servo until it has been enabled on purpose */
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);
    assert(game_status("fishing","robot fishing status","phase=")==4);run(200);dump(out,"fishing-fight");
    const unsigned need=game_status("fishing","robot fishing status","need=");
    assert(fishing_fight(out,true)>=need);
    const unsigned phase=game_status("fishing","robot fishing status","phase=");
    assert(phase==5 || phase==6);run(300);dump(out,"fishing-land");run(380);dump(out,"fishing-lift");run(620);
    assert(game_status("fishing","robot fishing status","phase=")==6 && game_status("fishing","robot fishing status","caught=")==1);
    assert(shows_text(lv_screen_active(),"钓1") && shows_hint("按键再钓一次"));dump(out,"fishing-result");
    assert(servo_log.arm==0 && servo_log.angle==0);
    /* a long hold enables the servo: from then on the pulls shake it, inside its limits */
    run(800);event(APP_EVENT_DOWN);event(APP_EVENT_UP);event(APP_EVENT_CONFIRM);
    assert(servo_log.arm==1 && servo_state.armed && game_status("fishing","robot fishing status","servo=")==2);
    event(APP_EVENT_CONFIRM);assert(servo_log.arm==1); /* already enabled: no second request */
    fishing_hook();
    assert(shows_text(lv_screen_active(),"舵机"));
    assert(fishing_fight(out,false)>=1);
    assert(servo_log.angle>=4 && servo_log.hi>=2 && servo_log.lo<=-1 && servo_log.hi<=29 && servo_log.lo>=-29);
    /* a fight that is not answered at all ends with the line broken and the fish gone */
    fishing_hook();
    for(unsigned i=0;i<400 && game_status("fishing","robot fishing status","phase=")==4;++i) run(50);
    assert(game_status("fishing","robot fishing status","phase=")==7 && game_status("fishing","robot fishing status","lost=")>=1);
    run(250);assert(shows_text(lv_screen_active(),"鱼跑了… 断线了"));dump(out,"fishing-lost");
    assert(game_status("fishing","robot fishing status","frames=")>300);
    servo_state.armed=false;
    event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"fishing"));
    /* three quick presses are casts and strikes, never an exit; holding for two seconds leaves while the line is calm */
    assert(!mash_leaves("fishing") && !hold_leaves("fishing",1900) && hold_leaves("fishing",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
    /* ...but a hold that began with the bite belongs to the fight, however long it lasts */
    assert(app_open(&manager.context,"fishing"));app_manager_commit(&manager);run(300);
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);
    for(unsigned i=0;i<200 && game_status("fishing","robot fishing status","phase=")!=3;++i) run(50);
    assert(game_status("fishing","robot fishing status","phase=")==3);
    k1(true);run(2600);assert(!left("fishing"));k1(false);
}
#endif
#ifdef ESPHOOK_TEST_STARSHIP
#define SS_STATUS(k) game_status("starship","robot starship status",k)
/* A real key edge through the framework's gesture layer (tap = SHORT, half a second = CONFIRM). */
static void key(unsigned k,bool down) { app_manager_key(&manager,k,down,manager.context.now_ms);run(20); }
static void tap(void) { key(0,true);run(100);key(0,false); }
/* A person leaning: the radar sees them move there, not teleport. */
static void lean_to(int x,unsigned ms)
{
    for(unsigned t=0;t<ms;t+=20) { int d=x-radar_target.x_mm;if(d>40) d=40;if(d<-40) d=-40;radar_target.x_mm=(int16_t)(radar_target.x_mm+d);run(20); }
}
static unsigned weave(unsigned ms,bool fire,bool frames,const char *out)
{
    /* The "player": leans left and right in front of the radar and holds the trigger. */
    unsigned boom=SS_STATUS("kills="),hp=SS_STATUS("hp="),foe_frames=0;bool saw_foe=false;
    for(unsigned t=0;t<ms && SS_STATUS("phase=")==1;t+=20) {
        radar_target.x_mm=(int16_t)(1000+(int)(300*sin(t/1100.0)));
        run(20);
        if(!frames) continue;
        if(SS_STATUS("kills=")>boom) { boom=SS_STATUS("kills=");dump(out,"starship-boom"); }
        if(SS_STATUS("hp=")<hp) { hp=SS_STATUS("hp=");run(60);char name[24];snprintf(name,sizeof name,"starship-hit%u",hp);dump(out,name); }
        if(SS_STATUS("foes=")>=1 && ++foe_frames==150 && !saw_foe) { saw_foe=true;dump(out,"starship-enemy"); }
    }
    (void)fire;
    return SS_STATUS("kills=");
}
/* Poses a scene by writing into the game core (the first member of the app state) so close-ups can be looked at. */
static void pose_starship(const char *out)
{
    for(unsigned i=0;i<4000 && SS_STATUS("phase=")!=3;++i) run(50); /* wait for the current game to end */
    run(1000);tap();assert(SS_STATUS("phase=")==1);
    ss_t *g=manager.context.state;
    memset(g->obj,0,sizeof g->obj);memset(g->bolt,0,sizeof g->bolt);memset(g->shot,0,sizeof g->shot);
    g->rock_wait=g->enemy_wait=60000;g->cam_x=g->target_x=0;
    g->obj[0]=(ss_obj_t){.alive=1,.kind=SO_ENEMY,.x=-150,.x0=-150,.z=430,.pz=430,.r=95,.hp=2,.state=SE_HOLD,.fire=9,.seed=40,.age=100};
    g->obj[1]=(ss_obj_t){.alive=1,.kind=SO_ASTEROID,.x=300,.z=300,.pz=300,.r=150,.hp=3,.seed=17,.age=30};
    g->obj[2]=(ss_obj_t){.alive=1,.kind=SO_ASTEROID,.x=-420,.z=520,.pz=520,.r=100,.hp=1,.seed=130,.age=5};
    g->obj[3]=(ss_obj_t){.alive=1,.kind=SO_ENEMY,.x=420,.x0=420,.z=900,.pz=900,.r=95,.hp=2,.state=SE_HOLD,.fire=200,.seed=90,.age=10};
    g->bolt[0]=(ss_bolt_t){.x=0,.z=560,.alive=1};
    g->shot[0]=(ss_shot_t){.x=-60,.z=820,.pz=770,.alive=1};
    g->shot[1]=(ss_shot_t){.x=60,.z=1200,.pz=1148,.alive=1};
    g->cooldown=SS_FIRE_STEPS;
    run(40);dump(out,"starship-pose-near");
    g->obj[0].z=g->obj[0].pz=190;g->obj[0].state=SE_DIVE;g->obj[0].x=60;g->obj[0].flash=3;
    g->obj[1].z=g->obj[1].pz=190;g->obj[1].x=-260;g->obj[1].r=190;g->obj[0].hp=1;
    g->bolt[0].z=300;g->bolt[0].x=-130;
    memset(g->shot,0,sizeof g->shot);
    run(40);dump(out,"starship-pose-close");
    g->obj[0].z=g->obj[0].pz=1100;g->obj[0].state=SE_ENTER;
    run(40);dump(out,"starship-pose-far");
}
static void play_starship(const char *out)
{
    servo_state.armed=false;memset(&servo_log,0,sizeof servo_log);radar_online=true;radar_target.x_mm=1000;
    assert(app_open(&manager.context,"starship"));app_manager_commit(&manager);run(400);
    assert(SS_STATUS("phase=")==0 && SS_STATUS("servo=")==1 && SS_STATUS("hp=")==3 && SS_STATUS("objs=")==0);
    assert(shows_text(lv_screen_active(),"星际战机") && shows_hint("按键出击 长按启用舵机"));dump(out,"starship-title");
    tap();assert(SS_STATUS("phase=")==1 && shows_text(lv_screen_active(),"按住开火 雷达转向") && !shows_text(lv_screen_active(),"星际战机"));
    /* the key is the gun: holding it for more than half a second is shooting, never the servo confirmation */
    key(0,true);run(700);assert(SS_STATUS("shots=")>=3);dump(out,"starship-fire");key(0,false);
    assert(servo_log.arm==0 && SS_STATUS("servo=")==1 && SS_STATUS("phase=")==1);
    /* the radar: the first position is the centre, leaning 35 cm either way is full lock */
    run(300);assert(abs((int)SS_STATUS("cam="))<60 && SS_STATUS("src=")==1);
    lean_to(1350,900);assert((int)SS_STATUS("cam=")>800 && (int)SS_STATUS("steer=")>200);dump(out,"starship-right");
    lean_to(650,1400);assert((int)SS_STATUS("cam=")<-800 && (int)SS_STATUS("steer=")<-200);dump(out,"starship-left");
    lean_to(1000,1500);assert(abs((int)SS_STATUS("cam="))<150);
    /* a pilot who weaves and shoots gets kills and takes hits; with the servo disabled nothing moves */
    key(0,true);
    assert(weave(60000,true,true,out)>=1);
    key(0,false);
    assert(servo_log.angle==0 && servo_log.arm==0);
    /* a pilot who sits still does not last: three hits and the ship is gone */
    for(unsigned i=0;i<4000 && SS_STATUS("phase=")==1;++i) { radar_target.x_mm=1000;run(50); }
    assert(SS_STATUS("phase=")!=1 && SS_STATUS("hp=")==0);
    run(600);dump(out,"starship-dying"); /* the blast fills the canopy */
    for(unsigned i=0;i<100 && SS_STATUS("phase=")!=3;++i) run(50);
    run(1000);
    assert(SS_STATUS("phase=")==3 && shows_text(lv_screen_active(),"战机损毁") && shows_hint("按键再战"));
    char card[64];snprintf(card,sizeof card,"击落 %u 最高 %u",SS_STATUS("kills="),SS_STATUS("best="));assert(shows_text(lv_screen_active(),card));
    dump(out,"starship-over");
    assert(SS_STATUS("best=")>0 && SS_STATUS("best=")>=SS_STATUS("score="));
    /* the half-second hold that enabled nothing in the game does enable the servo here, and only here */
    const unsigned best=SS_STATUS("best=");
    key(0,true);run(700);key(0,false);
    assert(SS_STATUS("phase=")==3 && servo_log.arm==1 && SS_STATUS("servo=")==2);
    tap();assert(SS_STATUS("phase=")==1 && SS_STATUS("hp=")==3 && SS_STATUS("score=")==0 && SS_STATUS("best=")==best);
    assert(shows_text(lv_screen_active(),"按住开火 舵机已启用"));run(200);dump(out,"starship-armed");
    /* every hit is a short, light shake of the servo around the centre, never past +-8 degrees */
    unsigned shakes=0;
    for(unsigned t=0;t<4000 && shakes<2 && SS_STATUS("phase=")==1;++t) {
        const unsigned hp=SS_STATUS("hp="),before=servo_log.angle;
        run(50);
        if(SS_STATUS("hp=")<hp) { run(700);assert(servo_log.angle>before+1);++shakes; }
    }
    assert(shakes>=1 && servo_log.lo>=-8 && servo_log.hi<=8 && servo_log.hi>0 && servo_log.lo<0);
    /* K2 and K3 steer when the radar sees nobody (and win while they are held) */
    for(unsigned i=0;i<4000 && SS_STATUS("phase=")!=3;++i) run(50);
    assert(SS_STATUS("phase=")==3);run(1000);tap();assert(SS_STATUS("phase=")==1);
    radar_online=false;run(300);
    key(2,true);run(1000);assert((int)SS_STATUS("cam=")>600 && SS_STATUS("src=")==2);key(2,false);
    run(1200);assert(abs((int)SS_STATUS("cam="))<300 || SS_STATUS("phase=")!=1);
    key(1,true);run(1000);assert((int)SS_STATUS("cam=")<-600 || SS_STATUS("phase=")!=1);key(1,false);
    assert(!strcmp(app_active_id(&manager.context),"starship")); /* neither is navigation */
    assert(SS_STATUS("frames=")>1000);
    radar_online=true;
    pose_starship(out);
}
/* The two ways out of a cannon: never three quick presses, and a two second hold only from the title or game over. */
static void guard_starship(const char *out)
{
    servo_state.armed=false;memset(&servo_log,0,sizeof servo_log);radar_online=false;
    assert(app_open(&manager.context,"starship"));app_manager_commit(&manager);run(300);
    k1(true);run(1300);dump(out,"starship-exit-hold");k1(false); /* the red line fills towards the exit, the gold one has served the servo */
    assert(SS_STATUS("phase=")==0 && !hold_leaves("starship",1900) && SS_STATUS("phase=")==0); /* a hold is not "start" */
    assert(hold_leaves("starship",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
    assert(app_open(&manager.context,"starship"));app_manager_commit(&manager);run(300);tap();assert(SS_STATUS("phase=")==1);
    assert(!mash_leaves("starship") && SS_STATUS("shots=")>=3);
    assert(!hold_leaves("starship",3000) && SS_STATUS("phase=")==1); /* firing for three seconds is play */
    /* a trigger held through the loss of the ship does not leave on the game over screen: that press began in play */
    k1(true);
    for(unsigned i=0;i<4000 && SS_STATUS("phase=")!=3;++i) run(50);
    assert(SS_STATUS("phase=")==3);run(3000);assert(!left("starship"));k1(false);
    assert(hold_leaves("starship",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
    radar_online=true;
}
#endif
#ifdef ESPHOOK_TEST_RACING
#define RC_STATUS(k) game_status("racing","robot racing status",k)
#define RC_INT(k) ((int)RC_STATUS(k))
static void rkey(unsigned k,bool down) { app_manager_key(&manager,k,down,manager.context.now_ms);run(20); }
static void rlean(int x,unsigned ms)
{
    for(unsigned t=0;t<ms;t+=20) { int d=x-radar_target.x_mm;if(d>40) d=40;if(d<-40) d=-40;radar_target.x_mm=(int16_t)(radar_target.x_mm+d);run(20); }
}
/* A driver on the radar: leans the opposite way to the car's drift, holds the pedal down, uses the nitro when it is full. */
static void rdrive(unsigned ms,bool frames,const char *out,bool *nitro_seen)
{
    unsigned crashes=RC_STATUS("crashes="),cps=RC_STATUS("cps="),speed_dump=0;
    for(unsigned t=0;t<ms && RC_STATUS("phase=")==2;t+=20) {
        const int px=RC_INT("px="),steer=(0-px)*256/220,lean=steer*350/256;
        rlean(1000+(lean>350?350:lean<-350?-350:lean),20);
        if(RC_STATUS("nitro=")>=1000 && !*nitro_seen) { /* double tap: down, up, down */
            rkey(0,false);run(40);rkey(0,true);*nitro_seen=true;run(200); /* lift and put the pedal down again */
            assert(RC_STATUS("nitro=")<1000);if(frames) dump(out,"racing-nitro");
        }
        if(!frames) continue;
        if(RC_STATUS("crashes=")>crashes) { crashes=RC_STATUS("crashes=");run(60);dump(out,"racing-crash"); }
        if(RC_STATUS("cps=")>cps) { cps=RC_STATUS("cps=");run(100);dump(out,"racing-checkpoint"); }
        if(RC_STATUS("speed=")>150 && abs(RC_INT("steer="))>120 && !speed_dump) { speed_dump=1;dump(out,"racing-corner"); }
    }
}
static void pose_racing(const char *out)
{
    for(unsigned i=0;i<4000 && RC_STATUS("phase=")!=4;++i) run(50);
    run(1000);
    rkey(0,true);run(100);rkey(0,false);assert(RC_STATUS("phase=")==1);
    run(3000);assert(RC_STATUS("phase=")==2);
    rc_t *r=manager.context.state;
    int32_t at=400000;while(rc_curve_at(r,at)<230) at+=1000;
    r->pos_mm=at-10000;r->speed16=170*16;r->spawn_wait=60000;r->throttle=1;r->time_ms=30000;r->next_cp_mm=at+160000;
    memset(r->car,0,sizeof r->car);
    r->car[0]=(rc_car_t){.pos_mm=r->pos_mm+22000,.px=-640,.speed=80,.alive=1,.color=0};
    r->car[1]=(rc_car_t){.pos_mm=r->pos_mm+50000,.px=0,.speed=90,.alive=1,.color=1};
    r->car[2]=(rc_car_t){.pos_mm=r->pos_mm+95000,.px=640,.speed=70,.alive=1,.color=2};
    r->car[3]=(rc_car_t){.pos_mm=r->pos_mm+170000,.px=-640,.speed=90,.alive=1,.color=3};
    run(40);dump(out,"racing-pose-bend");
    r->nitro_ms=2000;r->nitro=700;r->speed16=250*16;
    run(40);dump(out,"racing-pose-nitro");
    r->nitro_ms=0;r->car[2].pos_mm=r->pos_mm+4600;r->car[2].px=rc_px(r);r->car[2].speed=60;
    run(60);dump(out,"racing-pose-crash");
}
static void play_racing(const char *out)
{
    servo_state.armed=false;memset(&servo_log,0,sizeof servo_log);radar_online=true;radar_target.x_mm=1000;
    assert(app_open(&manager.context,"racing"));app_manager_commit(&manager);run(400);
    assert(RC_STATUS("phase=")==0 && RC_STATUS("servo=")==1 && RC_STATUS("time=")==45);
    assert(shows_text(lv_screen_active(),"极速赛车") && shows_hint("按键出发 长按启用舵机"));dump(out,"racing-title");
    /* the half-second hold enables the servo, and does not start the race */
    rkey(0,true);run(700);rkey(0,false);
    assert(RC_STATUS("phase=")==0 && servo_log.arm==1 && RC_STATUS("servo=")==2);
    rkey(0,true);run(100);rkey(0,false);
    assert(RC_STATUS("phase=")==1 && shows_text(lv_screen_active(),"准备…"));run(300);dump(out,"racing-count3");
    run(1000);dump(out,"racing-count2");
    for(unsigned i=0;i<60 && RC_STATUS("phase=")!=2;++i) run(50);
    assert(RC_STATUS("phase=")==2);run(100);dump(out,"racing-go");
    assert(shows_text(lv_screen_active(),"按住加速 舵机已启用") && RC_STATUS("speed=")==0);
    /* the key is the pedal; the radar is the wheel and the servo follows it */
    rkey(0,true);run(2500);
    assert(RC_STATUS("speed=")>=80 && RC_STATUS("dist=")>20 && RC_STATUS("phase=")==2);
    rlean(1000,200);
    rlean(1350,600);run(200);
    assert(RC_INT("steer=")>150 && RC_INT("px=")>50 && servo_log.last>6 && servo_log.last<=14 && RC_STATUS("src=")==1);dump(out,"racing-right");
    rlean(650,1000);run(300);
    assert(RC_INT("steer=")<-150 && servo_log.last<-6 && servo_log.last>=-14);
    rlean(1000,1500);
    for(unsigned i=0;i<60;++i) { rlean(1000-(RC_INT("px=")>0?200:-200),20); } /* wheel back to the middle */
    run(500);assert(abs(servo_log.last)<=14 && servo_log.hi<=14 && servo_log.lo>=-14);
    /* race on: checkpoints, a crash with a shake, the nitro, and the clock */
    bool nitro_seen=false;
    rdrive(120000,true,out,&nitro_seen);
    assert(nitro_seen);
    /* run out of road, then of time */
    rkey(0,false);
    radar_target.x_mm=1000;
    for(unsigned i=0;i<4000 && RC_STATUS("phase=")==2;++i) run(50);
    assert(RC_STATUS("phase=")==3);assert(shows_text(lv_screen_active(),"时间到!"));
    for(unsigned i=0;i<100 && RC_STATUS("phase=")!=4;++i) run(50);
    run(1100);
    assert(RC_STATUS("phase=")==4 && shows_text(lv_screen_active(),"时间到") && shows_hint("按键再来"));
    char card[64];snprintf(card,sizeof card,"里程 %u米 最高 %u",RC_STATUS("dist="),RC_STATUS("best="));assert(shows_text(lv_screen_active(),card));
    assert(RC_STATUS("best=")>0 && RC_STATUS("best=")>=RC_STATUS("score="));dump(out,"racing-over");
    rkey(0,true);run(100);rkey(0,false);
    assert(RC_STATUS("phase=")==1 && RC_STATUS("time=")==45);
    /* K2 and K3 steer when the radar sees nobody */
    radar_online=false;
    for(unsigned i=0;i<60 && RC_STATUS("phase=")!=2;++i) run(50);
    rkey(0,true);run(2500);
    rkey(2,true);run(900);assert(RC_INT("px=")>50 && RC_STATUS("src=")==2);rkey(2,false);
    const int right=RC_INT("px=");rkey(1,true);run(1400);assert(RC_INT("px=")<right-50);rkey(1,false);
    assert(!strcmp(app_active_id(&manager.context),"racing")); /* neither is navigation */
    assert(RC_STATUS("frames=")>1000);
    rkey(0,false);
    radar_online=true;
    pose_racing(out);
}
/* The same for the car: never three quick presses, a two second hold only from the title or game over. */
static void guard_racing(void)
{
    servo_state.armed=false;memset(&servo_log,0,sizeof servo_log);radar_online=false;
    assert(app_open(&manager.context,"racing"));app_manager_commit(&manager);run(300);
    assert(RC_STATUS("phase=")==0 && !hold_leaves("racing",1900) && RC_STATUS("phase=")==0);
    assert(hold_leaves("racing",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
    assert(app_open(&manager.context,"racing"));app_manager_commit(&manager);run(300);rkey(0,true);run(100);rkey(0,false);assert(RC_STATUS("phase=")==1);
    assert(!mash_leaves("racing"));
    for(unsigned i=0;i<100 && RC_STATUS("phase=")!=2;++i) run(50);
    assert(RC_STATUS("phase=")==2 && !hold_leaves("racing",3000)); /* the pedal is down for the whole race */
    rkey(0,true);
    for(unsigned i=0;i<4000 && RC_STATUS("phase=")!=4;++i) run(50);
    assert(RC_STATUS("phase=")==4);run(3000);assert(!left("racing"));rkey(0,false); /* that press began in the race */
    assert(hold_leaves("racing",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
    radar_online=true;
}
#endif
#ifdef ESPHOOK_TEST_WEDDING
/* The banner scene is a pure function of the time since entry: sample it across both banners and a popper. */
static void play_wedding(const char *out)
{
    assert(app_open(&manager.context,"wedding"));app_manager_commit(&manager);run(100);
    assert(game_status("wedding","robot wedding status","active=")==1);
    static const unsigned times[]={100,400,1000,2400,4200,4900,5600,7200,9000};
    static uint16_t previous[160*80];
    unsigned now=100,frames=game_status("wedding","robot wedding status","frames=");
    for(unsigned i=0;i<sizeof times/sizeof times[0];++i) {
        run(times[i]-now);now=times[i];
        char name[32];snprintf(name,sizeof name,"wedding-%04u",now);dump(out,name);lv_refr_now(lv_display_get_default());
        if(i) assert(changed_pixels(previous,framebuffer)>200); /* confetti, ribbons and streamers never stand still */
        memcpy(previous,framebuffer,sizeof previous);
        unsigned ink=0; /* the golden banner letters: warm, bright pixels in the middle of the canvas */
        for(unsigned y=10;y<56;++y) for(unsigned x=20;x<140;++x) { unsigned c=framebuffer[y*160+x],r=(c>>11)*255/31,g=((c>>5)&63)*255/63,b=(c&31)*255/31;ink+=r>220 && g>150 && b<150; }
        if(i==3 || i==6) assert(ink>250 && game_status("wedding","robot wedding status","page=")==(i==3?0U:1U));
    }
    assert(game_status("wedding","robot wedding status","frames=")>frames+150); /* about 25 frames a second */
    app_event_t wish={.kind=APP_EVENT_MESSAGE,.text="{\"text\":\"祝小明和小红 百年好合 永结同心\"}",.ms=manager.context.now_ms};
    app_manager_event(&manager,&wish);run(400);dump(out,"wedding-wish"); /* dump() checks every glyph of the new text exists */
    event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"launcher"));
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
    assert(app_manager_init(&manager,esphook_apps,esphook_app_count,&p,app_manager_default_standby(esphook_apps,esphook_app_count,"face"),0));run(100);
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
#ifdef ESPHOOK_TEST_MUYU
    play_muyu(out);
#endif
#ifdef ESPHOOK_TEST_WEDDING
    play_wedding(out);
#endif
#ifdef ESPHOOK_TEST_AQUARIUM
    play_aquarium(out);
#endif
#ifdef ESPHOOK_TEST_FISHING
    play_fishing(out);
#endif
#ifdef ESPHOOK_TEST_STARSHIP
    play_starship(out);
    guard_starship(out);
#endif
#ifdef ESPHOOK_TEST_RACING
    play_racing(out);
    guard_racing();
#endif
    assert(app_open(&manager.context,"settings"));app_manager_commit(&manager);run(20);
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(standby[0] && strcmp(standby,"face"));dump(out,"settings-standby");
    event(APP_EVENT_SHORT);dump(out,"settings-hardware");event(APP_EVENT_SHORT);dump(out,"settings-profile");
    event(APP_EVENT_CONFIRM);event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);
    assert(hardware_profile==APP_PROFILE_AUTO && restarts==0);
    event(APP_EVENT_CONFIRM);assert(restarts==1);
    assert(app_open(&manager.context,"launcher"));app_manager_commit(&manager);run(20);
    for(size_t i=0;i<app_count(&manager.context);++i) {
        event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"launcher"));
        if(i%3==0) { char name[64];snprintf(name,sizeof name,"launcher-page%zu",i/3+1);run(300);dump(out,name); } /* icons of every page */
    }
#ifdef ESPHOOK_TEST_DEMO
    assert(app_open(&manager.context,"demo"));app_manager_commit(&manager);event(APP_EVENT_SHORT);
    app_event_t message={.kind=APP_EVENT_MESSAGE,.text="{\"text\":\"PC 自定义标题\"}",.ms=manager.context.now_ms};app_manager_event(&manager,&message);run(20);dump(out,"demo-message");
#endif
    assert(app_open(&manager.context,"face"));app_manager_commit(&manager);
    if(manager.context.app->on_leave) manager.context.app->on_leave(&manager.context);
    destroy(manager.context.root,NULL);lv_display_delete(d);lv_deinit();free(font);
    printf("all built-in apps: real LVGL rendering, %u app transitions, 64KB UI heap (peak %u bytes at sampled frames), settings persistence and PC message passed\n",(unsigned)(30*esphook_app_count),peak_heap);
}
