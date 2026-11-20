#include "app_hardware.h"
#include "robot_runtime.h"
#include "motion_ports.h"
#include "nvs.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static app_hardware_profile_t profile;
static unsigned servo_inits,radar_inits,servo_writes,tasks;
static bool fail_servo,fail_radar;
static int tx_pin;
app_hardware_profile_t app_hardware_active(void) { return profile; }
esp_err_t servo_port_init(int gpio,unsigned hz) { assert(gpio==10 && hz==50);++servo_inits;return fail_servo?ESP_ERR_NOT_SUPPORTED:ESP_OK; }
esp_err_t servo_port_write(int pulse) { (void)pulse;++servo_writes;return ESP_OK; }
esp_err_t radar_port_init(int rx,int tx,unsigned baud) { assert(rx==20 && baud==256000);tx_pin=tx;++radar_inits;return fail_radar?ESP_ERR_NOT_SUPPORTED:ESP_OK; }
void radar_port_close(void) { }
void radar_port_poll(void (*cb)(const ld2450_frame_t *,uint64_t,void *),void *u) { (void)cb;(void)u;assert(false); }
const ld2450_frame_t *radar_port_frame(void) { static ld2450_frame_t f;return &f; }
const ld2450_parser_t *radar_port_parser(void) { static ld2450_parser_t p;return &p; }
int64_t esp_timer_get_time(void) { return 100000; }
esp_err_t cdc_write(const char *s) { (void)s;return ESP_OK; }
void *xQueueCreate(unsigned n,unsigned size) { assert(n && size);return &tasks; }
int xQueueSend(void *q,const void *v,unsigned wait) { (void)q;(void)v;(void)wait;return 1; }
int xQueueReceive(void *q,void *v,unsigned wait) { (void)q;(void)v;(void)wait;return 0; }
unsigned uxQueueMessagesWaiting(void *q) { (void)q;return 0; }
int xQueueReset(void *q) { (void)q;return 1; }
void vQueueDelete(void *q) { (void)q; }
int xTaskCreate(void (*f)(void *),const char *n,unsigned stack,void *u,unsigned priority,TaskHandle_t *h)
{ assert(f && n && stack && priority);(void)u;(void)h;++tasks;return pdPASS; }
void vTaskDelay(unsigned ticks) { (void)ticks;assert(false); }
esp_err_t nvs_open(const char *n,int mode,nvs_handle_t *h) { (void)n;(void)mode;(void)h;return ESP_ERR_NOT_SUPPORTED; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h;return ESP_OK; }
esp_err_t nvs_get_i32(nvs_handle_t h,const char *k,int32_t *v) { (void)h;(void)k;(void)v;return ESP_ERR_NOT_SUPPORTED; }
esp_err_t nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) { (void)h;(void)k;(void)v;return ESP_ERR_NOT_SUPPORTED; }
esp_err_t nvs_set_i32(nvs_handle_t h,const char *k,int32_t v) { (void)h;(void)k;(void)v;return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v) { (void)h;(void)k;(void)v;return ESP_OK; }
int main(int argc,char **argv)
{
    assert(argc>=2 && app_hardware_profile_parse(argv[1],&profile));
    fail_servo=argc>2 && !strcmp(argv[2],"servo");fail_radar=argc>2 && !strcmp(argv[2],"radar");
    app_hardware_policy_t policy=app_hardware_policy(profile);
    assert(robot_start()==ESP_OK);assert(servo_writes==0);
    assert(servo_inits==(unsigned)policy.servo && radar_inits==(unsigned)policy.radar);
    if(profile==APP_PROFILE_AUTO) { assert(!servo_inits && policy.physical_keys==3 && tx_pin==-1);assert(!robot_servo_request(0,0)); }
    if(profile==APP_PROFILE_SCREEN) { assert(!tasks && !servo_inits && !radar_inits && policy.physical_keys==3); }
    robot_status_t status;robot_get_status(&status);
    assert(status.servo_ready==(policy.servo&&!fail_servo));assert(status.radar_ready==(policy.radar&&!fail_radar));
    printf("hardware boot: %s servo_failure=%d radar_failure=%d, no output pulse, independent optional drivers passed\n",argv[1],fail_servo,fail_radar);
}
