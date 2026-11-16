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
    const char *bad[]={"{\"v\":1,\"seq\":1,\"op\":\"list\",\"op\":\"menu\"}","{\"v\":1,\"seq\":1.5,\"op\":\"list\"}","{\"v\":1,\"seq\":1,\"op\":\"open\",\"app\":\"../bad\"}","{\"v\":1,\"seq\":1,\"op\":\"list\"} trailing","{\"v\":1,\"seq\":1,\"op\":\"message\",\"app\":\"demo\",\"data\":{\"text\":\"\\u0000\"}}"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i) assert(!app_protocol_parse(bad[i],strlen(bad[i]),&r));
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
    protocol();puts("app framework: lifecycle ownership, catalog, cadence, standby persistence, global input, deferred navigation and bounded protocol passed");
}
