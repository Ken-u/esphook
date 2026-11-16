#include "app_keys.h"
#include <string.h>
app_keymap_t app_keymap_default(void)
{ return (app_keymap_t){.physical={0,1,2},.usb={0,1,2},.words={"ok","continue","done"}}; }
bool app_keymap_valid(const app_keymap_t *map)
{
    if (!map) return false;
    for (unsigned i=0;i<APP_KEY_COUNT;++i) {
        if (map->physical[i]>=APP_KEY_COUNT || map->usb[i]>=APP_KEY_COUNT || !map->words[i][0] ||
            !memchr(map->words[i],0,APP_KEY_WORD_SIZE)) return false;
        for (const unsigned char *p=(const unsigned char *)map->words[i];*p;++p)
            if (*p<0x20 || *p>0x7e) return false;
    }
    return true;
}
void app_key_router_init(app_key_router_t *r,const app_keymap_t *map,
                        void (*emit)(void *,unsigned,bool,bool,uint64_t),void *user)
{
    memset(r,0,sizeof *r);r->map=app_keymap_valid(map)?*map:app_keymap_default();r->emit=emit;r->user=user;
}
bool app_key_router_configure(app_key_router_t *r,const app_keymap_t *map)
{ if (!app_keymap_valid(map)) return false;r->map=*map;return true; }
static bool held(const app_key_router_t *r,unsigned key)
{
    for(unsigned source=0;source<2;++source) for(unsigned raw=0;raw<3;++raw)
        if(r->contacts[source][raw].down && r->contacts[source][raw].logical==key) return true;
    return false;
}
void app_key_router_edge(app_key_router_t *r,app_key_source_t source,unsigned raw,
                         bool pressed,bool cancel,uint64_t now)
{
    if((unsigned)source>APP_KEY_USB || raw>=3) return;
    app_key_contact_t *c=&r->contacts[source][raw];
    if(pressed) {
        c->renewed_ms=now;if(c->down) return;
        unsigned key=source==APP_KEY_USB?r->map.usb[raw]:r->map.physical[raw];
        bool was=held(r,key);c->logical=(uint8_t)key;c->down=true;
        if(!was && r->emit) r->emit(r->user,key,true,false,now);
    } else if(c->down) {
        unsigned key=c->logical;c->down=false;
        if(!held(r,key) && r->emit) r->emit(r->user,key,false,cancel,now);
    }
}
void app_key_router_tick(app_key_router_t *r,uint64_t now)
{
    for(unsigned raw=0;raw<3;++raw) {
        const app_key_contact_t *c=&r->contacts[APP_KEY_USB][raw];
        if(c->down && (now<c->renewed_ms || now-c->renewed_ms>=APP_USB_KEY_LEASE_MS))
            app_key_router_edge(r,APP_KEY_USB,raw,false,true,now);
    }
}
void app_key_router_release_usb(app_key_router_t *r,uint64_t now)
{ for(unsigned raw=0;raw<3;++raw) app_key_router_edge(r,APP_KEY_USB,raw,false,true,now); }
