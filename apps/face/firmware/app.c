#include "esphook/app.h"
#include "esphook/app_command.h"
#include "esphook/app_ui.h"
#include "esphook/app_services.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { lv_obj_t *face, *message, *eyes[2][35], *mouth[20], *indicator, *caption; } state_t;
_Static_assert(sizeof(state_t) <= APP_STATE_CAPACITY, "face state too large");
static void enter(app_context_t *ctx)
{
    state_t *s = ctx->state;
    if (ctx->standby) { char error[96]; const char *json = "{\"v\":1,\"mode\":\"auto\"}";
        (void)app_display_update(json, strlen(json), error, sizeof error); }
    s->face = app_ui_layer(ctx->root);
        lv_obj_set_style_bg_color(s->face, lv_color_hex(0x020006), LV_PART_MAIN);
        for (int eye = 0; eye < 2; ++eye)
            for (int i = 0; i < 35; ++i)
                s->eyes[eye][i] = app_ui_rect(s->face, 0, 0, 4, 4, 0xC178FF, 0);
        for (int i = 0; i < 20; ++i)
            s->mouth[i] = app_ui_rect(s->face, 70 + i % 5 * 4, 50 + i / 5 * 4,
                                 4, 4, 0xFDB927, 0);
        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < 3; ++i)
                app_ui_rect(s->face, 29 + side * 92 + i * 4, 48 - i % 2 * 2,
                          2, 5, 0xFF68BB, 0);
        s->indicator = app_ui_rect(s->face, 77, 74, 6, 2, 0x76539B, 0);
        s->caption = app_ui_label(s->face, &aihook_font_12, 7, 67, 146, 13, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(s->caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    s->message = app_ui_label(ctx->root, &aihook_font_16, 5, 6, 150, 68, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    app_ui_center(s->message); app_ui_hide(s->message, true);
}
static void render(app_context_t *ctx)
{
    state_t *s = ctx->state; robot_ui_t display; app_display_read(&display);
    const robot_ui_t *view = &display;
    if (view->mode == UI_RADAR && app_open(ctx, "radar")) return;
    bool text_mode = view->mode == UI_TEXT;
    app_ui_hide(s->face, text_mode); app_ui_hide(s->message, !text_mode);
    if (text_mode) { app_ui_text(s->message, view->text);
        lv_obj_set_style_text_color(s->message, app_ui_color(view->text_color), LV_PART_MAIN); return; }
    app_servo_t servo; app_servo_read(&servo);
    int state = servo.available ? servo.tracking : ROBOT_IDLE;
    float bearing = servo.target_deg; bool armed = servo.armed;
    static const char open[]  = "01110100011000110001100010111000000";
    static const char happy[] = "00000001000101010001100010000000000";
    static const char blink[] = "00000000000000011111000000000000000";
    static const char fault[] = "10001010100010001010100010000000000";
    static const char smile[] = "11111111110111000100";
    static const char flat[]  = "00000000001111100000";
    uint64_t ms = ctx->now_ms;
    bool blinking = view->blink && ms % 4300 > 4140;
    static const char sleepy[] = "00000000001111100000000000000000000";
    static const char sad[] = "00000000001000101010001000000000000";
    const char *pattern = state == ROBOT_FAULT ? fault :
                          state == ROBOT_TRACKING ? happy : open;
    switch (view->expression) {
        case FACE_NEUTRAL: pattern = open; break;
        case FACE_HAPPY: pattern = happy; break;
        case FACE_SLEEPY: pattern = sleepy; break;
        case FACE_SAD: pattern = sad; break;
        default: break;
    }
    int gaze = (int)lroundf(fmaxf(-4, fminf(4, bearing / 10)));
    if (view->expression == FACE_AUTO && state == ROBOT_IDLE) gaze = (int)lroundf(3 * sinf(ms / 2400.0f));
    uint32_t eye_rgb = view->expression == FACE_AUTO && state == ROBOT_FAULT
                       ? 0xFDB927 : view->eye_color;
    for (int e = 0; e < 2; ++e) {
        for (int i = 0; i < 35; ++i) {
            lv_obj_t *pixel = s->eyes[e][i];
            lv_obj_set_pos(pixel, 43 + e * 54 + i % 5 * 4 + gaze, 17 + i / 5 * 4);
            lv_obj_set_style_bg_color(pixel, app_ui_color(eye_rgb), LV_PART_MAIN);
            const char *eye_pattern = view->expression == FACE_CUSTOM ? (e ? view->right : view->left) : pattern;
            if (blinking && !(view->expression == FACE_AUTO && state == ROBOT_FAULT)) eye_pattern = blink;
            app_ui_hide(pixel, eye_pattern[i] != '1');
        }
    }
    const char *m = view->expression == FACE_CUSTOM ? view->mouth :
                    view->expression == FACE_HAPPY || (view->expression == FACE_AUTO && state == ROBOT_TRACKING) ? smile : flat;
    for (int i = 0; i < 20; ++i) {
        app_ui_hide(s->mouth[i], m[i] != '1');
        lv_obj_set_style_bg_color(s->mouth[i], app_ui_color(view->mouth_color), LV_PART_MAIN);
    }
    app_ui_text(s->caption, view->text);
    lv_obj_set_style_text_color(s->caption, app_ui_color(view->text_color), LV_PART_MAIN);
    app_ui_hide(s->caption, !view->text[0]);
    app_ui_hide(s->indicator, view->text[0] != '\0');
    lv_obj_set_style_bg_color(s->indicator, app_ui_color(armed ? 0xFDB927 : 0x76539B), LV_PART_MAIN);
}
static void event(app_context_t *ctx, const app_event_t *e)
{ if (e->kind == APP_EVENT_SHORT) app_menu(ctx); }
static void icon(void *parent, int x, int y, bool selected)
{
    uint32_t color = selected ? APP_COLOR_ACCENT : 0xC178FF;
    app_ui_rect(parent, x + 3, y + 8, 7, 12, color, 3);
    app_ui_rect(parent, x + 18, y + 8, 7, 12, color, 3);
}
static bool command(const char *line)
{
    if (!app_command_is(line, "robot face")) return false;
    char error[96]; const char *json="{\"v\":1,\"mode\":\"auto\"}";
    (void)app_display_update(json,strlen(json),error,sizeof error);
    (void)app_command_reply(app_command_open("face") ? "OK face queued" : "ERR app queue full");
    return true;
}
const app_descriptor_t esphook_app_face = {.api_version=APP_API_VERSION, .id="face", .title="表情",
    .flags=APP_STANDBY, .refresh_ms=40, .state_size=sizeof(state_t), .on_enter=enter, .on_event=event, .render=render, .icon=icon, .command=command};
