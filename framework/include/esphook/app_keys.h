#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define APP_KEY_COUNT 3U
#define APP_KEY_WORD_SIZE 16U
#define APP_USB_KEY_LEASE_MS 350U
typedef struct { uint8_t physical[3], usb[3]; char words[3][APP_KEY_WORD_SIZE]; } app_keymap_t;
typedef enum { APP_KEY_PHYSICAL, APP_KEY_USB } app_key_source_t;
typedef struct { bool down; uint8_t logical; uint64_t renewed_ms; } app_key_contact_t;
typedef struct {
    app_keymap_t map;
    app_key_contact_t contacts[2][3];
    void (*emit)(void *user, unsigned key, bool down, bool cancel, uint64_t ms);
    void *user;
} app_key_router_t;
app_keymap_t app_keymap_default(void);
bool app_keymap_valid(const app_keymap_t *map);
void app_key_router_init(app_key_router_t *r, const app_keymap_t *map,
                        void (*emit)(void *, unsigned, bool, bool, uint64_t), void *user);
/* New mappings affect new presses; releases keep their latched logical key. */
bool app_key_router_configure(app_key_router_t *r, const app_keymap_t *map);
void app_key_router_edge(app_key_router_t *r, app_key_source_t source, unsigned raw,
                         bool pressed, bool cancel, uint64_t now_ms);
void app_key_router_tick(app_key_router_t *r, uint64_t now_ms);
void app_key_router_release_usb(app_key_router_t *r, uint64_t now_ms);
