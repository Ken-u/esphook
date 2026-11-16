/* main/display_view.c
 *
 * 160x80 ST7735 横屏视图。
 *
 * LVGL 只在 display_task 中驱动，屏幕采用局部 DMA buffer；标题和正文用
 * LV_LABEL_LONG_MODE_SCROLL_CIRCULAR，由 LVGL 的动画系统持续滚动。这样
 * 滚动不会再被原来的 10fps 手工刷新限制，提醒态也能把主要面积留给内容。
 */
#include "display_view.h"
#include "sdkconfig.h"
#include "robot_follow.h"
#include "radar_view.h"
#include <math.h>
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
#include "src/misc/cache/instance/lv_image_cache.h"
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
#define LVGL_SCROLL_SPEED 18
#define LVGL_INFO_SCROLL_SPEED 18
#define LVGL_BODY_SCROLL_SPEED 10
#define DISPLAY_IDLE_TIMEOUT_US (10LL * 60LL * 1000000LL)

/* 网页效果图里的颜色，LVGL 会按 RGB565 输出到面板。 */
#define COLOR_LCD       0x071018
#define COLOR_TEXT      0xEDF4FB
#define COLOR_MUTED     0x7891A7
#define COLOR_LINE      0x254252
#define COLOR_GREEN     0x47E6AE
#define COLOR_YELLOW    0xFFD166
#define COLOR_RED       0xFF6B7A

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static lv_display_t *s_display;
static esp_timer_handle_t s_lv_tick_timer;
static char s_ip[16];
static int64_t s_last_activity_us;
static bool s_screen_awake;

static lv_obj_t *s_robot_layer;
static lv_obj_t *s_radar_layer;
static lv_obj_t *s_text_layer;
static lv_obj_t *s_game_layer;
static lv_obj_t *s_stock_layer;
static lv_obj_t *s_menu_layer;
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
/* Reused by the mutually exclusive radar and external chart screens. */
static uint16_t s_plot_pixels[160 * 48];
#endif
static lv_obj_t *s_alert_layer;
static lv_obj_t *s_net_layer;
static lv_obj_t *s_idle_layer;

static lv_obj_t *s_client;
static lv_obj_t *s_unread_dot;
static lv_obj_t *s_unread_count;
static lv_obj_t *s_wifi;
static lv_obj_t *s_accent_bar;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_keys;

static lv_obj_t *s_idle_title;
static lv_obj_t *s_idle_body;
static lv_obj_t *s_idle_meta;

static lv_obj_t *s_net_title;
static lv_obj_t *s_net_body;
static lv_obj_t *s_net_meta;

LV_FONT_DECLARE(aihook_font_12);
LV_FONT_DECLARE(aihook_font_16);

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
    if (s_robot_layer) set_hidden(s_robot_layer, layer != s_robot_layer);
    if (s_radar_layer) set_hidden(s_radar_layer, layer != s_radar_layer);
    if (s_text_layer) set_hidden(s_text_layer, layer != s_text_layer);
    if (s_game_layer) set_hidden(s_game_layer, layer != s_game_layer);
    if (s_stock_layer) set_hidden(s_stock_layer, layer != s_stock_layer);
    if (s_menu_layer) set_hidden(s_menu_layer, layer != s_menu_layer);
}

static void set_text(lv_obj_t *obj, const char *text)
{
    const char *next = text ? text : "";
    if (strcmp(lv_label_get_text(obj), next)) lv_label_set_text(obj, next);
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

    /* Header 保留静态 client 名称；它不滚动、不闪烁，避免浪费注意力。 */
    make_rect(s_alert_layer, 3, 4, 5, 5, COLOR_GREEN, 3);
    s_client = make_label(s_alert_layer, &aihook_font_12, 11, -1, 79, 14,
                          LV_LABEL_LONG_MODE_CLIP);
    s_unread_dot = make_rect(s_alert_layer, 96, 4, 5, 5, COLOR_RED, 3);
    s_unread_count = make_label(s_alert_layer, &aihook_font_12, 103, -1, 16, 14,
                                LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_color(s_unread_count, color_hex(COLOR_RED), LV_PART_MAIN);
    s_wifi = make_label(s_alert_layer, &aihook_font_12, 126, 0, 30, 13,
                        LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_wifi, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_wifi, color_hex(COLOR_GREEN), LV_PART_MAIN);
    make_rect(s_alert_layer, 0, 11, DV_WIDTH, 1, COLOR_LINE, 0);

    /* 状态只用色带和文字颜色表达，主体两行都给 16px 字体。 */
    s_accent_bar = make_rect(s_alert_layer, 0, 13, 2, 55, COLOR_YELLOW, 0);
    s_title = make_label(s_alert_layer, &aihook_font_16, 5, 14, 150, 26,
                         LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s_body = make_label(s_alert_layer, &aihook_font_16, 5, 41, 150, 26,
                        LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    set_scroll_speed(s_title, LVGL_SCROLL_SPEED);
    set_scroll_speed(s_body, LVGL_BODY_SCROLL_SPEED);

    make_rect(s_alert_layer, 0, 68, DV_WIDTH, 1, COLOR_LINE, 0);
    s_keys = make_label(s_alert_layer, &aihook_font_12, 4, 69, 152, 11,
                        LV_LABEL_LONG_MODE_CLIP);
}

static void create_idle_ui(void)
{
    s_idle_layer = make_layer(lv_screen_active());
    s_idle_title = make_label(s_idle_layer, &aihook_font_16, 7, 22, 146, 22,
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
    s_net_title = make_label(s_net_layer, &aihook_font_16, 4, 17, 152, 22,
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
    if (dm_current(client, NULL, sizeof client, 0, NULL, 0, NULL, 0)) {
        char display_client[32];
        const char *label = client_label(client, display_client, sizeof display_client);
        strncpy(client, label, sizeof client - 1);
        client[sizeof client - 1] = '\0';
    }
    set_text(s_client, client);
    set_text(s_wifi, "WiFi");

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
}

void dv_render_main(void)
{
    if (!s_alert_layer) return;

    char title[32];
    char body[128];
    if (!dm_current(NULL, NULL, 0, 0,
                   title, sizeof title, body, sizeof body)) {
        dv_render_idle();
        return;
    }

    show_layer(s_alert_layer);
    dm_status_t status = dm_current_status();
    lv_color_t accent = status_color(status);

    /* 左侧色带和标题/正文颜色直接表达状态，不再显示 DONE/ERROR/CONFIRM。 */
    lv_obj_set_style_bg_color(s_accent_bar, accent, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_title, accent, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_body, accent, LV_PART_MAIN);

    set_text(s_title, title[0] ? title : "ALERT");
    set_text(s_body, body[0] ? body : "-");
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

static void render_face(const robot_status_t *status, const robot_ui_t *view)
{
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    int state = status->state;
    float bearing = status->target_deg;
    bool armed = status->armed;
    static lv_obj_t *eyes[2][35], *mouth[20], *indicator, *caption;
    if (!s_robot_layer) {
        s_robot_layer = make_layer(lv_screen_active());
        lv_obj_set_style_bg_color(s_robot_layer, lv_color_hex(0x020006), LV_PART_MAIN);
        for (int eye = 0; eye < 2; ++eye)
            for (int i = 0; i < 35; ++i)
                eyes[eye][i] = make_rect(s_robot_layer, 0, 0, 4, 4, 0xC178FF, 0);
        for (int i = 0; i < 20; ++i)
            mouth[i] = make_rect(s_robot_layer, 70 + i % 5 * 4, 50 + i / 5 * 4,
                                 4, 4, 0xFDB927, 0);
        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < 3; ++i)
                make_rect(s_robot_layer, 29 + side * 92 + i * 4, 48 - i % 2 * 2,
                          2, 5, 0xFF68BB, 0);
        indicator = make_rect(s_robot_layer, 77, 74, 6, 2, 0x76539B, 0);
        caption = make_label(s_robot_layer, &aihook_font_12, 7, 67, 146, 13, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    static const char open[]  = "01110100011000110001100010111000000";
    static const char happy[] = "00000001000101010001100010000000000";
    static const char blink[] = "00000000000000011111000000000000000";
    static const char fault[] = "10001010100010001010100010000000000";
    static const char smile[] = "11111111110111000100";
    static const char flat[]  = "00000000001111100000";
    uint64_t ms = (uint64_t)esp_timer_get_time() / 1000;
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
            lv_obj_t *pixel = eyes[e][i];
            lv_obj_set_pos(pixel, 43 + e * 54 + i % 5 * 4 + gaze, 17 + i / 5 * 4);
            lv_obj_set_style_bg_color(pixel, color_hex(eye_rgb), LV_PART_MAIN);
            const char *eye_pattern = view->expression == FACE_CUSTOM ? (e ? view->right : view->left) : pattern;
            if (blinking && !(view->expression == FACE_AUTO && state == ROBOT_FAULT)) eye_pattern = blink;
            set_hidden(pixel, eye_pattern[i] != '1');
        }
    }
    const char *m = view->expression == FACE_CUSTOM ? view->mouth :
                    view->expression == FACE_HAPPY || (view->expression == FACE_AUTO && state == ROBOT_TRACKING) ? smile : flat;
    for (int i = 0; i < 20; ++i) {
        set_hidden(mouth[i], m[i] != '1');
        lv_obj_set_style_bg_color(mouth[i], color_hex(view->mouth_color), LV_PART_MAIN);
    }
    set_text(caption, view->text);
    lv_obj_set_style_text_color(caption, color_hex(view->text_color), LV_PART_MAIN);
    set_hidden(caption, !view->text[0]);
    set_hidden(indicator, view->text[0] != '\0');
    lv_obj_set_style_bg_color(indicator, color_hex(armed ? 0xFDB927 : 0x76539B), LV_PART_MAIN);
    show_layer(s_robot_layer);
    dv_activity();
#else
    (void)status; (void)view;
#endif
}


static void render_radar(const robot_status_t *status, const robot_ui_t *view)
{
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    uint16_t *pixels = s_plot_pixels;
    static lv_image_dsc_t image;
    static lv_obj_t *plot, *heading, *labels[3];
    if (!s_radar_layer) {
        s_radar_layer = make_layer(lv_screen_active());
        heading = make_label(s_radar_layer, LV_FONT_DEFAULT, 2, 1, 158, 12, LV_LABEL_LONG_MODE_CLIP);
        for (unsigned i = 0; i < 3; ++i) {
            labels[i] = make_label(s_radar_layer, LV_FONT_DEFAULT, 110, 17 + i * 21, 50, 20, LV_LABEL_LONG_MODE_CLIP);
            lv_obj_set_style_text_color(labels[i], lv_color_make(
                (radar_target_color(i) >> 11) * 255 / 31,
                ((radar_target_color(i) >> 5) & 63) * 255 / 63,
                (radar_target_color(i) & 31) * 255 / 31), LV_PART_MAIN);
        }
        image.header.magic = LV_IMAGE_HEADER_MAGIC;
        image.header.cf = LV_COLOR_FORMAT_RGB565;
        image.header.w = RADAR_VIEW_WIDTH;
        image.header.h = RADAR_VIEW_HEIGHT;
        image.header.stride = RADAR_VIEW_WIDTH * 2;
        image.data_size = RADAR_VIEW_WIDTH * RADAR_VIEW_HEIGHT * sizeof(uint16_t);
        image.data = (const uint8_t *)pixels;
        plot = lv_image_create(s_radar_layer);
        lv_obj_set_pos(plot, 0, 16);
        lv_image_set_src(plot, &image);
    }
    unsigned count = 0;
    for (unsigned i = 0; i < 3; ++i) {
        char value[32];
        const ld2450_target_t *t = &status->raw.targets[i];
        if (status->radar_online && t->valid) {
            ++count;
            snprintf(value, sizeof value, "%u%+.1f\nY%4.1f", i + 1,
                     t->x_mm / 1000.0, t->y_mm / 1000.0);
        } else snprintf(value, sizeof value, "%u --\nY --", i + 1);
        set_text(labels[i], value);
    }
    char title[40];
    if (status->radar_online) snprintf(title, sizeof title, "R%u %.1fm   X/Y m", count, view->range_mm / 1000.0);
    if (!status->radar_online) snprintf(title, sizeof title, "NO DATA %.1fm", view->range_mm / 1000.0);
    set_text(heading, title);
    radar_view_draw(pixels, &status->raw, status->radar_online, view->range_mm,
                    (unsigned)((uint64_t)esp_timer_get_time() / 1000));
    lv_image_cache_drop(&image);
    lv_obj_invalidate(plot);
    show_layer(s_radar_layer);
    dv_activity();
#else
    (void)status; (void)view;
#endif
}

static void render_text(const robot_ui_t *view)
{
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    static lv_obj_t *message;
    if (!s_text_layer) {
        s_text_layer = make_layer(lv_screen_active());
        message = make_label(s_text_layer, &aihook_font_16, 5, 6, 150, 68, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    set_text(message, view->text);
    lv_obj_set_style_text_color(message, color_hex(view->text_color), LV_PART_MAIN);
    show_layer(s_text_layer);
    dv_activity();
#else
    (void)view;
#endif
}
void dv_render_robot(const robot_status_t *status, const robot_ui_t *view)
{
    if (view->mode == UI_RADAR) render_radar(status, view);
    else if (view->mode == UI_TEXT) render_text(view);
    else render_face(status, view);
}


void dv_render_basketball(const basketball_view_t *game)
{
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    static lv_obj_t *court, *results, *hoop, *rim, *ball;
    static lv_obj_t *clock_label, *score_label, *shots_label, *hint;
    static lv_obj_t *result_made, *result_score, *result_accuracy;
    if (!s_game_layer) {
        s_game_layer = make_layer(lv_screen_active());
        court = make_layer(s_game_layer);
        results = make_layer(s_game_layer);
        clock_label = make_label(court, &aihook_font_12, 3, 0, 84, 14, LV_LABEL_LONG_MODE_CLIP);
        score_label = make_label(court, &aihook_font_12, 89, 0, 68, 14, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(score_label, color_hex(0xFDB927), LV_PART_MAIN);
        /* Central lane is the exact +/-10px hit window, not a random reward. */
        make_rect(court, 70, 17, 21, 45, 0x102C22, 0);
        for (int y = 18; y < 61; y += 6) make_rect(court, 80, y, 1, 3, 0x54836E, 0);
        make_rect(court, 2, 62, 156, 1, 0x59406C, 0);
        make_rect(court, 69, 61, 2, 3, 0x57A77E, 0);
        make_rect(court, 90, 61, 2, 3, 0x57A77E, 0);
        hoop = lv_obj_create(court);
        style_plain(hoop);
        lv_obj_set_size(hoop, 32, 32);
        make_rect(hoop, 2, 0, 28, 14, 0xE4E6F0, 1);
        make_rect(hoop, 4, 2, 24, 10, 0x38224A, 0);
        make_rect(hoop, 10, 7, 12, 7, 0xF9A12A, 0);
        make_rect(hoop, 12, 9, 8, 4, 0x38224A, 0);
        rim = make_rect(hoop, 1, 15, 30, 3, 0xFF922F, 0);
        for (int row = 0; row < 4; ++row) {
            make_rect(hoop, 4 + row, 19 + row * 3, 2, 3, 0xBAC0CC, 0);
            make_rect(hoop, 26 - row, 19 + row * 3, 2, 3, 0xBAC0CC, 0);
            make_rect(hoop, 6 + row, 20 + row * 3, 20 - row * 2, 1, 0x8890A4, 0);
        }
        ball = make_rect(court, 76, 61, 8, 8, 0xFF922F, 4);
        make_rect(ball, 3, 0, 1, 8, 0x51321C, 0);
        make_rect(ball, 0, 3, 8, 1, 0x51321C, 0);
        shots_label = make_label(court, &aihook_font_12, 3, 67, 80, 13, LV_LABEL_LONG_MODE_CLIP);
        hint = make_label(court, &aihook_font_12, 83, 67, 75, 13, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_t *title = make_label(results, &aihook_font_12, 3, 0, 154, 14, LV_LABEL_LONG_MODE_CLIP);
        set_text(title, "时间到!");
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        result_made = make_label(results, &aihook_font_16, 3, 17, 154, 20, LV_LABEL_LONG_MODE_CLIP);
        result_score = make_label(results, &aihook_font_16, 3, 36, 154, 20, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_color(result_score, color_hex(0xFDB927), LV_PART_MAIN);
        result_accuracy = make_label(results, &aihook_font_12, 3, 55, 154, 13, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_t *again = make_label(results, &aihook_font_12, 2, 68, 156, 12, LV_LABEL_LONG_MODE_CLIP);
        set_text(again, "短按菜单 / 三击菜单");
        lv_obj_set_style_text_align(result_made, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(result_score, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(result_accuracy, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(again, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    char text[80];
    bool ended = game->phase == BASKETBALL_RESULT;
    set_hidden(court, ended);
    set_hidden(results, !ended);
    if (ended) {
        snprintf(text, sizeof text, "投中 %lu 次", (unsigned long)game->made);
        set_text(result_made, text);
        snprintf(text, sizeof text, "得分 %lu", (unsigned long)game->score);
        set_text(result_score, text);
        unsigned accuracy = game->shots ? (unsigned)(game->made * 100 / game->shots) : 0;
        snprintf(text, sizeof text, "命中率 %u%%  共 %lu 次", accuracy, (unsigned long)game->shots);
        set_text(result_accuracy, text);
    } else {
        snprintf(text, sizeof text, "剩余 %02u:%02u", game->seconds_left / 60, game->seconds_left % 60);
        set_text(clock_label, text);
        snprintf(text, sizeof text, "得分 %lu", (unsigned long)game->score);
        set_text(score_label, text);
        snprintf(text, sizeof text, "进 %lu/%lu", (unsigned long)game->made, (unsigned long)game->shots);
        set_text(shots_label, text);
        set_text(hint, game->in_flight ? game->last_hit ? "命中! +2" : "没进" : "按键投篮");
        lv_obj_set_style_text_color(hint, color_hex(game->in_flight
            ? game->last_hit ? 0x47E6AE : 0xFF6B7A : 0xC178FF), LV_PART_MAIN);
        lv_obj_set_pos(hoop, (int)lroundf(game->hoop_x) - 16, 18);
        bool centered = fabsf(game->hoop_x - 80) <= BASKETBALL_HIT_PX;
        lv_obj_set_style_bg_color(rim, color_hex(centered ? 0x47E6AE : 0xFF922F), LV_PART_MAIN);
        lv_obj_set_pos(ball, (int)lroundf(game->ball_x) - 4, (int)lroundf(game->ball_y) - 4);
    }
    show_layer(s_game_layer);
    dv_activity();
#else
    (void)game;
#endif
}

void dv_render_stock(const remote_game_view_t *v)
{
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    static lv_obj_t *chart, *header, *footer, *panel, *title, *profit, *detail, *hint;
    static lv_image_dsc_t image;
    if (!s_stock_layer) {
        s_stock_layer = make_layer(lv_screen_active());
        header = make_label(s_stock_layer, &aihook_font_12, 2, 0, 156, 13, LV_LABEL_LONG_MODE_CLIP);
        image.header.magic = LV_IMAGE_HEADER_MAGIC;
        image.header.cf = LV_COLOR_FORMAT_RGB565;
        image.header.w = 160; image.header.h = 48; image.header.stride = 320;
        image.data_size = 160 * 48 * 2; image.data = (const uint8_t *)s_plot_pixels;
        chart = lv_image_create(s_stock_layer);
        lv_obj_set_pos(chart, 0, 15); lv_image_set_src(chart, &image);
        footer = make_label(s_stock_layer, &aihook_font_12, 2, 66, 156, 14, LV_LABEL_LONG_MODE_CLIP);
        panel = make_layer(s_stock_layer);
        title = make_label(panel, &aihook_font_12, 2, 0, 156, 16, LV_LABEL_LONG_MODE_CLIP);
        profit = make_label(panel, &aihook_font_16, 2, 19, 156, 22, LV_LABEL_LONG_MODE_CLIP);
        detail = make_label(panel, &aihook_font_12, 2, 43, 156, 16, LV_LABEL_LONG_MODE_CLIP);
        hint = make_label(panel, &aihook_font_12, 2, 65, 156, 15, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(profit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    char text[120];
    bool overlay = !v->active || v->offline || v->phase != REMOTE_PLAY;
    set_hidden(panel, !overlay);
    if (overlay) {
        set_text(title, !v->active ? "等待PC连接" : v->offline ? "PC 已断开" : v->phase == REMOTE_LOADING ? "加载下一天..." : "做T结束");
        snprintf(text, sizeof text, "收益 %+.2f%%", v->pct_bps / 100.0);
        set_text(profit, v->active ? text : "做T模拟");
        lv_obj_set_style_text_color(profit, color_hex(v->pct_bps >= 0 ? 0xFF6363 : 0x47E6AE), LV_PART_MAIN);
        snprintf(text, sizeof text, "%+.2f元  交易%u次", v->pnl_cents / 100.0, v->trades);
        set_text(detail, v->active ? text : "运行 stock-t.sh");
        set_text(hint, !v->active || v->offline ? "短按返回菜单" : "长按继续 / 短按退出");
    } else {
        int32_t price = v->count ? v->bars[v->count - 1].close : 0;
        snprintf(text, sizeof text, "%s %s %.2f", v->code, v->clock, price / 100.0);
        set_text(header, text);
        snprintf(text, sizeof text, "%s %+.2f%%  30/60", v->holding ? "持仓" : "空仓", v->pct_bps / 100.0);
        set_text(footer, text);
        stock_chart_draw(s_plot_pixels, v);
        lv_image_cache_drop(&image); lv_obj_invalidate(chart);
    }
    show_layer(s_stock_layer); dv_activity();
#else
    (void)v;
#endif
}

#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
static void menu_arc_x(void *object, int32_t value) { lv_obj_set_x((lv_obj_t *)object, value); }
#endif
void dv_render_launcher(const app_view_t *view)
{
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
    static lv_obj_t *ring, *titles[3];
    static int previous = -1;
    if (!s_menu_layer) {
        s_menu_layer = make_layer(lv_screen_active());
        lv_obj_t *instructions = make_label(s_menu_layer, &aihook_font_12, 4, 0, 152, 16, LV_LABEL_LONG_MODE_CLIP);
        set_text(instructions, "短按切换  长按进入");
        lv_obj_set_style_text_align(instructions, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        const char *names[] = {"投篮", "做T", "雷达"};
        for (int i = 0; i < 3; ++i) {
            lv_obj_t *icon = lv_obj_create(s_menu_layer); style_plain(icon);
            lv_obj_set_size(icon, 28, 28); lv_obj_set_pos(icon, 18 + i * 48, 24);
            if (i == 0) {
                lv_obj_t *ball = make_rect(icon, 2, 2, 24, 24, 0xFF922F, 12);
                make_rect(ball, 11, 0, 2, 24, 0x573122, 0);
                make_rect(ball, 0, 11, 24, 2, 0x573122, 0);
                make_rect(ball, 5, 1, 1, 22, 0xA85C28, 0);
                make_rect(ball, 18, 1, 1, 22, 0xA85C28, 0);
            } else if (i == 1) {
                for (int k = 0; k < 4; ++k) {
                    int top = 16 - k * 4;
                    uint32_t color = k == 1 ? 0x47E6AE : 0xFF6363;
                    make_rect(icon, 3 + k * 6, top, 1, 12, color, 0);
                    make_rect(icon, 1 + k * 6, top + 3, 5, 6, color, 0);
                }
            } else {
                for (int k = 0; k < 3; ++k) {
                    lv_obj_t *arc = lv_arc_create(icon);
                    lv_obj_set_size(arc, 28 - k * 8, 28 - k * 8); lv_obj_set_pos(arc, k * 4, k * 4);
                    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
                    lv_arc_set_bg_angles(arc, 180, 360); lv_arc_set_range(arc, 0, 100); lv_arc_set_value(arc, 0);
                    lv_obj_set_style_arc_color(arc, color_hex(0x47E6AE), LV_PART_MAIN);
                    lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
                }
                make_rect(icon, 13, 0, 2, 14, 0x47E6AE, 0);
            }
            titles[i] = make_label(s_menu_layer, &aihook_font_12, 8 + i * 48, 61, 48, 16, LV_LABEL_LONG_MODE_CLIP);
            set_text(titles[i], names[i]); lv_obj_set_style_text_align(titles[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        }
        ring = lv_arc_create(s_menu_layer); lv_obj_set_size(ring, 42, 42); lv_obj_set_pos(ring, 11, 17);
        lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
        lv_arc_set_bg_angles(ring, 0, 360); lv_arc_set_rotation(ring, 270);
        lv_arc_set_range(ring, 0, APP_CONFIRM_MS);
        lv_obj_set_style_arc_width(ring, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ring, color_hex(0x513663), LV_PART_MAIN);
        lv_obj_set_style_arc_width(ring, 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(ring, color_hex(0xFDB927), LV_PART_INDICATOR);
    }
    unsigned selected = view->selected % 3;
    if (previous != (int)selected) {
        int target = 11 + selected * 48;
        lv_anim_delete(ring, NULL);
        if (previous < 0) lv_obj_set_x(ring, target);
        else {
            lv_anim_t anim; lv_anim_init(&anim); lv_anim_set_var(&anim, ring);
            lv_anim_set_values(&anim, lv_obj_get_x(ring), target); lv_anim_set_exec_cb(&anim, menu_arc_x);
            lv_anim_set_duration(&anim, 160); lv_anim_set_path_cb(&anim, lv_anim_path_ease_out); lv_anim_start(&anim);
        }
        previous = selected;
    }
    for (unsigned i = 0; i < 3; ++i)
        lv_obj_set_style_text_color(titles[i], color_hex(i == selected ? 0xFDB927 : 0x8A8196), LV_PART_MAIN);
    lv_arc_set_value(ring, view->hold_ms);
    show_layer(s_menu_layer); dv_activity();
#else
    (void)view;
#endif
}
