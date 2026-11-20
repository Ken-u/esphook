#include "esphook/app_manager.h"
#include "esphook/app_command.h"
#include "esphook/app_runtime.h"
#include "esphook/app_sync.h"
#include "esphook/app_services.h"
#include "esphook/app_screen.h"
#include "esphook/app_ui.h"
#include "aihook_font.h"
#ifdef ESPHOOK_TEST_STOCK
#include "stock_transport.h"
#endif
#ifdef ESPHOOK_TEST_STARSHIP
#include "starship_core.h"
#endif
#ifdef ESPHOOK_TEST_RACING
#include "racing_core.h"
#endif
#ifdef ESPHOOK_TEST_FISHING
#include "fishing_core.h"
#endif
#ifdef ESPHOOK_TEST_FORTUNE
#include "fortune_core.h"
#endif
#ifdef ESPHOOK_TEST_ALARM
#include "alarm_core.h"
#endif
#ifdef ESPHOOK_TEST_MINER
#include "miner_core.h"
#endif
#ifdef ESPHOOK_TEST_JUMP
#include "jump_core.h"
#endif
#ifdef ESPHOOK_TEST_ZOMBIE
#include "zombie_core.h"
#endif
#ifdef ESPHOOK_TEST_MIRROR
#include "esphook/app_gfx.h"
#include "mirror_codec.h"
#include "mirror_testenc.h"
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static app_manager_t manager;
static robot_ui_t ui;
static char standby[24],output[512];
/* app_sync.h: a section may never nest or leak, and a lock may never be taken twice by the one test thread. */
static unsigned critical_depth,critical_total;
struct app_lock { bool held; };
static struct app_lock the_lock;
static uint16_t framebuffer[160*80],draw_buffer[160*16];
static unsigned roots;
void app_critical_enter(void) { assert(!critical_depth);++critical_depth;++critical_total; }
void app_critical_exit(void) { assert(critical_depth);--critical_depth; }
app_lock_t *app_lock_create(void) { return &the_lock; }
void app_lock_take(app_lock_t *l) { assert(l==&the_lock && !l->held);l->held=true; }
void app_lock_give(app_lock_t *l) { assert(l==&the_lock && l->held);l->held=false; }
uint64_t app_clock_ms(void) { return manager.context.now_ms; }
uint32_t app_random(void) { return 24; }
/* The wall clock is a plain variable and the settings store a few RAM slots: a test sets the time and reads what an app saved. */
static struct { bool set;uint32_t epoch;int tz;uint64_t base_ms; } fake_clock;
static void read_time(app_time_t *out,void *user) { (void)user;app_time_read(out); }
void app_time_read(app_time_t *t)
{
    memset(t,0,sizeof *t);
    if(fake_clock.set) app_time_from_epoch(fake_clock.epoch+(uint32_t)((manager.context.now_ms-fake_clock.base_ms)/1000U),fake_clock.tz,t);
}
bool app_time_set(uint32_t epoch,int tz) { fake_clock.set=true;fake_clock.epoch=epoch;fake_clock.tz=tz;fake_clock.base_ms=manager.context.now_ms;return epoch>=APP_EPOCH_MIN; }
static struct { char key[16];unsigned char data[APP_STORAGE_MAX];size_t size; } storage[16];
bool app_storage_load(const char *key,void *out,size_t size)
{
    for(unsigned i=0;i<16;++i) if(!strcmp(storage[i].key,key) && storage[i].size==size) { memcpy(out,storage[i].data,size);return true; }
    return false;
}
bool app_storage_save(const char *key,const void *data,size_t size)
{
    assert(size && size<=APP_STORAGE_MAX && strlen(key)<16);
    for(unsigned i=0;i<16;++i) if(!storage[i].key[0] || !strcmp(storage[i].key,key)) { strcpy(storage[i].key,key);memcpy(storage[i].data,data,size);storage[i].size=size;return true; }
    return false;
}
bool app_transport_write(const char *s) { snprintf(output,sizeof output,"%s",s);return true; }
/* The buzzer is a recorder too: which sounds an app asked for. */
static struct { unsigned total,by_kind[APP_BEEP_COUNT]; } beep_log;
static struct { unsigned count,hz,ms,low,high; } tone_log; /* app_beep_tone(): the notes of the buzzer test */
static bool beep_on=true,beep_full,beep_save_ok=true;
static unsigned beep_saves;
bool app_beep(app_beep_t k) { if(beep_full || (unsigned)k>=APP_BEEP_COUNT) return false;++beep_log.total;++beep_log.by_kind[k];return true; }
bool app_beep_tone(unsigned hz,unsigned ms)
{
    if(beep_full || !app_sound_tone_valid(hz,ms)) return false;
    ++tone_log.count;tone_log.hz=hz;tone_log.ms=ms;
    if(!tone_log.low || hz<tone_log.low) tone_log.low=hz;
    if(hz>tone_log.high) tone_log.high=hz;
    return true;
}
bool app_beep_enabled(void) { return beep_on; }
bool app_beep_enable(bool on) { beep_on=on;++beep_saves;return beep_save_ok; }
/* The panel tuning is a plain record: the screen test is checked by what it asks the service for. */
static struct { app_screen_t now;unsigned applies,saves,raws,forgets;uint8_t raw[APP_SCREEN_RAW_MAX];size_t raw_length;bool busy; } screen={.now={.invert=true,.x_gap=1,.y_gap=26,.mhz=20,.saved_mhz=20,.configured=true}};
void app_screen_read(app_screen_t *s) { *s=screen.now; }
bool app_screen_apply(const app_screen_t *w,bool save)
{
    if(screen.busy || !app_screen_valid(w)) return false;
    ++screen.applies;screen.now.invert=w->invert;screen.now.x_gap=w->x_gap;screen.now.y_gap=w->y_gap;
    if(save) { ++screen.saves;screen.now.saved_mhz=w->mhz;screen.now.configured=true; }
    return true;
}
bool app_screen_forget(void)
{
    if(screen.busy) return false;
    ++screen.forgets;screen.now=(app_screen_t){.invert=true,.x_gap=1,.y_gap=26,.mhz=screen.now.mhz,.saved_mhz=20};return true;
}
bool app_screen_raw(const uint8_t *b,size_t n)
{
    if(screen.busy || !b || !n || n>APP_SCREEN_RAW_MAX) return false;
    memcpy(screen.raw,b,n);screen.raw_length=n;++screen.raws;return true;
}
bool app_command_active(const char *id) { return !strcmp(app_active_id(&manager.context),id); }
bool app_command_open(const char *id) { return app_open(&manager.context,id); }
bool app_command_menu(void) { app_menu(&manager.context);return true; }
/* The radar sees one person and the servo is a plain recorder: a game can be steered and its servo requests inspected. */
static ld2450_target_t radar_target={.valid=true,.x_mm=1000,.y_mm=1500};
static bool radar_online=true;
void app_radar_read(app_radar_t *r) { memset(r,0,sizeof *r);r->available=true;r->online=radar_online;r->range_mm=ui.range_mm;r->frame.targets[0]=radar_target;r->frame.targets[0].x_mm=(int16_t)-radar_target.x_mm; }
bool app_radar_set_view_range(unsigned range_mm) { ui.range_mm = range_mm; return true; }
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
/* The calibration service is a record too: what the servo uses, what was stored, and every request. */
static app_servo_cal_t servo_cal={.config={.center_us=1500,.limit_deg=30,.speed_dps=35},.available=true,.stored=true};
static struct { unsigned applies,saves;bool busy,defer_save; } cal_log;
void app_servo_cal_read(app_servo_cal_t *c) { *c=servo_cal; }
bool app_servo_cal_apply(app_context_t *c,const robot_config_t *cfg,bool save)
{
    (void)c;
    if(cal_log.busy || !servo_cal.available || !app_servo_cal_valid(cfg)) return false;
    ++cal_log.applies;servo_cal.config=*cfg;servo_state.limit_deg=cfg->limit_deg;servo_state.speed_dps=cfg->speed_dps;
    if(save && !cal_log.defer_save) { ++cal_log.saves;servo_cal.stored=true;++servo_cal.save_seq;servo_cal.save_ok=true; }
    return true;
}
#ifdef ESPHOOK_TEST_FOLLOWTRIM
static app_follow_trim_t follow_trim;
static bool follow_save_fails;
void app_follow_trim_read(app_follow_trim_t *out) { *out = follow_trim; }
bool app_follow_trim_apply(app_context_t *ctx, int degrees, bool save)
{
    assert(!strcmp(ctx->app->id, "followtrim") && robot_follow_trim_valid(degrees));
    if (!save) {
        assert(servo_state.armed);
        follow_trim.preview = true;
        follow_trim.active_deg = degrees;
    } else {
        ++follow_trim.save_seq;
        follow_trim.save_ok = !follow_save_fails;
        if (follow_trim.save_ok) follow_trim.saved_deg = degrees;
    }
    return true;
}
static void release_followtrim(const char *id, void *user)
{
    (void)user;
    if (strcmp(id, "followtrim")) return;
    follow_trim.preview = false;
    follow_trim.active_deg = 0;
    servo_state.armed = false;
}
#endif
void app_hook_read(app_hook_t *h) { memset(h,0,sizeof *h);h->current=h->connected=true;h->clients=1;h->unread=2;strcpy(h->client,"Codex");strcpy(h->title,"任务已完成");strcpy(h->body,"长按回复继续"); }
bool app_hook_reply(unsigned key) { return key<3; }
void app_key_word(unsigned key,char *s,size_t n) { const char *words[]={"ok","continue","done"};snprintf(s,n,"%s",words[key%3]); }
static unsigned restarts;
static app_hardware_profile_t hardware_profile=APP_PROFILE_ROBOT,hardware_active=APP_PROFILE_ROBOT;
static bool hardware_save_ok=true;
app_hardware_profile_t app_hardware_active(void) { return hardware_active; }
void app_hardware_info(app_hardware_info_t *h) { memset(h,0,sizeof *h);h->active=hardware_active;h->configured=hardware_profile;h->drivers_compiled=true;h->restart_required=h->active!=h->configured;h->screen=APP_HW_INITIALIZED;h->radar=APP_HW_DETECTED;h->servo=APP_HW_INITIALIZED;h->physical_keys=1; }
bool app_hardware_set_profile(app_hardware_profile_t p) { if(!hardware_save_ok) return false;hardware_profile=p;return true; }
void app_device_restart(void) { ++restarts; }
void app_device_info(app_device_info_t *d) { memset(d,0,sizeof *d);strcpy(d->version,"0.3.0");strcpy(d->build,"2026-11-16");strcpy(d->daemon_host,"192.168.1.10");strcpy(d->ip,"192.168.1.24");d->daemon_port=18765;d->robot_hardware=true; }
void app_display_read(robot_ui_t *v) { *v=ui; }
bool app_display_update(const char *j,size_t n,char *e,size_t z) { return robot_ui_parse(j,n,&ui,e,z); }
static char host_event[16];
static unsigned host_events;
bool app_host_event(const app_context_t *c,const char *a,unsigned k,const char *t) { (void)c;(void)k;(void)t;snprintf(host_event,sizeof host_event,"%s",a);++host_events;return true; }
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
#ifdef ESPHOOK_TEST_FOLLOWTRIM
static void open_followtrim(void)
{
    assert(app_open(&manager.context, "followtrim"));
    app_manager_commit(&manager);
    run(60);
}
static void play_followtrim(const char *out)
{
    follow_trim = (app_follow_trim_t){.available = true, .aim_deg = -17.6f};
    open_followtrim();
    const unsigned arms = servo_log.arm;
    dump(out, "followtrim-setup");
    assert(shows_text(lv_screen_active(), "正前取中") && shows_text(lv_screen_active(), "0°"));
    assert(!hold_leaves("followtrim", 600));
    assert(!follow_trim.preview && servo_log.arm == arms && shows_text(lv_screen_active(), "等待雷达目标"));

    follow_trim.tracking = ROBOT_TRACKING;
    follow_trim.online = true;
    assert(!hold_leaves("followtrim", 600)); /* capture and preview, retaining the saved value */
    assert(follow_trim.preview && follow_trim.active_deg == 18 && follow_trim.saved_deg == 0 && servo_log.arm == arms + 1);
    assert(shows_text(lv_screen_active(), "左 18°") && shows_text(lv_screen_active(), "跟随预览"));
    dump(out, "followtrim-preview");
    k1(true); run(650);
    assert(shows_text(lv_screen_active(), "松手保存") && follow_trim.save_seq == 0);
    app_manager_cancel_key(&manager, 0, manager.context.now_ms); run(60);
    assert(!left("followtrim") && follow_trim.save_seq == 0);
    assert(hold_leaves("followtrim", 2100));
    assert(!follow_trim.preview && !servo_state.armed && follow_trim.save_seq == 0);

    open_followtrim();
    event(APP_EVENT_SHORT);
    assert(shows_text(lv_screen_active(), "向左调"));
    assert(!hold_leaves("followtrim", 600));
    for (unsigned i = 0; i < 3; ++i) { k1(true); run(35); k1(false); }
    assert(!left("followtrim") && follow_trim.active_deg == 3 && follow_trim.saved_deg == 0);
    follow_save_fails = true;
    hold_leaves("followtrim", 600);
    assert(!left("followtrim") && follow_trim.save_seq == 1 && follow_trim.saved_deg == 0);
    assert(shows_text(lv_screen_active(), "保存失败 再试一次"));
    follow_save_fails = false;
    hold_leaves("followtrim", 600);
    assert(!left(app_standby_id(&manager.context)) && manager.context.standby);
    assert(!follow_trim.preview && follow_trim.save_seq == 2 && follow_trim.saved_deg == 3);
}
#endif
/* The hint line of a game that switched the triple off alternates with the way out: either may be on screen. */
static bool shows_hint(const char *hint) { return shows_text(lv_screen_active(),hint) || shows_text(lv_screen_active(),"按住2秒退出"); }
static unsigned changed_pixels(const uint16_t *a,const uint16_t *b) { unsigned n=0;for(unsigned i=0;i<160*64;++i) n+=a[i]!=b[i];return n; }
#ifdef ESPHOOK_TEST_RADAR
static void check_radar(const char *out)
{
    const ld2450_target_t saved_target = radar_target;
    const robot_ui_t saved_ui = ui;
    ui = robot_ui_default();
    radar_target.x_mm = 107;
    radar_target.y_mm = 1549;
    assert(app_open(&manager.context, "radar"));
    app_manager_commit(&manager);
    run(100);
    lv_obj_t *screen = lv_screen_active();
    assert(shows_text(screen, "R1 6000 X/Y mm") && shows_text(screen, "X+107\nY1549"));
    radar_target.y_mm = 1548;
    run(100);
    assert(shows_text(screen, "X+107\nY1548"));

    k1(true);
    run(1200);
    assert(ui.range_mm == 3000 && shows_text(screen, "R1 3000 X/Y mm"));
    k1(false);
    run(100);
    assert(!left("radar") && ui.range_mm == 3000);
    assert(!hold_leaves("radar", 650));
    run(100);
    assert(ui.range_mm == 1500 && shows_text(screen, "R1 1500 X/Y mm"));
    assert(shows_text(screen, "X+107\nY1548"));
    dump(out, "radar-mm");
    assert(!hold_leaves("radar", 650));
    assert(ui.range_mm == 6000);

    radar_target.x_mm = -32767;
    radar_target.y_mm = -32767;
    run(100);
    assert(shows_text(screen, "X-32767\nY-32767"));
    dump(out, "radar-mm-edge");
    k1(true);
    run(80);
    k1(false);
    run(850);
    assert(!strcmp(app_active_id(&manager.context), "launcher"));
    radar_target = saved_target;
    ui = saved_ui;
}
#endif
#ifdef ESPHOOK_TEST_BASKETBALL
/* Shoots until it has seen a make and a miss, then lets the clock run out. Frames are for eyes only. */
static void play_basketball(const char *out)
{
    assert(app_open(&manager.context,"basketball"));app_manager_commit(&manager);run(100);
    /* shots come fast: three quick presses are shots, and only holding the key for two seconds leaves mid-game */
    assert(!mash_leaves("basketball") && game_status("basketball","robot game status","shots=")>=1);
    assert(!hold_leaves("basketball",1900) && hold_leaves("basketball",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
    const unsigned previous_angles = servo_log.angle;
    servo_state.armed = true; /* a calibrated unit enters this game with haptics enabled */
    assert(app_open(&manager.context,"basketball"));app_manager_commit(&manager);run(100);
    assert(game_status("basketball","robot game status","phase=")==1);dump(out,"basketball-idle");
    assert(servo_log.angle > previous_angles && abs(servo_log.last) <= 8);
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
    assert(servo_log.last == 0);
    servo_state.armed = false;
}
#endif
#ifdef ESPHOOK_TEST_DINO
static void menu_double(void)
{
    for (unsigned i = 0; i < 2; ++i) { k1(true); run(60); k1(false); run(60); }
    run(APP_DOUBLE_MS);
}
static void check_menu(const char *out)
{
    const app_descriptor_t *games[32];
    size_t total = 0;
    for (size_t i = 0; i < app_count(&manager.context); ++i) {
        const app_descriptor_t *app = app_at(&manager.context, i);
        if (app->menu_group == APP_MENU_GAMES) {
            assert(total < sizeof games / sizeof games[0]);
            games[total++] = app;
        }
    }
    assert(total > 4 && !left("launcher"));
    assert(shows_text(lv_screen_active(), "菜单") && shows_text(lv_screen_active(), "双击返回"));
    dump(out, "launcher-groups");
    event(APP_EVENT_SHORT); /* Daily, with its own list and back stack */
    const size_t daily = app_selection(&manager.context);
    k1(true); run(250); dump(out, "launcher-hold-half");
    assert(!left("launcher") && shows_text(lv_screen_active(), "菜单"));
    run(300); k1(false);
    assert(!left("launcher") && !shows_text(lv_screen_active(), "菜单") && app_selection(&manager.context) == 0);
    menu_double();
    assert(!left("launcher") && shows_text(lv_screen_active(), "菜单") && app_selection(&manager.context) == daily);
    menu_double(); /* At the root, back is the previous app. */
    assert(!left("settings"));
    assert(app_open(&manager.context, "launcher")); app_manager_commit(&manager); run(20);
    while (app_selection(&manager.context) != 0) event(APP_EVENT_SHORT);
    event(APP_EVENT_CONFIRM);
    assert(!left("launcher") && shows_text(lv_screen_active(), "游戏"));
    for(size_t i=0;i<total;++i) {
        const size_t sel=app_selection(&manager.context);
        char count[16];snprintf(count,sizeof count,"%u/%u",(unsigned)sel+1U,(unsigned)total);
        run(300);
        assert(shows_text(lv_screen_active(),count) && shows_text(lv_screen_active(),games[sel]->title));
        if(i==0||i==2||i==3||i==4||i==9||i==total-1) { char name[64];snprintf(name,sizeof name,"launcher-%zu",i);dump(out,name); }
        event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"launcher"));
        if(i==3) { run(60);dump(out,"launcher-scroll-mid"); } /* the list sliding up one row under the bar */
    }
    assert(app_selection(&manager.context)==0);
    event(APP_EVENT_SHORT);
    k1(true);run(250);assert(!left("launcher"));
    run(400);assert(!left(games[1]->id)); /* The selected app opens from the submenu. */
    k1(false);run(100);
    assert(app_open(&manager.context,APP_LAUNCHER));app_manager_commit(&manager);run(20);
}
#ifdef ESPHOOK_TEST_SCREENTEST
/* The test pattern and its two ways to set a new 0.96" module right: K1 on the device, text commands from the PC. */
static const char *screen_line(const char *line)
{
    output[0]=0;
    for(size_t i=0;i<esphook_app_count;++i) if(!strcmp(esphook_apps[i]->id,"screentest")) { assert(esphook_apps[i]->command && esphook_apps[i]->command(line));return output; }
    assert(!"screentest is not in the catalog");return output;
}
static void play_screentest(const char *out)
{
    assert(app_open(&manager.context,"screentest"));app_manager_commit(&manager);run(300);
    dump(out,"screentest");
    assert(!manager.context.first_run && app_triple_exit(&manager.context));
    assert(shows_text(lv_screen_active(),"1/4 INV") && shows_text(lv_screen_active(),"gap 1,26  20MHz")); /* what the firmware was written on */
    assert(shows_text(lv_screen_active(),"短按 切换 长按 确认"));
    /* A short press moves on to the next of four combinations; nothing is stored while the pattern is up. */
    event(APP_EVENT_SHORT);run(100);
    assert(!screen.now.invert && screen.now.x_gap==1 && screen.now.y_gap==26 && screen.saves==0);
    assert(shows_text(lv_screen_active(),"2/4 NORM"));
    event(APP_EVENT_SHORT);run(100);dump(out,"screentest-plain");
    assert(!screen.now.invert && screen.now.x_gap==0 && screen.now.y_gap==24 && screen.saves==0);
    assert(shows_text(lv_screen_active(),"3/4 NORM") && shows_text(lv_screen_active(),"gap 0,24  20MHz"));
    event(APP_EVENT_SHORT);assert(screen.now.invert && screen.now.y_gap==24);run(100);assert(shows_text(lv_screen_active(),"4/4 INV"));
    event(APP_EVENT_SHORT);assert(screen.now.invert && screen.now.y_gap==26);run(100);assert(shows_text(lv_screen_active(),"1/4 INV")); /* and round again */
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);assert(!screen.now.invert && screen.now.y_gap==24);
    /* A state set from the PC that is none of the four starts the round again at the first. */
    screen.now.x_gap=2;run(100);assert(shows_text(lv_screen_active(),"-/4 NORM"));
    event(APP_EVENT_SHORT);assert(screen.now.invert && screen.now.x_gap==1 && screen.now.y_gap==26);
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);assert(!screen.now.invert && screen.now.y_gap==24 && screen.saves==0);
    /* The hold confirms: stores what is on screen, once, and goes back to the menu. */
    k1(true);run(250);dump(out,"screentest-hold");assert(!left("screentest") && screen.saves==0);
    run(400);k1(false);run(30);
    assert(screen.saves==1 && screen.now.saved_mhz==20 && !screen.now.invert && screen.now.y_gap==24 && !strcmp(app_active_id(&manager.context),"launcher"));
    /* Nothing changed and the tuning is stored: holding writes nothing again. */
    assert(app_open(&manager.context,"screentest"));app_manager_commit(&manager);run(100);
    event(APP_EVENT_CONFIRM);run(30);assert(screen.saves==1 && !strcmp(app_active_id(&manager.context),"launcher"));
    /* Any other way out (the triple press, a command) puts back what was on screen on entry: only the hold stores. */
    assert(app_open(&manager.context,"screentest"));app_manager_commit(&manager);run(100);
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);assert(screen.now.invert && screen.now.y_gap==26);
    assert(mash_leaves("screentest") && screen.saves==1 && !screen.now.invert && screen.now.x_gap==0 && screen.now.y_gap==24);
    assert(app_open(&manager.context,"screentest"));app_manager_commit(&manager);run(100);
    assert(mash_leaves("screentest") && screen.saves==1 && !screen.now.invert && screen.now.y_gap==24);
    /* A busy display service keeps the hold from leaving: it can be repeated. */
    assert(app_open(&manager.context,"screentest"));app_manager_commit(&manager);run(100);
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);assert(!screen.now.invert && screen.now.y_gap==26);
    screen.busy=true;event(APP_EVENT_CONFIRM);assert(!strcmp(app_active_id(&manager.context),"screentest") && screen.saves==1);
    screen.busy=false;event(APP_EVENT_SHORT);assert(!screen.now.invert && screen.now.y_gap==24);
    event(APP_EVENT_CONFIRM);run(30);assert(screen.saves==2 && !strcmp(app_active_id(&manager.context),"launcher"));
    /* The PC side. */
    assert(strstr(screen_line("robot screentest"),"invert=0 x=0 y=24 mhz=20 saved_mhz=20 variant=plain configured=1"));
    assert(strstr(screen_line("robot screentest plugin"),"OK") && screen.now.invert && screen.now.x_gap==1 && screen.now.y_gap==26 && screen.saves==2);
    assert(strstr(screen_line("robot screentest invert 0"),"OK") && !screen.now.invert && screen.now.y_gap==26);
    assert(strstr(screen_line("robot screentest gap 2 25"),"variant=custom") && screen.now.x_gap==2 && screen.now.y_gap==25);
    assert(strstr(screen_line("robot screentest plain"),"variant=plain") && screen.saves==2);
    assert(strstr(screen_line("robot screentest save"),"OK saving") && screen.saves==3 && screen.now.saved_mhz==20);
    assert(strstr(screen_line("robot screentest mhz 10"),"saved_mhz=10") && screen.saves==4 && screen.now.saved_mhz==10 && screen.now.mhz==20);
    assert(strstr(screen_line("robot screentest save"),"saved_mhz=10") && screen.now.saved_mhz==10); /* a plain save keeps the stored clock */
    assert(strstr(screen_line("robot screentest reset"),"OK forgotten") && screen.forgets==1 && !screen.now.configured && screen.now.invert && screen.now.y_gap==26 && screen.now.saved_mhz==20);
    assert(strstr(screen_line("robot screentest"),"configured=0"));
    assert(strstr(screen_line("robot screentest save"),"OK saving") && screen.now.configured && screen.saves==6); /* and stored again */
    assert(strstr(screen_line("robot screentest plain"),"variant=plain") && screen.now.y_gap==24);
    assert(strstr(screen_line("robot screentest raw 2A 00 01 00 A0"),"OK sent") && screen.raws==1 && screen.raw_length==5 && screen.raw[0]==0x2A && screen.raw[4]==0xA0);
    assert(strstr(screen_line("robot screentest gap 99 0"),"ERR") && strstr(screen_line("robot screentest blink"),"ERR") && screen.raws==1);
    screen.busy=true; /* the display task has not taken the last request yet */
    assert(strstr(screen_line("robot screentest plugin"),"ERR busy") && strstr(screen_line("robot screentest reset"),"ERR busy") && screen.forgets==1 && strstr(screen_line("robot screentest save"),"ERR busy") && strstr(screen_line("robot screentest raw 21"),"ERR busy"));
    assert(!screen.now.invert && screen.now.y_gap==24 && screen.raws==1);
    screen.busy=false;
    for(size_t i=0;i<esphook_app_count;++i) if(!strcmp(esphook_apps[i]->id,"screentest")) assert(!esphook_apps[i]->command("robot zombie status") && !esphook_apps[i]->command("robot screen"));
    /* start shows the pattern, stop goes back to the menu. */
    assert(strstr(screen_line("robot screentest start"),"queued"));app_manager_commit(&manager);run(50);
    assert(!strcmp(app_active_id(&manager.context),"screentest"));
    assert(strstr(screen_line("robot screentest stop"),"OK"));app_manager_commit(&manager);run(50);
    assert(!strcmp(app_active_id(&manager.context),"launcher"));
    /* A board that was never set up starts here (descriptor->first_run), ahead of the standby screen. Boot is what sets
     * the flag (checked in test_app_framework.c); the scenario sets it the same way. */
    const unsigned stored=screen.saves;
    screen.now=(app_screen_t){.invert=true,.x_gap=1,.y_gap=26,.mhz=20,.saved_mhz=20};
    for(size_t i=0;i<esphook_app_count;++i) if(!strcmp(esphook_apps[i]->id,"screentest")) assert(esphook_apps[i]->first_run && esphook_apps[i]->first_run());
    manager.first_run_next=true;assert(app_open(&manager.context,"screentest"));app_manager_commit(&manager);run(100);
    assert(manager.context.first_run && !manager.idle && !app_triple_exit(&manager.context));dump(out,"screentest-first-run");
    event(APP_EVENT_SHORT);run(100);assert(!screen.now.invert && shows_text(lv_screen_active(),"2/4 NORM"));
    assert(!mash_leaves("screentest")); /* the triple press is off: the way on is the hold, or the idle timeout */
    screen.now.invert=false;screen.now.x_gap=1;screen.now.y_gap=26;
    run(28000);assert(!left("screentest") && !manager.idle);
    k1(true);run(100);k1(false);run(100); /* a key starts the 30 s again */
    run(28000);assert(!left("screentest"));
    run(3000);
    assert(!strcmp(app_active_id(&manager.context),app_standby_id(&manager.context)) && manager.idle && !manager.context.first_run);
    assert(screen.saves==stored && !screen.now.configured && screen.now.invert && screen.now.y_gap==26); /* nothing stored, the panel as it was */
    manager.first_run_next=true;assert(app_open(&manager.context,"screentest"));app_manager_commit(&manager);run(100);
    event(APP_EVENT_SHORT);run(50);assert(!screen.now.invert);
    k1(true);run(250);dump(out,"screentest-first-run-hold");assert(!left("screentest") && screen.saves==stored);
    run(400);k1(false);run(30);
    assert(screen.saves==stored+1 && screen.now.configured && !screen.now.invert && screen.now.y_gap==26 && !manager.context.first_run);
    assert(!strcmp(app_active_id(&manager.context),app_standby_id(&manager.context)) && manager.idle);
    for(size_t i=0;i<esphook_app_count;++i) if(!strcmp(esphook_apps[i]->id,"screentest")) assert(!esphook_apps[i]->first_run()); /* stored: boot goes straight on */
    /* put the panel back for whoever looks at it next */
    screen.now=(app_screen_t){.invert=true,.x_gap=1,.y_gap=26,.mhz=20,.saved_mhz=20,.configured=true};
}
#endif
#ifdef ESPHOOK_TEST_BUZZTEST
/* The buzzer test: one list on K1, every sound through the public buzzer services; the mute switch is the USB `sound on|off` one. */
static void buzz_open(void) { assert(app_open(&manager.context,"buzztest"));app_manager_commit(&manager);run(100); }
static void buzz_menu(void) { assert(app_open(&manager.context,"launcher"));app_manager_commit(&manager);run(20); }
/* Short presses until the list shows `desc` (every entry has one of its own); false when it never does. */
static bool buzz_goto(const char *desc)
{
    for(unsigned i=0;i<20;++i) { if(shows_text(lv_screen_active(),desc)) return true;event(APP_EVENT_SHORT); }
    return shows_text(lv_screen_active(),desc);
}
static void play_buzztest(const char *out)
{
    memset(&beep_log,0,sizeof beep_log);memset(&tone_log,0,sizeof tone_log);beep_on=true;beep_full=false;beep_save_ok=true;beep_saves=0;
    buzz_open();dump(out,"buzztest");
    /* It starts on the first sound; a hold plays it, a short press moves on, and nothing is stored on the way. */
    assert(shows_text(lv_screen_active(),"通知") && shows_text(lv_screen_active(),"3000Hz 120ms") && shows_text(lv_screen_active(),"声音 开"));
    assert(shows_text(lv_screen_active(),"短按 1/15 长按 播放"));
    event(APP_EVENT_SHORT);assert(shows_text(lv_screen_active(),"确认") && shows_text(lv_screen_active(),"2400Hz 220ms") && beep_log.total==0);
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);
    assert(shows_text(lv_screen_active(),"闹钟响铃") && shows_text(lv_screen_active(),"3 声 490ms"));
    event(APP_EVENT_CONFIRM);assert(beep_log.by_kind[APP_BEEP_ALARM]==1 && beep_log.total==1 && shows_text(lv_screen_active(),"播放中"));dump(out,"buzztest-alarm");
    run(600);assert(shows_text(lv_screen_active(),"短按 5/15 长按 播放"));
    assert(buzz_goto("通知") && shows_text(lv_screen_active(),"短按 1/15 长按 播放")); /* the list is a ring */
    event(APP_EVENT_CONFIRM);assert(beep_log.by_kind[APP_BEEP_NOTIFY]==1 && beep_log.total==2);
    /* A full buzzer queue says so instead of looking like it played. */
    beep_full=true;event(APP_EVENT_CONFIRM);assert(beep_log.total==2 && shows_text(lv_screen_active(),"稍等再试"));beep_full=false;
    /* The sweep: 500 to 5000 Hz in 100 Hz steps, the screen follows it, any key stops it and is spent on that. */
    assert(buzz_goto("500-5000Hz 约5秒"));
    event(APP_EVENT_CONFIRM);run(300);
    assert(tone_log.count>=2 && tone_log.low==500 && tone_log.ms==100 && shows_text(lv_screen_active(),"按任意键停止"));dump(out,"buzztest-sweep");
    run(6000);assert(tone_log.count==46 && tone_log.high==5000 && shows_text(lv_screen_active(),"扫频结束") && shows_text(lv_screen_active(),"500-5000Hz 约5秒"));
    memset(&tone_log,0,sizeof tone_log);event(APP_EVENT_CONFIRM);run(500);
    assert(tone_log.count>=3 && tone_log.count<=6);
    event(APP_EVENT_SHORT);const unsigned stopped=tone_log.count;run(800);
    assert(tone_log.count==stopped && shows_text(lv_screen_active(),"扫频已停") && shows_text(lv_screen_active(),"500-5000Hz 约5秒")); /* stopped, and still on the sweep */
    /* A queue that is full holds the sweep at its note until there is room; it does not skip or crash. */
    memset(&tone_log,0,sizeof tone_log);beep_full=true;event(APP_EVENT_CONFIRM);run(500);assert(tone_log.count==0 && shows_text(lv_screen_active(),"按任意键停止"));
    beep_full=false;run(300);assert(tone_log.count>=1 && tone_log.low==500);event(APP_EVENT_CONFIRM);
    /* The single notes, 1000 to 4500 Hz, each 400 ms. */
    assert(buzz_goto("音调 1000Hz") && shows_text(lv_screen_active(),"单音 400ms"));
    memset(&tone_log,0,sizeof tone_log);event(APP_EVENT_CONFIRM);assert(tone_log.count==1 && tone_log.hz==1000 && tone_log.ms==400 && shows_text(lv_screen_active(),"播放中"));
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(tone_log.count==2 && tone_log.hz==1500);
    for(unsigned i=0;i<6;++i) event(APP_EVENT_SHORT);
    assert(shows_text(lv_screen_active(),"音调 4500Hz"));event(APP_EVENT_CONFIRM);assert(tone_log.hz==4500 && tone_log.high==4500);dump(out,"buzztest-tone");
    beep_full=true;event(APP_EVENT_CONFIRM);assert(tone_log.count==3 && shows_text(lv_screen_active(),"稍等再试"));beep_full=false;
    /* Mute: the switch is the last entry and flips the USB `sound on|off` one; muted, nothing is asked of the buzzer. */
    assert(buzz_goto("长按 切换静音") && shows_text(lv_screen_active(),"声音 开"));
    event(APP_EVENT_CONFIRM);assert(!beep_on && beep_saves==1 && shows_text(lv_screen_active(),"已静音"));dump(out,"buzztest-muted");
    const unsigned beeps=beep_log.total,notes=tone_log.count;
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(beep_log.total==beeps && shows_text(lv_screen_active(),"已静音 先开声音"));
    assert(buzz_goto("500-5000Hz 约5秒"));event(APP_EVENT_CONFIRM);run(300);assert(tone_log.count==notes && shows_text(lv_screen_active(),"已静音 先开声音") && !shows_text(lv_screen_active(),"按任意键停止"));
    /* A device that is muted when the app opens starts on the way to turn it back on. */
    buzz_menu();buzz_open();assert(shows_text(lv_screen_active(),"长按 切换静音") && shows_text(lv_screen_active(),"声音 关"));
    const unsigned confirms=beep_log.by_kind[APP_BEEP_CONFIRM];
    beep_save_ok=false;event(APP_EVENT_CONFIRM);assert(beep_on && beep_saves==2 && shows_text(lv_screen_active(),"已开启 未保存") && beep_log.by_kind[APP_BEEP_CONFIRM]==confirms+1); /* applied now, but not stored */
    beep_save_ok=true;event(APP_EVENT_CONFIRM);assert(!beep_on);event(APP_EVENT_CONFIRM);
    assert(beep_on && beep_saves==4 && shows_text(lv_screen_active(),"声音已开启") && beep_log.by_kind[APP_BEEP_CONFIRM]==confirms+2);
    buzz_menu();
}
#endif
#ifdef ESPHOOK_TEST_SERVOCAL
/* The servo calibration: five pages on K1 alone, the explicit go-ahead, what it asks of the servo, what it stores, and the first run. */
static void servo_fresh(void)
{
    servo_state.armed=false;servo_state.angle_deg=0;servo_state.limit_deg=30;servo_state.speed_dps=35;
    servo_cal.config=(robot_config_t){.center_us=1500,.limit_deg=30,.speed_dps=35};servo_cal.available=true;servo_cal.stored=true;
    cal_log.busy=false;
}
static void servo_open(void) { assert(app_open(&manager.context,"servocal"));app_manager_commit(&manager);run(100); }
static bool has_servocal_first_run(void)
{
    for(size_t i=0;i<esphook_app_count;++i) if(!strcmp(esphook_apps[i]->id,"servocal")) return esphook_apps[i]->first_run && esphook_apps[i]->first_run();
    assert(!"servocal is not in the catalog");return false;
}
static void play_servocal(const char *out)
{
    servo_fresh();
    const unsigned arms=servo_log.arm,disarms=servo_log.disarm;
    /* Without a servo (auto/screen profile) the app only says so, and nothing asks for a first run. */
    servo_cal.available=false;servo_cal.stored=false;
    assert(!has_servocal_first_run());
    /* The robot profile is on but the servo did not start: check the wiring; nothing a press could fix. */
    servo_open();assert(shows_text(lv_screen_active(),"舵机初始化失败") && shows_text(lv_screen_active(),"检查舵机接线与5V") && shows_text(lv_screen_active(),"短按 返回"));dump(out,"servocal-none");
    event(APP_EVENT_CONFIRM);assert(!strcmp(app_active_id(&manager.context),"launcher") && servo_log.arm==arms && cal_log.applies==0 && restarts==0);
    /* The `auto` profile never starts the servo PWM: a long press offers the robot profile and restarts; a short press leaves it. */
    hardware_profile=APP_PROFILE_AUTO;hardware_active=APP_PROFILE_AUTO;
    servo_open();assert(shows_text(lv_screen_active(),"舵机未启用") && shows_text(lv_screen_active(),"长按 切到机器人配置") && shows_text(lv_screen_active(),"会重启 短按 返回"));dump(out,"servocal-enable");
    event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"launcher") && hardware_profile==APP_PROFILE_AUTO && restarts==0);
    hardware_save_ok=false;servo_open();event(APP_EVENT_CONFIRM);
    assert(!strcmp(app_active_id(&manager.context),"servocal") && hardware_profile==APP_PROFILE_AUTO && restarts==0 && shows_text(lv_screen_active(),"保存失败 再试一次") && shows_text(lv_screen_active(),"短按 返回"));
    hardware_save_ok=true;event(APP_EVENT_CONFIRM);
    assert(hardware_profile==APP_PROFILE_ROBOT && restarts==1 && servo_log.arm==arms && cal_log.applies==0); /* saved, then restarted; the servo is not touched */
    assert(app_open(&manager.context,"launcher"));app_manager_commit(&manager);run(20);
    /* Saved as robot but not restarted yet. */
    hardware_active=APP_PROFILE_AUTO;restarts=0;servo_open();
    assert(shows_text(lv_screen_active(),"舵机待重启") && shows_text(lv_screen_active(),"长按 重启生效"));
    event(APP_EVENT_CONFIRM);assert(restarts==1 && hardware_profile==APP_PROFILE_ROBOT);
    assert(app_open(&manager.context,"launcher"));app_manager_commit(&manager);run(20);
    /* The `screen` profile has no servo either and offers the same way on. */
    hardware_active=hardware_profile=APP_PROFILE_SCREEN;restarts=0;servo_open();
    assert(shows_text(lv_screen_active(),"舵机未启用") && shows_text(lv_screen_active(),"长按 切到机器人配置"));
    event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"launcher") && restarts==0 && hardware_profile==APP_PROFILE_SCREEN);
    hardware_active=hardware_profile=APP_PROFILE_ROBOT;restarts=0;
    servo_fresh();
    /* A robot whose calibration is stored does not ask at boot; the first page waits for the go-ahead and arms nothing. */
    assert(!has_servocal_first_run());
    servo_open();
    assert(!manager.context.first_run && app_triple_exit(&manager.context));
    assert(shows_text(lv_screen_active(),"舵机校准 1/5") && shows_text(lv_screen_active(),"先别装舵盘") && shows_text(lv_screen_active(),"长按 解锁并回中") && shows_text(lv_screen_active(),"短按 返回"));
    dump(out,"servocal");run(500);assert(servo_log.arm==arms && !servo_state.armed);
    event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"launcher") && cal_log.applies==0 && servo_log.arm==arms); /* a short press only goes back */
    /* The long press is the go-ahead: it arms (at the centre) and moves to the centre page. */
    servo_open();event(APP_EVENT_CONFIRM);run(60);
    assert(servo_log.arm==arms+1 && servo_state.armed && shows_text(lv_screen_active(),"舵机校准 2/5") && shows_text(lv_screen_active(),"中位 1500us"));
    assert(shows_text(lv_screen_active(),"舵机应停在正中") && shows_text(lv_screen_active(),"短按 +10 长按 下一步"));dump(out,"servocal-center");
    /* Short presses step the centre pulse by 10 us and the servo follows at once (nothing stored); 1600 wraps to 1400. */
    event(APP_EVENT_SHORT);run(60);
    assert(servo_cal.config.center_us==1510 && cal_log.applies==1 && cal_log.saves==0 && shows_text(lv_screen_active(),"中位 1510us"));
    for(unsigned i=0;i<9;++i) event(APP_EVENT_SHORT);
    run(60);assert(servo_cal.config.center_us==1600 && shows_text(lv_screen_active(),"中位 1600us"));
    event(APP_EVENT_SHORT);run(60);assert(servo_cal.config.center_us==1400 && shows_text(lv_screen_active(),"中位 1400us"));
    /* Travel: the servo sweeps the whole range, and a centre that does not leave room for it is skipped (1400 with 40 degrees would run below 1000 us). */
    servo_log.lo=servo_log.hi=0;event(APP_EVENT_CONFIRM);run(60);
    assert(shows_text(lv_screen_active(),"舵机校准 3/5") && shows_text(lv_screen_active(),"摆幅 ±30°") && shows_text(lv_screen_active(),"短按 +5 长按 下一步"));
    assert(servo_log.hi==30 && servo_log.last==30);run(500);dump(out,"servocal-limit");
    run(2600);assert(servo_log.lo==-30 && servo_log.last==-30);
    servo_log.hi=0;event(APP_EVENT_SHORT);run(60);assert(servo_cal.config.limit_deg==35 && shows_text(lv_screen_active(),"摆幅 ±35°") && servo_log.hi==35); /* the new range is swept at once */
    event(APP_EVENT_SHORT);run(60);assert(servo_cal.config.limit_deg==5 && shows_text(lv_screen_active(),"摆幅 ±5°")); /* 40 and 45 do not fit with 1400 */
    event(APP_EVENT_CONFIRM);event(APP_EVENT_SHORT);run(60); /* a hold on page 3 goes to page 4; the short press there flips the direction */
    assert(shows_text(lv_screen_active(),"舵机校准 4/5") && servo_cal.config.reverse && shows_text(lv_screen_active(),"方向 反向"));
    event(APP_EVENT_SHORT);run(60);assert(!servo_cal.config.reverse && shows_text(lv_screen_active(),"方向 正向") && shows_text(lv_screen_active(),"应先朝你的左边转") && shows_text(lv_screen_active(),"短按 反向 长按 对了"));
    servo_log.hi=servo_log.lo=0;run(2500);assert(servo_log.hi==5 && servo_log.lo==0); /* a nod toward the positive side only */
    event(APP_EVENT_CONFIRM);run(60);
    assert(shows_text(lv_screen_active(),"舵机校准 5/5") && shows_text(lv_screen_active(),"速度 35°/s") && shows_text(lv_screen_active(),"短按 换档 长按 保存"));
    event(APP_EVENT_SHORT);run(60);assert(servo_cal.config.speed_dps==50 && shows_text(lv_screen_active(),"速度 50°/s"));
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);run(60);assert(servo_cal.config.speed_dps==90);
    event(APP_EVENT_SHORT);run(60);assert(servo_cal.config.speed_dps==15);
    /* A busy motion service keeps the final hold from leaving; the next one stores, disarms and goes back to the menu. */
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);run(60);assert(servo_cal.config.speed_dps==35);
    cal_log.busy=true;event(APP_EVENT_CONFIRM);assert(!strcmp(app_active_id(&manager.context),"servocal") && cal_log.saves==0);
    cal_log.busy=false;cal_log.defer_save=true;event(APP_EVENT_CONFIRM);run(120);
    assert(!left("servocal") && shows_text(lv_screen_active(),"正在保存"));
    ++servo_cal.save_seq;servo_cal.save_ok=false;run(120);
    assert(!left("servocal") && shows_text(lv_screen_active(),"保存失败 再试一次"));
    cal_log.defer_save=false;event(APP_EVENT_CONFIRM);run(30);
    assert(cal_log.saves==1 && servo_cal.stored && servo_cal.config.center_us==1400 && servo_cal.config.limit_deg==5 && servo_cal.config.speed_dps==35 && !servo_cal.config.reverse);
    assert(!servo_state.armed && servo_log.disarm>disarms && !strcmp(app_active_id(&manager.context),"launcher"));
    /* Only that hold stores: the triple press from the menu puts back what the app started with and disarms. */
    servo_fresh();servo_cal.stored=true;const unsigned saves=cal_log.saves;
    servo_open();event(APP_EVENT_CONFIRM);run(60);event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);run(60);assert(servo_cal.config.center_us==1520 && servo_state.armed);
    assert(mash_leaves("servocal"));run(30);
    assert(servo_cal.config.center_us==1500 && cal_log.saves==saves && !servo_state.armed);
    /* Armed and left without a change: only the disarm. */
    servo_open();event(APP_EVENT_CONFIRM);run(60);assert(servo_state.armed);
    assert(app_open(&manager.context,APP_LAUNCHER));app_manager_commit(&manager);run(30);
    assert(servo_cal.config.center_us==1500 && cal_log.saves==saves);

    /* First run: a robot with no stored calibration starts here and the triple press is off. A short press on the first page
     * skips for now: the standby screen, nothing stored, asked again at the next boot. */
    servo_fresh();servo_cal.stored=false;assert(has_servocal_first_run());
    manager.first_run_next=true;servo_open();
    assert(manager.context.first_run && !manager.idle && !app_triple_exit(&manager.context));
    assert(shows_text(lv_screen_active(),"短按 先跳过"));dump(out,"servocal-first-run");
    const unsigned applied=cal_log.applies;
    event(APP_EVENT_SHORT);run(30);
    assert(cal_log.saves==saves && !servo_cal.stored && !manager.context.first_run && manager.idle && cal_log.applies==applied);
    assert(!strcmp(app_active_id(&manager.context),app_standby_id(&manager.context)) && has_servocal_first_run());
    /* Armed, the three presses do not leave (they only step the centre), and they are not undone by a leave that never comes. */
    servo_fresh();servo_cal.stored=false;
    manager.first_run_next=true;servo_open();event(APP_EVENT_CONFIRM);run(60);
    assert(!mash_leaves("servocal") && servo_state.armed && shows_text(lv_screen_active(),"舵机校准 2/5"));
    assert(app_open(&manager.context,APP_LAUNCHER));app_manager_commit(&manager);run(30);
    assert(!servo_state.armed && servo_cal.config.center_us==1500 && !servo_cal.stored && cal_log.saves==saves); /* any exit puts the servo back */
    /* No key on the first page: 30 s later it goes on to the standby screen without storing anything. */
    servo_fresh();servo_cal.stored=false;
    manager.first_run_next=true;servo_open();
    run(28000);assert(!left("servocal"));
    run(3000);assert(!strcmp(app_active_id(&manager.context),app_standby_id(&manager.context)) && manager.idle && !servo_cal.stored && cal_log.saves==saves && has_servocal_first_run());
    /* Armed and changed, then nobody touches it: 60 s, then the servo is disarmed and has its old calibration back. */
    servo_fresh();servo_cal.stored=false;
    manager.first_run_next=true;servo_open();
    event(APP_EVENT_CONFIRM);event(APP_EVENT_SHORT);run(60);assert(servo_state.armed && servo_cal.config.center_us==1510);
    run(58000);assert(!left("servocal"));
    run(3000);assert(!strcmp(app_active_id(&manager.context),app_standby_id(&manager.context)) && !servo_state.armed && servo_cal.config.center_us==1500 && !servo_cal.stored && cal_log.saves==saves);
    /* The whole first run: confirmed on the last page it stores and goes on to the standby screen. */
    servo_fresh();servo_cal.stored=false;
    manager.first_run_next=true;servo_open();
    event(APP_EVENT_CONFIRM);event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);event(APP_EVENT_CONFIRM);event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);
    assert(shows_text(lv_screen_active(),"舵机校准 5/5") && shows_text(lv_screen_active(),"方向 反向")==false);
    event(APP_EVENT_CONFIRM);run(30);
    assert(cal_log.saves==saves+1 && servo_cal.stored && servo_cal.config.center_us==1510 && servo_cal.config.reverse && !manager.context.first_run);
    assert(!strcmp(app_active_id(&manager.context),app_standby_id(&manager.context)) && manager.idle && !servo_state.armed);
#ifdef ESPHOOK_TEST_SCREENTEST
    /* Both are due on a new robot: the screen first, then the servo, then the standby screen. */
    servo_fresh();servo_cal.stored=false;screen.now.configured=false;
    manager.first_run_next=true;assert(app_open(&manager.context,"screentest"));app_manager_commit(&manager);run(100);
    event(APP_EVENT_SHORT);k1(true);run(600);k1(false);run(60);
    assert(!strcmp(app_active_id(&manager.context),"servocal") && manager.context.first_run && !manager.idle && screen.now.configured);
    event(APP_EVENT_CONFIRM);event(APP_EVENT_CONFIRM);event(APP_EVENT_CONFIRM);event(APP_EVENT_CONFIRM);event(APP_EVENT_CONFIRM);run(30);
    assert(!strcmp(app_active_id(&manager.context),app_standby_id(&manager.context)) && manager.idle && servo_cal.stored);
    screen.now=(app_screen_t){.invert=true,.x_gap=1,.y_gap=26,.mhz=20,.saved_mhz=20,.configured=true};
#endif
    servo_fresh();
}
#endif
/* The best score outlives the game (app_best.h): a game that finished a run is left and entered again, and its record is
 * still on the title screen; a game that was never finished has none. Entering the others in between makes the framework
 * read the store again instead of answering from its RAM copy, which is also what a reboot looks like. */
static void check_records(void)
{
    static const struct { const char *id,*line; bool exact; } games[]={
        {"dino","robot dino status",true},{"ski","robot ski status",true},{"starship","robot starship status",false},
        {"racing","robot racing status",false},{"miner","robot miner status",false},{"jump","robot jump status",false},
        {"zombie","robot zombie status",false}};
    unsigned before[sizeof games/sizeof games[0]]={0};
    unsigned played=0;
    for(unsigned i=0;i<sizeof games/sizeof games[0];++i) {
        if(!app_find(&manager.context,games[i].id)) continue;
        assert(app_open(&manager.context,games[i].id));app_manager_commit(&manager);run(60);
        before[i]=game_status(games[i].id,games[i].line,"best=");
        assert(before[i]>0);++played;
        assert(app_open(&manager.context,APP_LAUNCHER));app_manager_commit(&manager);run(30);
    }
    for(unsigned i=0;i<sizeof games/sizeof games[0];++i) {
        if(!app_find(&manager.context,games[i].id)) continue;
        assert(app_open(&manager.context,games[i].id));app_manager_commit(&manager);run(60);
        const unsigned again=game_status(games[i].id,games[i].line,"best=");
        assert(again>0 && again<=before[i] && (!games[i].exact || again==before[i]));
        assert(app_open(&manager.context,APP_LAUNCHER));app_manager_commit(&manager);run(30);
    }
    assert(played>0);
}
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
    servo_state.armed = false;
    memset(&servo_log, 0, sizeof servo_log);
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
    fs_t *fish = manager.context.state;
    const fs_t before_swim = *fish;
    fish->next_start_ms = UINT32_MAX;
    fish->move_wait = 1000;
    run(500); /* let the bite shake finish before comparing directions */
    fish->fx = fish->gx = FS_FISH_X_MAX * 16;
    fish->fy = (FS_FISH_Y_MIN + FS_FISH_Y_MAX) * 8;
    fish->gy = FS_FISH_Y_MIN * 16;
    run(100);
    assert(servo_log.last >= 3);
    fish->fx = fish->gx = FS_FISH_X_MIN * 16;
    fish->fy = (FS_FISH_Y_MIN + FS_FISH_Y_MAX) * 8;
    fish->gy = FS_FISH_Y_MAX * 16;
    run(100);
    assert(servo_log.last <= -3);
    *fish = before_swim;
    fish->now_ms = (uint32_t)manager.context.now_ms;
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
    assert(SS_STATUS("vox=")>300 && SS_STATUS("vox=")<3000); /* rocks, fighters and the cannon pods are stacks of slices */
    g->obj[0].z=g->obj[0].pz=190;g->obj[0].state=SE_DIVE;g->obj[0].x=60;g->obj[0].flash=3;
    g->obj[1].z=g->obj[1].pz=190;g->obj[1].x=-260;g->obj[1].r=190;g->obj[0].hp=1;
    g->bolt[0].z=300;g->bolt[0].x=-130;
    memset(g->shot,0,sizeof g->shot);
    run(40);dump(out,"starship-pose-close");
    assert(SS_STATUS("vox=")>200 && SS_STATUS("vox=")<3000);
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
    key(2,true);run(1000);assert((int)SS_STATUS("cam=")>600 && SS_STATUS("src=")==2 && servo_log.last<0);key(2,false);
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
    assert(RC_STATUS("vox=")>1000 && RC_STATUS("vox=")<3500); /* cars, trees, posts and the gantry are stacks of slices, within budget */
    r->nitro_ms=2000;r->nitro=700;r->speed16=250*16;
    run(40);dump(out,"racing-pose-nitro");
    const unsigned crashes = RC_STATUS("crashes="), angle_requests = servo_log.angle;
    r->nitro_ms=0;r->car[2].pos_mm=r->pos_mm+4600;r->car[2].px=rc_px(r);r->car[2].speed=60;
    run(60);dump(out,"racing-pose-crash");
    assert(RC_STATUS("vox=")>500 && RC_STATUS("vox=")<3500);
    assert(RC_STATUS("crashes=") > crashes);
    run(550);
    assert(servo_log.angle > angle_requests + 1 && servo_log.last == 0);
    assert(servo_log.hi > 0 && servo_log.hi <= 10 && servo_log.lo >= -10);
    /* and a bend to the left, with the road empty: the chevrons and the swing of the road */
    memset(r->car,0,sizeof r->car);r->spawn_wait=60000;
    at=r->pos_mm+300000;while(rc_curve_at(r,at)>-230) at+=1000;
    r->pos_mm=at-45000;r->speed16=120*16;r->px16=0;r->nitro_ms=0;r->time_ms=30000;r->next_cp_mm=at+600000;
    run(600);dump(out,"racing-pose-left");
    assert(RC_STATUS("vox=")>100 && RC_STATUS("vox=")<3500); /* an empty road: trees and lamp posts only */
    at=r->pos_mm+300000;while(rc_curve_at(r,at)<230) at+=1000;
    r->pos_mm=at-45000;r->speed16=120*16;r->px16=0;r->next_cp_mm=at+600000;
    run(600);dump(out,"racing-pose-right");
    /* late in the run the bends are as hard as they get */
    at=8000000;while(rc_curve_at(r,at)<290) at+=1000;
    r->pos_mm=at-45000;r->speed16=120*16;r->px16=0;r->next_cp_mm=at+600000;
    run(600);dump(out,"racing-pose-hard");
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
    /* Leaning makes both the car and the servo steer in the same direction. */
    rkey(0,true);run(2500);
    assert(RC_STATUS("speed=")>=80 && RC_STATUS("dist=")>20 && RC_STATUS("phase=")==2);
    rlean(1000,200);
    rlean(1350,600);run(200);
    assert(RC_INT("steer=")>150 && RC_INT("px=")>50 && servo_log.last<0 && RC_STATUS("src=")==1);dump(out,"racing-right");
    rlean(650,1000);run(300);
    assert(RC_INT("steer=")<-150 && servo_log.last>0);
    rlean(1000,1500);
    for(unsigned i=0;i<60;++i) { rlean(1000-(RC_INT("px=")>0?200:-200),20); } /* wheel back to the middle */
    rlean(1000, 600);assert(servo_log.last==0 && servo_log.hi<=10 && servo_log.lo>=-10);
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
#ifdef ESPHOOK_TEST_FORTUNE
/* Draws slips until each grade has appeared, looking at the tube, the shaking, the stick, the unfolding slip and the strip. */
#define FT_STATUS(k) game_status("fortune","robot fortune status",k)
static void draw_slip(void) { event(APP_EVENT_DOWN);run(1300);event(APP_EVENT_UP);run(FT_POP_MS+FT_UNFOLD_MS+80); }
static void play_fortune(const char *out)
{
    servo_state.armed = true;
    memset(&servo_log, 0, sizeof servo_log);
    assert(app_open(&manager.context,"fortune"));app_manager_commit(&manager);run(300);
    assert(FT_STATUS("phase=")==FT_IDLE && FT_STATUS("draws=")==0 && shows_text(lv_screen_active(),"今日幸运签") && shows_text(lv_screen_active(),"按键摇签 按住摇得更久"));
    dump(out,"fortune-idle");
    assert(!servo_log.angle);
    static uint16_t previous[160*80];lv_refr_now(lv_display_get_default());memcpy(previous,framebuffer,sizeof previous);run(150);lv_refr_now(lv_display_get_default());
    assert(changed_pixels(previous,framebuffer)>4); /* motes of light drift over the lattice */
    event(APP_EVENT_DOWN);run(150);assert(FT_STATUS("phase=")==FT_SHAKE && shows_text(lv_screen_active(),"摇签中..."));dump(out,"fortune-shake-a");
    lv_refr_now(lv_display_get_default());memcpy(previous,framebuffer,sizeof previous);run(110);lv_refr_now(lv_display_get_default());
    assert(changed_pixels(previous,framebuffer)>200); /* the tube really moves */
    dump(out,"fortune-shake-b");run(900);
    assert(servo_log.hi >= 4 && servo_log.hi <= 5 && servo_log.lo <= -4 && servo_log.lo >= -5);
    event(APP_EVENT_UP);run(400);assert(FT_STATUS("phase=")==FT_POP && shows_text(lv_screen_active(),"有签出来了!"));dump(out,"fortune-pop");
    assert(servo_log.last == 0);
    run(FT_POP_MS-300);run(180);assert(FT_STATUS("phase=")==FT_UNFOLD);dump(out,"fortune-unfold");
    run(FT_UNFOLD_MS);assert(FT_STATUS("phase=")==FT_SLIP && FT_STATUS("draws=")==1);
    unsigned seen_grade[FT_LEVELS]={0},seen_info=0,seen_hint=0;
    for(unsigned n=0;n<40U && !(seen_grade[0] && seen_grade[1] && seen_grade[2] && seen_info && seen_hint);++n) {
        const unsigned category=FT_STATUS("category="),level=FT_STATUS("level="),text=FT_STATUS("text="),number=FT_STATUS("number="),colour=FT_STATUS("colour=");
        char line[48];
        assert(shows_text(lv_screen_active(),ft_level_name(level)) && shows_text(lv_screen_active(),ft_text(category,level,text)));
        snprintf(line,sizeof line,"%s运",ft_category_name(category));assert(shows_text(lv_screen_active(),line));
        for(unsigned look=0;look<6U;++look) { /* the strip alternates between the lucky numbers and the way to draw again */
            snprintf(line,sizeof line,"幸运数 %u",number);
            if(shows_text(lv_screen_active(),line)) { snprintf(line,sizeof line,"幸运色 %s",ft_colour_name(colour));assert(shows_text(lv_screen_active(),line));++seen_info; }
            if(shows_text(lv_screen_active(),"按键再抽一签")) ++seen_hint;
            if(!seen_grade[level] || look==0) { char name[48];snprintf(name,sizeof name,"fortune-slip-%s-%s",level==FT_BIG?"big":level==FT_MID?"mid":"small",look?"b":"a");if(!look && !seen_grade[level]) dump(out,name); }
            run(450);
        }
        seen_grade[level]=1;
        draw_slip();
    }
    assert(seen_grade[0] && seen_grade[1] && seen_grade[2] && seen_info && seen_hint);
    assert(FT_STATUS("draws=")>=4 && FT_STATUS("frames=")>300);
    const unsigned draws=FT_STATUS("draws=");
    event(APP_EVENT_DOWN);run(60);event(APP_EVENT_UP);run(60);event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"fortune")); /* a press is a draw, never navigation */
    run(FT_SHAKE_MIN_MS+FT_POP_MS+FT_UNFOLD_MS);assert(FT_STATUS("draws=")==draws+1);
    assert(mash_leaves("fortune")); /* the global triple-click exit stays on: this is not a button-mashing game */
    assert(app_time_set(1763366400U,480)); /* with the clock set the idle strip also shows today's date */
    assert(app_open(&manager.context,"fortune"));app_manager_commit(&manager);
    bool date=false;
    for(unsigned look=0;look<8U;++look) { date|=shows_text(lv_screen_active(),"11月17日 周一");run(400); }
    assert(date);dump(out,"fortune-date");
    fake_clock.set=false;
    servo_state.armed=false;
}
#endif
#ifdef ESPHOOK_TEST_ALARM
#define AL_STATUS(k) game_status("alarm","robot alarm status",k)
/* Local Monday 2025-11-17 00:00 in UTC+8; `day` counts from it, so 5 is a Saturday and 7 the next Monday. */
static void set_clock(unsigned day,unsigned hour,unsigned minute,unsigned second) { assert(app_time_set(1763308800U+day*86400U+hour*3600U+minute*60U+second,480)); }
static bool on_alarm(void) { return !strcmp(app_active_id(&manager.context),"alarm"); }
static void alarm_blob(al_config_t *c) { assert(app_storage_load("alarm",c,sizeof *c) && al_config_valid(c)); }
static void idle_screen(void) { app_idle(&manager.context);app_manager_commit(&manager);run(60);assert(manager.idle); }
static void play_alarm(const char *out)
{
    const app_descriptor_t *self=NULL;for(size_t i=0;i<esphook_app_count;++i) if(!strcmp(esphook_apps[i]->id,"alarm")) self=esphook_apps[i];
    assert(self && (self->flags&APP_STANDBY));
    /* nothing rings, and nothing is saved, on a fresh device with no clock */
    fake_clock.set=false;memset(&servo_log,0,sizeof servo_log);servo_state.armed=false;
    assert(app_open(&manager.context,"alarm"));app_manager_commit(&manager);run(300);
    assert(AL_STATUS("valid=")==0 && AL_STATUS("alarm=")==0 && shows_text(lv_screen_active(),"未校时"));dump(out,"alarm-unset");
    run(2500);assert(shows_text(lv_screen_active(),"或运行 run.py time 校时") || shows_text(lv_screen_active(),"联网后自动校时"));
    /* the clock is set at 10:08:30 on a Monday */
    set_clock(0,10,8,30);run(300);
    assert(AL_STATUS("valid=")==1 && shows_text(lv_screen_active(),"11月17日 周一") && !shows_text(lv_screen_active(),"未校时"));dump(out,"alarm-clock");
    lv_refr_now(lv_display_get_default());static uint16_t previous[160*80];memcpy(previous,framebuffer,sizeof previous);run(1000);lv_refr_now(lv_display_get_default());
    assert(changed_pixels(previous,framebuffer)>40); /* the colon blinks and the seconds bar grows */
    /* settings: alarm on, 10:10 */
    event(APP_EVENT_SHORT);assert(AL_STATUS("view=")==1);dump(out,"alarm-menu-a");
    assert(shows_text(lv_screen_active(),"闹钟时间") && shows_text(lv_screen_active(),"关"));
    event(APP_EVENT_CONFIRM);assert(AL_STATUS("alarm=")==1);
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(AL_STATUS("view=")==2);dump(out,"alarm-edit-hour");
    for(unsigned i=0;i<3;++i) event(APP_EVENT_SHORT);
    event(APP_EVENT_CONFIRM);event(APP_EVENT_SHORT);run(200);dump(out,"alarm-edit-min");event(APP_EVENT_CONFIRM);event(APP_EVENT_CONFIRM);
    assert(AL_STATUS("view=")==1 && AL_STATUS("alarm=")==1);
    {char line[8];snprintf(line,sizeof line,"10:10");assert(shows_text(lv_screen_active(),line));}
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(shows_text(lv_screen_active(),"工作日"));        /* repeat */
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(AL_STATUS("chime=")==1);                           /* hourly chime */
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(AL_STATUS("range=")==2 && shows_text(lv_screen_active(),"9-18点")); /* 9-18 */
    run(200);dump(out,"alarm-menu-b");
    event(APP_EVENT_SHORT);assert(shows_text(lv_screen_active(),"舵机提醒"));
    servo_state.available=false;event(APP_EVENT_CONFIRM);assert(AL_STATUS("servo=")==0 && shows_text(lv_screen_active(),"没有检测到舵机"));run(100);dump(out,"alarm-noservo");
    servo_state.available=true;servo_state.armed=false;event(APP_EVENT_CONFIRM);assert(AL_STATUS("servo=")==1);
    assert(servo_log.arm==0); /* switching the reminder on never arms the servo by itself */
    {al_config_t c;alarm_blob(&c);assert(c.alarm_on && c.hour==10 && c.minute==10 && c.repeat==AL_WEEKDAYS && c.chime_on && c.range==AL_RANGE_OFFICE && c.servo_on);}
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(AL_STATUS("view=")==0); /* back to the clock */
    event(APP_EVENT_CONFIRM);assert(AL_STATUS("alarm=")==0 && shows_text(lv_screen_active(),"闹钟已关闭"));dump(out,"alarm-toast");
    event(APP_EVENT_CONFIRM);assert(AL_STATUS("alarm=")==1 && shows_text(lv_screen_active(),"闹钟已开启 10:10"));
    /* the settings survive a reboot: init() loads them again, and refuses a blob that is not ours */
    assert(self->init() && AL_STATUS("alarm=")==1 && AL_STATUS("chime=")==1 && AL_STATUS("servo=")==1);
    {unsigned ids[APP_ALARM_SLOTS];assert(app_alarm_list("alarm",ids,APP_ALARM_SLOTS)==2);} /* a second init() starts over: no entry twice */
    {al_config_t bad={.version=99};assert(app_storage_save("alarm",&bad,sizeof bad));assert(self->init() && AL_STATUS("alarm=")==0 && AL_STATUS("chime=")==0 && AL_STATUS("servo=")==0);
     al_config_t c;al_defaults(&c);c.alarm_on=1;c.hour=10;c.minute=10;c.repeat=AL_WEEKDAYS;c.chime_on=1;c.range=AL_RANGE_OFFICE;c.servo_on=1;assert(app_storage_save("alarm",&c,sizeof c));assert(self->init() && AL_STATUS("alarm=")==1);}
    /* the alarm rings over another app: the framework opens it, and the servo reminder arms and shakes */
    assert(app_open(&manager.context,"settings"));app_manager_commit(&manager);run(60);
    set_clock(0,10,9,50);run(3000);assert(!on_alarm() && AL_STATUS("fired=")==0);
    memset(&servo_log,0,sizeof servo_log);servo_state.armed=false;memset(&beep_log,0,sizeof beep_log);
    run(7500);assert(on_alarm() && AL_STATUS("view=")==3 && AL_STATUS("ring=")==1 && AL_STATUS("fired=")==1);
    run(200);dump(out,"alarm-ring-a");run(160);dump(out,"alarm-ring-b");run(2000);dump(out,"alarm-ring-c");
    assert(shows_text(lv_screen_active(),"闹钟") && shows_text(lv_screen_active(),"短按稍后提醒 长按关闭"));
    assert(servo_log.arm==1 && servo_log.angle>=3 && servo_log.hi>=8 && servo_log.lo<=-4 && servo_log.hi<=29 && servo_log.lo>=-29); /* a shake inside the limits */
    assert(beep_log.by_kind[APP_BEEP_ALARM]>=2 && beep_log.by_kind[APP_BEEP_ALARM]<=3 && beep_log.total==beep_log.by_kind[APP_BEEP_ALARM]); /* the ring beeps once every 2.2 s, nothing else */
    /* a short press snoozes it for five minutes (the framework counts them on its own uptime clock, not the wall clock) */
    event(APP_EVENT_SHORT);assert(!on_alarm() && !strcmp(app_active_id(&manager.context),app_standby_id(&manager.context)));
    const unsigned snoozed_beeps=beep_log.total;
    run(5000);assert(!on_alarm());
    run(280000);assert(!on_alarm() && beep_log.total==snoozed_beeps); /* 285 s from the press: not yet, and a snoozed alarm is silent */
    run(20000);assert(on_alarm() && AL_STATUS("view=")==3 && AL_STATUS("fired=")==2);
    /* holding the key switches this ring off for good */
    event(APP_EVENT_CONFIRM);assert(!on_alarm());
    run(30000);assert(!on_alarm() && AL_STATUS("fired=")==2);
    /* weekday repeat: Saturday is quiet, the next Monday rings, and the minute rings only once */
    set_clock(5,10,9,55);run(15000);assert(!on_alarm() && AL_STATUS("fired=")==2);
    set_clock(7,10,9,55);run(8000);assert(on_alarm() && AL_STATUS("fired=")==3);
    run(60000);assert(!on_alarm() && AL_STATUS("fired=")==3); /* unanswered for a minute: it snoozes by itself */
    run(300000);assert(on_alarm() && AL_STATUS("fired=")==4);event(APP_EVENT_CONFIRM);
    /* the hourly chime opens only over the standby screen, only in its first seconds, and only inside the hours set */
    const unsigned beeps_before_chimes=beep_log.total;
    assert(app_open(&manager.context,"settings"));app_manager_commit(&manager);run(60);
    set_clock(7,11,59,57);run(20000);assert(!on_alarm() && AL_STATUS("chimes=")==0); /* not on the standby screen: the hour passes in silence */
    idle_screen();run(1000);assert(!on_alarm() && AL_STATUS("chimes=")==0);       /* and the chime is not made up for afterwards */
    set_clock(7,13,59,58);memset(&servo_log,0,sizeof servo_log);servo_state.armed=false;run(2800);
    assert(on_alarm() && AL_STATUS("view=")==3 && AL_STATUS("ring=")==2 && AL_STATUS("chimes=")==1);dump(out,"alarm-chime-a");
    run(600);dump(out,"alarm-chime-b");
    assert(shows_text(lv_screen_active(),"整点报时") && shows_text(lv_screen_active(),"14点整 按键关闭"));
    assert(servo_log.arm==1 && servo_log.angle>=3 && servo_log.hi>=4 && servo_log.hi<=29); /* the servo nods with the strokes */
    run(2000);assert(on_alarm());run(1500);assert(!on_alarm() && manager.idle); /* two strokes: it ends by itself and goes back to standby */
    set_clock(7,17,59,59);run(2500);assert(on_alarm() && AL_STATUS("chimes=")==2);run(1500);dump(out,"alarm-chime-c");event(APP_EVENT_SHORT);assert(!on_alarm()); /* a key stops it */
    set_clock(7,18,59,59);run(2500);assert(!on_alarm() && AL_STATUS("chimes=")==2); /* 19:00 is outside 9-18 */
    set_clock(7,11,59,59);run(2500);assert(on_alarm() && AL_STATUS("chimes=")==3);run(2600);dump(out,"alarm-chime-d");event(APP_EVENT_CONFIRM);assert(!on_alarm());
    set_clock(7,13,0,30);assert(app_open(&manager.context,"alarm"));app_manager_commit(&manager);run(500);
    assert(AL_STATUS("view=")==0 && AL_STATUS("chimes=")==3); /* opening the app half a minute after the hour is silent */
    assert(mash_leaves("alarm")); /* the global triple-click exit stays on */
    assert(beep_log.total==beeps_before_chimes); /* the chimes stay silent: only a ring that waits for an answer makes noise */
    /* the same ring screen serves any app that registers an alarm with this one as the target; the servo is not moved for it */
    assert(app_open(&manager.context,"settings"));app_manager_commit(&manager);run(60);
    memset(&servo_log,0,sizeof servo_log);servo_state.armed=false;
    const unsigned fired=(unsigned)AL_STATUS("fired="),rung=beep_log.by_kind[APP_BEEP_ALARM];
    const unsigned foreign=app_alarm_add(&(app_alarm_spec_t){.owner="demo",.target="alarm",.label="提醒",.kind=APP_ALARM_AFTER,.after_s=2,.enabled=true,.tag=AL_TAG_ALARM});
    assert(foreign);run(1500);assert(!on_alarm());run(1500);
    assert(on_alarm() && AL_STATUS("view=")==3 && AL_STATUS("fired=")==fired+1U && shows_text(lv_screen_active(),"提醒") && shows_text(lv_screen_active(),"按键关闭"));
    run(300);dump(out,"alarm-foreign");assert(servo_log.arm==0 && beep_log.by_kind[APP_BEEP_ALARM]>rung); /* it rings audibly, the servo stays put */
    {app_alarm_info_t info;assert(app_alarm_get(foreign,&info) && info.state==APP_ALARM_RINGING && al_kind_of(&info)==AL_NONE);}
    event(APP_EVENT_SHORT);assert(!on_alarm() && !app_alarm_get(foreign,&(app_alarm_info_t){0})); /* a key answers it, and a one-shot is gone */
    /* an alarm that nobody answers does not block the next one, and leaving the app answers it */
    const unsigned first=app_alarm_add(&(app_alarm_spec_t){.owner="demo",.target="alarm",.label="提醒",.kind=APP_ALARM_AFTER,.after_s=1,.enabled=true});
    run(1500);assert(on_alarm() && first);
    assert(app_open(&manager.context,"settings"));app_manager_commit(&manager);run(60);assert(!app_alarm_get(first,&(app_alarm_info_t){0}));
    assert(app_alarm_cancel_owner("demo")==0 && app_alarm_add(&(app_alarm_spec_t){.owner="demo",.target="nowhere",.kind=APP_ALARM_AFTER,.after_s=1,.enabled=true}));
    run(1500);assert(!on_alarm() && app_alarm_cancel_owner("demo")==0); /* a target that does not exist is dropped without a trace */
}
#endif
#ifdef ESPHOOK_TEST_MINER
#define MN_STATUS(k) game_status("miner","robot miner status",k)
static mn_t *mine(void) { return manager.context.state; } /* the core is the first thing in the state */
/* Fires when the rope points at something worth having: a bot that aims by the numbers. */
static bool miner_aim(const mn_t *g,bool avoid_junk)
{
    int dx,dy,first=-1,first_at=1<<20;mn_dir(g,&dx,&dy);
    for(unsigned i=0;i<g->count;++i) {
        if(!g->items[i].alive) continue;
        const int vx=g->items[i].x-MN_ORIGIN_X,vy=g->items[i].y-MN_ORIGIN_Y,r=mn_info(g->items[i].kind)->radius;
        const int along=(vx*dx+vy*dy)/256,perp=abs(vx*dy-vy*dx)/256;
        if(along<=0 || perp>r-1) continue;
        if(along-r<first_at) { first_at=along-r;first=(int)i; }
    }
    if(first<0) return false;
    const unsigned kind=g->items[first].kind;
    return !(avoid_junk && (kind==MN_TNT || kind==MN_STONE_L || kind==MN_STONE_S));
}
static void miner_fire(void)
{
    for(unsigned guard=0;guard<600 && !(mine()->claw==MN_SWING && miner_aim(mine(),true));++guard) run(10);
    assert(mine()->claw==MN_SWING);event(APP_EVENT_DOWN);event(APP_EVENT_UP);
}
static void miner_pose(unsigned n,const mn_item_t *items)
{
    mn_t *g=mine();memset(g->items,0,sizeof g->items);for(unsigned i=0;i<n;++i) g->items[i]=items[i];g->count=(uint8_t)n;
}
static void play_miner(const char *out)
{
    assert(app_open(&manager.context,"miner"));app_manager_commit(&manager);run(300);
    assert(MN_STATUS("phase=")==MN_TITLE && shows_text(lv_screen_active(),"黄金矿工") && shows_hint("按键出发"));dump(out,"miner-title");
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(60);
    char text[48];snprintf(text,sizeof text,"目标 $%u",MN_STATUS("goal="));
    assert(MN_STATUS("phase=")==MN_INTRO && shows_text(lv_screen_active(),"第1关") && shows_text(lv_screen_active(),text));dump(out,"miner-intro");
    run(MN_INTRO_MS);assert(MN_STATUS("phase=")==MN_PLAY && MN_STATUS("time=")>=58);
    snprintf(text,sizeof text,"$0/%u",MN_STATUS("goal="));assert(shows_text(lv_screen_active(),text) && shows_text(lv_screen_active(),"第1关"));dump(out,"miner-play");
    static uint16_t previous[160*80];lv_refr_now(lv_display_get_default());memcpy(previous,framebuffer,sizeof previous);run(300);lv_refr_now(lv_display_get_default());
    assert(changed_pixels(previous,framebuffer)>40); /* the claw swings */
    unsigned score=0;
    for(unsigned shot=0;shot<4 && !score;++shot) { /* a bot throws until it has something */
        miner_fire();run(120);if(!shot) dump(out,"miner-extend");
        for(unsigned guard=0;guard<500 && mine()->claw!=MN_RETRACT;++guard) run(10);
        if(mine()->hooked>=0) { run(80);if(!shot || shot==1) dump(out,"miner-haul"); }
        for(unsigned guard=0;guard<2000 && mine()->claw!=MN_SWING;++guard) run(10);
        score=MN_STATUS("score=");
    }
    assert(score>0 && MN_STATUS("caught=")>=1);
    run(50);char gain[24];snprintf(gain,sizeof gain,"+%d",mine()->pop_value);assert(shows_text(lv_screen_active(),gain) || mine()->pop_kind);dump(out,"miner-gain");
    /* a boulder is too heavy: dynamite frees the claw */
    mn_t *g=mine();g->swing_ms=0;miner_pose(2,(mn_item_t[]){{80,56,MN_STONE_L,1},{30,50,MN_GOLD_M,1}});g->dynamite=2;
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);
    for(unsigned guard=0;guard<500 && mine()->hooked<0;++guard) run(10);
    assert(g->hooked==0);run(300);
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(100);
    assert(MN_STATUS("dyn=")==1 && g->boom_ms<200 && !g->items[0].alive);dump(out,"miner-blast");
    for(unsigned guard=0;guard<500 && g->claw!=MN_SWING;++guard) run(10);
    /* a barrel takes its neighbours with it */
    g->swing_ms=0;g->boom_ms=0xFFFFFFFFU;miner_pose(3,(mn_item_t[]){{80,40,MN_TNT,1},{96,43,MN_GOLD_M,1},{62,44,MN_GOLD_S,1}});
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);
    for(unsigned guard=0;guard<500 && g->boom_ms==0xFFFFFFFFU;++guard) run(10);
    run(90);dump(out,"miner-tnt");assert(!g->items[1].alive && !g->items[2].alive);
    for(unsigned guard=0;guard<500 && g->claw!=MN_SWING;++guard) run(10);
    /* the time runs out: a level that reached its goal is cleared, then the next one is announced */
    miner_pose(1,(mn_item_t[]){{140,58,MN_GOLD_S,1}});g->score=g->level_base+g->goal;g->time_ms=40;run(160);
    assert(MN_STATUS("phase=")==MN_CLEAR && shows_text(lv_screen_active(),"过关!") && shows_text(lv_screen_active(),"按键继续"));dump(out,"miner-clear");
    run(700);event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(60);
    assert(MN_STATUS("phase=")==MN_INTRO && shows_text(lv_screen_active(),"第2关") && MN_STATUS("level=")==2);dump(out,"miner-level2");
    run(MN_INTRO_MS);assert(MN_STATUS("phase=")==MN_PLAY);
    /* one that did not is over, and the best so far is kept */
    g->score=g->level_base;g->time_ms=40;run(160);
    assert((MN_STATUS("phase=")==MN_OVER && shows_text(lv_screen_active(),"时间到")) || mine()->new_best);dump(out,"miner-over");
    run(1000);event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(60);
    assert(MN_STATUS("phase=")==MN_INTRO && MN_STATUS("level=")==1 && MN_STATUS("score=")==0 && MN_STATUS("best=")>0);
    assert(MN_STATUS("frames=")>100);
    event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"miner")); /* a press fires, never navigates */
    assert(!mash_leaves("miner") && !hold_leaves("miner",1900) && hold_leaves("miner",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
}
#endif
#ifdef ESPHOOK_TEST_JUMP
#define JP_STATUS(k) game_status("jump","robot jump status",k)
static jp_t *jumper(void) { return manager.context.state; } /* the core is the first thing in the state */
/* A hold that is `extra16` sixteenths longer than the one for the middle of the next box. */
static void jump_hop(int extra16)
{
    jp_t *g=jumper();
    for(unsigned guard=0;guard<400 && g->phase!=JP_READY;++guard) run(10);
    assert(g->phase==JP_READY);
    const unsigned ms=jp_charge_for(jp_next_distance16(g)+extra16);
    event(APP_EVENT_DOWN);run(ms);event(APP_EVENT_UP);
}
static void play_jump(const char *out)
{
    assert(app_open(&manager.context,"jump"));app_manager_commit(&manager);run(300);
    assert(JP_STATUS("phase=")==JP_TITLE && shows_text(lv_screen_active(),"跳一跳") && shows_text(lv_screen_active(),"最高 0 分"));dump(out,"jump-title");
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(700);
    assert(JP_STATUS("phase=")==JP_READY && shows_text(lv_screen_active(),"按住蓄力 松开起跳") && !shows_text(lv_screen_active(),"跳一跳"));dump(out,"jump-ready");
    static uint16_t previous[160*80];lv_refr_now(lv_display_get_default());memcpy(previous,framebuffer,sizeof previous);
    /* holding squashes the pawn and fills the strength bar */
    event(APP_EVENT_DOWN);run(250);lv_refr_now(lv_display_get_default());
    assert(JP_STATUS("phase=")==JP_CHARGE && changed_pixels(previous,framebuffer)>60);dump(out,"jump-charge");
    event(APP_EVENT_UP);run(100);dump(out,"jump-hop");
    jp_t *g=jumper();
    for(unsigned guard=0;guard<200 && g->phase!=JP_READY;++guard) run(10);
    assert(g->phase==JP_READY && g->cur==0 && g->score==0 && g->pa16>0 && g->result==JP_STAYED); /* a short hold hops on the same box */
    /* a bot that aims by the numbers lands in the middle every time */
    unsigned shots=0;
    for(unsigned i=0;i<7;++i) {
        jump_hop(0);run(150);
        if(i==2) dump(out,"jump-fly");
        for(unsigned guard=0;guard<200 && g->phase==JP_FLY;++guard) run(10);
        assert(g->phase==JP_LAND && g->result==JP_PERFECT);
        run(40);
        if(i==2) { assert(shows_text(lv_screen_active(),"完美+6"));dump(out,"jump-perfect"); }
        run(JP_LAND_MS);++shots;
        if(i==0) { run(500);dump(out,"jump-settled"); }
    }
    assert(shots==7 && JP_STATUS("score=")==2+4+6+8+10+12+12 && JP_STATUS("combo=")==6 && JP_STATUS("perfects=")>=7);
    assert(shows_text(lv_screen_active(),"连击 x6") && shows_text(lv_screen_active(),"54"));dump(out,"jump-combo");
    /* a landing off the middle is only a point and ends the streak */
    jump_hop(60);run(JP_LAND_MS+JP_LAND_MS+500);
    assert(g->phase==JP_READY && g->result==JP_GOOD && JP_STATUS("combo=")==0 && JP_STATUS("score=")==55);dump(out,"jump-good");
    /* a long hold flies past the box: it falls, and the game is over */
    jump_hop(11*16);
    for(unsigned guard=0;guard<200 && g->phase!=JP_FALL;++guard) run(10);
    assert(g->phase==JP_FALL && g->result==JP_MISSED);run(250);dump(out,"jump-fall");
    for(unsigned guard=0;guard<200 && g->phase!=JP_OVER;++guard) run(10);
    run(300);
    assert(g->phase==JP_OVER && shows_text(lv_screen_active(),"新纪录!") && shows_text(lv_screen_active(),"55 分  最高 55"));dump(out,"jump-over");
    event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(100);
    assert(JP_STATUS("phase=")==JP_OVER); /* a press that lands too soon after the fall is not a restart */
    run(JP_RESTART_MS);event(APP_EVENT_DOWN);event(APP_EVENT_UP);run(700);
    assert(JP_STATUS("phase=")==JP_READY && JP_STATUS("score=")==0 && JP_STATUS("best=")==55);dump(out,"jump-again");
    assert(JP_STATUS("frames=")>150);
    event(APP_EVENT_SHORT);assert(!strcmp(app_active_id(&manager.context),"jump")); /* a short press jumps nowhere and navigates nowhere */
    assert(mash_leaves("jump") && !strcmp(app_active_id(&manager.context),"launcher")); /* the triple press is still the way out */
}
#endif
#ifdef ESPHOOK_TEST_ZOMBIE
#define ZB_STATUS(k) game_status("zombie","robot zombie status",k)
#define ZB_INT(k) ((int)ZB_STATUS(k))
static zb_t *zomb(void) { return manager.context.state; } /* the core is the first thing in the state */
static void zkey(unsigned k,bool down) { app_manager_key(&manager,k,down,manager.context.now_ms);run(20); }
static void ztap(void) { zkey(0,true);run(100);zkey(0,false); }
static void zlean(int x,unsigned ms)
{
    for(unsigned t=0;t<ms;t+=20) { int d=x-radar_target.x_mm;if(d>40) d=40;if(d<-40) d=-40;radar_target.x_mm=(int16_t)(radar_target.x_mm+d);run(20); }
}
static zb_z_t zz(unsigned kind,int x,int z)
{
    return (zb_z_t){.alive=1,.kind=(uint8_t)kind,.x=(int16_t)x,.x0=(int16_t)x,.z=(int16_t)z,.hp=zb_info(kind)->hp,.seed=(uint8_t)(x*7+z)};
}
static void zpose(unsigned n,const zb_z_t *list)
{
    zb_t *g=zomb();memset(g->z,0,sizeof g->z);for(unsigned i=0;i<n;++i) g->z[i]=list[i];
    g->to_spawn=5;g->spawn_wait=60000U;g->invuln=0;
}
static void play_zombie(const char *out)
{
    servo_state.armed=false;memset(&servo_log,0,sizeof servo_log);radar_online=false;
    assert(app_open(&manager.context,"zombie"));app_manager_commit(&manager);run(400);
    assert(ZB_STATUS("phase=")==ZB_TITLE && ZB_STATUS("servo=")==1 && ZB_STATUS("hp=")==3);
    assert(shows_text(lv_screen_active(),"僵尸围城") && shows_hint("按键开战 长按开舵机"));dump(out,"zombie-title");
    /* the gun and the title's pair of zombies are put together slice by slice: the picture grows for a second and a half */
    const unsigned building=(unsigned)ZB_STATUS("vox=");assert(building>0);run(1700);
    assert((unsigned)ZB_STATUS("vox=")>building+100 && ZB_STATUS("phase=")==ZB_TITLE);
    ztap();
    assert(ZB_STATUS("phase=")==ZB_INTRO && shows_text(lv_screen_active(),"第1波") && shows_text(lv_screen_active(),"僵尸 7 只"));dump(out,"zombie-intro");
    run(ZB_INTRO_MS+200);
    assert(ZB_STATUS("phase=")==ZB_PLAY && shows_text(lv_screen_active(),"按住开火 雷达转向") && !shows_text(lv_screen_active(),"僵尸围城"));
    /* a crowd, and the gun: the key held is a gun, and never the servo's confirmation */
    zomb()->cam_x=zomb()->target_x=0;
    zb_z_t crowd[]={zz(ZK_WALKER,-40,430),zz(ZK_RUNNER,70,700),zz(ZK_BRUTE,-300,900),zz(ZK_WALKER,260,620),zz(ZK_WALKER,10,1200),zz(ZK_RUNNER,-120,1100)};
    zpose(6,crowd);run(40);dump(out,"zombie-crowd");
    /* the whole crowd, the gravestones and the gun are a thousand or two slices: that is the cost of the picture */
    assert(ZB_STATUS("vox=")>800 && ZB_STATUS("vox=")<3500);
    zkey(0,true);run(60);dump(out,"zombie-fire");run(700);
    assert(ZB_STATUS("shots=")>=3 && ZB_STATUS("hits=")>=1 && servo_log.arm==0 && ZB_STATUS("servo=")==1);
    zkey(0,false);
    zb_z_t near3[]={zz(ZK_WALKER,-70,230),zz(ZK_BRUTE,10,300),zz(ZK_RUNNER,80,260)};
    zpose(3,near3);run(30);dump(out,"zombie-closeup");
    assert(ZB_STATUS("vox=")>800 && ZB_STATUS("vox=")<3500);
    zb_z_t dead[]={zz(ZK_WALKER,0,360),zz(ZK_WALKER,90,700)};
    zpose(2,dead);zomb()->ammo=ZB_MAG;ztap();run(40);dump(out,"zombie-kill");
    assert(ZB_STATUS("kills=")>=1 && ZB_STATUS("score=")>=100);
    /* the ones off to the side light the edge of the screen */
    zb_z_t aside[]={zz(ZK_WALKER,900,500),zz(ZK_RUNNER,-850,800)};
    zpose(2,aside);run(40);dump(out,"zombie-edge");
    /* a bite: one life, and a red screen */
    zb_z_t biter[]={zz(ZK_WALKER,0,ZB_ZN+4)};
    zpose(1,biter);run(120);
    assert(ZB_STATUS("hp=")==2);dump(out,"zombie-bite");
    assert(servo_log.angle==0 && servo_log.arm==0); /* the servo is off unless it was asked for */
    /* a pilot who stands still is eaten: the last life ends the game */
    zomb()->hp=1;zomb()->score=700;zb_z_t last[]={zz(ZK_WALKER,0,ZB_ZN+4)};zpose(1,last);zomb()->invuln=0;run(100);
    assert(ZB_STATUS("phase=")==ZB_DYING);run(500);dump(out,"zombie-dying");
    run(1900);
    char card[64];snprintf(card,sizeof card,"第%u波 击杀%u 最高%u",ZB_STATUS("wave="),ZB_STATUS("kills="),ZB_STATUS("best="));
    assert(ZB_STATUS("phase=")==ZB_OVER && ZB_STATUS("best=")==700);
    assert(shows_text(lv_screen_active(),"新纪录!"));
    assert(shows_text(lv_screen_active(),card));
    assert(shows_hint("按键再战"));
    dump(out,"zombie-over");
    /* the half-second hold that is only a gun in the fight enables the servo here */
    zkey(0,true);run(700);zkey(0,false);
    assert(ZB_STATUS("phase=")==ZB_OVER && servo_log.arm==1 && ZB_STATUS("servo=")==2);
    ztap();
    assert(ZB_STATUS("phase=")==ZB_INTRO && ZB_STATUS("hp=")==3 && ZB_STATUS("score=")==0 && ZB_STATUS("best=")==700);
    run(ZB_INTRO_MS+200);assert(shows_text(lv_screen_active(),"按住开火 舵机已启用"));dump(out,"zombie-armed");
    /* a bite is a short, light shake of the servo around the centre, never past +-8 degrees */
    zomb()->hp=3;zb_z_t shake[]={zz(ZK_WALKER,0,ZB_ZN+4)};zpose(1,shake);
    const unsigned before=servo_log.angle;
    run(100);assert(ZB_STATUS("hp=")==2);run(700);
    assert(servo_log.angle>before+1 && servo_log.lo>=-8 && servo_log.hi<=8 && servo_log.hi>0 && servo_log.lo<0);
    /* K2 and K3 turn the view when the radar sees nobody; the radar turns it when it does */
    zb_z_t quiet[]={zz(ZK_WALKER,900,1400)};zpose(1,quiet);
    zkey(2,true);run(1000);assert(ZB_INT("cam=")>600 && ZB_STATUS("src=")==2 && servo_log.last<0);zkey(2,false);
    run(1200);assert(abs(ZB_INT("cam="))<300);
    zkey(1,true);run(1000);assert(ZB_INT("cam=")<-600);zkey(1,false);
    assert(!strcmp(app_active_id(&manager.context),"zombie")); /* neither is navigation */
    radar_online=true;radar_target.x_mm=1000;run(400);
    zlean(1350,900);assert(ZB_INT("cam=")>800 && ZB_INT("steer=")>200 && ZB_STATUS("src=")==1);dump(out,"zombie-right");
    zlean(650,1400);assert(ZB_INT("cam=")<-800 && ZB_INT("steer=")<-200);dump(out,"zombie-left");
    zlean(1000,1500);assert(abs(ZB_INT("cam="))<150);
    assert(ZB_STATUS("frames=")>400);
    radar_online=false;
}
/* The two ways out of a gun: never three quick presses, and a two second hold only from the title or game over. */
static void guard_zombie(const char *out)
{
    servo_state.armed=false;memset(&servo_log,0,sizeof servo_log);radar_online=false;
    assert(app_open(&manager.context,"zombie"));app_manager_commit(&manager);run(300);
    k1(true);run(1300);dump(out,"zombie-exit-hold");k1(false);
    assert(ZB_STATUS("phase=")==ZB_TITLE && !hold_leaves("zombie",1900) && ZB_STATUS("phase=")==ZB_TITLE); /* a hold is not "start" */
    assert(hold_leaves("zombie",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
    assert(app_open(&manager.context,"zombie"));app_manager_commit(&manager);run(300);ztap();run(ZB_INTRO_MS+200);assert(ZB_STATUS("phase=")==ZB_PLAY);
    assert(!mash_leaves("zombie") && ZB_STATUS("shots=")>=1);
    assert(!hold_leaves("zombie",3000) && ZB_STATUS("phase=")==ZB_PLAY); /* firing for three seconds is play */
    /* a trigger held through the loss of the last life does not leave on the game over screen: that press began in play */
    zomb()->hp=1;zomb()->invuln=0;zb_z_t last[]={zz(ZK_WALKER,0,ZB_ZN+4)};zpose(1,last);
    k1(true);
    for(unsigned i=0;i<400 && ZB_STATUS("phase=")!=ZB_OVER;++i) run(50);
    assert(ZB_STATUS("phase=")==ZB_OVER);run(3000);assert(!left("zombie"));k1(false);
    assert(hold_leaves("zombie",2100) && !strcmp(app_active_id(&manager.context),"launcher"));
}
#endif
#ifdef ESPHOOK_TEST_MIRROR
#define MR_STATUS(k) game_status("mirror","robot mirror status",k)
/* A line sent the way the USB task hands one over; the reply is what the app wrote back. */
static const char *mirror_send(const char *line)
{
    for(size_t i=0;i<esphook_app_count;++i) if(!strcmp(esphook_apps[i]->id,"mirror")) { output[0]=0;assert(esphook_apps[i]->command(line));return output; }
    assert(!"the mirror app is not built");return NULL;
}
/* What a PC window could look like: bars of a chart whose heights follow the frame number, on a dark grid. */
static void mirror_chart(uint8_t payload[MIRROR_PAYLOAD],unsigned frame,unsigned strip)
{
    static const uint32_t colours[16]={0x0A1020,0x1C2C4C,0x2F7FD6,0xFDB927,0x2E8B57,0xE0503C,0x9B59B6,0x1ABC9C,0xF39C12,0xFFFFFF,0,0,0,0,0,0};
    memset(payload,0,MIRROR_PAYLOAD);
    for(unsigned i=0;i<MIRROR_COLOURS;++i) { const uint16_t c=gfx_rgb565(colours[i]);payload[i*2U]=(uint8_t)c;payload[i*2U+1U]=(uint8_t)(c>>8); }
    uint8_t *nibbles=payload+MIRROR_COLOURS*2U;
    for(unsigned row=0;row<MIRROR_ROWS;++row)
        for(unsigned x=0;x<MIRROR_W;++x) {
            const unsigned y=strip*MIRROR_ROWS+row,bar=x/10U,height=8U+(bar*37U+frame*11U)%48U;
            unsigned c=0;
            if(y%16U==0U) c=1;
            if(x%10U>=2U && x%10U<9U && y>=MIRROR_H-height) c=2U+bar%8U;
            const unsigned at=row*MIRROR_W+x;
            nibbles[at/2U]=(uint8_t)(at%2U?nibbles[at/2U]|c:c<<4);
        }
}
static const char *mirror_frame(unsigned frame,unsigned from,unsigned to)
{
    static char reply[512];
    reply[0]=0;
    for(unsigned strip=from;strip<to;++strip) {
        uint8_t payload[MIRROR_PAYLOAD];char line[1100];
        mirror_chart(payload,frame,strip);mirror_test_line(line,sizeof line,frame,strip,payload);
        snprintf(reply,sizeof reply,"%s",mirror_send(line));
    }
    return reply;
}
/* The colour the chart has at a screen pixel, as the panel gets it. */
static uint16_t mirror_expect(unsigned frame,unsigned x,unsigned y)
{
    uint8_t payload[MIRROR_PAYLOAD];mirror_chart(payload,frame,y/MIRROR_ROWS);
    const unsigned at=(y%MIRROR_ROWS)*MIRROR_W+x,nibble=at%2U?payload[MIRROR_COLOURS*2U+at/2U]&15U:payload[MIRROR_COLOURS*2U+at/2U]>>4;
    return (uint16_t)(payload[nibble*2U]|payload[nibble*2U+1U]<<8);
}
static bool mirror_canvas_is(unsigned frame,unsigned strips)
{
    lv_refr_now(lv_display_get_default());
    for(unsigned y=0;y<strips*MIRROR_ROWS;++y) for(unsigned x=0;x<MIRROR_W;++x) if(framebuffer[y*160+x]!=mirror_expect(frame,x,y)) return false;
    return true;
}
static bool shows_within(const char *a,const char *b,unsigned ms)
{
    bool seen=false;
    for(unsigned t=0;t<ms && !seen;t+=100) { run(100);seen=shows_text(lv_screen_active(),a) || (b && shows_text(lv_screen_active(),b)); }
    return seen;
}
static void play_mirror(const char *out)
{
    /* Not on screen: the PC is told so, and nothing is queued. */
    assert(strstr(mirror_send("mirror hello"),"\"ok\":false") && strstr(output,"\"err\":\"inactive\""));
    assert(strstr(mirror_frame(1,0,1),"inactive"));
    assert(app_open(&manager.context,"mirror"));app_manager_commit(&manager);run(400);
    assert(!strcmp(host_event,"enter") && MR_STATUS("active=")==1 && MR_STATUS("live=")==0);
    assert(shows_text(lv_screen_active(),"电脑副屏") && (shows_text(lv_screen_active(),"等待电脑画面") || shows_text(lv_screen_active(),"电脑运行 run.py mirror")));
    static uint16_t before[160*80];
    dump(out,"mirror-wait");memcpy(before,framebuffer,sizeof before);run(500);lv_refr_now(lv_display_get_default());
    assert(changed_pixels(before,framebuffer)>40); /* the waiting picture is alive */
    assert(strstr(mirror_send("mirror hello"),"\"ok\":true") && strstr(output,"\"w\":160") && strstr(output,"\"h\":64") && strstr(output,"\"rows\":8") && strstr(output,"\"free\":8"));
    assert(strstr(mirror_send("mirror nonsense"),"\"err\":\"args\"") && strstr(mirror_send("mirror s"),"\"ok\":false"));
    for(size_t i=0;i<esphook_app_count;++i) if(!strcmp(esphook_apps[i]->id,"mirror")) assert(!esphook_apps[i]->command("robot zombie status") && !esphook_apps[i]->command("mirrors"));
    /* A whole picture is eight strips and exactly fills the ring; a ninth waits until the screen has painted. */
    const char *r=mirror_frame(1,0,8);
    assert(strstr(r,"\"ok\":true") && strstr(r,"\"f\":1,\"i\":7") && strstr(r,"\"free\":0"));
    assert(strstr(mirror_frame(1,0,1),"\"err\":\"busy\"") && MR_STATUS("busy=")==1);
    run(60);
    assert(MR_STATUS("live=")==1 && MR_STATUS("strips=")==8 && MR_STATUS("frames=")==1 && mirror_canvas_is(1,8));
    assert(!shows_text(lv_screen_active(),"电脑副屏")); /* the picture is the whole screen now */
    assert(shows_text(lv_screen_active(),"画面 0 帧/秒") || shows_text(lv_screen_active(),"单击暂停 三击退出"));
    dump(out,"mirror-live");
    /* One strip changes the rows of that strip and nothing else. */
    assert(strstr(mirror_frame(2,3,4),"\"ok\":true"));run(60);lv_refr_now(lv_display_get_default());
    for(unsigned y=0;y<64;++y) for(unsigned x=0;x<160;++x) assert(framebuffer[y*160+x]==mirror_expect(y/8==3?2U:1U,x,y));
    /* Damaged or malformed lines are refused and counted, and the picture stays. */
    uint8_t payload[MIRROR_PAYLOAD];char line[1100];
    mirror_chart(payload,9,4);mirror_test_line(line,sizeof line,9,4,payload);
    line[200]=(char)(line[200]=='A'?'B':'A');
    assert(strstr(mirror_send(line),"\"err\":\"crc\"") && strstr(output,"\"f\":9,\"i\":4") && MR_STATUS("crc=")==1);
    mirror_test_line(line,sizeof line,9,4,payload);line[100]='*';
    assert(strstr(mirror_send(line),"\"err\":\"b64\"") && MR_STATUS("b64=")==1);
    assert(strstr(mirror_send("mirror s 1 9 0000 AAAA"),"\"err\":\"args\"") && MR_STATUS("args=")>=1);
    run(60);assert(MR_STATUS("strips=")==9 && mirror_canvas_is(1,3)); /* nothing of the damaged strips was painted */
    /* A real stream: twelve pictures a second, and the band says how fast it is going. */
    unsigned frame=3;
    for(unsigned t=0;t<2600;t+=83,++frame) { assert(strstr(mirror_frame(frame,0,8),"\"ok\":true"));run(83); }
    const unsigned fps=MR_STATUS("fps=");
    assert(fps>=9 && fps<=14);
    assert(mirror_canvas_is(frame-1,8));
    char rate[40];snprintf(rate,sizeof rate,"画面 %u 帧/秒",fps);
    assert(shows_within(rate,"单击暂停 三击退出",2600));
    dump(out,"mirror-stream");
    /* K1 freezes the picture and tells the PC; strips sent meanwhile are thrown away. */
    k1(true);run(100);k1(false);run(60);
    assert(MR_STATUS("paused=")==1 && !strcmp(host_event,"pause"));
    assert(shows_within("已暂停","再按一下继续",2600));dump(out,"mirror-paused");
    const unsigned frozen=frame-1;
    assert(strstr(mirror_frame(200,0,8),"\"p\":1") && strstr(output,"\"ok\":true"));run(80);
    assert(mirror_canvas_is(frozen,8) && MR_STATUS("dropped=")>=8);
    k1(true);run(100);k1(false);run(60);
    assert(MR_STATUS("paused=")==0 && !strcmp(host_event,"resume"));
    r=mirror_frame(201,0,8);assert(strstr(r,"\"p\":0") && strstr(r,"\"ok\":true"));run(80);assert(mirror_canvas_is(201,8));
    /* Nothing arrives for a while: the band says so, and the last picture stays. */
    assert(shows_within("画面中断 等待电脑",NULL,6000) && MR_STATUS("age=")>3000);
    assert(mirror_canvas_is(201,8));dump(out,"mirror-stale");
    /* The PC says goodbye. */
    assert(strstr(mirror_send("mirror bye"),"\"ok\":true"));run(100);
    assert(shows_text(lv_screen_active(),"电脑已断开") && mirror_canvas_is(201,8));dump(out,"mirror-bye");
    assert(strstr(mirror_frame(202,0,1),"\"ok\":true"));run(80);assert(!shows_text(lv_screen_active(),"电脑已断开"));
    /* Three quick presses leave, and the app is off the air. */
    assert(mash_leaves("mirror") && !strcmp(host_event,"leave"));
    assert(MR_STATUS("active=")==0);
    assert(strstr(mirror_send("mirror hello"),"\"err\":\"inactive\"") && strstr(mirror_frame(1,0,1),"inactive"));
    /* Entering again starts from a clean screen: no strip of the last session is left in the ring. */
    assert(app_open(&manager.context,"mirror"));app_manager_commit(&manager);run(200);
    assert(MR_STATUS("live=")==0 && MR_STATUS("strips=")==0 && strstr(mirror_send("mirror hello"),"\"free\":8"));
    assert(shows_text(lv_screen_active(),"电脑副屏"));
    event(APP_EVENT_SHORT);run(20);k1(true);run(60);k1(false);run(60);
    assert(!strcmp(app_active_id(&manager.context),"mirror"));
    assert(mash_leaves("mirror"));
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
/* A fingerprint of everything the two Chinese fonts answer for every code point: whether the glyph exists, which glyph
 * record it maps to and that record's metrics. It pins the fontdata image and its loader to the tables that used to be
 * compiled into the firmware. */
static uint64_t fnv(uint64_t h,uint32_t v) { for(int i=0;i<4;++i) { h^=(v>>(8*i))&255u;h*=1099511628211ULL; } return h; }
static uint64_t font_fingerprint(const lv_font_t *font)
{
    uint64_t h=1469598103934665603ULL;
    for(uint32_t cp=0;cp<0x20000;++cp) {
        lv_font_glyph_dsc_t g;memset(&g,0,sizeof g);
        bool ok=lv_font_get_glyph_dsc(font,&g,cp,0);
        h=fnv(h,ok);
        if(ok) { h=fnv(h,g.adv_w);h=fnv(h,g.box_w);h=fnv(h,g.box_h);h=fnv(h,(uint32_t)g.ofs_x);h=fnv(h,(uint32_t)g.ofs_y);h=fnv(h,g.format);h=fnv(h,g.gid.index); }
    }
    return h;
}
/* The real image through the real loader, then damaged copies of it. Every copy lives in an exactly sized heap block so
 * AddressSanitizer notices a single byte read past its end. A damaged image has to be rejected, or, when the damage
 * left it self-consistent, still be safe to query for every code point. */
static void check_font_image(uint8_t *image,size_t size)
{
    assert(aihook_font_load(image,size) && aihook_font_ready());
    unsigned long long f12=font_fingerprint(&aihook_font_12),f16=font_fingerprint(&aihook_font_16);
    if(getenv("ESPHOOK_FONT_REPORT")) fprintf(stderr,"font fingerprints 12px=0x%016llx 16px=0x%016llx\n",f12,f16);
    assert(f12==0x6096b5fe5723ebc5ULL && f16==0x36ef25aa9392b743ULL); /* the tables that used to be compiled into the firmware */
    lv_font_glyph_dsc_t glyph;memset(&glyph,0,sizeof glyph);
    assert(lv_font_get_glyph_dsc(&aihook_font_16,&glyph,0x65B0 /* 新 */,0) && glyph.box_w>8);

    size_t cuts[]={0,3,4,16,79,80,81,200,size/3,size/2,size-1};
    for(size_t i=0;i<sizeof cuts/sizeof cuts[0];++i) {
        uint8_t *copy=malloc(cuts[i]?cuts[i]:1);assert(copy);memcpy(copy,image,cuts[i]);
        assert(!aihook_font_load(copy,cuts[i]) && !aihook_font_ready());
        free(copy);
    }
    assert(!aihook_font_load(NULL,size) && !aihook_font_load(image+1,size-1));
    uint32_t version=2,bad=3;
    uint8_t *copy=malloc(size);assert(copy);
    memcpy(copy,image,size);memcpy(copy+4,&bad,4);assert(!aihook_font_load(copy,size));memcpy(copy+4,&version,4);
    unsigned accepted=0,rejected=0,swept=0;
    uint32_t cmap_at[2],cmap_len[2];
    for(unsigned f=0;f<2;++f) { memcpy(&cmap_at[f],image+16+32*f+16,4);memcpy(&cmap_len[f],image+16+32*f+20,4);cmap_len[f]*=28; }
    srand(12345);
    for(unsigned round=0;round<1500;++round) {
        memcpy(copy,image,size);
        unsigned flips=1+(unsigned)rand()%3;
        for(unsigned k=0;k<flips;++k) {
            size_t at;
            switch(round%3) { /* the header, the character-map records, anywhere */
            case 0: at=(size_t)rand()%80;break;
            case 1: { unsigned f=(unsigned)rand()%2;at=cmap_at[f]+(size_t)rand()%cmap_len[f];break; }
            default: at=(size_t)rand()%size;break;
            }
            copy[at]^=(uint8_t)(1u<<(rand()%8));
        }
        if(aihook_font_load(copy,size)) {
            ++accepted;
            if(swept<40) { ++swept;for(uint32_t cp=0;cp<0x10000;++cp) { lv_font_glyph_dsc_t g;memset(&g,0,sizeof g);(void)lv_font_get_glyph_dsc(&aihook_font_12,&g,cp,0);(void)lv_font_get_glyph_dsc(&aihook_font_16,&g,cp,0); } }
        } else ++rejected;
    }
    free(copy);
    assert(rejected>400 && swept>0); /* the checks must actually reject things, not wave everything through */
    assert(aihook_font_load(image,size) && aihook_font_ready());
    printf("fontdata: %zu bytes, fingerprints match, %u damaged images rejected, %u harmless ones safe to query\n",size,rejected,accepted);
}
int main(int argc,char **argv)
{
    FILE *file=fopen(ESPHOOK_FONT_FILE,"rb");assert(file);fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    uint8_t *font=malloc((size_t)size);assert(font && fread(font,1,(size_t)size,file)==(size_t)size);fclose(file);
    lv_init();
    check_font_image(font,(size_t)size);lv_display_t *d=lv_display_create(160,80);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,draw_buffer,NULL,sizeof draw_buffer,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    for(size_t i=0;i<esphook_app_count;++i) if(esphook_apps[i]->init) assert(esphook_apps[i]->init());
    app_platform_t p={.scene_create=scene,.scene_delete=destroy,.save_standby=save,.time_read=read_time};ui=robot_ui_default();
#ifdef ESPHOOK_TEST_FOLLOWTRIM
    p.release_services = release_followtrim;
#endif
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
#ifdef ESPHOOK_TEST_RADAR
    check_radar(out);
#endif
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
#ifdef ESPHOOK_TEST_FORTUNE
    play_fortune(out);
#endif
#ifdef ESPHOOK_TEST_ALARM
    play_alarm(out);
#endif
#ifdef ESPHOOK_TEST_MINER
    play_miner(out);
#endif
#ifdef ESPHOOK_TEST_JUMP
    play_jump(out);
#endif
#ifdef ESPHOOK_TEST_ZOMBIE
    play_zombie(out);
    guard_zombie(out);
#endif
#ifdef ESPHOOK_TEST_MIRROR
    play_mirror(out);
#endif
    check_records();
#ifdef ESPHOOK_TEST_SCREENTEST
    play_screentest(out);
#endif
#ifdef ESPHOOK_TEST_BUZZTEST
    play_buzztest(out);
#endif
#ifdef ESPHOOK_TEST_SERVOCAL
    play_servocal(out);
#endif
#ifdef ESPHOOK_TEST_FOLLOWTRIM
    play_followtrim(out);
#endif
    assert(app_open(&manager.context,"settings"));app_manager_commit(&manager);run(20);
    event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);
    event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);assert(standby[0] && strcmp(standby,"face"));dump(out,"settings-standby");
    event(APP_EVENT_SHORT);dump(out,"settings-hardware");event(APP_EVENT_SHORT);dump(out,"settings-profile");
    event(APP_EVENT_CONFIRM);event(APP_EVENT_SHORT);event(APP_EVENT_CONFIRM);
    assert(hardware_profile==APP_PROFILE_AUTO && restarts==0);
    event(APP_EVENT_CONFIRM);assert(restarts==1);
    assert(app_open(&manager.context,"launcher"));app_manager_commit(&manager);run(20);
    check_menu(out);
#ifdef ESPHOOK_TEST_DEMO
    assert(app_open(&manager.context,"demo"));app_manager_commit(&manager);event(APP_EVENT_SHORT);
    app_event_t message={.kind=APP_EVENT_MESSAGE,.text="{\"text\":\"PC 自定义标题\"}",.ms=manager.context.now_ms};app_manager_event(&manager,&message);run(20);dump(out,"demo-message");
#endif
    assert(app_open(&manager.context,"face"));app_manager_commit(&manager);
    if(manager.context.app->on_leave) manager.context.app->on_leave(&manager.context);
    assert(!critical_depth && critical_total>100 && !the_lock.held); /* every app published through the shared sections, none leaked one */
    lv_mem_monitor_t pool;lv_mem_monitor(&pool);
    assert(pool.max_used<pool.total_size*90/100); /* the true high-water mark of the whole run, transitions and effects included */
    destroy(manager.context.root,NULL);lv_display_delete(d);lv_deinit();free(font);
    printf("all built-in apps: real LVGL rendering, %u app transitions, 48KB UI heap (peak %u bytes at sampled frames, high-water %u of %u), settings persistence and PC message passed\n",(unsigned)(30*esphook_app_count),peak_heap,(unsigned)pool.max_used,(unsigned)pool.total_size);
}
