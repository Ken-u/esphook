#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 今日幸运签: every slip is a lucky one. A draw picks one of five areas of life and a grade (大吉 / 中吉 / 小吉), a
 * blessing for that pair, a lucky number and a lucky colour. Pure C and driven by timestamps, so the tests can run
 * the whole ceremony (shaking the tube, the stick that jumps out, the slip that unfolds) in a loop. */
#define FT_CATEGORIES 5U
#define FT_LEVELS 3U
#define FT_TEXTS 3U         /* blessings per area and grade */
#define FT_COLOURS 6U
#define FT_SHAKE_MIN_MS 1200U
#define FT_SHAKE_MAX_MS 4000U /* a key held down does not shake for ever */
#define FT_POP_MS 800U
#define FT_UNFOLD_MS 500U

typedef enum { FT_CAREER, FT_PEOPLE, FT_FAMILY, FT_STUDY, FT_LOVE } ft_category_t; /* 事业 人际 家庭 学业 爱情 */
typedef enum { FT_BIG, FT_MID, FT_SMALL } ft_level_t;                                 /* 大吉 中吉 小吉 */
typedef enum { FT_IDLE, FT_SHAKE, FT_POP, FT_UNFOLD, FT_SLIP } ft_phase_t;

typedef struct { uint8_t category, level, text, number, colour; } ft_sign_t;
typedef struct {
    uint32_t rng, phase_ms, draws, counts[FT_LEVELS];
    ft_sign_t sign; /* the slip of the current or the last draw; only meaningful once draws > 0 */
    uint8_t phase, held, released;
} ft_t;

const char *ft_category_name(unsigned category); /* "事业" */
const char *ft_level_name(unsigned level);       /* "大吉" */
const char *ft_colour_name(unsigned colour);     /* "金" */
uint32_t ft_colour_rgb(unsigned colour);
const char *ft_text(unsigned category, unsigned level, unsigned text);

void ft_init(ft_t *f, uint32_t seed);
/* Picks one slip from the generator, 25 / 40 / 35 percent for big / middle / small luck. */
ft_sign_t ft_pick(uint32_t *rng);
/* The key goes down: the tube starts to shake (from the idle screen or from a slip that is showing). The slip is
 * chosen at this moment. Ignored while a draw is under way. */
void ft_press(ft_t *f);
/* The key is up (or the press was cancelled): the shake ends once it has lasted its minimum. */
void ft_release(ft_t *f);
void ft_advance(ft_t *f, uint32_t dt_ms);
/* True from the moment the stick leaves the tube: what the text of the slip may show. */
bool ft_slip_ready(const ft_t *f);
