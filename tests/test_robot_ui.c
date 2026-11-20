#include "robot_ui_protocol.h"
#include "radar_view.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool parse(const char *s, robot_ui_t *out)
{
    char error[96];
    return robot_ui_parse(s, strlen(s), out, error, sizeof error);
}
static void protocol_tests(void)
{
    robot_ui_t v;
    assert(parse("{\"v\":1,\"mode\":\"text\",\"text\":\"你好，小机器人\\n任务完成\",\"ttl_ms\":5000}", &v));
    assert(v.mode == UI_TEXT && v.ttl_ms == 5000 && strstr(v.text, "任务完成"));
    assert(parse("{\"v\":1,\"mode\":\"radar\",\"range_mm\":3000}", &v));
    assert(v.range_mm == 3000 && v.mode == UI_RADAR);
    assert(parse("{\"v\":1,\"mode\":\"face\",\"expression\":\"happy\",\"text\":\"Hi\",\"eye_color\":\"#123aBc\",\"blink\":false}", &v));
    assert(v.expression == FACE_HAPPY && v.eye_color == 0x123abc && !v.blink);
    assert(parse("{\"v\":1,\"mode\":\"auto\"}  \n", &v));
    assert(v.mode == UI_AUTO && v.text[0] == '\0' && v.expression == FACE_AUTO);
    const char *custom = "{\"v\":1,\"mode\":\"face\",\"expression\":\"custom\",\"blink\":false,"
        "\"left\":[\"00100\",\"01110\",\"11111\",\"11111\",\"01110\",\"00100\",\"00000\"],"
        "\"right\":[\"00100\",\"01110\",\"11111\",\"11111\",\"01110\",\"00100\",\"00000\"],"
        "\"mouth\":[\"11111\",\"11111\",\"01110\",\"00100\"]}";
    assert(parse(custom, &v) && v.expression == FACE_CUSTOM);
    assert(!strcmp(v.left, "00100011101111111111011100010000000"));
    assert(strlen(v.right) == 35 && strlen(v.mouth) == 20);
    const char *bad[] = {
        "{\"v\":1,\"mode\":\"auto\",\"expression\":\"happy\"}",
        "{}", "[]", "null", "{\"v\":2,\"mode\":\"face\"}",
        "{\"v\":1,\"mode\":\"bogus\"}", "{\"v\":1,\"mode\":\"text\"}",
        "{\"v\":1,\"mode\":\"face\",\"expression\":\"custom\"}",
        "{\"v\":1,\"mode\":\"radar\",\"range_mm\":999}",
        "{\"v\":1,\"mode\":\"radar\",\"range_mm\":6001}",
        "{\"v\":1,\"mode\":\"face\",\"ttl_ms\":-1}",
        "{\"v\":1,\"mode\":\"face\",\"ttl_ms\":0.5}",
        "{\"v\":1,\"mode\":\"face\",\"ttl_ms\":600001}",
        "{\"v\":1,\"mode\":\"face\",\"blink\":1}",
        "{\"v\":1,\"mode\":\"face\",\"eye_color\":\"#GG0000\"}",
        "{\"v\":1,\"mode\":\"face\",\"Eye_color\":\"#FF0000\"}",
        "{\"v\":1,\"mode\":\"face\",\"mode\":\"radar\"}",
        "{\"v\":1,\"mode\":\"face\",\"text\":\"\\u0000hidden\"}",
        "{\"v\":1,\"mode\":\"face\",\"left\":[]}",
        "{\"v\":1,\"mode\":\"face\"} trailing",
        "{\"v\":1,\"mode\":\"face\",\"text\":\"\xC0\x80\"}",
        "{\"v\":1,\"mode\":\"face\",\"text\":\"\xED\xA0\x80\"}",
        "{\"v\":1,\"mode\":\"face\",\"text\":\"\xF4\x90\x80\x80\"}",
        "{\"v\":1,\"mode\":\"face\",\"text\":\"\xE4\"}",
        "{\"v\":1,\"mode\":\"face\",\"text\":[[[[[[]]]]]]}",
    };
    robot_ui_t previous = v;
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        assert(!parse(bad[i], &v));
        assert(!memcmp(&v, &previous, sizeof v)); /* Failed requests are atomic. */
    }
    char large[1200];
    memset(large, ' ', sizeof large);
    char error[96];
    assert(!robot_ui_parse(large, sizeof large, &v, error, sizeof error));
    snprintf(large, sizeof large, "{\"v\":1,\"mode\":\"text\",\"text\":\"%0193d\"}", 0);
    assert(!parse(large, &v));
    snprintf(large, sizeof large, "{\"v\":1,\"mode\":\"text\",\"text\":\"%0192d\"}", 0);
    assert(parse(large, &v) && strlen(v.text) == 192);
    /* Invalid custom row width/pixel values, not just a missing bitmap. */
    strcpy(large, custom);
    char *point = strstr(large, "00100");
    point[0] = '2';
    assert(!parse(large, &v));
    /* Raw NUL is rejected even when a caller supplies bytes after it. */
    char raw_null[] = "{\"v\":1,\"mode\":\"auto\"}\0{}";
    assert(!robot_ui_parse(raw_null, sizeof raw_null - 1, &v, error, sizeof error));
    /* Exact-length buffers do not need an extra NUL byte. */
    char exact[] = {'{','"','v','"',':','1',',','"','m','o','d','e','"',':','"','a','u','t','o','"','}'};
    assert(robot_ui_parse(exact, sizeof exact, &v, error, sizeof error));
}
static void radar_tests(void)
{
    int x, y;
    ld2450_target_t t = {.x_mm = 0, .y_mm = 6000, .valid = true};
    assert(radar_view_project(&t, 6000, &x, &y) && x == 54 && y == 63); /* the far end of the range is the bottom */
    t.y_mm = 3000;
    assert(radar_view_project(&t, 6000, &x, &y) && x == 54 && y == 33);
    t.y_mm = 600;
    assert(radar_view_project(&t, 6000, &x, &y) && y == 9); /* near the machine, which sits at the top */
    /* The viewer faces the machine, so the sensor's +X (its own right) is on the viewer's left. */
    t.x_mm = -2000; t.y_mm = 2000;
    assert(radar_view_project(&t, 6000, &x, &y) && x > 54);
    t.x_mm = 2000;
    assert(radar_view_project(&t, 6000, &x, &y) && x < 54);
    t.x_mm = 0; t.y_mm = 6100;
    assert(!radar_view_project(&t, 6000, &x, &y));
    t.y_mm = -1;
    assert(!radar_view_project(&t, 6000, &x, &y));
    t.x_mm = 3000; t.y_mm = 100;
    assert(!radar_view_project(&t, 6000, &x, &y));
    uint16_t image[RADAR_VIEW_WIDTH * RADAR_VIEW_HEIGHT];
    ld2450_frame_t frame = {0};
    frame.targets[0] = (ld2450_target_t){.x_mm = -1200, .y_mm = 2400, .valid = true};
    frame.targets[1] = (ld2450_target_t){.x_mm = 1000, .y_mm = 4100, .valid = true};
    frame.targets[2] = (ld2450_target_t){.x_mm = 2000, .y_mm = 1500, .valid = true};
    radar_view_draw(image, &frame, true, 6000, 0);
    for (unsigned i = 0; i < 3; ++i) {
        assert(radar_view_project(&frame.targets[i], 6000, &x, &y));
        uint16_t color = radar_target_color(i);
        assert(image[y * RADAR_VIEW_WIDTH + x] == color);
        unsigned pixels = 0;
        for (size_t j = 0; j < sizeof image / sizeof *image; ++j) {
            if (image[j] == color) ++pixels;
        }
        assert(pixels == 9); /* Each target has one cross and no separate annotation. */
    }
    radar_view_draw(image, &frame, false, 6000, 0);
    assert(radar_view_project(&frame.targets[0], 6000, &x, &y));
    assert(image[y * RADAR_VIEW_WIDTH + x] != radar_target_color(0));
    /* Outlying signed coordinates, extreme phases and edges must be clipped. */
    uint32_t seed = 123;
    for (unsigned i = 0; i < 1000; ++i) {
        for (unsigned j = 0; j < 3; ++j) {
            seed = seed * 1664525u + 1013904223u;
            frame.targets[j].x_mm = (int16_t)(seed >> 16);
            seed = seed * 1664525u + 1013904223u;
            frame.targets[j].y_mm = (int16_t)(seed >> 16);
            frame.targets[j].valid = true;
        }
        radar_view_draw(image, &frame, true, i % 5001 + 1000, UINT_MAX - i);
    }
}
int main(void)
{
    protocol_tests(); radar_tests();
    puts("robot UI: JSON validation/atomicity, custom expressions, UTF-8 and radar projection passed");
}
