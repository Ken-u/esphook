#include "app_manager.h"
#include "app_protocol.h"
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
    protocol();puts("app framework: lifecycle ownership, catalog, cadence, standby persistence, default standby, global input, deferred navigation and bounded protocol passed");
}
