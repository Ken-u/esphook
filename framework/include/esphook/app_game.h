#pragma once
/* The plumbing the first-person arcade games (racing, starship, zombie) have in common, so a new one only writes its
 * rules, its picture and its words.
 *
 * What a game gets:
 *   - one game_t in its state (after the core): the radar/key steering, the servo feel, the status strip widgets and
 *     the caches that keep them from being rewritten every frame;
 *   - input: game_steer() reads the radar and K2/K3 once per tick, game_side() tells those keys from the main key,
 *     game_may_start() / game_confirm() keep a press that began in the game from starting one or arming the servo;
 *   - the way out: game_sync_exit() opens the two second hold only on the title and game over screens (see
 *     app_set_exit_hold), game_hud_msg() announces it every other GAME_HINT_SLOT_MS, game_hud_servo() draws its progress;
 *   - the status strip and the title/game over card: game_build() makes the widgets, game_hud_* fill them.
 *
 * Everything is header-only and runs on the UI task like the app itself. The layout of the strip is the game's own
 * (colours and x positions go in a game_style_t); the rules, the message texts and the picture never come from here. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "app.h"
#include "app_haptic.h"
#include "app_services.h"
#include "app_steer.h"
#include "app_ui.h"

#define GAME_EXIT_HOLD_MS 2000U    /* hold the key this long on the title or game over screen to leave */
#define GAME_HINT_SLOT_MS 2600U    /* the strip alternates between the hint and the way out every this long */
#define GAME_OVER_SETTLE_MS 800U   /* the game over card ignores the key for this long: a mashed key must not skip it */
#define GAME_BAND_Y 64             /* the status strip starts under the canvas */
#define GAME_UNSET 0xFFFFFFFFU

typedef struct {
    steer_t steer;
    haptic_t haptic;
    lv_obj_t *plot, *band, *hold, *msg, *score, *tag, *title, *sub; /* plot stays NULL until game_build() ran */
    uint64_t last_ms;
    uint32_t shown_key, shown_score, shown_card;
    uint8_t down_in_play; /* the press that is down began while the game was running */
} game_t;

typedef struct {
    uint32_t band_bg, band_rule;                    /* the strip under the canvas */
    uint32_t msg_rgb, score_rgb, title_rgb, sub_rgb;
    int msg_w, score_x, score_w;                    /* the message on the left, the score on the right of the strip */
    bool score_right;                               /* right-align the score */
    int tag_x, tag_y, title_y, sub_y;               /* the "servo" tag over the picture and the card's two lines */
} game_style_t;

/* ---- lifecycle ---------------------------------------------------------------------------------------------- */
/* In on_enter, after the game core was initialised. Games are played by mashing a key, so three quick presses are never
 * an exit; the hold on the title and game over screens is. */
static inline void game_enter(app_context_t *ctx, game_t *g)
{
    steer_init(&g->steer);
    haptic_init(&g->haptic);
    g->last_ms = ctx->now_ms;
    g->down_in_play = 0;
    app_set_triple_exit(ctx, false);
}
/* In on_leave. The framework disarms the servo it leased to the game; the shared canvas goes to the next app. */
static inline void game_leave(game_t *g)
{
    haptic_stop(&g->haptic);
    if (g->plot) app_ui_canvas_refresh(g->plot);
}
/* Milliseconds since the previous call (0 on the first), for the core's advance function. */
static inline uint32_t game_dt(game_t *g, uint64_t now)
{
    const uint32_t dt = now > g->last_ms ? (uint32_t)(now - g->last_ms) : 0U;
    g->last_ms = now;
    return dt;
}

/* ---- input -------------------------------------------------------------------------------------------------- */
/* K2 steers left and K3 right; K1 is the main key (pedal, trigger, "start"). */
static inline bool game_side(const app_event_t *e) { return e->key == 1U || e->key == 2U; }
/* This frame's steering, -STEER_MAX..STEER_MAX: the radar when it sees someone, otherwise the K2/K3 keys. */
static inline int game_steer(app_context_t *ctx, game_t *g)
{
    app_radar_t radar;
    app_radar_read(&radar);
    if (radar.available && radar.online) steer_radar(&g->steer, ctx->now_ms, true, radar.frame.targets);
    return steer_tick(&g->steer, ctx->now_ms);
}
/* The title screen, or the game over screen once it has settled. */
static inline bool game_idle(bool title, bool over, uint32_t over_age_ms)
{
    return title || (over && over_age_ms > GAME_OVER_SETTLE_MS);
}
/* A short press on the main key starts a game only from a quiet screen, and only if it began there: a trigger that was
 * held through the fight must not start the next one the moment it lets go. */
static inline bool game_may_start(const game_t *g, bool side, bool idle) { return !side && !g->down_in_play && idle; }
/* The 500 ms confirmation: arms the servo, but only from the title or game over screen and only for a press that began
 * there. Nothing else in a game ever arms it. */
static inline void game_confirm(app_context_t *ctx, game_t *g, bool side, bool title_or_over)
{
    if (!side && !g->down_in_play && title_or_over) (void)haptic_arm(ctx, &g->haptic);
}
/* The way out is a two second hold, open only while the screen is quiet. */
static inline void game_sync_exit(app_context_t *ctx, bool idle)
{
    app_set_exit_hold(ctx, idle ? GAME_EXIT_HOLD_MS : 0U);
}
/* A shake of the servo (a hit, a crash) when the player has enabled it; silent otherwise. */
static inline void game_shake(app_context_t *ctx, game_t *g, int amp_deg, unsigned half_ms, unsigned duration_ms)
{
    haptic_cue(ctx, &g->haptic, amp_deg, half_ms, duration_ms);
}

/* ---- status strip and card ----------------------------------------------------------------------------------- */
/* Builds the canvas, the strip with its message and score, the exit hold line, the "servo" tag and the card (a title
 * and a line under it, hidden until game_hud_card()). Call it once from render() while g->plot is still NULL. */
static inline void game_build(game_t *g, lv_obj_t *root, const game_style_t *st)
{
    g->plot = app_ui_canvas(root, 0, 0, APP_CANVAS_W, APP_CANVAS_H);
    g->band = app_ui_band(root, st->band_bg, st->band_rule);
    g->msg = app_ui_label(g->band, &aihook_font_12, 3, 2, st->msg_w, 14, LV_LABEL_LONG_MODE_CLIP);
    g->score = app_ui_label(g->band, &aihook_font_12, st->score_x, 2, st->score_w, 14, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(g->msg, app_ui_color(st->msg_rgb), LV_PART_MAIN);
    lv_obj_set_style_text_color(g->score, app_ui_color(st->score_rgb), LV_PART_MAIN);
    if (st->score_right) lv_obj_set_style_text_align(g->score, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    g->hold = app_ui_hold_bar(root, GAME_BAND_Y, APP_COLOR_ACCENT);
    app_ui_hide(g->hold, true);
    g->tag = app_ui_rect(root, st->tag_x, st->tag_y, 29, 14, 0x2E8B57, 5);
    lv_obj_t *tag_text = app_ui_label(g->tag, &aihook_font_12, 0, 1, 29, 12, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(tag_text);
    app_ui_text(tag_text, "舵机");
    g->title = app_ui_label(root, &aihook_font_16, 14, st->title_y, 132, 20, LV_LABEL_LONG_MODE_CLIP);
    g->sub = app_ui_label(root, &aihook_font_12, 14, st->sub_y, 132, 14, LV_LABEL_LONG_MODE_CLIP);
    app_ui_center(g->title);
    app_ui_center(g->sub);
    lv_obj_set_style_text_color(g->title, app_ui_color(st->title_rgb), LV_PART_MAIN);
    lv_obj_set_style_text_color(g->sub, app_ui_color(st->sub_rgb), LV_PART_MAIN);
    app_ui_hide(g->tag, true);
    app_ui_hide(g->title, true);
    app_ui_hide(g->sub, true);
    g->shown_key = g->shown_score = g->shown_card = GAME_UNSET;
}
/* The message in the strip. `key` is any number that changes whenever `msg` does (the label is only rewritten then).
 * While `idle` the strip alternates with "按住2秒退出", the way out. */
static inline void game_hud_msg(game_t *g, const char *msg, uint32_t key, bool idle, uint64_t now)
{
    const bool exit_slot = idle && (now / GAME_HINT_SLOT_MS) % 2U == 1U;
    const uint32_t shown = key << 1 | (exit_slot ? 1U : 0U);
    if (shown == g->shown_key) return;
    app_ui_text(g->msg, exit_slot ? "按住2秒退出" : msg);
    g->shown_key = shown;
}
static inline void game_hud_score(game_t *g, uint32_t score)
{
    if (g->shown_score == score) return;
    char text[16];
    snprintf(text, sizeof text, "%lu", (unsigned long)score);
    app_ui_text(g->score, text);
    g->shown_score = score;
}
/* The progress of the exit hold, and the "servo" tag while the servo is enabled. */
static inline void game_hud_servo(game_t *g, unsigned hold_ms)
{
    const haptic_state_t hs = haptic_state();
    app_ui_hold_line(g->hold, hold_ms, hs == HAPTIC_READY, GAME_EXIT_HOLD_MS);
    app_ui_hide(g->tag, hs != HAPTIC_ARMED);
}
/* Shows or hides the card. Returns true when the card is up and `key` (a number that changes with what it says) is new:
 * the caller then writes the two lines with game_hud_card_text(). */
static inline bool game_hud_card(game_t *g, bool show, uint32_t key)
{
    app_ui_hide(g->title, !show);
    app_ui_hide(g->sub, !show);
    if (!show || g->shown_card == key) return false;
    g->shown_card = key;
    return true;
}
static inline void game_hud_card_text(game_t *g, const char *title, const char *sub)
{
    app_ui_text(g->title, title);
    app_ui_text(g->sub, sub);
}
