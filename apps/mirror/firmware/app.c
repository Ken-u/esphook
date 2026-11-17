#include "esphook/app.h"
#include "esphook/app_gfx.h"
#include "esphook/app_services.h"
#include "esphook/app_ui.h"
#include "freertos/FreeRTOS.h"
#include "mirror_codec.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 副屏: the screen shows what the PC sends, a picture of one of its windows (apps/mirror/pc, `run.py mirror`). The
 * 160 x 64 canvas is the picture and the 16 px band under it says what is going on. The PC sends the picture as
 * strips (mirror_codec.h) and the device answers every strip with a JSON `mirror_ack` that says whether it was taken,
 * so the PC never sends faster than the screen can show and a damaged strip is sent again.
 *
 * Threads: command() runs on the USB task. It only checks a line, decodes it into a free slot of the ring and
 * answers. The UI task is the only one that paints and the only one that touches LVGL: tick() empties the ring into
 * the canvas. The two share the ring, the counters in s_st and nothing else, all behind s_lock. A single key press
 * freezes the picture (the PC is told, and stops sending until it is let go again); three presses leave as always. */
#define BAND_Y 64
#define STALE_MS 3000U
#define HINT_SLOT_MS 2400U
#define PER_TICK 8U /* strips painted per tick: a whole picture, at most, so a slow frame never queues up */

typedef struct {
    lv_obj_t *plot, *band, *line, *msg, *title;
    lv_image_dsc_t image;
    uint64_t last_strip_ms, sample_ms;
    uint32_t shown_key, frames_at_sample;
    int16_t last_frame;
    uint16_t fps;
    uint8_t live, paused, bye, dirty;
} state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "mirror state too large");

/* What the USB task and the UI task share. */
typedef struct {
    bool active, paused, bye, claimed;
    uint32_t strips, frames, bad_args, bad_b64, bad_crc, busy, dropped;
    uint16_t fps;
    bool live;
    uint32_t age_ms;
} status_t;
static status_t s_st;
static mirror_ring_t s_ring;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint16_t c565(uint32_t rgb) { return gfx_rgb565(rgb); }

/* ---- the picture shown while nothing has come yet --------------------------------------------------------------- */
static void draw_wait(uint16_t *px, unsigned now)
{
    gfx_gradient(px, 0, APP_CANVAS_H, 0x0C1830, 0x050A14);
    for (int y = 3; y < APP_CANVAS_H; y += 8)
        for (int x = 3; x < APP_CANVAS_W; x += 8) gfx_plot(px, x, y, c565(0x14264A));
    const int mx = 56, my = 5, mw = 48, mh = 30;
    gfx_tint_ellipse(px, 80, my + mh + 7, 30, 3, 0x000000, 110);
    gfx_rect(px, mx, my, mw, mh, c565(0x3A4E78));
    gfx_rect(px, mx + 2, my + 2, mw - 4, mh - 4, c565(0x07101E));
    gfx_rect(px, 77, my + mh, 6, 4, c565(0x2A3C5E));
    gfx_rect(px, 66, my + mh + 4, 28, 2, c565(0x3A4E78));
    /* a picture being drawn on the monitor, row by row */
    const unsigned t = now / 90U;
    const int rows = mh - 4;
    const int fill = (int)(t % (unsigned)(rows + 8)) - 4;
    for (int y = 0; y < rows; ++y) {
        if (y > fill) break;
        const int w = (mw - 6) * (40 + (int)(gfx_hash((unsigned)y * 7U + 3U) % 60U)) / 100;
        gfx_hline(px, mx + 3, mx + 3 + w, my + 2 + y, c565(y % 6 < 3 ? 0x2F7FD6 : 0x1B4E8A));
    }
    if (fill >= 0 && fill < rows) gfx_hline(px, mx + 2, mx + mw - 3, my + 2 + fill, c565(0xBFE4FF));
    /* data on its way from the left */
    for (unsigned k = 0; k < 3U; ++k) {
        const int x = 8 + (int)((t * 2U + k * 11U) % 34U);
        gfx_rect(px, x, my + 13, 3, 2, c565(0x4FA8FF));
    }
}

/* ---- app -------------------------------------------------------------------------------------------------------- */
static void build(state_t *s, lv_obj_t *root)
{
    s->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    s->image.header.cf = LV_COLOR_FORMAT_RGB565;
    s->image.header.w = APP_CANVAS_W; s->image.header.h = APP_CANVAS_H; s->image.header.stride = APP_CANVAS_W * 2;
    s->image.data_size = APP_CANVAS_W * APP_CANVAS_H * 2; s->image.data = (const uint8_t *)app_ui_pixels();
    s->plot = lv_image_create(root);
    lv_obj_set_pos(s->plot, 0, 0);
    lv_image_set_src(s->plot, &s->image);
    s->band = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, APP_HEIGHT - BAND_Y, 0x0A1220, 0);
    s->line = app_ui_rect(root, 0, BAND_Y, APP_WIDTH, 1, 0x24406E, 0);
    s->msg = app_ui_label(s->band, &aihook_font_12, 4, 2, 152, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s->msg, app_ui_color(0xCFE0F4), LV_PART_MAIN);
    s->title = app_ui_label(root, &aihook_font_16, 0, 44, APP_WIDTH, 20, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s->title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s->title, app_ui_color(0xBFD8FF), LV_PART_MAIN);
    app_ui_text(s->title, "电脑副屏");
    s->shown_key = 0xFFFFFFFFU;
}
static void publish(const state_t *s, bool active)
{
    portENTER_CRITICAL(&s_lock);
    s_st.active = active; s_st.live = s->live != 0U; s_st.fps = s->fps; s_st.paused = s->paused != 0U;
    portEXIT_CRITICAL(&s_lock);
}
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    s->last_frame = -1;
    s->dirty = 1;
    s->sample_ms = ctx->now_ms;
    portENTER_CRITICAL(&s_lock);
    mirror_ring_reset(&s_ring);
    s_st = (status_t){0};
    s_st.active = true;
    portEXIT_CRITICAL(&s_lock);
    (void)app_host_event(ctx, "enter", 0, NULL);
}
static void leave(app_context_t *ctx)
{
    state_t *s = ctx->state;
    portENTER_CRITICAL(&s_lock); s_st.active = false; s_st.paused = false; portEXIT_CRITICAL(&s_lock);
    (void)app_host_event(ctx, "leave", 0, NULL);
    if (s->plot) app_ui_plot_refresh(s->plot, &s->image); /* The shared canvas goes to the next app. */
}
static void event(app_context_t *ctx, const app_event_t *e)
{
    state_t *s = ctx->state;
    if (e->kind != APP_EVENT_SHORT || e->key != 0U) return;
    s->paused = s->paused ? 0U : 1U;
    publish(s, true);
    (void)app_host_event(ctx, s->paused ? "pause" : "resume", 0, NULL);
}
static void tick(app_context_t *ctx)
{
    state_t *s = ctx->state;
    const uint64_t now = ctx->now_ms;
    bool bye;
    portENTER_CRITICAL(&s_lock); bye = s_st.bye; portEXIT_CRITICAL(&s_lock);
    if (bye && !s->bye && s->live) s->dirty = 1;
    s->bye = bye ? 1U : 0U;
    for (unsigned n = 0; n < PER_TICK; ++n) {
        const mirror_strip_t *slot;
        portENTER_CRITICAL(&s_lock); slot = mirror_ring_peek(&s_ring); portEXIT_CRITICAL(&s_lock);
        if (!slot) break;
        /* The slot is the USB task's again only after the release, so the UI task reads it without the lock. */
        if (!s->paused) {
            if (!s->live) { gfx_fill(app_ui_pixels(), 0); s->live = 1; }
            mirror_expand(slot, app_ui_pixels());
            if (slot->frame != s->last_frame) {
                s->last_frame = slot->frame;
                portENTER_CRITICAL(&s_lock); ++s_st.frames; portEXIT_CRITICAL(&s_lock);
            }
            s->dirty = 1;
            s->last_strip_ms = now;
        } else {
            portENTER_CRITICAL(&s_lock); ++s_st.dropped; portEXIT_CRITICAL(&s_lock);
        }
        portENTER_CRITICAL(&s_lock); mirror_ring_release(&s_ring); portEXIT_CRITICAL(&s_lock);
    }
    if (now - s->sample_ms >= 1000U) {
        uint32_t frames;
        portENTER_CRITICAL(&s_lock); frames = s_st.frames; portEXIT_CRITICAL(&s_lock);
        s->fps = (uint16_t)((frames - s->frames_at_sample) * 1000U / (unsigned)(now - s->sample_ms));
        s->frames_at_sample = frames;
        s->sample_ms = now;
    }
    portENTER_CRITICAL(&s_lock);
    s_st.age_ms = s->live ? (uint32_t)(now - s->last_strip_ms) : 0U;
    portEXIT_CRITICAL(&s_lock);
    publish(s, true);
}
static void sync_band(state_t *s, uint64_t now)
{
    enum { WAIT, LIVE, STALE, PAUSED, BYE } state = !s->live ? WAIT : s->paused ? PAUSED : s->bye ? BYE : now - s->last_strip_ms > STALE_MS ? STALE : LIVE;
    const unsigned slot = (unsigned)(now / HINT_SLOT_MS) & 1U;
    char text[40];
    switch (state) {
    case WAIT: snprintf(text, sizeof text, "%s", slot ? "电脑运行 run.py mirror" : "等待电脑画面"); break;
    case LIVE:
        if (slot) snprintf(text, sizeof text, "单击暂停 三击退出");
        else snprintf(text, sizeof text, "画面 %u 帧/秒", s->fps);
        break;
    case STALE: snprintf(text, sizeof text, "%s", slot ? "单击暂停 三击退出" : "画面中断 等待电脑"); break;
    case BYE: snprintf(text, sizeof text, "电脑已断开"); break;
    default: snprintf(text, sizeof text, "%s", slot ? "再按一下继续" : "已暂停"); break;
    }
    const uint32_t key = gfx_hash((uint32_t)state << 4 | slot << 8 | (state == LIVE ? (uint32_t)s->fps << 12 : 0U));
    if (key != s->shown_key) { s->shown_key = key; app_ui_text(s->msg, text); }
    app_ui_hide(s->title, s->live != 0U);
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (!s->plot) build(s, ctx->root);
    if (!s->live) { draw_wait(app_ui_pixels(), (unsigned)ctx->now_ms); s->dirty = 1; }
    if (s->dirty) { app_ui_plot_refresh(s->plot, &s->image); s->dirty = 0; }
    sync_band(s, ctx->now_ms);
}

/* ---- USB: validate, copy, answer -------------------------------------------------------------------------------- */
static void reply(bool ok, const char *err, int frame, int index, unsigned free_slots, bool paused)
{
    char out[160];
    int n = snprintf(out, sizeof out, "{\"v\":1,\"type\":\"mirror_ack\",\"ok\":%s", ok ? "true" : "false");
    if (err) n += snprintf(out + n, sizeof out - (size_t)n, ",\"err\":\"%s\"", err);
    if (frame >= 0) n += snprintf(out + n, sizeof out - (size_t)n, ",\"f\":%d,\"i\":%d", frame, index);
    snprintf(out + n, sizeof out - (size_t)n, ",\"free\":%u,\"p\":%u}", free_slots, paused ? 1U : 0U);
    (void)app_transport_write(out);
}
static bool strip_command(const char *line)
{
    bool active, paused, claimed;
    mirror_strip_t *slot = NULL;
    unsigned free_slots;
    portENTER_CRITICAL(&s_lock);
    active = s_st.active; paused = s_st.paused; claimed = s_st.claimed; free_slots = mirror_ring_free(&s_ring);
    if (active && !paused && !claimed) { slot = mirror_ring_reserve(&s_ring); if (slot) s_st.claimed = true; }
    portEXIT_CRITICAL(&s_lock);
    if (!active) { reply(false, "inactive", -1, 0, 0, false); return true; }
    /* A strip sent while the picture is frozen is taken and thrown away: the PC learns it from `p` and waits. */
    if (paused) { portENTER_CRITICAL(&s_lock); ++s_st.dropped; portEXIT_CRITICAL(&s_lock); reply(true, NULL, -1, 0, free_slots, true); return true; }
    if (!slot) {
        portENTER_CRITICAL(&s_lock); ++s_st.busy; portEXIT_CRITICAL(&s_lock);
        reply(false, "busy", -1, 0, free_slots, false);
        return true;
    }
    const mirror_status_t st = mirror_parse_strip(line, slot);
    const int frame = st == MIRROR_ERR_ARGS ? -1 : slot->frame, index = st == MIRROR_ERR_ARGS ? 0 : slot->index;
    portENTER_CRITICAL(&s_lock);
    s_st.claimed = false;
    if (st == MIRROR_OK && s_st.active) { mirror_ring_commit(&s_ring); ++s_st.strips; s_st.bye = false; }
    else if (st == MIRROR_ERR_CRC) ++s_st.bad_crc;
    else if (st == MIRROR_ERR_B64) ++s_st.bad_b64;
    else ++s_st.bad_args;
    free_slots = mirror_ring_free(&s_ring);
    portEXIT_CRITICAL(&s_lock);
    reply(st == MIRROR_OK, st == MIRROR_OK ? NULL : mirror_status_name(st), frame, index, free_slots, false);
    return true;
}
static bool command(const char *line)
{
    if (!strcmp(line, "robot mirror status")) {
        status_t v;
        portENTER_CRITICAL(&s_lock); v = s_st; portEXIT_CRITICAL(&s_lock);
        char out[200];
        snprintf(out, sizeof out, "active=%u live=%u paused=%u fps=%u frames=%lu strips=%lu args=%lu b64=%lu crc=%lu busy=%lu dropped=%lu age=%lu",
                 v.active ? 1U : 0U, v.live ? 1U : 0U, v.paused ? 1U : 0U, v.fps, (unsigned long)v.frames, (unsigned long)v.strips,
                 (unsigned long)v.bad_args, (unsigned long)v.bad_b64, (unsigned long)v.bad_crc, (unsigned long)v.busy,
                 (unsigned long)v.dropped, (unsigned long)v.age_ms);
        (void)app_transport_write(out);
        return true;
    }
    if (strncmp(line, "mirror ", 7)) return false;
    if (!strncmp(line, "mirror s ", 9)) return strip_command(line);
    bool active, paused;
    unsigned free_slots;
    if (!strcmp(line, "mirror bye")) {
        portENTER_CRITICAL(&s_lock);
        active = s_st.active; paused = s_st.paused; free_slots = mirror_ring_free(&s_ring);
        if (active) s_st.bye = true;
        portEXIT_CRITICAL(&s_lock);
        reply(active, active ? NULL : "inactive", -1, 0, free_slots, paused);
        return true;
    }
    if (!strcmp(line, "mirror hello")) {
        portENTER_CRITICAL(&s_lock);
        active = s_st.active; paused = s_st.paused; free_slots = mirror_ring_free(&s_ring);
        portEXIT_CRITICAL(&s_lock);
        char out[160];
        snprintf(out, sizeof out, "{\"v\":1,\"type\":\"mirror_ack\",\"op\":\"hello\",\"ok\":%s%s,\"w\":%u,\"h\":%u,\"rows\":%u,\"free\":%u,\"p\":%u}",
                 active ? "true" : "false", active ? "" : ",\"err\":\"inactive\"", MIRROR_W, MIRROR_H, MIRROR_ROWS, free_slots, paused ? 1U : 0U);
        (void)app_transport_write(out);
        return true;
    }
    reply(false, "args", -1, 0, 0, false);
    return true;
}
static void icon(void *parent, int x, int y, bool selected)
{
    app_ui_rect(parent, x + 1, y + 2, 26, 18, selected ? 0x6A86B8 : 0x4A5E86, 2);           /* the monitor */
    app_ui_rect(parent, x + 3, y + 4, 22, 14, 0x0A1424, 1);
    app_ui_rect(parent, x + 4, y + 5, 20, 3, 0x2F7FD6, 0);                                 /* a window on it */
    app_ui_rect(parent, x + 5, y + 10, 9, 2, 0x8FB8F0, 0);
    app_ui_rect(parent, x + 5, y + 13, 14, 2, 0x4A78B8, 0);
    app_ui_rect(parent, x + 16, y + 10, 6, 5, 0xFDB927, 0);
    app_ui_rect(parent, x + 12, y + 20, 4, 3, 0x4A5E86, 0);                                /* the stand */
    app_ui_rect(parent, x + 7, y + 23, 14, 2, 0x4A5E86, 1);
}
const app_descriptor_t esphook_app_mirror = {.api_version = APP_API_VERSION, .id = "mirror", .title = "副屏",
    .refresh_ms = 40, .state_size = sizeof(state_t), .on_enter = enter, .on_leave = leave, .on_event = event,
    .on_tick = tick, .render = render, .icon = icon, .command = command};
