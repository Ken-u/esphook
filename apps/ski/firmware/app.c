#include "esphook/app.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include "esphook/app_runtime.h"
#include "ski_game.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>

/* Everything is plain LVGL objects over a snow-white background. Trees and rocks come from fixed pools that are
 * created once and only moved/hidden afterwards, so a run allocates nothing after the first frame. */
#define TREE_POOL 30
#define ROCK_POOL 10
#define MARKS 8
#define SNOW 0xE9F2F9
#define INK 0x16324F
typedef struct {
    ski_game_t game;
    lv_obj_t *skier, *ski_lower[2], *yeti, *pips[SKI_LIVES], *marks[MARKS];
    lv_obj_t *trees[TREE_POOL], *trunks[TREE_POOL], *rocks[ROCK_POOL];
    lv_obj_t *meters, *panel, *title, *detail, *hint;
    uint8_t tree_variant[TREE_POOL];
    int8_t drawn_dir;
    bool built;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "ski state too large");
typedef struct { ski_phase_t phase; unsigned meters, best, lives, speed; } status_t;
static status_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void style_tree(lv_obj_t *obj, unsigned variant)
{
    lv_obj_set_size(obj, variant ? 7 : 8, variant ? 8 : 10);
    lv_obj_set_style_bg_color(obj, app_ui_color(variant ? 0x2E9E63 : 0x1F6F4A), LV_PART_MAIN);
}
static void build(state_t *s, lv_obj_t *root)
{
    app_ui_rect(root, 0, 0, APP_WIDTH, APP_HEIGHT, SNOW, 0);
    for (unsigned i = 0; i < MARKS; ++i) s->marks[i] = app_ui_rect(root, (int)((i * 53U + 17U) % 150U) + 4, 0, 3, 1, 0xBFD3E6, 0);
    for (unsigned i = 0; i < TREE_POOL; ++i) {
        s->trunks[i] = app_ui_rect(root, 0, 0, 2, 3, 0x7A5230, 0);
        s->trees[i] = app_ui_rect(root, 0, 0, 8, 10, 0x1F6F4A, 3);
        lv_obj_add_flag(s->trunks[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s->trees[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (unsigned i = 0; i < ROCK_POOL; ++i) {
        s->rocks[i] = app_ui_rect(root, 0, 0, 7, 5, 0x7C8798, 2);
        lv_obj_add_flag(s->rocks[i], LV_OBJ_FLAG_HIDDEN);
    }
    s->skier = lv_obj_create(root);
    app_ui_plain(s->skier);
    lv_obj_set_size(s->skier, 10, 16);
    app_ui_rect(s->skier, 3, 0, 4, 4, 0xFFD0A6, 2);   /* head */
    app_ui_rect(s->skier, 2, 4, 6, 6, 0xE5484D, 2);   /* jacket */
    app_ui_rect(s->skier, 2, 9, 2, 4, 0x203040, 0);   /* skis: upper halves ... */
    app_ui_rect(s->skier, 6, 9, 2, 4, 0x203040, 0);
    s->ski_lower[0] = app_ui_rect(s->skier, 2, 12, 2, 4, 0x203040, 0); /* ... lower halves lean with the heading */
    s->ski_lower[1] = app_ui_rect(s->skier, 6, 12, 2, 4, 0x203040, 0);
    s->yeti = lv_obj_create(root);
    app_ui_plain(s->yeti);
    lv_obj_set_size(s->yeti, 14, 12);
    app_ui_rect(s->yeti, 0, 0, 14, 12, 0x8FA9D1, 5);
    app_ui_rect(s->yeti, 3, 3, 8, 6, 0x2A2142, 2);
    app_ui_rect(s->yeti, 4, 4, 2, 2, 0xFF5A5A, 0);
    app_ui_rect(s->yeti, 8, 4, 2, 2, 0xFF5A5A, 0);
    app_ui_rect(s->yeti, 5, 7, 4, 1, 0xF4F7FB, 0);
    lv_obj_add_flag(s->yeti, LV_OBJ_FLAG_HIDDEN);
    s->meters = app_ui_label(root, &aihook_font_12, 3, 0, 80, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->meters, app_ui_color(INK), LV_PART_MAIN);
    for (unsigned i = 0; i < SKI_LIVES; ++i) s->pips[i] = app_ui_rect(root, 160 - 3 - (int)(SKI_LIVES - i) * 8, 3, 6, 6, 0xE5484D, 3);
    s->panel = app_ui_rect(root, 12, 12, 136, 56, 0x102536, 6);
    s->title = app_ui_label(s->panel, &aihook_font_16, 0, 4, 136, 20, LV_LABEL_LONG_MODE_CLIP);
    s->detail = app_ui_label(s->panel, &aihook_font_12, 0, 26, 136, 14, LV_LABEL_LONG_MODE_CLIP);
    s->hint = app_ui_label(s->panel, &aihook_font_12, 0, 40, 136, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->title, app_ui_color(APP_COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_color(s->hint, app_ui_color(APP_COLOR_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_align(s->title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s->detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(s->hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s->drawn_dir = 0;
    s->built = true;
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    ski_start(&s->game, ctx->now_ms, app_random());
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    ski_tick(&s->game, ctx->now_ms);
    ski_view_t view;
    ski_snapshot(&s->game, &view);
    status_t status = {view.phase, view.meters, view.best, view.lives, view.speed_pps};
    portENTER_CRITICAL(&s_lock); s_status = status; portEXIT_CRITICAL(&s_lock);
}
static void leave(app_context_t *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&s_lock); s_status.phase = (ski_phase_t)255; portEXIT_CRITICAL(&s_lock);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->kind == APP_EVENT_DOWN) (void)ski_press(&s->game, e->ms); /* turn on the press, not the release */
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    ski_view_t v;
    ski_snapshot(&s->game, &v);
    if (!s->built) build(s, ctx->root);
    unsigned trees = 0, rocks = 0;
    for (unsigned i = 0; i < v.count; ++i) {
        const ski_sprite_t *sprite = &v.sprites[i];
        if (sprite->kind == SKI_TREE && trees < TREE_POOL) {
            lv_obj_t *obj = s->trees[trees];
            if (s->tree_variant[trees] != sprite->variant + 1U) { style_tree(obj, sprite->variant); s->tree_variant[trees] = (uint8_t)(sprite->variant + 1U); }
            int half = sprite->variant ? 4 : 5; /* canopy half height; the trunk hangs below it */
            lv_obj_set_pos(obj, sprite->x - (sprite->variant ? 3 : 4), sprite->y - half);
            lv_obj_set_pos(s->trunks[trees], sprite->x - 1, sprite->y + half);
            app_ui_hide(obj, false);
            app_ui_hide(s->trunks[trees], false);
            ++trees;
        } else if (sprite->kind == SKI_ROCK && rocks < ROCK_POOL) {
            lv_obj_set_pos(s->rocks[rocks], sprite->x - 3, sprite->y - 2);
            app_ui_hide(s->rocks[rocks], false);
            ++rocks;
        }
    }
    for (unsigned i = trees; i < TREE_POOL; ++i) { app_ui_hide(s->trees[i], true); app_ui_hide(s->trunks[i], true); }
    for (unsigned i = rocks; i < ROCK_POOL; ++i) app_ui_hide(s->rocks[i], true);
    for (unsigned i = 0; i < MARKS; ++i) /* Snow flecks sweep upwards at the speed of the slope. */
        lv_obj_set_y(s->marks[i], (int)((i * 29U + 96U - v.scroll % 96U) % 96U) - 8);
    if (s->drawn_dir != v.dir) { /* The lower halves of the skis lean into the turn. */
        lv_obj_set_x(s->ski_lower[0], 2 + v.dir * 2);
        lv_obj_set_x(s->ski_lower[1], 6 + v.dir * 2);
        s->drawn_dir = (int8_t)v.dir;
    }
    lv_obj_set_pos(s->skier, (int)(v.skier_x + 0.5f) - 5, (int)SKI_SKIER_Y - 8);
    app_ui_hide(s->skier, v.invulnerable && v.phase == SKI_PLAY && ((ctx->now_ms / 90U) & 1U)); /* blink after a crash */
    bool yeti_visible = v.yeti_y > -6.0f;
    app_ui_hide(s->yeti, !yeti_visible);
    if (yeti_visible) lv_obj_set_pos(s->yeti, (int)(v.yeti_x + 0.5f) - 7, (int)(v.yeti_y + 0.5f) - 6);
    for (unsigned i = 0; i < SKI_LIVES; ++i)
        lv_obj_set_style_bg_color(s->pips[i], app_ui_color(i < v.lives ? 0xE5484D : 0xB8C6D6), LV_PART_MAIN);
    char text[48];
    snprintf(text, sizeof text, "%u米", v.meters);
    app_ui_text(s->meters, text);
    bool overlay = v.phase != SKI_PLAY;
    app_ui_hide(s->panel, !overlay);
    if (v.phase == SKI_READY) {
        app_ui_text(s->title, "滑雪冒险");
        if (v.best) snprintf(text, sizeof text, "最远 %u米", v.best);
        else snprintf(text, sizeof text, "避开树木 躲开雪怪");
        app_ui_text(s->detail, text);
        app_ui_text(s->hint, "按键开始 再按换向");
    } else if (v.phase == SKI_OVER) {
        app_ui_text(s->title, "被雪怪抓住!");
        if (v.new_best) snprintf(text, sizeof text, "滑行 %u米 新纪录", v.meters);
        else snprintf(text, sizeof text, "滑行 %u米 最远 %u", v.meters, v.best);
        app_ui_text(s->detail, text);
        app_ui_text(s->hint, v.can_restart ? "按键重来 / 三击菜单" : "");
    }
}
static bool command(const char *line)
{
    if (!strcmp(line, "robot ski start")) {
        (void)app_transport_write(app_runtime_open("ski") ? "OK ski queued" : "ERR ski queue full");
        return true;
    }
    if (!strcmp(line, "robot ski stop")) { (void)app_runtime_menu(); return true; }
    if (strcmp(line, "robot ski status")) return false;
    status_t v;
    portENTER_CRITICAL(&s_lock); v = s_status; portEXIT_CRITICAL(&s_lock);
    char reply[120];
    snprintf(reply, sizeof reply, "phase=%u meters=%u best=%u lives=%u speed=%u", (unsigned)v.phase, v.meters, v.best, v.lives, v.speed);
    (void)app_transport_write(reply);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x + 1, y + 1, 26, 26, selected ? APP_COLOR_ACCENT : 0xDCE9F5, 5); /* snow */
    app_ui_rect(parent, x + 5, y + 14, 6, 9, 0x1F6F4A, 3);                                 /* tree */
    app_ui_rect(parent, x + 8, y + 3, 5, 5, 0xFFD0A6, 2);                                  /* head */
    app_ui_rect(parent, x + 7, y + 8, 7, 6, 0xE5484D, 2);                                  /* jacket */
    app_ui_rect(parent, x + 7, y + 14, 2, 8, 0x203040, 0);                                 /* skis */
    app_ui_rect(parent, x + 12, y + 14, 2, 8, 0x203040, 0);
    app_ui_rect(parent, x + 19, y + 11, 6, 12, 0x2E9E63, 3);                               /* second tree */
}
const app_descriptor_t esphook_app_ski = {.api_version = APP_API_VERSION, .id = "ski", .title = "滑雪冒险",
    .refresh_ms = 50, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
