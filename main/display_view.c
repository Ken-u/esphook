/* main/display_view.c
 * ST7735 0.96" 80x160 横屏，用 esp_lcd 框架。
 * panel_io(SPI) + ST7789 驱动复用(ST7735 命令集兼容)。
 * 额外手动发 ST7735 专属 init 序列(FRMCTR/PWCTR/GAMMA/INVON)，
 * 再用 esp_lcd 的 set_gap/mirror/invert/disp_on_off 完成配置。
 *
 * 验证参数(screen-test)：MADCTL=0x68(BGR+MV+MY), INVON, x_gap=1, y_gap=26
 */
#include "display_view.h"
#include "display_model.h"
#include "config_store.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_commands.h"
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

#define X_GAP 1   /* 横屏 x 偏移(rowstart) */
#define Y_GAP 26   /* 横屏 y 偏移(colstart) */

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
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

esp_err_t dv_init(void)
{
    /* 背光 */
    gpio_set_direction(PIN_BLK, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BLK, 1);

    /* SPI 总线 */
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_SDA,
        .sclk_io_num = PIN_SCL,
        .miso_io_num = -1,
        .max_transfer_sz = DV_WIDTH * DV_HEIGHT * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* panel IO：esp_lcd 自动管理 DC/CS 切换 */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_CS,
        .dc_gpio_num = PIN_DC,
        .spi_mode = 0,
        .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &s_io));

    /* ST7789 驱动复用（ST7735 命令兼容） */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel));

    /* 1. 复位 */
    esp_lcd_panel_reset(s_panel);
    /* 2. ST7789 驱动的 init（SLPOUT + MADCTL + COLMOD + RAMCTRL）。
       ST7735 不认 RAMCTRL(0xB0)，但 tx_param 发不存在的命令屏会忽略，无害。 */
    esp_lcd_panel_init(s_panel);

    /* 3. 补 ST7735 专属 init 序列（FRMCTR/PWCTR/GAMMA） */
    esp_lcd_panel_io_tx_param(s_io, 0xB1, (uint8_t[]){0x01,0x2C,0x2D}, 3);
    esp_lcd_panel_io_tx_param(s_io, 0xB2, (uint8_t[]){0x01,0x2C,0x2D}, 3);
    esp_lcd_panel_io_tx_param(s_io, 0xB3, (uint8_t[]){0x01,0x2C,0x2D,0x01,0x2C,0x2D}, 6);
    esp_lcd_panel_io_tx_param(s_io, 0xB4, (uint8_t[]){0x07}, 1);
    esp_lcd_panel_io_tx_param(s_io, 0xC0, (uint8_t[]){0xA2,0x02,0x84}, 3);
    esp_lcd_panel_io_tx_param(s_io, 0xC1, (uint8_t[]){0xC5}, 1);
    esp_lcd_panel_io_tx_param(s_io, 0xC2, (uint8_t[]){0x0A,0x00}, 2);
    esp_lcd_panel_io_tx_param(s_io, 0xC3, (uint8_t[]){0x8A,0x2A}, 2);
    esp_lcd_panel_io_tx_param(s_io, 0xC4, (uint8_t[]){0x8A,0xEE}, 2);
    esp_lcd_panel_io_tx_param(s_io, 0xC5, (uint8_t[]){0x0E}, 1);
    esp_lcd_panel_io_tx_param(s_io, 0xE0, (uint8_t[]){0x02,0x1c,0x07,0x12,0x37,0x32,0x29,0x2d,0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10}, 16);
    esp_lcd_panel_io_tx_param(s_io, 0xE1, (uint8_t[]){0x03,0x1d,0x07,0x06,0x2E,0x2C,0x29,0x2D,0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10}, 16);
    esp_lcd_panel_io_tx_param(s_io, 0x13, NULL, 0); /* NORON */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* MADCTL = 0x68 = MX|MY|MV|BGR。MX=mirror_x, MY=mirror_y, MV=swap_xy, BGR=rgb_ele_order。
       刚刚 mirror(false,true) 只有 MY 缺 MX，左右反；补 MX 即可。 */
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, true, true);  /* MX+MY → 0x68 */
    esp_lcd_panel_set_gap(s_panel, X_GAP, Y_GAP);
    esp_lcd_panel_invert_color(s_panel, true);  /* INVON */
    esp_lcd_panel_disp_on_off(s_panel, true);

    dv_clear();
    dv_flush();
    ESP_LOGI(TAG, "ST7735(via esp_lcd) initialized %dx%d", DV_WIDTH, DV_HEIGHT);
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
    /* esp_lcd_panel_draw_bitmap 自动加 gap 偏移、发 CASET/RASET/RAMWR。
       data_endian: st7789 驱动默认大端(RGB565 高字节先)，与帧缓冲一致。 */
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DV_WIDTH, DV_HEIGHT, s_fb);
}

/* ===== 布局渲染 ===== */

static int s_scroll = 0;

static const char *client_label(const char *client_id, char *out, size_t len)
{
    const char *r = config_get_display_name("client", client_id, out, len);
    if (r[0] != '\0') return r;
    const char *p = strchr(client_id, ':');
    strncpy(out, p ? p + 1 : client_id, len - 1);
    out[len - 1] = '\0';
    return out;
}

/* 画实心圆（中点画圆法，r<=10 够用） */
static void fill_circle(int cx, int cy, int r, uint16_t color)
{
    for (int yy = -r; yy <= r; yy++)
        for (int xx = -r; xx <= r; xx++)
            if (xx*xx + yy*yy <= r*r)
                dv_fill_rect(cx+xx, cy+yy, 1, 1, color);
}

static int draw_badge(int x, const char *client_id, uint16_t color)
{
    int unread = dm_client_unread(client_id);
    if (unread == 0) return x;
    char label[24];
    client_label(client_id, label, sizeof label);
    int n = (unread > 9) ? 9 : unread;

    /* 红点圆：圆心白字数字。圆 r=4，中心 (x+4, 4)，直径 9px 贴状态栏顶 */
    fill_circle(x + 4, 4, 4, DV_RED);
    char numbuf[12];
    snprintf(numbuf, sizeof numbuf, "%d", n);
    /* 数字居中画到圆心。5x7 字 size=1，宽约 3-6px，画在 (x+2, 1) */
    dv_draw_text(x + 2, 1, numbuf, DV_WHITE, 1);

    /* 圆右侧跟 client 名字 */
    int nx = x + 10;  /* 圆右边留 1px */
    dv_draw_text(nx, 1, label, DV_WHITE, 1);
    return nx + 6 * strlen(label) + 4;
}

void dv_render_status_bar(void)
{
    /* 红点圆 9px 高，状态栏 11px */
    dv_fill_rect(0, 0, DV_WIDTH, 11, DV_BLACK);
    int x = 2 - s_scroll;
    int n = dm_client_count();
    for (int i = 0; i < n; i++) {
        char cid[24];
        dm_client_at(i, cid, sizeof cid);
        x = draw_badge(x, cid, DV_RED);
        if (x > DV_WIDTH) break;
    }
    dv_fill_rect(0, 11, DV_WIDTH, 1, DV_GRAY);
}

void dv_render_main(void)
{
    dv_fill_rect(0, 12, DV_WIDTH, DV_HEIGHT - 12 - 9, DV_BLACK);
    char client[24], session[48], title[32], body[128];
    if (!dm_current(client, session, sizeof client, sizeof session,
                    title, sizeof title, body, sizeof body)) {
        dv_render_idle();
        return;
    }
    dv_draw_text(2, 14, title, DV_WHITE, DV_SIZE_TITLE);
    dv_draw_text(2, 40, body, DV_YELLOW, DV_SIZE_BODY);
    char cname[24], sname[24];
    const char *cn = config_get_display_name("client", client, cname, sizeof cname);
    const char *sn = config_get_display_name("session", client, sname, sizeof sname);
    char src[96];
    snprintf(src, sizeof src, "[%s/%s]", cn[0] ? cn : client, sn[0] ? sn : session);
    dv_draw_text(2, 66, src, DV_GRAY, DV_SIZE_BAR);
    dv_fill_rect(0, DV_HEIGHT - 9, DV_WIDTH, 1, DV_GRAY);
}

void dv_render_idle(void)
{
    dv_draw_text(2, 35, "idle", DV_GRAY, DV_SIZE_BODY);
}

void dv_render_net(int state, const char *ip)
{
    dv_clear();
    if (state == 0) {
        dv_draw_text(4, 18, "no wifi cfg", DV_RED, DV_SIZE_NET);
        dv_draw_text(4, 38, "esphook provision", DV_WHITE, DV_SIZE_NET);
        dv_draw_text(4, 50, "<ssid> <pass>", DV_WHITE, DV_SIZE_NET);
    } else if (state == 1) {
        dv_draw_text(20, 35, "connecting...", DV_YELLOW, DV_SIZE_NET);
    } else {
        dv_draw_text(16, 18, "wifi connected", DV_GREEN, DV_SIZE_NET);
        char buf[32];
        snprintf(buf, sizeof buf, "ip: %s", ip ? ip : "");
        dv_draw_text(16, 40, buf, DV_WHITE, DV_SIZE_NET);
    }
}

void dv_tick_scroll(void)
{
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
