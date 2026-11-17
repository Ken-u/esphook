#include "fortune_core.h"
#include <string.h>

static const char *const k_category[FT_CATEGORIES] = {"事业", "人际", "家庭", "学业", "爱情"};
static const char *const k_level[FT_LEVELS] = {"大吉", "中吉", "小吉"};
static const char *const k_colour[FT_COLOURS] = {"红", "金", "绿", "蓝", "紫", "粉"};
static const uint32_t k_rgb[FT_COLOURS] = {0xE5484D, 0xFDB927, 0x3CB371, 0x3A86FF, 0x9B5DE5, 0xFF7EB6};
/* [area][grade][variant]: at most two lines of seven characters in the 12 px font. */
static const char *const k_text[FT_CATEGORIES][FT_LEVELS][FT_TEXTS] = {
    {{"贵人相助，步步高升", "机遇临门，一展宏图", "乘风破浪，所向披靡"},
     {"稳扎稳打，渐入佳境", "努力终有回报", "新的机会正在靠近"},
     {"按部就班也有惊喜", "小有进展，继续加油", "今日宜专心做事"}},
    {{"人缘爆棚，朋友相伴", "贵人现身，真心相待", "言谈得体，处处逢源"},
     {"与人为善，福气自来", "一句问候暖了人心", "合作顺利，沟通无碍"},
     {"多听少说，静待佳音", "微笑是最好的名片", "老友一条消息很惊喜"}},
    {{"阖家欢乐，平安喜乐", "家和万事兴，喜讯到", "团圆美满，笑声满屋"},
     {"温暖陪伴，幸福相随", "家人安康，心里踏实", "一顿好饭，一份欢喜"},
     {"回家路上有好事", "常联系，常问候", "小小惊喜藏在日常"}},
    {{"金榜题名，心想事成", "灵感涌现，一点就通", "过目不忘，考运亨通"},
     {"学有所得，渐入佳境", "专注就是好运气", "难题迎刃而解"},
     {"坚持一点就会进步", "今日宜复习巩固", "勤能补拙，日有所进"}},
    {{"良缘天赐，甜蜜满满", "心动有回应，缘分到", "有情人终成眷属"},
     {"温柔相待，细水长流", "默契加分，爱意渐浓", "真心换来真心"},
     {"一杯奶茶，一个笑容", "主动一点会有惊喜", "爱自己也是好运"}},
};

const char *ft_category_name(unsigned c) { return k_category[c % FT_CATEGORIES]; }
const char *ft_level_name(unsigned l) { return k_level[l % FT_LEVELS]; }
const char *ft_colour_name(unsigned c) { return k_colour[c % FT_COLOURS]; }
uint32_t ft_colour_rgb(unsigned c) { return k_rgb[c % FT_COLOURS]; }
const char *ft_text(unsigned category, unsigned level, unsigned text)
{
    return k_text[category % FT_CATEGORIES][level % FT_LEVELS][text % FT_TEXTS];
}

static uint32_t rnd(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}
ft_sign_t ft_pick(uint32_t *rng)
{
    ft_sign_t s;
    const unsigned grade = (unsigned)(rnd(rng) % 100U);
    s.category = (uint8_t)(rnd(rng) % FT_CATEGORIES);
    s.level = (uint8_t)(grade < 25U ? FT_BIG : grade < 65U ? FT_MID : FT_SMALL);
    s.text = (uint8_t)(rnd(rng) % FT_TEXTS);
    s.number = (uint8_t)(1U + rnd(rng) % 99U);
    s.colour = (uint8_t)(rnd(rng) % FT_COLOURS);
    return s;
}

void ft_init(ft_t *f, uint32_t seed)
{
    memset(f, 0, sizeof *f);
    f->rng = seed ? seed : 0x5EEDF00DU;
    for (unsigned i = 0; i < 4U; ++i) (void)rnd(&f->rng); /* a small seed (a clock) is not a small first draw */
    f->phase = FT_IDLE;
}
void ft_press(ft_t *f)
{
    if (f->phase != FT_IDLE && f->phase != FT_SLIP) return;
    f->sign = ft_pick(&f->rng);
    f->phase = FT_SHAKE;
    f->phase_ms = 0;
    f->held = 1;
    f->released = 0;
}
void ft_release(ft_t *f)
{
    f->held = 0;
    if (f->phase == FT_SHAKE) f->released = 1;
}
static void enter(ft_t *f, ft_phase_t phase)
{
    f->phase = (uint8_t)phase;
    f->phase_ms = 0;
}
void ft_advance(ft_t *f, uint32_t dt_ms)
{
    if (dt_ms > 200U) dt_ms = 200U; /* a long gap is a pause, not a backlog */
    f->phase_ms += dt_ms;
    switch (f->phase) {
    case FT_SHAKE:
        if ((f->released && f->phase_ms >= FT_SHAKE_MIN_MS) || f->phase_ms >= FT_SHAKE_MAX_MS) enter(f, FT_POP);
        break;
    case FT_POP:
        if (f->phase_ms >= FT_POP_MS) enter(f, FT_UNFOLD);
        break;
    case FT_UNFOLD:
        if (f->phase_ms >= FT_UNFOLD_MS) {
            enter(f, FT_SLIP);
            ++f->draws;
            ++f->counts[f->sign.level];
        }
        break;
    default:
        break;
    }
}
bool ft_slip_ready(const ft_t *f) { return f->phase == FT_UNFOLD || f->phase == FT_SLIP; }
