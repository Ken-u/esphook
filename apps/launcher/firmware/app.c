#include "esphook/app.h"
#include "esphook/app_ui.h"

/* The menu: a dark gradient screen, three beveled tiles with the apps' own icons, a lit cursor that slides to the
 * selected tile (the half-second confirm ring closes around it), titles below and page pips at the top right.
 * Only plain rectangles and one arc are used, so no shadow or mask buffers are needed on the LVGL heap. */
#define TILE 40
#define TILE_Y 17
#define TILE_X0 8
#define TILE_STEP 52
#define ICON_PAD 6
#define CURSOR_SIZE 50
#define MAX_PIPS 8
#define COLOR_TILE_TOP 0x1C2745
#define COLOR_TILE_LOW 0x0F1730
#define COLOR_TILE_ON_TOP 0x2E4074
#define COLOR_TILE_ON_LOW 0x182549
#define COLOR_EDGE 0x33437A
#define COLOR_TEXT_DIM 0x8E9BC2
typedef struct {
    lv_obj_t *plates[3], *icons[3], *titles[3], *cursor, *ring, *pips[MAX_PIPS];
    size_t page_index, previous;
    unsigned pages;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "launcher state too large");

static void arc_x(void *object, int32_t value) { lv_obj_set_x(object, value); }
static int tile_x(unsigned slot) { return TILE_X0 + (int)slot * TILE_STEP; }
static lv_obj_t *gradient(lv_obj_t *parent, int x, int y, int w, int h, uint32_t top, uint32_t low, int radius)
{
    lv_obj_t *o = app_ui_rect(parent, x, y, w, h, top, radius);
    lv_obj_set_style_bg_grad_color(o, app_ui_color(low), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, LV_PART_MAIN);
    return o;
}
static void colour_plate(lv_obj_t *plate, bool selected)
{
    lv_obj_set_style_bg_color(plate, app_ui_color(selected ? COLOR_TILE_ON_TOP : COLOR_TILE_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(plate, app_ui_color(selected ? COLOR_TILE_ON_LOW : COLOR_TILE_LOW), LV_PART_MAIN);
    lv_obj_set_style_border_color(plate, app_ui_color(selected ? 0x4A5E9C : COLOR_EDGE), LV_PART_MAIN);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    s->previous = (size_t)-1; s->page_index = (size_t)-1;
    s->pages = (unsigned)((app_count(ctx) + 2) / 3);
    if (s->pages > MAX_PIPS) s->pages = MAX_PIPS;
    gradient(ctx->root, 0, 0, APP_WIDTH, APP_HEIGHT, 0x0E1730, 0x04070E, 0);
    app_ui_rect(ctx->root, 0, 13, APP_WIDTH, 1, 0x1B2850, 0); /* the top bar's rule */
    app_ui_rect(ctx->root, 0, 14, APP_WIDTH, 1, 0x0A1224, 0);
    lv_obj_t *hint = app_ui_label(ctx->root, &aihook_font_12, 5, 0, 104, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(hint, app_ui_color(COLOR_TEXT_DIM), LV_PART_MAIN);
    app_ui_text(hint, "短按切换 长按进入");
    for (unsigned i = 0; i < MAX_PIPS; ++i) {
        s->pips[i] = app_ui_rect(ctx->root, 0, 5, 4, 4, 0x2C3A68, 2);
        app_ui_hide(s->pips[i], i >= s->pages);
    }
    for (unsigned i = 0; i < 3; ++i) {
        const int x = tile_x(i);
        s->plates[i] = gradient(ctx->root, x, TILE_Y, TILE, TILE, COLOR_TILE_TOP, COLOR_TILE_LOW, 9);
        lv_obj_set_style_border_width(s->plates[i], 1, LV_PART_MAIN);
        lv_obj_set_style_border_opa(s->plates[i], LV_OPA_COVER, LV_PART_MAIN);
        colour_plate(s->plates[i], false);
        lv_obj_t *gloss = app_ui_rect(s->plates[i], 5, 1, TILE - 12, 1, 0xFFFFFF, 0); /* the lit top edge of the bevel */
        lv_obj_set_style_bg_opa(gloss, 46, LV_PART_MAIN);
        s->icons[i] = lv_obj_create(ctx->root); app_ui_plain(s->icons[i]);
        lv_obj_set_pos(s->icons[i], x + ICON_PAD, TILE_Y + ICON_PAD); lv_obj_set_size(s->icons[i], 28, 28);
        s->titles[i] = app_ui_label(ctx->root, &aihook_font_12, x - 6, 63, TILE + 12, 15, LV_LABEL_LONG_MODE_CLIP);
        app_ui_center(s->titles[i]);
    }
    /* The cursor is one object that slides: a soft glow, a bright frame and the confirm ring around them. */
    s->cursor = lv_obj_create(ctx->root); app_ui_plain(s->cursor);
    lv_obj_set_size(s->cursor, CURSOR_SIZE, CURSOR_SIZE); lv_obj_set_pos(s->cursor, tile_x(0) - 5, TILE_Y - 5);
    lv_obj_t *glow = app_ui_rect(s->cursor, 1, 1, CURSOR_SIZE - 2, CURSOR_SIZE - 2, APP_COLOR_ACCENT, 12);
    lv_obj_set_style_bg_opa(glow, 40, LV_PART_MAIN);
    lv_obj_t *frame = app_ui_rect(s->cursor, 3, 3, CURSOR_SIZE - 6, CURSOR_SIZE - 6, APP_COLOR_ACCENT, 10);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(frame, 2, LV_PART_MAIN); lv_obj_set_style_border_opa(frame, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(frame, app_ui_color(APP_COLOR_ACCENT), LV_PART_MAIN);
    s->ring = app_ui_ring(s->cursor, 0, 0, CURSOR_SIZE);
    lv_obj_set_style_arc_color(s->ring, app_ui_color(0x2A3868), LV_PART_MAIN);
    app_ui_hide(s->ring, true);
}
static void layout_pips(state_t *s, size_t page)
{
    unsigned total = (s->pages ? s->pages - 1U : 0U) * 7U + 10U;
    int x = APP_WIDTH - 5 - (int)total;
    for (unsigned i = 0; i < s->pages; ++i) {
        const bool on = i == page;
        lv_obj_set_pos(s->pips[i], x, 5); lv_obj_set_size(s->pips[i], on ? 10 : 4, 4);
        lv_obj_set_style_bg_color(s->pips[i], app_ui_color(on ? APP_COLOR_ACCENT : 0x2C3A68), LV_PART_MAIN);
        x += (on ? 10 : 4) + 3;
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const size_t selected = app_selection(ctx), page = selected / 3;
    if (s->previous != selected) {
        for (unsigned i = 0; i < 3; ++i) {
            const app_descriptor_t *a = app_at(ctx, page * 3 + i);
            const bool on = page * 3 + i == selected;
            lv_obj_clean(s->icons[i]);
            app_ui_hide(s->plates[i], !a); app_ui_hide(s->icons[i], !a); app_ui_hide(s->titles[i], !a);
            app_ui_text(s->titles[i], a ? a->title : "");
            if (a && a->icon) a->icon(s->icons[i], 0, 0, on);
            else if (a) app_ui_rect(s->icons[i], 4, 4, 20, 20, APP_COLOR_MUTED, 4);
            colour_plate(s->plates[i], on);
            lv_obj_set_style_text_color(s->titles[i], app_ui_color(on ? APP_COLOR_ACCENT : COLOR_TEXT_DIM), LV_PART_MAIN);
        }
        const int target = tile_x((unsigned)(selected % 3)) - 5;
        lv_anim_delete(s->cursor, NULL);
        if (s->page_index != page || s->previous == (size_t)-1) lv_obj_set_x(s->cursor, target);
        else {
            lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, s->cursor); lv_anim_set_values(&a, lv_obj_get_x(s->cursor), target);
            lv_anim_set_exec_cb(&a, arc_x); lv_anim_set_duration(&a, 160); lv_anim_set_path_cb(&a, lv_anim_path_ease_out); lv_anim_start(&a);
        }
        if (s->page_index != page) layout_pips(s, page);
        s->page_index = page; s->previous = selected;
    }
    const unsigned hold = app_hold_ms(ctx);
    app_ui_hide(s->ring, hold == 0);
    app_ui_progress(s->ring, hold);
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    if (e->kind == APP_EVENT_SHORT) app_select_next(ctx);
    if (e->kind == APP_EVENT_CONFIRM) { const app_descriptor_t *a = app_at(ctx, app_selection(ctx)); if (a) (void)app_open(ctx, a->id); }
}
const app_descriptor_t esphook_app_launcher = {.api_version = APP_API_VERSION, .id = "launcher", .title = "菜单",
    .flags = APP_HIDDEN, .refresh_ms = 16, .state_size = sizeof(state_t), .on_enter = enter, .on_event = event, .render = render};
