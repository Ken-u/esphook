/* main/font_store.c
 *
 * 字库位图不放进 OTA app 槽，而是作为固定的 fontdata 数据分区映射到
 * LVGL 的字体描述中。这样 app OTA 只会改 ota_0/ota_1，字库只在整机
 * USB 烧录时更新。
 */
#include "font_store.h"
#include "esp_log.h"
#include "esp_partition.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void aihook_font_12_set_bitmap(const uint8_t *bitmap);
void aihook_font_14_set_bitmap(const uint8_t *bitmap);

static const char *TAG = "font_store";

#define FONT_PARTITION_LABEL "fontdata"
#define FONT_DATA_MAGIC 0x44464841U /* bytes: AHFD */
#define FONT_DATA_VERSION 1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t total_size;
    uint32_t font12_offset;
    uint32_t font12_size;
    uint32_t font14_offset;
    uint32_t font14_size;
} font_data_header_t;

static esp_partition_mmap_handle_t s_map_handle;
static bool s_initialized;

static bool range_is_valid(uint32_t offset, uint32_t size, uint32_t limit)
{
    return offset <= limit && size <= limit - offset;
}

esp_err_t font_store_init(void)
{
    if (s_initialized) return ESP_OK;

    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, FONT_PARTITION_LABEL);
    if (!partition) {
        ESP_LOGE(TAG, "partition '%s' not found; full USB flash is required", FONT_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    const void *mapped = NULL;
    esp_err_t err = esp_partition_mmap(partition, 0, partition->size,
                                       ESP_PARTITION_MMAP_DATA,
                                       &mapped, &s_map_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "map '%s' failed: %s", FONT_PARTITION_LABEL, esp_err_to_name(err));
        return err;
    }

    const font_data_header_t *header = (const font_data_header_t *)mapped;
    if (header->magic != FONT_DATA_MAGIC ||
        header->version != FONT_DATA_VERSION ||
        header->header_size < sizeof(*header) ||
        header->header_size > header->total_size ||
        header->total_size > partition->size ||
        header->font12_offset < header->header_size ||
        header->font14_offset < header->header_size ||
        !range_is_valid(header->font12_offset, header->font12_size, header->total_size) ||
        !range_is_valid(header->font14_offset, header->font14_size, header->total_size)) {
        ESP_LOGE(TAG, "invalid fontdata header in '%s'", FONT_PARTITION_LABEL);
        esp_partition_munmap(s_map_handle);
        s_map_handle = 0;
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t *base = (const uint8_t *)mapped;
    aihook_font_12_set_bitmap(base + header->font12_offset);
    aihook_font_14_set_bitmap(base + header->font14_offset);
    s_initialized = true;

    ESP_LOGI(TAG, "mapped Chinese fonts: 12px=%u bytes, 14px=%u bytes",
             (unsigned)header->font12_size, (unsigned)header->font14_size);
    return ESP_OK;
}
