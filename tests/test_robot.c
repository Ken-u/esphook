#include "ld2450.h"
#include "robot_follow.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void put_unsigned(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void put_signed(uint8_t *p, int n)
{
    put_unsigned(p, (uint16_t)(n < 0 ? -n : n) | (n >= 0 ? 0x8000 : 0));
}
static void packet(uint8_t *p, int x, int y, int speed)
{
    memset(p, 0, 30);
    memcpy(p, (uint8_t[]){0xaa, 0xff, 3, 0}, 4);
    put_signed(p + 4, x); put_signed(p + 6, y); put_signed(p + 8, speed);
    put_unsigned(p + 10, 320);
    p[28] = 0x55; p[29] = 0xcc;
}
static int feed(ld2450_parser_t *p, const uint8_t *b, size_t n, ld2450_frame_t *f)
{
    int count = 0;
    for (size_t i = 0; i < n; ++i) count += ld2450_feed(p, b[i], f);
    return count;
}
static ld2450_frame_t frame(int x, int y)
{
    ld2450_frame_t f = {0};
    f.targets[0] = (ld2450_target_t){.x_mm = x, .y_mm = y, .valid = true};
    return f;
}
static void parser_tests(void)
{
    ld2450_parser_t parser = {0};
    ld2450_frame_t f;
    uint8_t p[30];
    packet(p, -500, 1200, -17);
    /* Arbitrary chunk boundaries and a partial/noisy header. */
    assert(feed(&parser, (uint8_t[]){0x12, 0xaa, 0xaa, 0xff, 0x11}, 5, &f) == 0);
    assert(feed(&parser, p, 17, &f) == 0);
    assert(feed(&parser, p + 17, 13, &f) == 1);
    assert(f.targets[0].x_mm == -500 && f.targets[0].y_mm == 1200);
    assert(f.targets[0].speed_cms == -17 && f.targets[0].resolution_mm == 320);
    assert(f.targets[0].valid && !f.targets[1].valid);
    packet(p, 500, 1200, 0);
    assert(feed(&parser, p, 30, &f) == 1);
    assert(f.targets[0].valid && f.targets[0].speed_cms == 0);
    assert(f.targets[0].x_mm == 500);
    /* Fixed frame length: a footer inside the payload is not the end. */
    p[12] = 0x55; p[13] = 0xcc;
    assert(feed(&parser, p, 30, &f) == 1);
    p[28] = 0;
    assert(feed(&parser, p, 30, &f) == 0);
    p[28] = 0x55;
    assert(feed(&parser, p, 30, &f) == 1);
    assert(parser.rejected == 1);
    /* Dropped bytes, then back-to-back good reports. */
    assert(feed(&parser, p, 12, &f) == 0);
    (void)feed(&parser, p, 30, &f);
    assert(feed(&parser, p, 30, &f) >= 1);
    assert(feed(&parser, p, 30, &f) == 1);
    /* Deterministic byte noise must never overflow the parser. */
    uint32_t seed = 7;
    for (int i = 0; i < 100000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        (void)ld2450_feed(&parser, seed >> 24, &f);
        assert(parser.used < LD2450_FRAME_SIZE);
    }
    (void)feed(&parser, p, 30, &f);
    assert(feed(&parser, p, 30, &f) == 1);
}
static void acquire(robot_follow_t *f, ld2450_frame_t *v, uint64_t start)
{
    for (int i = 0; i < 3; ++i) {
        robot_follow_frame(f, v, start + i * 100);
        robot_follow_tick(f, start + i * 100);
    }
    assert(f->state == ROBOT_TRACKING && f->locked);
}
static void follow_tests(void)
{
    robot_config_t c = robot_default_config();
    robot_follow_t f;
    robot_follow_init(&f, &c, 0);
    robot_follow_tick(&f, 801);
    assert(f.state == ROBOT_FAULT && f.goal_deg == 0);
    robot_follow_init(&f, &c, 0);
    ld2450_frame_t v = frame(1000, 1000);
    robot_follow_frame(&f, &v, 0);
    assert(!f.locked && f.state == ROBOT_ACQUIRING);
    robot_follow_frame(&f, &v, 100);
    assert(!f.locked);
    robot_follow_frame(&f, &v, 200);
    robot_follow_tick(&f, 200);
    assert(f.locked && f.state == ROBOT_TRACKING);
    assert(f.goal_deg == c.limit_deg);
    assert(f.angle_deg <= 3.51f); /* elapsed tick is capped to 100ms */
    /* Slot order changes and a closer bystander must preserve spatial lock. */
    v.targets[2] = v.targets[0];
    v.targets[0] = (ld2450_target_t){.x_mm = -100, .y_mm = 300, .valid = true};
    robot_follow_frame(&f, &v, 300);
    assert(f.x_mm > 900 && f.state == ROBOT_TRACKING);
    ld2450_frame_t empty = {0};
    robot_follow_frame(&f, &empty, 400);
    assert(f.state == ROBOT_LOST && f.goal_deg > 0);
    /* A valid empty report means lost, not a communication fault. */
    for (int t = 500; t <= 1700; t += 100) {
        robot_follow_frame(&f, &empty, t);
        robot_follow_tick(&f, t);
    }
    assert(f.state == ROBOT_LOST);
    robot_follow_frame(&f, &empty, 1801);
    robot_follow_tick(&f, 1801);
    assert(f.state == ROBOT_IDLE && !f.locked && f.goal_deg == 0);
    float old = f.angle_deg;
    robot_follow_tick(&f, 10000);
    assert(f.state == ROBOT_FAULT && f.angle_deg >= old - 3.51f);
    /* Recovered data cannot revive an association from a long outage. */
    v = frame(-1000, 1000);
    robot_follow_frame(&f, &v, 10100);
    assert(!f.locked && f.candidate_hits == 1);
    robot_follow_frame(&f, &v, 11000);
    assert(!f.locked && f.candidate_hits == 1);
    acquire(&f, &v, 11100);
    assert(f.goal_deg < 0);
    /* Behind, too near, too far, outside radar FOV: reject all. */
    int xy[][2] = {{0, -1000}, {0, 100}, {0, 5000}, {2000, 500}};
    for (size_t i = 0; i < sizeof xy / sizeof xy[0]; ++i) {
        robot_follow_init(&f, &c, 0);
        v = frame(xy[i][0], xy[i][1]);
        for (int j = 0; j < 5; ++j) robot_follow_frame(&f, &v, j * 100);
        assert(!f.locked && f.state == ROBOT_IDLE);
    }
    /* One-frame detections interleaved with empty frames never acquire. */
    robot_follow_init(&f, &c, 0);
    v = frame(300, 1500);
    for (int i = 0; i < 20; ++i) {
        robot_follow_frame(&f, i % 2 ? &empty : &v, i * 100);
        assert(!f.locked);
    }
}
static void motion_tests(void)
{
    robot_config_t c = robot_default_config();
    assert(robot_config_valid(&c));
    assert(robot_pulse_us(&c, 0) == 1500);
    assert(robot_pulse_us(&c, 300) == robot_pulse_us(&c, 30));
    c.limit_deg = 45;
    assert(robot_pulse_us(&c, 45) == 2000 && robot_pulse_us(&c, -45) == 1000);
    c.reverse = true;
    assert(robot_pulse_us(&c, 45) == 1000);
    c.center_us = 1550;
    assert(!robot_config_valid(&c)); /* offset + full 45 degrees is unsafe */
    c.limit_deg = 30;
    assert(robot_config_valid(&c));
    for (int a = -180; a <= 180; ++a) {
        int p = robot_pulse_us(&c, a);
        assert(p >= 1000 && p <= 2000);
    }
    assert(fabsf(robot_slew(0, 30, 35, 0.02f) - 0.7f) < 0.001f);
    assert(robot_slew(0, 30, 35, 20) == 3.5f);
    assert(robot_slew(3, 3.1f, 35, 0.02f) == 3.1f);
    assert(robot_slew(0, 30, 35, -1) == 0);
}
static void calibration_line_tests(void)
{
    robot_config_t c = {0};
    bool save = true;
    assert(robot_cal_parse("cal:1500,30,35,0", &c, &save) && !save && c.center_us == 1500 && c.limit_deg == 30 && c.speed_dps == 35 && !c.reverse);
    assert(robot_cal_parse("cal:1420,45,90,1,save", &c, &save) == false); /* 1420 - 500 us is outside 1000..2000 */
    assert(robot_cal_parse("cal:1500,45,90,1,save", &c, &save) && save && c.reverse && c.limit_deg == 45 && c.speed_dps == 90);
    assert(robot_cal_parse("cal:1400,5,5,0,save", &c, &save) && c.center_us == 1400 && c.limit_deg == 5 && c.speed_dps == 5);
    const char *bad[] = {"", "cal:", "cal:1500", "cal:1500,30,35", "cal:1500,30,35,0,", "cal:1500,30,35,0,save ", "cal:1500,30,35,0,keep",
        "cal:1500,30,35,2", "cal:1500,30,35,-1", "cal:1399,30,35,0", "cal:1601,30,35,0", "cal:1500,4,35,0", "cal:1500,46,35,0",
        "cal:1500,30,4,0", "cal:1500,30,91,0", "cal:1500,30,35,0,save,save", "cal:1500;30,35,0", "cal: 1500,30,35,0", "cal:1500,30,35,0x",
        "cal:99999999999999999999,30,35,0", "cal:,30,35,0", "angle:5", "calx:1500,30,35,0"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        c = (robot_config_t){.center_us = 7};
        assert(!robot_cal_parse(bad[i], &c, &save) && c.center_us == 7); /* a rejected line leaves the output alone */
    }
    assert(!robot_cal_parse(NULL, &c, &save) && !robot_cal_parse("cal:1500,30,35,0", NULL, &save) && !robot_cal_parse("cal:1500,30,35,0", &c, NULL));
    char line[80];
    const robot_config_t d = robot_default_config();
    assert(robot_cal_format(line, sizeof line, &d, false) > 0 && !strcmp(line, "cal:1500,30,35,0"));
    robot_config_t r = {.center_us = 1450, .limit_deg = 25, .speed_dps = 50, .reverse = true};
    assert(robot_cal_format(line, sizeof line, &r, true) > 0 && !strcmp(line, "cal:1450,25,50,1,save"));
    assert(robot_cal_parse(line, &c, &save) && save && c.center_us == 1450 && c.limit_deg == 25 && c.speed_dps == 50 && c.reverse); /* round trip */
    assert(strlen(line) < 80); /* fits the motion service's command slot */
}
int main(void)
{
    parser_tests(); follow_tests(); motion_tests(); calibration_line_tests();
    puts("robot: parser, target association, loss/fault recovery, servo limits and calibration lines passed");
}
