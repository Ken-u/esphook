#include "display_view.h"
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
#include <stdint.h>
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
#define DISPLAY_IDLE_TIMEOUT_US (10LL * 60LL * 1000000LL)

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static lv_display_t *s_display;
static esp_timer_handle_t s_lv_tick_timer;
static int64_t s_last_activity_us;
static bool s_screen_awake;
static bool s_initialized;
bool dv_is_initialized(void) { return s_initialized; }
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

    s_initialized=true;
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

