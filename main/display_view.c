/* main/display_view.c
 * ST7735 SPI 驱动 + 5x7 字库 + 布局。
 * 字库：Adafruit glcdfont 公共域 0x20~0x7E 段（95 字符 × 5 列）。
 */
#include "display_view.h"
#include "display_model.h"
#include "config_store.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "dv";
#define PIN_CS  0
#define PIN_DC  3
#define PIN_RST 2
#define PIN_SCL 4
#define PIN_SDA 5
#define PIN_BLK 1

static spi_device_handle_t s_spi;
static uint16_t s_fb[DV_WIDTH * DV_HEIGHT];

/* Adafruit glcdfont 5x7，0x20 起 95 字符。公共域。 */
static const uint8_t FONT[][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},
  {0x3E,0x41,0x41,0x51,0x32},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
  {0x40,0x40,0x40,0x40,0x40},{0x00,0x03,0x07,0x08,0x00},{0x20,0x54,0x54,0x54,0x78},
  {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x28,0x28},{0x38,0x44,0x44,0x48,0x7F},
  {0x38,0x54,0x54,0x54,0x18},{0x00,0x08,0x7E,0x09,0x02},{0x18,0xA4,0xA4,0x9C,0x78},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x40,0x3D,0x00},
  {0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
  {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0xFC,0x24,0x24,0x24,0x18},
  {0x18,0x24,0x24,0x18,0xFC},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x24},
  {0x04,0x04,0x3F,0x44,0x24},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
  {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x4C,0x50,0x50,0x50,0x3C},
  {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x77,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00},{0x02,0x01,0x02,0x04,0x02},{0x3C,0x26,0x23,0x26,0x3C},
};

static void dc_cmd(void) { gpio_set_level(PIN_DC, 0); }
static void dc_data(void) { gpio_set_level(PIN_DC, 1); }

static void spi_send_cmd(uint8_t cmd)
{
    dc_cmd();
    spi_transaction_t t = {0};
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_send_data(const uint8_t *data, int len)
{
    dc_data();
    spi_transaction_t t = {0};
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(s_spi, &t);
}

static void st7735_init(void)
{
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    spi_send_cmd(0x11); /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_send_cmd(0x3A); /* COLMOD 16bit */
    uint8_t colmod = 0x05;
    spi_send_data(&colmod, 1);

    /* MINI160x80：列地址 0..80，行地址 0..160（横屏后宽160高80） */
    spi_send_cmd(0x2A); uint8_t cx[4] = {0,0,0,80}; spi_send_data(cx,4);
    spi_send_cmd(0x2B); uint8_t cy[4] = {0,26,0,154}; spi_send_data(cy,4);

    /* MADCTL: 横屏。0x60 = MV+MX (横屏，颜色正常)。
       若颜色错换 0x60/0xA0；若镜像换 MX/MY 位。 */
    spi_send_cmd(0x36); uint8_t madctl = 0x60; spi_send_data(&madctl,1);

    spi_send_cmd(0x29); /* DISPON */
}

esp_err_t dv_init(void)
{
    gpio_set_direction(PIN_BLK, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BLK, 1);
    gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS, 1);

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_SDA,
        .sclk_io_num = PIN_SCL,
        .miso_io_num = -1,
        .max_transfer_sz = DV_WIDTH * DV_HEIGHT * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 24 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 6,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi));

    st7735_init();
    dv_clear();
    dv_flush();
    ESP_LOGI(TAG, "ST7735 initialized %dx%d", DV_WIDTH, DV_HEIGHT);
    return ESP_OK;
}

void dv_clear(void)
{
    for (int i = 0; i < DV_WIDTH * DV_HEIGHT; i++) s_fb[i] = DV_BLACK;
}

void dv_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h && yy < DV_HEIGHT; yy++)
        for (int xx = x; xx < x + w && xx < DV_WIDTH; xx++)
            if (xx >= 0 && yy >= 0) s_fb[yy * DV_WIDTH + xx] = color;
}

void dv_draw_text(int x, int y, const char *s, uint16_t color, int size)
{
    int cx = x;
    for (int i = 0; s[i]; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x20 || c > 0x7E) c = ' ';
        c -= 0x20;
        for (int col = 0; col < 5; col++) {
            uint8_t line = FONT[c][col];
            for (int row = 0; row < 7; row++) {
                if (line & (1 << (6 - row))) {
                    dv_fill_rect(cx + col * size, y + row * size, size, size, color);
                }
            }
        }
        cx += 6 * size;
        if (cx >= DV_WIDTH) break;
    }
}

void dv_flush(void)
{
    gpio_set_level(PIN_CS, 0);
    spi_send_cmd(0x2C);
    static uint8_t buf[DV_WIDTH * DV_HEIGHT * 2];
    for (int i = 0; i < DV_WIDTH * DV_HEIGHT; i++) {
        buf[i * 2] = s_fb[i] >> 8;
        buf[i * 2 + 1] = s_fb[i] & 0xFF;
    }
    dc_data();
    spi_transaction_t t = {0};
    t.length = sizeof(buf) * 8;
    t.tx_buffer = buf;
    spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(PIN_CS, 1);
}

/* ===== 布局渲染 ===== */

static int s_scroll = 0;

/* 取 client 显示名：有映射用映射，否则取冒号后简写。写到 out，返回 out。 */
static const char *client_label(const char *client_id, char *out, size_t len)
{
    const char *r = config_get_display_name("client", client_id, out, len);
    if (r[0] != '\0') return r;
    const char *p = strchr(client_id, ':');
    strncpy(out, p ? p + 1 : client_id, len - 1);
    out[len - 1] = '\0';
    return out;
}

/* 画一个客户端红点徽章，返回下一个 x。unread=0 则不画，返回原 x。 */
static int draw_badge(int x, const char *client_id, uint16_t color)
{
    int unread = dm_client_unread(client_id);
    if (unread == 0) return x;
    char label[24];
    client_label(client_id, label, sizeof label);
    int n = (unread > 9) ? 9 : unread;
    char buf[48];
    snprintf(buf, sizeof buf, "%d%s ", n, label);
    int w = 6 * strlen(buf);
    dv_draw_text(x, 1, buf, color, 1);
    return x + w;
}

void dv_render_status_bar(void)
{
    dv_fill_rect(0, 0, DV_WIDTH, 12, DV_BLACK);
    int x = 2 - s_scroll;
    int n = dm_client_count();
    for (int i = 0; i < n; i++) {
        char cid[24];
        dm_client_at(i, cid, sizeof cid);
        x = draw_badge(x, cid, DV_RED);
        if (x > DV_WIDTH) break;
    }
    dv_fill_rect(0, 12, DV_WIDTH, 1, DV_GRAY);
}

void dv_render_main(void)
{
    dv_fill_rect(0, 13, DV_WIDTH, DV_HEIGHT - 13 - 9, DV_BLACK);
    char client[24], session[48], title[32], body[128];
    if (!dm_current(client, session, sizeof client, sizeof session,
                    title, sizeof title, body, sizeof body)) {
        dv_render_idle();
        return;
    }
    dv_draw_text(2, 16, title, DV_WHITE, 1);
    dv_draw_text(2, 28, body, DV_YELLOW, 1);
    char cname[24], sname[24];
    const char *cn = config_get_display_name("client", client, cname, sizeof cname);
    const char *sn = config_get_display_name("session", session, sname, sizeof sname);
    char src[96];
    snprintf(src, sizeof src, "[%s/%s]", cn[0] ? cn : client, sn[0] ? sn : session);
    dv_draw_text(2, 50, src, DV_GRAY, 1);
    dv_fill_rect(0, DV_HEIGHT - 9, DV_WIDTH, 1, DV_GRAY);
}

void dv_render_idle(void)
{
    dv_draw_text(2, 35, "idle", DV_GRAY, 1);
}

void dv_render_net(int state, const char *ip)
{
    dv_clear();
    if (state == 0) {
        dv_draw_text(4, 18, "no wifi cfg", DV_RED, 1);
        dv_draw_text(4, 38, "esphook provision", DV_WHITE, 1);
        dv_draw_text(4, 50, "<ssid> <pass>", DV_WHITE, 1);
    } else if (state == 1) {
        dv_draw_text(20, 35, "connecting...", DV_YELLOW, 1);
    } else {
        dv_draw_text(16, 18, "wifi connected", DV_GREEN, 1);
        char buf[32];
        snprintf(buf, sizeof buf, "ip: %s", ip ? ip : "");
        dv_draw_text(16, 40, buf, DV_WHITE, 1);
    }
}

void dv_tick_scroll(void)
{
    /* 估算所有徽章总宽，超屏才滚动，否则归零。简化：每帧 +2，到末尾回卷。 */
    int n = dm_client_count();
    int total = 0;
    for (int i = 0; i < n; i++) {
        char cid[24];
        dm_client_at(i, cid, sizeof cid);
        int u = dm_client_unread(cid);
        if (u == 0) continue;
        char label[24];
        client_label(cid, label, sizeof label);
        char buf[48];
        snprintf(buf, sizeof buf, "%d%s ", (u > 9 ? 9 : u), label);
        total += 6 * strlen(buf);
    }
    if (total <= DV_WIDTH - 4) {
        s_scroll = 0;
        return;
    }
    s_scroll += 2;
    if (s_scroll > total) s_scroll = 0;
}
