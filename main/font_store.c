/* main/font_store.c
 *
 * Glyph bitmaps, glyph records and character maps of the two Chinese LVGL fonts are not part of the OTA app slots.
 * They live once in the fixed `fontdata` data partition, which is memory mapped here and handed to
 * aihook_font_load(). An app OTA therefore only touches ota_0/ota_1 and the fonts only change with a full USB flash.
 */
#include "font_store.h"
#include "aihook_font.h"
#include "esp_log.h"
#include "esp_partition.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *TAG = "font_store";

#define FONT_PARTITION_LABEL "fontdata"

static esp_partition_mmap_handle_t s_map_handle;
static bool s_initialized;

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

    if (!aihook_font_load((const uint8_t *)mapped, partition->size)) {
        ESP_LOGE(TAG, "'%s' does not hold a usable font image (old layout or damaged); "
                      "full USB flash is required, using the built-in ASCII font", FONT_PARTITION_LABEL);
        esp_partition_munmap(s_map_handle);
        s_map_handle = 0;
        return ESP_ERR_INVALID_VERSION;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "mapped Chinese fonts from '%s' (%u KiB partition)", FONT_PARTITION_LABEL,
             (unsigned)(partition->size / 1024));
    return ESP_OK;
}
