#include "input_task.h"
#include "sdkconfig.h"
#include "esphook/app_runtime.h"
#include "esphook/robot_runtime.h"
#include "esphook/robot_ui.h"
#include "usb_cdc.h"
#include "config_store.h"
#include "net_wifi.h"
#include "device_link.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef CONFIG_DESKTOP_FOLLOW_ROBOT
static const int s_gpios[]={6};
#else
static const int s_gpios[]={6,10,11};
#endif
#define KEY_COUNT (sizeof s_gpios / sizeof s_gpios[0])
static void IRAM_ATTR button_isr(void *arg)
{
    BaseType_t wake=pdFALSE; vTaskNotifyGiveFromISR((TaskHandle_t)arg,&wake);
    if(wake) portYIELD_FROM_ISR();
}
static void input_task(void *arg)
{
    (void)arg; app_button_t buttons[KEY_COUNT]; memset(buttons,0,sizeof buttons);
    uint64_t now=(uint64_t)esp_timer_get_time()/1000;
    for(unsigned i=0;i<KEY_COUNT;++i) (void)app_button_sample(&buttons[i],gpio_get_level(s_gpios[i])==0,now);
    TickType_t wait=portMAX_DELAY;
    for(;;) {
        uint32_t edges=ulTaskNotifyTake(pdTRUE,wait); now=(uint64_t)esp_timer_get_time()/1000;
        wait=portMAX_DELAY;
        for(unsigned i=0;i<KEY_COUNT;++i) {
            app_button_t *b=&buttons[i]; bool level=gpio_get_level(s_gpios[i])==0;
            if(edges) app_button_edge(b,level,now);
            bool was=b->stable_pressed,down=app_button_sample(b,level,now);
            if(down) app_runtime_key(i,true,now);
            else if(was && !b->stable_pressed) app_runtime_key(i,false,now);
            if(b->raw_pressed!=b->stable_pressed) {
                uint64_t elapsed=now>=b->changed_ms?now-b->changed_ms:0;
                unsigned left=elapsed<APP_DEBOUNCE_MS?APP_DEBOUNCE_MS-(unsigned)elapsed:1;
                TickType_t delay=pdMS_TO_TICKS(left); if(!delay) delay=1; if(delay<wait) wait=delay;
            }
        }
    }
}
void input_start(void)
{
    for(unsigned i=0;i<KEY_COUNT;++i) {
        gpio_config_t c={.pin_bit_mask=1ULL<<s_gpios[i],.mode=GPIO_MODE_INPUT,
            .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
        ESP_ERROR_CHECK(gpio_config(&c));
    }
    esp_err_t err=gpio_install_isr_service(0);
    if(err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
    TaskHandle_t task=NULL;
    if(xTaskCreate(input_task,"app_input",3072,NULL,5,&task)!=pdPASS) { ESP_LOGE("input","input task failed"); return; }
    for(unsigned i=0;i<KEY_COUNT;++i) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_gpios[i],button_isr,task));
        ESP_ERROR_CHECK(gpio_set_intr_type(s_gpios[i],GPIO_INTR_ANYEDGE)); ESP_ERROR_CHECK(gpio_intr_enable(s_gpios[i]));
    }
    xTaskNotifyGive(task);
}
void input_cdc_handler(const char *line)
{
    if (app_runtime_command(line)) return;
    if (robot_ui_command(line) || robot_command(line)) return;
    if (strncmp(line, "ssid:", 5) == 0) {
        config_set_wifi(line + 5, NULL);
        cdc_write("OK ssid set");
    } else if (strncmp(line, "pass:", 5) == 0) {
        config_set_wifi(NULL, line + 5);
        cdc_write("OK pass set");
    } else if (strcmp(line, "connect") == 0) {
        cdc_write("connecting...");
        wifi_start();
    } else if (strcmp(line, "status") == 0) {
        char buf[112];
        snprintf(buf, sizeof buf, "wifi=%s ip=%s clients=%d link=%s",
                 wifi_is_connected() ? "connected" : "disconnected",
                 wifi_get_ip(), dm_client_count(),
                 device_link_is_connected() ? "connected" : "offline");
        cdc_write(buf);
    } else if (strcmp(line, "id") == 0) {
        uint8_t mac[6] = {0};
        char id[24];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(id, sizeof id, "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        cdc_write(id);
    } else if (strncmp(line, "daemon_host:", 12) == 0) {
        config_set_link(line + 12, 0, NULL);
        cdc_write("OK daemon host set");
    } else if (strncmp(line, "daemon_port:", 12) == 0) {
        unsigned long port = strtoul(line + 12, NULL, 10);
        if (port == 0 || port > 65535) {
            cdc_write("ERR bad daemon port");
        } else {
            config_set_link(NULL, (uint16_t)port, NULL);
            cdc_write("OK daemon port set");
        }
    } else if (strncmp(line, "daemon_secret:", 14) == 0) {
        const char *secret = line + 14;
        if (strlen(secret) != 64) {
            cdc_write("ERR daemon secret must be 64 hex chars");
        } else {
            config_set_link(NULL, 0, secret);
            cdc_write("OK daemon secret set");
        }
    } else if (strcmp(line, "reset") == 0) {
        config_factory_reset();
        cdc_write("OK factory reset, rebooting");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else if (strcmp(line, "help") == 0) {
        cdc_write("apps: short GPIO6 opens menu, triple-click returns menu | robot menu | robot game start/stop/status");
        cdc_write("robot help: tracking / servo calibration");
        cdc_write("cmds: ssid:<s> pass:<p> daemon_host:<h> daemon_port:<p> daemon_secret:<64hex> connect status id reset");
    } else if (line[0] != '\0') {
        cdc_write("ERR unknown command (try: help)");
    }
}
