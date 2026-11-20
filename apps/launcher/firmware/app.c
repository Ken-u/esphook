#include "esphook/app.h"
#include "esphook/app_ui.h"
#include <stdio.h>

/* The menu, laid out like an old iPod: a header with the title and the position, a list of four rows on the left
 * with a blue bar that slides between them and a thin scroll bar, and the selected app's icon on a plate on the right.
 * A short press moves the bar down (the list scrolls when the bar reaches an edge), a 500 ms hold opens the app, and
 * while it is held the bar fills from the left and a gold line grows under it. Nothing here is round, so nothing can
 * collide with the shape of the selection. Only plain rectangles are used (no arcs, shadows or masks), and only four
 * row slots exist however long the list is: they are re-bound, and slid by one row when the list scrolls. */
#define HEAD_H 16
#define ROW_H 16
#define ROWS 4
#define LIST_W 100
#define BAR_X 3
#define BAR_W (LIST_W - 2 * BAR_X)
#define BAR_H 14
#define SCROLL_X 102
#define SCROLL_Y (HEAD_H + 2)
#define SCROLL_H (APP_HEIGHT - HEAD_H - 4)
#define PANE_X 106
#define PANE_W (APP_WIDTH - PANE_X)
#define PLATE 32
#define PLATE_X (PANE_X + (PANE_W - PLATE) / 2)
#define PLATE_Y 18
#define SLIDE_MS 130
#define COLOR_TEXT 0xFFFFFF
#define COLOR_TEXT_DIM 0xA9B6DB
#define COLOR_BAR_TOP 0x5A98FF
#define COLOR_BAR_LOW 0x2C62D2
#define COLOR_RULE 0x1B2850
static const struct { app_menu_group_t group; const char *title; } groups[] = {
    {APP_MENU_GAMES, "游戏"}, {APP_MENU_DAILY, "日常"}, {APP_MENU_TOOLS, "工具"},
};
typedef struct {
    lv_obj_t *bar, *sweep, *edge, *rows[ROWS], *names[ROWS], *chevrons[ROWS], *title, *count, *track, *thumb, *plate, *icon;
    size_t top, seen, category, group_count, app_counts[sizeof groups / sizeof groups[0]];
    bool in_group;
    int bar_slot, sweep_px;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "launcher state too large");

static void set_y(void *object, int32_t value) { lv_obj_set_y(object, value); }
static void slide(lv_obj_t *object, int from, int to)
{
    lv_anim_delete(object, NULL);
    lv_obj_set_y(object, from);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, object); lv_anim_set_values(&a, from, to);
    lv_anim_set_exec_cb(&a, set_y); lv_anim_set_duration(&a, SLIDE_MS); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}
static lv_obj_t *gradient(lv_obj_t *parent, int x, int y, int w, int h, uint32_t top, uint32_t low, int radius)
{
    lv_obj_t *o = app_ui_rect(parent, x, y, w, h, top, radius);
    lv_obj_set_style_bg_grad_color(o, app_ui_color(low), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, LV_PART_MAIN);
    return o;
}
static lv_obj_t *text(lv_obj_t *parent, int x, int y, int w, uint32_t rgb)
{
    lv_obj_t *label = app_ui_label(parent, &aihook_font_12, x, y, w, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(label, app_ui_color(rgb), LV_PART_MAIN);
    return label;
}
static size_t group_at(const state_t *s, size_t index)
{
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; ++i) {
        if (s->app_counts[i] && index-- == 0) return i;
    }
    return 0;
}
static size_t item_count(const state_t *s)
{
    return s->in_group ? s->app_counts[group_at(s, s->category)] : s->group_count;
}
static const app_descriptor_t *item_app(app_context_t *ctx, const state_t *s, size_t index)
{
    const app_menu_group_t group = groups[group_at(s, s->category)].group;
    const app_descriptor_t *app;
    for (size_t i = 0; (app = app_at(ctx, i)); ++i)
        if (app->menu_group == group && index-- == 0) return app;
    return NULL;
}
static void reset_list(state_t *s)
{
    s->top = s->seen = (size_t)-1;
    s->bar_slot = -1;
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const app_descriptor_t *app;
    for (size_t i = 0; (app = app_at(ctx, i)); ++i) {
        for (size_t j = 0; j < sizeof groups / sizeof groups[0]; ++j) {
            if (app->menu_group == groups[j].group && s->app_counts[j]++ == 0) ++s->group_count;
        }
    }
    if (app_selection(ctx) >= s->group_count) app_select(ctx, 0);
    reset_list(s);
    s->sweep_px = -1;
    gradient(ctx->root, 0, 0, APP_WIDTH, APP_HEIGHT, 0x0E1730, 0x04070E, 0);
    /* the header: a lit strip with the title on the left and the position on the right */
    gradient(ctx->root, 0, 0, APP_WIDTH, HEAD_H - 1, 0x33436F, 0x182348, 0);
    app_ui_rect(ctx->root, 0, 0, APP_WIDTH, 1, 0x5A6CA0, 0);
    app_ui_rect(ctx->root, 0, HEAD_H - 1, APP_WIDTH, 1, 0x0A1224, 0);
    s->title = text(ctx->root, 6, 0, 70, COLOR_TEXT);
    s->count = text(ctx->root, APP_WIDTH - 46, 0, 40, COLOR_TEXT_DIM);
    lv_obj_set_style_text_align(s->count, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    /* the list: a window that clips four rows, the bar under their names */
    lv_obj_t *view = lv_obj_create(ctx->root); app_ui_plain(view);
    lv_obj_set_pos(view, 0, HEAD_H); lv_obj_set_size(view, LIST_W, ROWS * ROW_H);
    s->bar = gradient(view, BAR_X, 1, BAR_W, BAR_H, COLOR_BAR_TOP, COLOR_BAR_LOW, 4);
    s->sweep = app_ui_rect(s->bar, 0, 0, 0, BAR_H, 0xFFFFFF, 4);
    lv_obj_set_style_bg_opa(s->sweep, 72, LV_PART_MAIN);
    s->edge = app_ui_rect(s->bar, 0, BAR_H - 2, 0, 2, APP_COLOR_ACCENT, 0);
    for (unsigned i = 0; i < ROWS; ++i) {
        s->rows[i] = lv_obj_create(view); app_ui_plain(s->rows[i]);
        lv_obj_set_pos(s->rows[i], 0, (int)i * ROW_H); lv_obj_set_size(s->rows[i], LIST_W, ROW_H);
        s->names[i] = text(s->rows[i], 10, 0, BAR_W - 24, COLOR_TEXT_DIM);
        s->chevrons[i] = text(s->rows[i], LIST_W - 15, 0, 10, COLOR_TEXT_DIM);
        app_ui_text(s->chevrons[i], ">");
    }
    /* the scroll bar and the rule that separates the list from the preview */
    s->track = app_ui_rect(ctx->root, SCROLL_X, SCROLL_Y, 2, SCROLL_H, 0x16203F, 1);
    s->thumb = app_ui_rect(ctx->root, SCROLL_X, SCROLL_Y, 2, 6, 0x6F8CD6, 1);
    app_ui_rect(ctx->root, PANE_X - 2, HEAD_H, 1, APP_HEIGHT - HEAD_H, COLOR_RULE, 0);
    /* the preview: the selected app's own icon on a plate, and what a hold does */
    s->plate = gradient(ctx->root, PLATE_X, PLATE_Y, PLATE, PLATE, 0x2E4074, 0x182549, 9);
    lv_obj_set_style_border_width(s->plate, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s->plate, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s->plate, app_ui_color(0x4A5E9C), LV_PART_MAIN);
    lv_obj_t *gloss = app_ui_rect(s->plate, 5, 1, PLATE - 12, 1, 0xFFFFFF, 0); /* the lit top edge of the bevel */
    lv_obj_set_style_bg_opa(gloss, 46, LV_PART_MAIN);
    s->icon = lv_obj_create(ctx->root); app_ui_plain(s->icon);
    lv_obj_set_pos(s->icon, PLATE_X + 2, PLATE_Y + 2); lv_obj_set_size(s->icon, 28, 28);
    lv_obj_t *caption = text(ctx->root, PANE_X, 51, PANE_W, COLOR_TEXT_DIM);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    app_ui_text(caption, "长按进入");
    lv_obj_t *back_hint = text(ctx->root, PANE_X, 65, PANE_W, COLOR_TEXT_DIM);
    lv_obj_set_style_text_align(back_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    app_ui_text(back_hint, "双击返回");
}
/* The first app of the window that keeps `selected` in view with the least movement, and the whole list when it fits. */
static size_t window_top(size_t top, size_t selected, size_t total)
{
    if (total <= ROWS) return 0;
    if (top == (size_t)-1) return selected >= ROWS ? selected - ROWS + 1 : 0;
    if (selected < top) return selected;
    if (selected >= top + ROWS) return selected - ROWS + 1;
    return top;
}
static void draw_list(app_context_t *ctx, state_t *s, size_t selected, size_t total)
{
    const size_t top = window_top(s->top, selected, total);
    const int moved = s->top == (size_t)-1 ? 0 : top == s->top + 1 ? 1 : top + 1 == s->top ? -1 : 0;
    for (unsigned i = 0; i < ROWS; ++i) {
        const size_t index = top + i;
        const app_descriptor_t *a = s->in_group && index < total ? item_app(ctx, s, index) : NULL;
        const char *name = index >= total ? "" : s->in_group ? (a ? a->title : "") : groups[group_at(s, index)].title;
        const bool on = top + i == selected;
        app_ui_hide(s->rows[i], index >= total);
        app_ui_text(s->names[i], name);
        lv_obj_set_style_text_color(s->names[i], app_ui_color(on ? COLOR_TEXT : COLOR_TEXT_DIM), LV_PART_MAIN);
        lv_obj_set_style_text_color(s->chevrons[i], app_ui_color(on ? COLOR_TEXT : COLOR_TEXT_DIM), LV_PART_MAIN);
        if (moved) slide(s->rows[i], (int)i * ROW_H + moved * ROW_H, (int)i * ROW_H);
        else if (s->bar_slot < 0) { lv_anim_delete(s->rows[i], NULL); lv_obj_set_y(s->rows[i], (int)i * ROW_H); }
    }
    const int slot = (int)(selected - top);
    if (s->bar_slot < 0) { lv_anim_delete(s->bar, NULL); lv_obj_set_y(s->bar, slot * ROW_H + 1); }
    else if (slot != s->bar_slot) slide(s->bar, lv_obj_get_y(s->bar), slot * ROW_H + 1);
    s->bar_slot = slot; s->top = top;
    /* the scroll bar shows where the window is in the list; there is none when the list fits */
    const bool scrolls = total > ROWS;
    app_ui_hide(s->track, !scrolls); app_ui_hide(s->thumb, !scrolls);
    if (scrolls) {
        int th = SCROLL_H * ROWS / (int)total;
        if (th < 6) th = 6;
        lv_obj_set_height(s->thumb, th);
        lv_obj_set_y(s->thumb, SCROLL_Y + (SCROLL_H - th) * (int)top / (int)(total - ROWS));
    }
}
static void draw_preview(app_context_t *ctx, state_t *s, size_t selected, size_t total)
{
    const app_descriptor_t *a = s->in_group ? item_app(ctx, s, selected) : NULL;
    char position[24]; /* two 32-bit numbers and a slash fit even in theory */
    snprintf(position, sizeof position, "%u/%u", total ? (unsigned)selected + 1U : 0, (unsigned)total);
    app_ui_text(s->title, s->in_group ? groups[group_at(s, s->category)].title : "菜单");
    app_ui_text(s->count, position);
    lv_obj_clean(s->icon);
    if (a && a->icon) a->icon(s->icon, 0, 0, true);
    else if (a) app_ui_rect(s->icon, 4, 4, 20, 20, APP_COLOR_MUTED, 4);
    else if (total) {
        app_ui_rect(s->icon, 4, 5, 10, 5, APP_COLOR_ACCENT, 2);
        app_ui_rect(s->icon, 3, 9, 22, 15, APP_COLOR_ACCENT, 3);
    }
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const size_t selected = app_selection(ctx), total = item_count(s);
    app_ui_hide(s->bar, total == 0);
    if (s->seen != selected) {
        draw_list(ctx, s, selected, total);
        draw_preview(ctx, s, selected, total);
        s->seen = selected;
    }
    /* the confirmation: the bar fills from the left, a gold line grows under it, and the plate takes the gold edge */
    const unsigned hold = app_hold_ms(ctx);
    const int px = (int)((uint64_t)hold * BAR_W / APP_CONFIRM_MS);
    if (px != s->sweep_px) {
        s->sweep_px = px;
        app_ui_hide(s->sweep, px == 0); app_ui_hide(s->edge, px == 0);
        lv_obj_set_width(s->sweep, px); lv_obj_set_width(s->edge, px);
        lv_obj_set_style_border_color(s->plate, app_ui_color(px ? APP_COLOR_ACCENT : 0x4A5E9C), LV_PART_MAIN);
    }
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    const size_t total = item_count(s), selected = app_selection(ctx);
    if (!total) return;
    if (e->kind == APP_EVENT_SHORT) app_select(ctx, (selected + 1) % total);
    if (e->kind != APP_EVENT_CONFIRM) return;
    if (s->in_group) {
        const app_descriptor_t *app = item_app(ctx, s, selected);
        if (app) (void)app_open(ctx, app->id);
    } else {
        s->category = selected;
        s->in_group = true;
        app_select(ctx, 0);
        reset_list(s);
    }
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (s->in_group) app_select(ctx, s->category);
}
static bool back(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->in_group) return false;
    leave(ctx);
    s->in_group = false;
    reset_list(s);
    return true;
}
const app_descriptor_t esphook_app_launcher = {.api_version = APP_API_VERSION, .id = "launcher", .title = "菜单",
    .flags = APP_HIDDEN, .refresh_ms = 16, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave,
    .on_event = event, .render = render, .on_back = back};
