#include "app_manager.h"
#include "app_protocol.h"
#include "app_alarm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned enters,leaves,scenes,releases,renders,shorts,confirms,ups;
static bool save_ok=true;
static char saved[24];
static void *scene(void *u) { (void)u; assert(!scenes); ++scenes; return &scenes; }
static void destroy(void *root,void *u) { (void)u; assert(root==&scenes && scenes==1); --scenes; }
static void *scene2(void *u) { (void)u; return &saved; }
static void destroy2(void *root,void *u) { (void)u; assert(root==&saved); }
static void release(const char *id,void *u) { (void)id;(void)u;++releases; }
static bool save(const char *id,void *u) { (void)u;if(save_ok) strcpy(saved,id);return save_ok; }
static void enter(app_context_t *c) { assert(*(unsigned *)c->state==0);*(unsigned *)c->state=24;++enters; }
static void leave(app_context_t *c) { assert(*(unsigned *)c->state==24);++leaves; }
static void render(app_context_t *c) { (void)c;++renders; }
static void event(app_context_t *c,const app_event_t *e)
{
    if(e->kind==APP_EVENT_SHORT) { ++shorts;if(!strcmp(c->app->id,"launcher")) app_select_next(c); }
    if(e->kind==APP_EVENT_CONFIRM) { ++confirms;if(!strcmp(c->app->id,"launcher")) assert(app_open(c,app_at(c,app_selection(c))->id)); }
    if(e->kind==APP_EVENT_UP) ++ups;
    if(e->kind==APP_EVENT_MESSAGE) { assert(!strcmp(e->text,"{\"text\":\"hi\"}"));app_menu(c); }
}
#define DESC(ID,FLAGS) {.api_version=APP_API_VERSION,.id=ID,.title=ID,.flags=FLAGS,.refresh_ms=16,.state_size=sizeof(unsigned),.on_enter=enter,.on_leave=leave,.on_event=event,.render=render}
static const app_descriptor_t face=DESC("face",APP_STANDBY), launcher=DESC("launcher",APP_HIDDEN), game=DESC("game",0), radar=DESC("radar",APP_STANDBY);
static const app_descriptor_t *const apps[]={&launcher,&game,&face,&radar};
static void key(app_manager_t *m,bool down,uint64_t ms) { app_manager_key(m,0,down,ms);app_manager_tick(m,ms); }
static void active(app_manager_t *m,const char *id) { assert(!strcmp(app_active_id(&m->context),id)); }
static void protocol(void)
{
    app_request_t r;
    assert(app_protocol_parse("{\"v\":1,\"seq\":1,\"op\":\"list\"}",27,&r) && r.op==APP_OP_LIST);
    const char *message="{\"v\":1,\"seq\":24,\"op\":\"message\",\"app\":\"demo\",\"data\":{\"text\":\"hi\"}}";
    assert(app_protocol_parse(message,strlen(message),&r) && !strcmp(r.message,"{\"text\":\"hi\"}"));
    const char *key="{\"v\":1,\"seq\":2,\"op\":\"key\",\"key\":0,\"pressed\":true}";
    assert(app_protocol_parse(key,strlen(key),&r) && r.op==APP_OP_KEY && r.key==0 && r.pressed);
    const char *map="{\"v\":1,\"seq\":3,\"op\":\"keymap\",\"mapping\":{\"physical\":[0,1,2],\"usb\":[2,1,0],\"words\":[\"yes\",\"no\",\"next\"]}}";
    assert(app_protocol_parse(map,strlen(map),&r) && r.keymap_set && r.keymap.usb[0]==2 && !strcmp(r.keymap.words[0],"yes"));
    const char *bad[]={"{\"v\":1,\"seq\":1,\"op\":\"list\",\"op\":\"menu\"}","{\"v\":1,\"seq\":1.5,\"op\":\"list\"}","{\"v\":1,\"seq\":1,\"op\":\"open\",\"app\":\"../bad\"}","{\"v\":1,\"seq\":1,\"op\":\"list\"} trailing","{\"v\":1,\"seq\":1,\"op\":\"message\",\"app\":\"demo\",\"data\":{\"text\":\"\\u0000\"}}"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i) assert(!app_protocol_parse(bad[i],strlen(bad[i]),&r));
    const char *bad_keys[]={"{\"v\":1,\"seq\":1,\"op\":\"key\",\"key\":3,\"pressed\":true}","{\"v\":1,\"seq\":1,\"op\":\"key\",\"key\":0,\"pressed\":1}","{\"v\":1,\"seq\":1,\"op\":\"keymap\",\"mapping\":{\"physical\":[0,1,2],\"usb\":[0,1,3],\"words\":[\"a\",\"b\",\"c\"]}}"};
    for(unsigned i=0;i<sizeof bad_keys/sizeof bad_keys[0];++i) assert(!app_protocol_parse(bad_keys[i],strlen(bad_keys[i]),&r));
    const char *hardware="{\"v\":1,\"seq\":4,\"op\":\"hardware\",\"profile\":\"robot\",\"reboot\":true}";
    assert(app_protocol_parse(hardware,strlen(hardware),&r) && r.hardware_set && r.hardware_profile==APP_PROFILE_ROBOT && r.reboot);
    const char *bad_hardware[]={"{\"v\":1,\"seq\":1,\"op\":\"hardware\",\"profile\":\"missing\"}","{\"v\":1,\"seq\":1,\"op\":\"hardware\",\"reboot\":true}","{\"v\":1,\"seq\":1,\"op\":\"hardware\",\"profile\":\"robot\",\"reboot\":1}"};
    for(unsigned i=0;i<sizeof bad_hardware/sizeof bad_hardware[0];++i) assert(!app_protocol_parse(bad_hardware[i],strlen(bad_hardware[i]),&r));
    char nested[100];memset(nested,'[',20);memset(nested+20,']',20);assert(!app_protocol_parse(nested,40,&r));
}
/* The alarm registry opens apps by itself: an alarm over anything, a chime only over the standby screen, and the
 * target is told (APP_EVENT_ALARM) right after it has been entered. */
static app_time_t fake_now;
static unsigned fake_time_reads;
static void read_fake_time(app_time_t *out,void *u) { (void)u;++fake_time_reads;*out=fake_now; }
static unsigned alarms_seen,alarm_key;static char alarm_text[24];static const char *alarm_app_at_event;static bool alarm_answer_done;
static void alarm_event(app_context_t *c,const app_event_t *e)
{
    if(e->kind==APP_EVENT_ALARM) { ++alarms_seen;alarm_key=e->key;snprintf(alarm_text,sizeof alarm_text,"%s",e->text);alarm_app_at_event=c->app->id;
        if(alarm_answer_done) app_alarm_done(e->key); }
}
static void clock_at(unsigned hour,unsigned minute,unsigned second) { app_time_from_epoch(1763308800U+hour*3600U+minute*60U+second,480,&fake_now); }
static void alarms(void)
{
    app_alarm_reset();
    app_descriptor_t clock_app=DESC("clock",APP_STANDBY);clock_app.on_event=alarm_event;
    app_descriptor_t mute=DESC("mute",0);mute.on_event=NULL; /* an app that cannot be told anything */
    const app_descriptor_t *const catalog[]={&launcher,&game,&face,&clock_app,&mute};
    app_platform_t platform={.scene_create=scene2,.scene_delete=destroy2,.time_read=read_fake_time};
    app_manager_t w;assert(app_manager_init(&w,catalog,5,&platform,"face",0));active(&w,"face");assert(w.idle);
    memset(&fake_now,0,sizeof fake_now);fake_time_reads=0;alarms_seen=0;alarm_answer_done=false;
    const unsigned chime=app_alarm_add(&(app_alarm_spec_t){.owner="clock",.kind=APP_ALARM_CLOCK,.enabled=true,.idle_only=true,.window_s=15,
        .minute=0,.day_mask=APP_ALARM_EVERY_DAY,.hour_mask=APP_ALARM_EVERY_HOUR,.label="整点"});
    /* it is looked at a few times a second, not on every tick, and costs nothing while nothing is registered for it */
    app_manager_tick(&w,1000);app_manager_tick(&w,1100);app_manager_tick(&w,1200);assert(fake_time_reads==1);active(&w,"face");
    app_manager_tick(&w,1000+APP_ALARM_POLL_MS-1);assert(fake_time_reads==1);
    app_manager_tick(&w,1000+APP_ALARM_POLL_MS);assert(fake_time_reads==2);
    clock_at(9,0,0);app_manager_tick(&w,2000);
    active(&w,"clock");assert(!w.idle && alarms_seen==1 && alarm_key==chime && !strcmp(alarm_text,"整点") && !strcmp(alarm_app_at_event,"clock"));
    assert(enters>0); /* the app had been entered when it was told: it sees a screen */
    app_alarm_done(chime);
    app_manager_tick(&w,3000);app_manager_tick(&w,4000);assert(alarms_seen==1); /* the same minute is not announced twice */
    /* a chime does not interrupt a game, and is not made up for when the standby screen is back */
    assert(app_open(&w.context,"game"));app_manager_commit(&w);active(&w,"game");
    clock_at(10,0,1);app_manager_tick(&w,5000);active(&w,"game");assert(alarms_seen==1);
    app_idle(&w.context);app_manager_commit(&w);active(&w,"face");assert(w.idle);
    clock_at(10,0,16);app_manager_tick(&w,6000);active(&w,"face");assert(alarms_seen==1);
    /* an alarm does take any screen: a game, then the menu */
    const unsigned ring=app_alarm_add(&(app_alarm_spec_t){.owner="clock",.kind=APP_ALARM_AFTER,.enabled=true,.after_s=2,.label="闹钟",.tag=7});
    assert(app_open(&w.context,"game"));app_manager_commit(&w);active(&w,"game");
    app_manager_tick(&w,6500);active(&w,"game");app_manager_tick(&w,8600);
    active(&w,"clock");assert(alarms_seen==2 && alarm_key==ring && !strcmp(alarm_text,"闹钟"));
    app_alarm_info_t info;assert(app_alarm_get(ring,&info) && info.state==APP_ALARM_RINGING && info.tag==7);
    app_alarm_done(ring);
    const unsigned menu_ring=app_alarm_add(&(app_alarm_spec_t){.owner="game",.target="clock",.kind=APP_ALARM_AFTER,.enabled=true,.after_s=1});
    assert(app_open(&w.context,"launcher"));app_manager_commit(&w);active(&w,"launcher");
    app_manager_tick(&w,9000);app_manager_tick(&w,10000);active(&w,"clock");assert(alarms_seen==3 && alarm_key==menu_ring); /* even over the menu, for another app's entry */
    app_alarm_done(menu_ring);
    /* the target is already on screen: it is told, and not entered a second time */
    const unsigned e0=enters,l0=leaves;
    const unsigned again=app_alarm_add(&(app_alarm_spec_t){.owner="clock",.kind=APP_ALARM_AFTER,.enabled=true,.after_s=1});
    app_manager_tick(&w,11000);app_manager_tick(&w,12000);
    active(&w,"clock");assert(alarms_seen==4 && enters==e0 && leaves==l0 && alarm_key==again);
    /* leaving the app that is ringing answers the ring: the entry is not left blocking the others */
    assert(app_alarm_get(again,&info) && info.state==APP_ALARM_RINGING);
    assert(app_open(&w.context,"game"));app_manager_commit(&w);active(&w,"game");
    assert(!app_alarm_get(again,&info)); /* a one-shot that was answered is gone */
    /* the app answers from its own callback */
    alarm_answer_done=true;
    const unsigned quick=app_alarm_add(&(app_alarm_spec_t){.owner="clock",.kind=APP_ALARM_AFTER,.enabled=true,.after_s=1});
    app_manager_tick(&w,13000);app_manager_tick(&w,14000);
    active(&w,"clock");assert(alarms_seen==5 && alarm_key==quick && !app_alarm_get(quick,&info));
    alarm_answer_done=false;
    /* an app with no on_event is only opened, and its entry is closed at once */
    const unsigned only_open=app_alarm_add(&(app_alarm_spec_t){.owner="mute",.kind=APP_ALARM_AFTER,.enabled=true,.after_s=1});
    app_manager_tick(&w,15000);app_manager_tick(&w,16000);
    active(&w,"mute");assert(alarms_seen==5 && !app_alarm_get(only_open,&info));
    /* a target that is not in the catalog is dropped quietly; it must not block the entries behind it */
    const unsigned ghost=app_alarm_add(&(app_alarm_spec_t){.owner="clock",.target="nowhere",.kind=APP_ALARM_AFTER,.enabled=true,.after_s=1});
    const unsigned after_ghost=app_alarm_add(&(app_alarm_spec_t){.owner="clock",.kind=APP_ALARM_AFTER,.enabled=true,.after_s=1});
    app_manager_tick(&w,17000);app_manager_tick(&w,18000);app_manager_tick(&w,19000);
    assert(!app_alarm_get(ghost,&info));active(&w,"clock");assert(alarms_seen==6 && alarm_key==after_ghost);
    app_alarm_done(after_ghost);
    /* a navigation that is already waiting goes first: the alarm is announced on the next look */
    const unsigned late=app_alarm_add(&(app_alarm_spec_t){.owner="clock",.kind=APP_ALARM_AFTER,.enabled=true,.after_s=1});
    assert(app_open(&w.context,"game"));app_manager_tick(&w,20000);active(&w,"game"); /* the open was committed by this tick, before the alarm was looked at */
    app_manager_tick(&w,21000);active(&w,"clock");assert(alarms_seen==7 && alarm_key==late);app_alarm_done(late);
    /* with no clock callback only the entries that count uptime fire, and nothing else is read */
    app_platform_t bare={.scene_create=scene2,.scene_delete=destroy2};
    app_alarm_reset();
    app_manager_t q;assert(app_manager_init(&q,catalog,5,&bare,"face",0));
    const unsigned c2=app_alarm_add(&(app_alarm_spec_t){.owner="clock",.kind=APP_ALARM_CLOCK,.enabled=true,.minute=0,.day_mask=APP_ALARM_EVERY_DAY,.hour_mask=APP_ALARM_EVERY_HOUR});
    app_manager_tick(&q,1000);app_manager_tick(&q,2000);active(&q,"face");
    const unsigned s2=app_alarm_add(&(app_alarm_spec_t){.owner="clock",.kind=APP_ALARM_AFTER,.enabled=true,.after_s=1});
    app_manager_tick(&q,3000);app_manager_tick(&q,4000);active(&q,"clock");assert(alarm_key==s2);(void)c2;
    app_alarm_reset();
}
static void time_protocol(void)
{
    app_request_t r;
    const char *read="{\"v\":1,\"seq\":5,\"op\":\"time\"}";assert(app_protocol_parse(read,strlen(read),&r) && r.op==APP_OP_TIME && !r.time_set); /* a bare request reads the clock */
    const char *set="{\"v\":1,\"seq\":6,\"op\":\"time\",\"epoch\":1763366400,\"tz_min\":480}";
    assert(app_protocol_parse(set,strlen(set),&r) && r.op==APP_OP_TIME && r.time_set && r.epoch==1763366400U && r.tz_min==480);
    const char *west="{\"v\":1,\"seq\":6,\"op\":\"time\",\"epoch\":1763366400,\"tz_min\":-300}";
    assert(app_protocol_parse(west,strlen(west),&r) && r.tz_min==-300);
    const char *bad[]={"{\"v\":1,\"seq\":6,\"op\":\"time\",\"epoch\":1763366400}","{\"v\":1,\"seq\":6,\"op\":\"time\",\"tz_min\":480}",
        "{\"v\":1,\"seq\":6,\"op\":\"time\",\"epoch\":1000,\"tz_min\":480}","{\"v\":1,\"seq\":6,\"op\":\"time\",\"epoch\":1763366400,\"tz_min\":900}",
        "{\"v\":1,\"seq\":6,\"op\":\"time\",\"epoch\":1763366400.5,\"tz_min\":480}","{\"v\":1,\"seq\":6,\"op\":\"time\",\"epoch\":\"1763366400\",\"tz_min\":480}",
        "{\"v\":1,\"seq\":6,\"op\":\"time\",\"epoch\":5000000000,\"tz_min\":480}"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i) assert(!app_protocol_parse(bad[i],strlen(bad[i]),&r));
}
/* Back and the double click. History: the last two apps other than the menu. Gesture: two clicks <= 300 ms apart, then a
 * quiet 300 ms, so that a quick triple press stays the menu. */
static void click(app_manager_t *m,uint64_t t) { key(m,true,t);key(m,false,t+40); }
static void reopen(app_manager_t *m,const char *id) { assert(app_open(&m->context,id));app_manager_commit(m);active(m,id); }
static void back_and_double(void)
{
    app_platform_t p={.scene_create=scene2,.scene_delete=destroy2};
    app_manager_t d;assert(app_manager_init(&d,apps,4,&p,"face",0));active(&d,"face");assert(d.idle);
    uint64_t t=1000;
    /* nothing to go back to right after boot, and the API says so */
    assert(!app_back(&d.context) && app_double_back(&d.context));
    click(&d,t);active(&d,"launcher"); /* a click wakes the menu */
    t+=200;click(&d,t);app_manager_tick(&d,t+400);active(&d,"launcher"); /* a double with no history is just two clicks */
    /* from the menu, back is the app the menu was opened from: the standby screen, as the standby screen */
    assert(app_back(&d.context));app_manager_commit(&d);active(&d,"face");assert(d.idle);
    /* a game opened from the menu: history is [game, face] */
    t+=20000;click(&d,t);active(&d,"launcher");reopen(&d,"game");assert(!d.idle);
    unsigned before=shorts;
    t+=1000;click(&d,t);t+=250;click(&d,t);                    /* a double click on the game */
    active(&d,"game");assert(shorts==before+2);                  /* both clicks reached it as ordinary presses */
    app_manager_tick(&d,t+40+299);active(&d,"game");              /* not before the quiet moment is over */
    app_manager_tick(&d,t+40+300);active(&d,"face");assert(d.idle); /* then back, to the standby screen it left */
    /* and again: the toggle between the two. On the standby screen the first click opens the menu, the second moves
     * its cursor, and the gesture is still read as made on the standby screen: back is the game. */
    const size_t cursor=app_selection(&d.context);
    t+=5000;click(&d,t);active(&d,"launcher");t+=200;click(&d,t);
    assert(app_selection(&d.context)==(cursor+1)%3);               /* the second click did move the cursor ... */
    app_manager_tick(&d,t+40+300);active(&d,"game");assert(!d.idle);
    assert(app_selection(&d.context)==cursor);                     /* ... and the double click gave that back */
    t+=5000;click(&d,t);t+=200;click(&d,t);app_manager_tick(&d,t+400);active(&d,"face");
    /* the menu in between does not count: game -> menu -> radar, and back from the radar is the game */
    reopen(&d,"game");t+=5000;click(&d,t);t+=120;click(&d,t);t+=120;click(&d,t);active(&d,"launcher"); /* a quick triple: the menu */
    app_manager_tick(&d,t+2000);active(&d,"launcher");             /* and not a double that left first */
    reopen(&d,"radar");t+=5000;click(&d,t);t+=200;click(&d,t);app_manager_tick(&d,t+400);active(&d,"game");
    t+=5000;click(&d,t);t+=200;click(&d,t);app_manager_tick(&d,t+400);active(&d,"radar");
    /* from the menu: back is where it came from, whatever the cursor did */
    app_menu(&d.context);app_manager_commit(&d);active(&d,"launcher");
    const size_t menu_cursor=app_selection(&d.context);
    t+=5000;click(&d,t);t+=150;click(&d,t);assert(app_selection(&d.context)==(menu_cursor+2)%3);
    app_manager_tick(&d,t+400);active(&d,"radar");assert(app_selection(&d.context)==menu_cursor);
    /* opening the app the menu came from keeps the app before it */
    app_menu(&d.context);app_manager_commit(&d);reopen(&d,"radar");
    t+=5000;click(&d,t);t+=200;click(&d,t);app_manager_tick(&d,t+400);active(&d,"game");
    /* a third press inside the quiet moment is a triple, one after it is an ordinary click of the app it went back to */
    reopen(&d,"game");reopen(&d,"radar");reopen(&d,"game");
    t+=5000;click(&d,t);t+=250;click(&d,t);                       /* presses at 0 and 250, released at 290 */
    key(&d,true,t+285);key(&d,false,t+330);active(&d,"launcher");  /* the third at 285 after the second: the menu */
    app_manager_tick(&d,t+1000);active(&d,"launcher");
    reopen(&d,"game");
    t+=5000;click(&d,t);t+=250;click(&d,t);
    before=shorts;app_manager_tick(&d,t+40+300);active(&d,"radar");key(&d,true,t+400);key(&d,false,t+440);active(&d,"radar");
    assert(shorts==before+1);                                      /* the late press landed in the app it went back to */
    /* slow clicks are only clicks, and so is a hold followed by a click */
    reopen(&d,"game");reopen(&d,"radar");reopen(&d,"game");
    t+=5000;click(&d,t);t+=301;click(&d,t);app_manager_tick(&d,t+2000);active(&d,"game");
    t+=5000;key(&d,true,t);app_manager_tick(&d,t+700);key(&d,false,t+700);t+=800;click(&d,t);app_manager_tick(&d,t+2000);active(&d,"game");
    /* a click followed by a hold is not a double, and neither are two clicks and a third press that is no triple */
    t+=5000;click(&d,t);key(&d,true,t+200);app_manager_tick(&d,t+800);key(&d,false,t+800);app_manager_tick(&d,t+3000);active(&d,"game");
    t+=5000;click(&d,t);key(&d,true,t+290);key(&d,false,t+740);                    /* the second click is slow: released at 450 ms */
    key(&d,true,t+1000);key(&d,false,t+1030);app_manager_tick(&d,t+2000);active(&d,"game"); /* a third inside the quiet moment, 1 s after the first */
    /* a cancelled press (a USB lease running out) is no click */
    t+=5000;click(&d,t);app_manager_cancel_key(&d,0,t+100);t+=150;click(&d,t);app_manager_tick(&d,t+2000);active(&d,"game");
    /* the app can opt out: of both gestures (a game played by mashing) or of the double click alone */
    app_set_triple_exit(&d.context,false);assert(!app_double_back(&d.context));
    t+=5000;click(&d,t);t+=150;click(&d,t);app_manager_tick(&d,t+2000);active(&d,"game");
    app_set_triple_exit(&d.context,true);assert(app_double_back(&d.context));
    app_set_double_back(&d.context,false);assert(!app_double_back(&d.context) && app_triple_exit(&d.context));
    t+=5000;click(&d,t);t+=150;click(&d,t);app_manager_tick(&d,t+2000);active(&d,"game");
    t+=5000;click(&d,t);t+=100;click(&d,t);t+=100;click(&d,t);active(&d,"launcher"); /* the triple still works */
    reopen(&d,"game");assert(app_double_back(&d.context));        /* restored on every app change */
    /* an app that opts out between the two clicks stops the gesture */
    t+=5000;click(&d,t);app_set_double_back(&d.context,false);t+=150;click(&d,t);app_manager_tick(&d,t+2000);active(&d,"game");
    /* back by hand, from an app: one step, and then the toggle */
    reopen(&d,"radar");reopen(&d,"game");
    assert(app_back(&d.context));app_manager_commit(&d);active(&d,"radar");
    assert(app_back(&d.context));app_manager_commit(&d);active(&d,"game");
    /* a pending double belongs to its screen: a change of app in the quiet moment cancels it */
    t+=5000;click(&d,t);t+=200;click(&d,t);reopen(&d,"radar");app_manager_tick(&d,t+2000);active(&d,"radar");
    /* keys other than K1 never make a double */
    reopen(&d,"game");
    for(unsigned i=0;i<2;++i) { app_manager_key(&d,1,true,t+30000+i*100);app_manager_key(&d,1,false,t+30040+i*100); }
    app_manager_tick(&d,t+31000);active(&d,"game");
}
int main(void)
{
    app_manager_t m;app_platform_t p={.scene_create=scene,.scene_delete=destroy,.release_services=release,.save_standby=save};
    assert(app_manager_init(&m,apps,4,&p,"face",0));active(&m,"face");assert(m.idle && app_count(&m.context)==3);
    assert(!app_set_standby(&m.context,"game") && !app_open(&m.context,"missing"));
    app_manager_render(&m);assert(renders==1);app_manager_tick(&m,10);app_manager_render(&m);assert(renders==1);
    key(&m,true,100);key(&m,false,150);active(&m,"launcher");
    app_manager_tick(&m,10149);active(&m,"launcher");app_manager_tick(&m,10150);active(&m,"face");
    key(&m,true,20000);key(&m,false,20050);active(&m,"launcher");
    key(&m,true,21000);app_manager_tick(&m,21499);active(&m,"launcher");app_manager_tick(&m,21500);active(&m,"game");
    unsigned old_short=shorts,old_up=ups;key(&m,false,21550);assert(shorts==old_short && ups==old_up);
    key(&m,true,23000);key(&m,false,23050);key(&m,true,23150);key(&m,false,23200);key(&m,true,23300);active(&m,"launcher");key(&m,false,23350);
    save_ok=false;assert(!app_set_standby(&m.context,"radar"));assert(!strcmp(app_standby_id(&m.context),"face"));
    save_ok=true;assert(app_set_standby(&m.context,"radar") && !strcmp(saved,"radar"));app_manager_tick(&m,33400);active(&m,"radar");assert(m.idle);
    assert(app_open(&m.context,"game"));app_manager_commit(&m);active(&m,"game");
    unsigned baseline=shorts;
    for(unsigned i=0;i<3;++i) { app_manager_key(&m,1,true,34000+i*100);app_manager_key(&m,1,false,34050+i*100); }
    active(&m,"game");assert(shorts==baseline+3); /* K2 never triggers global K1 triple. */
    /* A game that needs every press can switch the global exit off: six quick presses stay plain presses. */
    assert(app_triple_exit(&m.context));
    app_set_triple_exit(&m.context,false);assert(!app_triple_exit(&m.context));
    baseline=shorts;
    for(unsigned i=0;i<6;++i) { key(&m,true,36000+i*100);key(&m,false,36050+i*100); }
    active(&m,"game");assert(shorts==baseline+6);
    app_set_triple_exit(&m.context,true); /* switched back on, the next three quick presses leave again */
    key(&m,true,37000);key(&m,false,37050);key(&m,true,37150);key(&m,false,37200);key(&m,true,37300);active(&m,"launcher");key(&m,false,37350);
    assert(app_open(&m.context,"game"));app_manager_commit(&m);active(&m,"game");
    app_set_triple_exit(&m.context,false);assert(app_open(&m.context,"face"));app_manager_commit(&m);
    assert(app_triple_exit(&m.context)); /* restored on every app change */
    assert(app_open(&m.context,"game"));app_manager_commit(&m);active(&m,"game");
    /* The deliberate exit of a game without the triple: hold K1 for the chosen time; shorter holds are only holds. */
    assert(app_exit_hold_progress(&m.context)==0);
    app_set_triple_exit(&m.context,false);app_set_exit_hold(&m.context,2000);
    key(&m,true,40000);app_manager_tick(&m,40600);assert(app_exit_hold_progress(&m.context)==600);
    app_manager_tick(&m,41900);active(&m,"game");key(&m,false,41950);assert(app_exit_hold_progress(&m.context)==0);
    app_manager_tick(&m,43000);active(&m,"game"); /* released at 1.95 s: nothing */
    key(&m,true,44000);app_manager_tick(&m,45999);active(&m,"game");assert(app_exit_hold_progress(&m.context)==1999);
    app_manager_tick(&m,46000);active(&m,"launcher");key(&m,false,46100);active(&m,"launcher"); /* its release goes nowhere */
    assert(app_open(&m.context,"game"));app_manager_commit(&m);active(&m,"game");
    key(&m,true,47000);app_manager_tick(&m,60000);active(&m,"game"); /* off by default and after every app change */
    key(&m,false,60100);
    app_set_exit_hold(&m.context,50); /* clamped to one second so a mistake cannot make a tap an exit */
    key(&m,true,61000);app_manager_tick(&m,61900);active(&m,"game");app_manager_tick(&m,62000);active(&m,"launcher");key(&m,false,62100);
    assert(app_open(&m.context,"game"));app_manager_commit(&m);active(&m,"game");
    app_set_exit_hold(&m.context,2000);app_set_exit_hold(&m.context,0);key(&m,true,63000);app_manager_tick(&m,70000);active(&m,"game");key(&m,false,70100);
    /* Only a press that began with the hold on counts: a game over screen reached while fire is still held is not an exit. */
    key(&m,true,71000);app_set_exit_hold(&m.context,2000);app_manager_tick(&m,71600);assert(app_exit_hold_progress(&m.context)==0);
    app_manager_tick(&m,74000);active(&m,"game");assert(app_exit_hold_progress(&m.context)==0);key(&m,false,74100);
    key(&m,true,75000);app_manager_tick(&m,76999);active(&m,"game");assert(app_exit_hold_progress(&m.context)==1999); /* the next press is armed */
    app_manager_tick(&m,77000);active(&m,"launcher");key(&m,false,77100);
    /* Switching it off drops the press under way: switching it back on does not revive it. */
    assert(app_open(&m.context,"game"));app_manager_commit(&m);active(&m,"game");
    app_set_exit_hold(&m.context,2000);key(&m,true,80000);app_manager_tick(&m,80500);assert(app_exit_hold_progress(&m.context)==500);
    app_set_exit_hold(&m.context,0);assert(app_exit_hold_progress(&m.context)==0);
    app_set_exit_hold(&m.context,2000);assert(app_exit_hold_progress(&m.context)==0);
    app_manager_tick(&m,83000);active(&m,"game");key(&m,false,83100);
    app_set_exit_hold(&m.context,0);
    app_event_t msg={.kind=APP_EVENT_MESSAGE,.text="{\"text\":\"hi\"}",.ms=35000};app_manager_event(&m,&msg);active(&m,"launcher");
    assert(enters==leaves+1 && scenes==1 && releases==leaves);
    app_descriptor_t invalid=face;invalid.state_size=APP_STATE_CAPACITY+1;
    const app_descriptor_t *bad[]={&launcher,&invalid};app_manager_t unused;
    assert(!app_manager_init(&unused,bad,2,&p,"face",0));
    invalid=face;invalid.api_version=0;assert(!app_manager_init(&unused,bad,2,&p,"face",0));
    /* Boot default: only an APP_STANDBY app that also asks for it counts; everything else falls back. */
    static const app_descriptor_t wedding=DESC("wedding",APP_STANDBY|APP_DEFAULT_STANDBY), stray=DESC("stray",APP_DEFAULT_STANDBY);
    const app_descriptor_t *const with_default[]={&launcher,&game,&face,&stray,&wedding,&radar};
    assert(!strcmp(app_manager_default_standby(apps,4,"face"),"face"));
    assert(!strcmp(app_manager_default_standby(with_default,6,"face"),"wedding"));
    assert(!strcmp(app_manager_default_standby(with_default,4,"face"),"face")); /* no standby flag: ignored */
    assert(!strcmp(app_manager_default_standby(NULL,0,"face"),"face"));
    app_platform_t p2={.scene_create=scene2,.scene_delete=destroy2}; /* the first manager still owns the single scene above */
    app_manager_t booted;assert(app_manager_init(&booted,with_default,6,&p2,app_manager_default_standby(with_default,6,"face"),0));
    active(&booted,"wedding");assert(booted.idle && !strcmp(app_standby_id(&booted.context),"wedding"));
    key(&booted,true,100);key(&booted,false,150);active(&booted,"launcher");
    app_manager_tick(&booted,10150);active(&booted,"wedding"); /* the menu timeout returns to the default standby */
    protocol();time_protocol();alarms();back_and_double();puts("app framework: lifecycle ownership, catalog, cadence, standby persistence, default standby, global input, deferred navigation, alarm registry wake-ups, back and the double click, and bounded protocol passed");
}
