#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define REMOTE_GAME_MAX_BARS 128
#define REMOTE_GAME_WINDOW 48
#define REMOTE_GAME_JSON_MAX 1024
#define REMOTE_GAME_WATCHDOG_MS 5000

typedef struct { int32_t open, high, low, close, ma30, ma60; } chart_bar_t;
typedef enum { REMOTE_PLAY, REMOTE_RESULT, REMOTE_LOADING } remote_phase_t;
typedef struct {
    uint32_t id;
    unsigned count, first;
    char label[49], code[9], date[11];
    chart_bar_t bars[REMOTE_GAME_MAX_BARS];
    bool received[REMOTE_GAME_MAX_BARS];
} remote_dataset_t;
typedef struct {
    bool active;
    remote_dataset_t data, pending;
    remote_phase_t phase;
    unsigned cursor, held_ms, trades;
    int32_t pct_bps, baseline_bps, pnl_cents;
    bool holding;
    uint64_t updated_ms;
    char clock[6];
} remote_game_t;
typedef struct {
    bool active, offline, holding;
    uint32_t id;
    remote_phase_t phase;
    unsigned cursor, first, count, held_ms, trades;
    int32_t pct_bps, baseline_bps, pnl_cents;
    char label[49], code[9], date[11], clock[6];
    chart_bar_t bars[REMOTE_GAME_WINDOW];
} remote_game_view_t;
/* Atomic validated operations: begin/bars/show/heartbeat/close. */
bool remote_game_apply(remote_game_t *game, const char *json, size_t length,
                       uint64_t now_ms, int *sequence, char *error, size_t error_len);
void remote_game_snapshot(const remote_game_t *game, uint64_t now_ms, remote_game_view_t *view);
void stock_chart_draw(uint16_t *pixels, const remote_game_view_t *view);
