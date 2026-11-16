#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_get_i32(nvs_handle_t handle,const char *key,int32_t *value);
esp_err_t nvs_get_u8(nvs_handle_t handle,const char *key,uint8_t *value);
esp_err_t nvs_set_i32(nvs_handle_t handle,const char *key,int32_t value);
esp_err_t nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value);
