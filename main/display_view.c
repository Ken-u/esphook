/* main/display_view.c
 *
 * 160x80 ST7735 横屏视图。
 *
 * LVGL 只在 display_task 中驱动，屏幕采用局部 DMA buffer；标题和正文用
 * LV_LABEL_LONG_MODE_SCROLL_CIRCULAR，由 LVGL 的动画系统持续滚动。这样
 * 滚动不会再被原来的 10fps 手工刷新限制，提醒态也能把主要面积留给内容。
 */
#include "display_view.h"
#include "display_model.h"
#include "config_store.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "dv";

#define PIN_CS  0
#define PIN_DC  3
#define PIN_RST 2
#define PIN_SCL 4
#define PIN_SDA 5
#define PIN_BLK 1

#define X_GAP 1
#define Y_GAP 26

#define ST7735_CASET 0x2A
#define ST7735_RASET 0x2B
#define ST7735_RAMWR 0x2C

/* 20MHz SPI 下，局部 buffer 比整屏 buffer 更适合高频滚动刷新。 */
#define LVGL_DRAW_BUF_LINES 16
#define LVGL_TICK_PERIOD_MS 1
#define LVGL_SCROLL_SPEED 45
#define LVGL_INFO_SCROLL_SPEED 24
#define DISPLAY_IDLE_TIMEOUT_US (10LL * 60LL * 1000000LL)

/* 网页效果图里的颜色，LVGL 会按 RGB565 输出到面板。 */
#define COLOR_LCD       0x071018
#define COLOR_TEXT      0xEDF4FB
#define COLOR_MUTED     0x7891A7
#define COLOR_LINE      0x254252
#define COLOR_GREEN     0x47E6AE
#define COLOR_YELLOW    0xFFD166
#define COLOR_RED       0xFF6B7A
#define COLOR_CYAN      0x6BB8FF

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static lv_display_t *s_display;
static esp_timer_handle_t s_lv_tick_timer;
static char s_ip[16];
static int64_t s_last_activity_us;
static bool s_screen_awake;

static lv_obj_t *s_alert_layer;
static lv_obj_t *s_net_layer;
static lv_obj_t *s_idle_layer;

static lv_obj_t *s_client;
static lv_obj_t *s_unread_dot;
static lv_obj_t *s_unread_count;
static lv_obj_t *s_session;
static lv_obj_t *s_wifi;
static lv_obj_t *s_accent_bar;
static lv_obj_t *s_type_icon;
static lv_obj_t *s_type;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_meta;
static lv_obj_t *s_position;
static lv_obj_t *s_keys;
static lv_obj_t *s_online;

static lv_obj_t *s_idle_title;
static lv_obj_t *s_idle_body;
static lv_obj_t *s_idle_meta;

static lv_obj_t *s_net_title;
static lv_obj_t *s_net_body;
static lv_obj_t *s_net_meta;

LV_FONT_DECLARE(aihook_font_12);
LV_FONT_DECLARE(aihook_font_14);

static lv_color_t color_hex(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

static lv_color_t status_color(dm_status_t status)
{
    switch (status) {
        case DM_STATUS_CONFIRM: return color_hex(COLOR_YELLOW);
        case DM_STATUS_ERROR:   return color_hex(COLOR_RED);
        case DM_STATUS_DONE:
        default:                return color_hex(COLOR_GREEN);
    }
}

static void style_plain(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
}

static lv_obj_t *make_layer(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    style_plain(obj);
    lv_obj_set_size(obj, DV_WIDTH, DV_HEIGHT);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, color_hex(COLOR_LCD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    return obj;
}

static lv_obj_t *make_rect(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t rgb, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    style_plain(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    return obj;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            int x, int y, int w, int h,
                            lv_label_long_mode_t long_mode)
{
    lv_obj_t *obj = lv_label_create(parent);
    style_plain(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(obj,
                                   lv_anim_speed_clamped(LVGL_SCROLL_SPEED, 300, 10000),
                                   LV_PART_MAIN);
    lv_label_set_long_mode(obj, long_mode);
    lv_label_set_text(obj, "");
    return obj;
}

static void set_scroll_speed(lv_obj_t *obj, uint32_t pixels_per_second)
{
    lv_obj_set_style_anim_duration(obj,
                                   lv_anim_speed_clamped(pixels_per_second, 300, 10000),
                                   LV_PART_MAIN);
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void show_layer(lv_obj_t *layer)
{
    set_hidden(s_alert_layer, layer != s_alert_layer);
    set_hidden(s_net_layer, layer != s_net_layer);
    set_hidden(s_idle_layer, layer != s_idle_layer);
}

static void set_text(lv_obj_t *obj, const char *text)
{
    lv_label_set_text(obj, text ? text : "");
}

static const char *client_label(const char *client_id, char *out, size_t len)
{
    const char *mapped = config_get_display_name("client", client_id, out, len);
    if (mapped[0] != '\0') return mapped;

    const char *short_id = strchr(client_id, ':');
    short_id = short_id ? short_id + 1 : client_id;
    if (len > 0) {
        strncpy(out, short_id, len - 1);
        out[len - 1] = '\0';
    }
    return out;
}

static const char *session_label(const char *session_id, char *out, size_t len)
{
    const char *mapped = config_get_display_name("session", session_id, out, len);
    if (mapped[0] != '\0') return mapped;

    const char *short_id = strchr(session_id, ':');
    short_id = short_id ? short_id + 1 : session_id;
    if (len > 0) {
        strncpy(out, short_id, len - 1);
        out[len - 1] = '\0';
    }
    return out;
}

static bool lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area,
                          uint8_t *px_map)
{
    esp_lcd_panel_io_handle_t io = (esp_lcd_panel_io_handle_t)
        lv_display_get_user_data(display);
    const int width = area->x2 - area->x1 + 1;
    const int height = area->y2 - area->y1 + 1;
    const uint16_t x_start = (uint16_t)(area->x1 + X_GAP);
    const uint16_t x_end = (uint16_t)(area->x2 + X_GAP);
    const uint16_t y_start = (uint16_t)(area->y1 + Y_GAP);
    const uint16_t y_end = (uint16_t)(area->y2 + Y_GAP);
    const uint8_t caset[] = {
        (uint8_t)(x_start >> 8), (uint8_t)x_start,
        (uint8_t)(x_end >> 8), (uint8_t)x_end,
    };
    const uint8_t raset[] = {
        (uint8_t)(y_start >> 8), (uint8_t)y_start,
        (uint8_t)(y_end >> 8), (uint8_t)y_end,
    };

    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, ST7735_CASET, caset, sizeof(caset)));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, ST7735_RASET, raset, sizeof(raset)));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io, ST7735_RAMWR, px_map,
                                               (size_t)width * height * sizeof(uint16_t)));
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void create_alert_ui(void)
{
    s_alert_layer = make_layer(lv_screen_active());

    /* Header：在线点、client、未读数、session、WiFi，全部比旧版更醒目。 */
    make_rect(s_alert_layer, 3, 4, 5, 5, COLOR_GREEN, 3);
    s_client = make_label(s_alert_layer, &aihook_font_12, 11, -1, 63, 14,
                          LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    set_scroll_speed(s_client, LVGL_INFO_SCROLL_SPEED);
    s_unread_dot = make_rect(s_alert_layer, 77, 4, 5, 5, COLOR_RED, 3);
    s_unread_count = make_label(s_alert_layer, &aihook_font_12, 84, -1, 13, 14,
                                LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s_unread_count, color_hex(COLOR_RED), LV_PART_MAIN);
    s_session = make_label(s_alert_layer, &aihook_font_12, 103, -1, 36, 14,
                           LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    set_scroll_speed(s_session, LVGL_INFO_SCROLL_SPEED);
    lv_obj_set_style_text_align(s_session, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    s_wifi = make_label(s_alert_layer, &aihook_font_12, 143, 0, 15, 13,
                        LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_wifi, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_wifi, color_hex(COLOR_GREEN), LV_PART_MAIN);
    make_rect(s_alert_layer, 0, 13, DV_WIDTH, 1, COLOR_LINE, 0);

    /* 提醒主体：类型、标题、正文、来源；标题和正文都占据主要高度。 */
    s_accent_bar = make_rect(s_alert_layer, 0, 15, 2, 51, COLOR_YELLOW, 0);
    s_type_icon = make_label(s_alert_layer, &aihook_font_12, 5, 14, 13, 12,
                             LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_type_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_type_icon, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_type_icon, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_type_icon, 2, LV_PART_MAIN);
    s_type = make_label(s_alert_layer, &aihook_font_12, 21, 14, 134, 12,
                        LV_LABEL_LONG_MODE_CLIP);
    s_title = make_label(s_alert_layer, &aihook_font_14, 5, 26, 152, 19,
                         LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s_body = make_label(s_alert_layer, &aihook_font_12, 5, 45, 152, 14,
                        LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s_meta = make_label(s_alert_layer, &aihook_font_12, 5, 59, 103, 10,
                        LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    set_scroll_speed(s_meta, LVGL_INFO_SCROLL_SPEED);
    lv_obj_set_style_text_color(s_meta, color_hex(COLOR_MUTED), LV_PART_MAIN);
    s_position = make_label(s_alert_layer, &aihook_font_12, 111, 59, 45, 10,
                            LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_position, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_position, color_hex(COLOR_CYAN), LV_PART_MAIN);

    make_rect(s_alert_layer, 0, 69, DV_WIDTH, 1, COLOR_LINE, 0);
    s_keys = make_label(s_alert_layer, &aihook_font_12, 4, 69, 116, 11,
                        LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    set_scroll_speed(s_keys, LVGL_INFO_SCROLL_SPEED);
    s_online = make_label(s_alert_layer, &aihook_font_12, 124, 69, 32, 11,
                          LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_online, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_online, color_hex(COLOR_GREEN), LV_PART_MAIN);
}

static void create_idle_ui(void)
{
    s_idle_layer = make_layer(lv_screen_active());
    s_idle_title = make_label(s_idle_layer, &aihook_font_14, 7, 22, 146, 22,
                              LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_idle_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_idle_title, color_hex(COLOR_GREEN), LV_PART_MAIN);
    s_idle_body = make_label(s_idle_layer, &aihook_font_12, 7, 46, 146, 16,
                             LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_idle_body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_idle_body, color_hex(COLOR_MUTED), LV_PART_MAIN);
    s_idle_meta = make_label(s_idle_layer, &aihook_font_12, 7, 63, 146, 12,
                             LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_idle_meta, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_idle_meta, color_hex(COLOR_MUTED), LV_PART_MAIN);
}

static void create_net_ui(void)
{
    s_net_layer = make_layer(lv_screen_active());
    s_net_title = make_label(s_net_layer, &aihook_font_14, 4, 17, 152, 22,
                             LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_net_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_net_body = make_label(s_net_layer, &aihook_font_12, 4, 41, 152, 17,
                            LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_net_body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_net_meta = make_label(s_net_layer, &aihook_font_12, 4, 61, 152, 12,
                            LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_net_meta, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_net_meta, color_hex(COLOR_MUTED), LV_PART_MAIN);
}

esp_err_t dv_init(void)
{
    gpio_set_direction(PIN_BLK, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BLK, 1);
    s_screen_awake = true;
    s_last_activity_us = esp_timer_get_time();

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_SDA,
        .sclk_io_num = PIN_SCL,
        .miso_io_num = -1,
        .max_transfer_sz = DV_WIDTH * LVGL_DRAW_BUF_LINES * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_CS,
        .dc_gpio_num = PIN_DC,
        .spi_mode = 0,
        .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                              &io_cfg, &s_io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    /* ST7735 黑底屏的补充参数，沿用已验证过的屏驱初始化。 */
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xB1,
                                               (uint8_t[]){0x01, 0x2C, 0x2D}, 3));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xB2,
                                               (uint8_t[]){0x01, 0x2C, 0x2D}, 3));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xB3,
                                               (uint8_t[]){0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xB4, (uint8_t[]){0x07}, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xC0,
                                               (uint8_t[]){0xA2, 0x02, 0x84}, 3));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xC1, (uint8_t[]){0xC5}, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xC2,
                                               (uint8_t[]){0x0A, 0x00}, 2));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xC3,
                                               (uint8_t[]){0x8A, 0x2A}, 2));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xC4,
                                               (uint8_t[]){0x8A, 0xEE}, 2));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xC5, (uint8_t[]){0x0E}, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xE0,
                                               (uint8_t[]){0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                                                           0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10}, 16));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0xE1,
                                               (uint8_t[]){0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                                                           0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10}, 16));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0x13, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_lcd_panel_swap_xy(s_panel, true);
    /* MV=1 交换行列后，物理上下方向对应逻辑 MX。 */
    esp_lcd_panel_mirror(s_panel, false, true);
    esp_lcd_panel_set_gap(s_panel, X_GAP, Y_GAP);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_disp_on_off(s_panel, true);

    lv_init();
    s_display = lv_display_create(DV_WIDTH, DV_HEIGHT);
    if (!s_display) return ESP_ERR_NO_MEM;

    const lv_color_format_t color_format = LV_COLOR_FORMAT_RGB565_SWAPPED;
    const size_t draw_buffer_size = DV_WIDTH * LVGL_DRAW_BUF_LINES * sizeof(uint16_t);
    void *buf1 = spi_bus_dma_memory_alloc(SPI2_HOST, draw_buffer_size, 0);
    void *buf2 = spi_bus_dma_memory_alloc(SPI2_HOST, draw_buffer_size, 0);
    if (!buf1 || !buf2) return ESP_ERR_NO_MEM;
    lv_display_set_color_format(s_display, color_format);
    lv_display_set_buffers(s_display, buf1, buf2, draw_buffer_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(s_display, s_io);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);

    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = lcd_color_trans_done_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io, &callbacks, s_display));

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_lv_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lv_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    create_alert_ui();
    create_net_ui();
    create_idle_ui();
    memset(s_ip, 0, sizeof s_ip);
    show_layer(s_net_layer);

    /* 首帧在 display_task 启动前主动送出，避免开机黑屏等待事件。 */
    dv_render_net(0, NULL);
    dv_flush();
    ESP_LOGI(TAG, "LVGL ST7735 initialized %dx%d, partial=%d lines",
             DV_WIDTH, DV_HEIGHT, LVGL_DRAW_BUF_LINES);
    return ESP_OK;
}

void dv_flush(void)
{
    if (s_display && s_screen_awake) lv_refr_now(s_display);
}

void dv_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
    if (s_screen_awake || !s_panel) return;

    /* 先开面板，再开背光，避免唤醒时短暂显示旧帧。 */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    gpio_set_level(PIN_BLK, 1);
    s_screen_awake = true;

    /*
     * 部分 ST7735 模块在 DISPON 后不会自动重新锁存 GRAM 内容；如果
     * 唤醒事件没有改变任何 label，LVGL 也可能认为没有脏区域可刷。
     * 强制让整棵屏幕失效并立即刷新，保证按键/新提醒都能真正亮屏。
     */
    if (s_display) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(s_display);
    }
}

void dv_process(void)
{
    if (s_screen_awake &&
        esp_timer_get_time() - s_last_activity_us >= DISPLAY_IDLE_TIMEOUT_US) {
        /* ST7735 关闭显示输出，同时关闭独立背光 GPIO，避免白白耗电。 */
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, false));
        gpio_set_level(PIN_BLK, 0);
        s_screen_awake = false;
        return;
    }

    /* 熄灭期间保留 LVGL tick，但不再驱动滚动和刷新，降低空闲功耗。 */
    if (s_display && s_screen_awake) (void)lv_timer_handler();
}

void dv_render_status_bar(void)
{
    if (!s_alert_layer) return;

    char client[24] = "AI-HOOK";
    char session[48] = "--";
    if (dm_current(client, session, sizeof client, sizeof session,
                   NULL, 0, NULL, 0)) {
        char display_client[32];
        char display_session[32];
        const char *cn = client_label(client, display_client, sizeof display_client);
        const char *sn = session_label(session, display_session, sizeof display_session);
        snprintf(client, sizeof client, "%s", cn);
        snprintf(session, sizeof session, "%s", sn);
    }

    lv_label_set_text(s_client, client);
    lv_label_set_text(s_session, session);
    set_text(s_wifi, "ON");

    int total_unread = 0;
    const int client_count = dm_client_count();
    for (int i = 0; i < client_count; i++) {
        char client_id[24];
        dm_client_at(i, client_id, sizeof client_id);
        total_unread += dm_client_unread(client_id);
    }
    if (total_unread > 0) {
        set_hidden(s_unread_dot, false);
        lv_label_set_text_fmt(s_unread_count, "%d", total_unread > 9 ? 9 : total_unread);
    } else {
        set_hidden(s_unread_dot, true);
        set_text(s_unread_count, "");
    }
}

static void update_action_bar(void)
{
    char keys[128];
    char key0[32], key1[32], key2[32];
    snprintf(key0, sizeof key0, "%s", config_get_key(0));
    snprintf(key1, sizeof key1, "%s", config_get_key(1));
    snprintf(key2, sizeof key2, "%s", config_get_key(2));
    snprintf(keys, sizeof keys, "1 %s   2 %s   3 %s", key0, key1, key2);
    set_text(s_keys, keys);
    set_text(s_online, "ON");
}

void dv_render_main(void)
{
    if (!s_alert_layer) return;

    char client[24];
    char session[48];
    char title[32];
    char body[128];
    if (!dm_current(client, session, sizeof client, sizeof session,
                    title, sizeof title, body, sizeof body)) {
        dv_render_idle();
        return;
    }

    show_layer(s_alert_layer);
    dm_status_t status = dm_current_status();
    lv_color_t accent = status_color(status);

    /* 左侧色带跟随状态，标题/正文/类型用同一颜色建立一眼可见的语义。 */
    lv_obj_set_style_bg_color(s_accent_bar, accent, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_type_icon, accent, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_type_icon, accent, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_type, accent, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_title, accent, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_body, accent, LV_PART_MAIN);

    switch (status) {
        case DM_STATUS_CONFIRM:
            set_text(s_type_icon, "!");
            set_text(s_type, "CONFIRM");
            break;
        case DM_STATUS_ERROR:
            set_text(s_type_icon, "X");
            set_text(s_type, "ERROR");
            break;
        case DM_STATUS_DONE:
        default:
            set_text(s_type_icon, "OK");
            set_text(s_type, "DONE");
            break;
    }

    set_text(s_title, title[0] ? title : "ALERT");
    set_text(s_body, body[0] ? body : "-");

    char display_client[32];
    char display_session[32];
    const char *cn = client_label(client, display_client, sizeof display_client);
    const char *sn = session_label(session, display_session, sizeof display_session);
    char source[96];
    snprintf(source, sizeof source, "[%s / %s]", cn, sn);
    set_text(s_meta, source);
    set_text(s_position, "TEXT");
    update_action_bar();
}

void dv_render_idle(void)
{
    if (!s_idle_layer) return;
    show_layer(s_idle_layer);
    set_text(s_idle_title, "AI-HOOK READY");
    set_text(s_idle_body, "waiting for alerts");
    char meta[40];
    snprintf(meta, sizeof meta, "WiFi online  %s", s_ip[0] ? s_ip : "--.--.--.--");
    set_text(s_idle_meta, meta);
}

void dv_render_net(int state, const char *ip)
{
    if (!s_net_layer) return;
    if (ip) {
        strncpy(s_ip, ip, sizeof s_ip - 1);
        s_ip[sizeof s_ip - 1] = '\0';
    }
    if (state == 2) return;

    show_layer(s_net_layer);
    if (state == 0) {
        set_text(s_net_title, "NO WIFI CONFIG");
        set_text(s_net_body, "USB: ssid:<name>");
        set_text(s_net_meta, "pass:<password>  connect");
        lv_obj_set_style_text_color(s_net_title, color_hex(COLOR_RED), LV_PART_MAIN);
    } else {
        set_text(s_net_title, "CONNECTING...");
        set_text(s_net_body, "waiting for wifi");
        set_text(s_net_meta, "USB: status");
        lv_obj_set_style_text_color(s_net_title, color_hex(COLOR_YELLOW), LV_PART_MAIN);
    }
}
