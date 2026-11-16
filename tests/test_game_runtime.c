#include "game_runtime.h"
#include "robot_ui.h"
#include "usb_cdc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t clock_ms;
static char output[256];
static unsigned resets;
static struct { unsigned size, count, read, capacity; unsigned char data[2048]; } queue;
static bool locked;
int64_t esp_timer_get_time(void) { return (int64_t)clock_ms * 1000; }
uint32_t esp_random(void) { return 24; }
void *xQueueCreate(unsigned count, unsigned size)
{ assert(count * size <= sizeof queue.data); queue.capacity = count; queue.size = size; return &queue; }
int xQueueSend(void *q, const void *value, unsigned wait)
{
    (void)q; (void)wait; if (queue.count == queue.capacity) return 0;
    unsigned offset = (queue.read + queue.count) % queue.capacity;
    memcpy(queue.data + offset * queue.size, value, queue.size); ++queue.count; return 1;
}
int xQueueReceive(void *q, void *value, unsigned wait)
{
    (void)q; (void)wait; if (!queue.count) return 0;
    memcpy(value, queue.data + queue.read * queue.size, queue.size);
    queue.read = (queue.read + 1) % queue.capacity; --queue.count; return 1;
}
void *xSemaphoreCreateMutex(void) { return &locked; }
int xSemaphoreTake(void *semaphore, unsigned wait) { (void)semaphore; (void)wait; assert(!locked); locked = true; return 1; }
int xSemaphoreGive(void *semaphore) { (void)semaphore; assert(locked); locked = false; return 1; }
esp_err_t cdc_write(const char *line) { snprintf(output, sizeof output, "%s", line); return ESP_OK; }
esp_err_t robot_ui_submit(const char *json, size_t length, char *error, size_t size)
{ (void)json; (void)length; (void)error; (void)size; ++resets; return ESP_OK; }
static void tick(uint64_t now) { clock_ms = now; game_runtime_tick(now); }
static void edge(bool pressed, uint64_t now) { game_post_button(pressed, now); tick(now); }
static void app(app_kind_t expected, unsigned selected)
{ app_view_t v; game_get_app_view(&v);
  if (v.app != expected || v.selected != selected)
      fprintf(stderr, "at %llu: app=%d selected=%u, expected=%d/%u\n", (unsigned long long)clock_ms, v.app, v.selected, expected, selected);
  assert(v.app == expected && v.selected == selected); }
int main(void)
{
    assert(game_runtime_init() == ESP_OK);
    tick(0); app(APP_IDLE, 0);
    /* Data upload and PC heartbeat never steal focus from standby/menu. */
    assert(game_command("game {\"v\":1,\"seq\":1,\"id\":24,\"op\":\"begin\",\"count\":1,\"first\":0,\"code\":\"600000\",\"date\":\"2026-11-12\",\"label\":\"test\"}"));
    assert(game_command("game {\"v\":1,\"seq\":2,\"id\":24,\"op\":\"bars\",\"offset\":0,\"bars\":[[100,110,90,105,103,104]]}"));
    assert(game_command("game {\"v\":1,\"seq\":3,\"id\":24,\"op\":\"show\",\"cursor\":0,\"state\":\"play\",\"held_ms\":0,\"trades\":0,\"pct_bps\":0,\"baseline_bps\":0,\"pnl_cents\":0,\"holding\":false,\"clock\":\"09:35\"}"));
    assert(strstr(output, "\"ok\":true") && strstr(output, "\"selected\":false"));
    tick(10); app(APP_IDLE, 0);
    edge(true, 100); edge(false, 150); app(APP_MENU, 0);
    tick(10150); app(APP_IDLE, 0); assert(resets == 1);
    edge(true, 20000); edge(false, 20050); app(APP_MENU, 0);
    edge(true, 21000); tick(21499); app(APP_MENU, 0);
    tick(21500); app(APP_BASKETBALL, 0);
    edge(false, 21550);
    basketball_view_t b; game_get_basketball_view(&b); assert(b.shots == 0);
    edge(true, 22000); edge(false, 22050); game_get_basketball_view(&b); assert(b.shots == 1);
    edge(true, 23000); edge(false, 23050);
    edge(true, 23150); edge(false, 23200);
    edge(true, 23300); app(APP_MENU, 0); edge(false, 23350);
    /* A short switches, a 500ms hold launches stock, release does not sell. */
    edge(true, 24000); edge(false, 24050); app(APP_MENU, 1);
    assert(game_command("game {\"v\":1,\"seq\":4,\"id\":24,\"op\":\"heartbeat\"}"));
    edge(true, 25000); tick(25500); app(APP_STOCK, 1);
    assert(strstr(output, "\"action\":\"launch\""));
    edge(false, 25550); assert(strstr(output, "\"action\":\"launch\""));
    edge(true, 26000); assert(strstr(output, "\"action\":\"down\""));
    tick(26500); assert(strstr(output, "\"action\":\"hold\""));
    edge(false, 26600); assert(strstr(output, "\"action\":\"up\""));
    edge(true, 27000); edge(false, 27050);
    edge(true, 27150); edge(false, 27200);
    edge(true, 27300); app(APP_MENU, 1); assert(strstr(output, "\"action\":\"menu\""));
    edge(false, 27350); tick(37400); app(APP_IDLE, 1);
    assert(game_command("game {\"v\":1,\"seq\":4,\"id\":24,\"op\":\"heartbeat\"}"));
    assert(strstr(output, "\"selected\":false"));
    edge(true, 40000); edge(false, 40050); app(APP_MENU, 1);
    edge(true, 41000); edge(false, 41050); app(APP_MENU, 2);
    edge(true, 42000); tick(42500); app(APP_RADAR, 2);
    edge(false, 42550); app(APP_RADAR, 2);
    edge(true, 43000); edge(false, 43050); app(APP_MENU, 2);
    edge(true, 43150); edge(false, 43200);
    edge(true, 43300); app(APP_MENU, 0); edge(false, 43350);
    puts("game runtime: launcher, 10s idle, confirmation, releases, triple navigation and PC focus passed");
}
