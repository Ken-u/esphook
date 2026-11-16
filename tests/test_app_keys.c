#include "app_keys.h"
#include "app_manager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {unsigned key;bool down,cancel;uint64_t ms;} events[32];
static unsigned count,shorts,confirms,cancels;
static void record(void *u,unsigned key,bool down,bool cancel,uint64_t ms)
{(void)u;assert(count<32);events[count].key=key;events[count].down=down;events[count].cancel=cancel;events[count++].ms=ms;}
static void manager_edge(void *u,unsigned key,bool down,bool cancel,uint64_t ms)
{if(cancel) app_manager_cancel_key(u,key,ms);else app_manager_key(u,key,down,ms);}
static void event(app_context_t *c,const app_event_t *e)
{(void)c;if(e->kind==APP_EVENT_SHORT)++shorts;if(e->kind==APP_EVENT_CONFIRM)++confirms;if(e->kind==APP_EVENT_CANCEL)++cancels;}
static const app_descriptor_t face={.api_version=1,.id="face",.title="face",.flags=APP_STANDBY,.refresh_ms=16,.on_event=event};
static const app_descriptor_t menu={.api_version=1,.id="launcher",.title="menu",.flags=APP_HIDDEN,.refresh_ms=16};
int main(void)
{
    app_keymap_t map=app_keymap_default();assert(app_keymap_valid(&map));
    app_key_router_t r;app_key_router_init(&r,&map,record,NULL);
    app_key_router_edge(&r,APP_KEY_USB,0,true,false,0);
    app_key_router_edge(&r,APP_KEY_PHYSICAL,0,true,false,10);
    app_key_router_edge(&r,APP_KEY_USB,0,false,false,20);assert(count==1);
    app_key_router_edge(&r,APP_KEY_PHYSICAL,0,false,false,30);assert(count==2 && !events[1].down);
    map.physical[0]=2;assert(app_key_router_configure(&r,&map));
    app_key_router_edge(&r,APP_KEY_PHYSICAL,0,true,false,100);assert(events[2].key==2);
    map.physical[0]=1;map.usb[1]=2;assert(app_key_router_configure(&r,&map));
    app_key_router_edge(&r,APP_KEY_PHYSICAL,0,false,false,200);assert(events[3].key==2);
    app_key_router_edge(&r,APP_KEY_USB,1,true,false,210);app_key_router_edge(&r,APP_KEY_USB,1,true,false,400);
    assert(count==5);app_key_router_tick(&r,749);assert(count==5);app_key_router_tick(&r,750);
    assert(count==6 && events[5].cancel && events[5].key==2);
    map.usb[0]=3;assert(!app_key_router_configure(&r,&map));map=app_keymap_default();
    memset(map.words[0],'x',sizeof map.words[0]);assert(!app_keymap_valid(&map));
    map=app_keymap_default();strcpy(map.words[0],"bad\nword");assert(!app_keymap_valid(&map));
    /* Real manager integration: expired/disconnected keys never become taps or buys. */
    app_manager_t m;const app_descriptor_t *apps[]={&face,&menu};app_platform_t platform={0};
    assert(app_manager_init(&m,apps,2,&platform,"face",0));
    assert(app_open(&m.context,"face"));app_manager_commit(&m);
    map=app_keymap_default();app_key_router_init(&r,&map,manager_edge,&m);
    app_key_router_edge(&r,APP_KEY_USB,0,true,false,0);app_key_router_tick(&r,350);app_manager_tick(&m,600);
    assert(cancels==1 && shorts==0 && confirms==0 && !m.input[0].down);
    app_key_router_edge(&r,APP_KEY_USB,0,true,false,1000);
    for(uint64_t t=1100;t<=1600;t+=100){app_key_router_edge(&r,APP_KEY_USB,0,true,false,t);app_key_router_tick(&r,t);app_manager_tick(&m,t);}
    assert(confirms==1);app_key_router_edge(&r,APP_KEY_USB,0,false,false,1650);assert(shorts==0);
    app_key_router_edge(&r,APP_KEY_USB,1,true,false,2000);app_key_router_release_usb(&r,2010);
    assert(cancels==2 && shorts==0);
    puts("keys: source merging, latched remaps, USB renewals, expiry cancellation and real shared confirmation passed");
}
