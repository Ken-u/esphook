#include "radar_view.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define ORIGIN_X (RADAR_VIEW_WIDTH / 2)
#define ORIGIN_Y (RADAR_VIEW_HEIGHT - 1)
#define RADIUS 61

static void pixel(uint16_t *image, int x, int y, uint16_t color)
{
    if (x >= 0 && x < RADAR_VIEW_WIDTH && y >= 0 && y < RADAR_VIEW_HEIGHT)
        image[y * RADAR_VIEW_WIDTH + x] = color;
}
uint16_t radar_target_color(unsigned i)
{
    static const uint16_t colors[] = {0xFDC5, 0x07FF, 0xF81F};
    return colors[i % 3];
}
bool radar_view_project(const ld2450_target_t *t, unsigned range, int *x, int *y)
{
    if (!range || !t->valid || t->y_mm <= 0 || hypotf(t->x_mm, t->y_mm) > range ||
        fabsf(atan2f(t->x_mm, t->y_mm)) > PI_F / 3) return false;
    *x = ORIGIN_X + (int)lroundf((float)t->x_mm * RADIUS / range);
    *y = ORIGIN_Y - (int)lroundf((float)t->y_mm * RADIUS / range);
    return *x >= 0 && *x < RADAR_VIEW_WIDTH && *y >= 0 && *y < RADAR_VIEW_HEIGHT;
}
static void ray(uint16_t *p, float angle, uint16_t color)
{
    for (int r = 0; r <= RADIUS; ++r)
        pixel(p, ORIGIN_X + (int)lroundf(sinf(angle) * r),
              ORIGIN_Y - (int)lroundf(cosf(angle) * r), color);
}
void radar_view_draw(uint16_t *p, const ld2450_frame_t *raw, bool online,
                     unsigned range, unsigned phase_ms)
{
    memset(p, 0, RADAR_VIEW_WIDTH * RADAR_VIEW_HEIGHT * sizeof *p);
    for (int ring = 1; ring <= 3; ++ring) {
        for (int degree = -60; degree <= 60; ++degree) {
            float a = degree * PI_F / 180;
            float r = RADIUS * ring / 3.0f;
            pixel(p, ORIGIN_X + (int)lroundf(sinf(a) * r),
                  ORIGIN_Y - (int)lroundf(cosf(a) * r), 0x2928);
        }
    }
    ray(p, -PI_F / 3, 0x414C);
    ray(p, 0, 0x2928);
    ray(p, PI_F / 3, 0x414C);
    if (online) {
        unsigned phase = phase_ms % 4000;
        float degree = -60 + (phase <= 2000 ? phase : 4000 - phase) * 0.06f;
        ray(p, degree * PI_F / 180, 0x21A9);
        /* Marker numbers are radar slot numbers, not persistent identities. */
        static const uint16_t digits[] = {0x2497, 0x73E7, 0x73CF};
        for (unsigned i = 0; i < LD2450_TARGETS; ++i) {
            int x, y;
            if (!radar_view_project(&raw->targets[i], range, &x, &y)) continue;
            for (int d = -2; d <= 2; ++d) {
                pixel(p, x + d, y, radar_target_color(i));
                pixel(p, x, y + d, radar_target_color(i));
            }
            /* Clamp annotations independently: never write beyond the image. */
            int label_x = x + 4 < RADAR_VIEW_WIDTH - 3 ? x + 4 : x - 6;
            int label_y = y > 5 ? y - 5 : y + 3;
            for (int bit = 0; bit < 15; ++bit)
                if (digits[i] & (1U << (14 - bit)))
                    pixel(p, label_x + bit % 3, label_y + bit / 3, radar_target_color(i));
        }
    }
    pixel(p, ORIGIN_X, ORIGIN_Y, 0xFFFF);
}
