#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Points the two Chinese LVGL fonts (aihook_font_12 / aihook_font_16, declared in esphook/app_ui.h) at a fontdata image
 * in the format written by tools/make_fontdata.py. Every offset, length, glyph record and character map is validated
 * first, so a truncated, old or damaged image can neither crash the UI nor read outside the mapped partition. The tables
 * stay where they are (in the memory-mapped partition); only a few small character-map records live in RAM.
 *
 * Returns false and leaves both fonts empty when the image is not usable. Empty fonts fall back to LVGL's built-in
 * 8 px ASCII font, so the device still shows something readable until it gets a full USB flash. Call it before the
 * first LVGL screen is created. */
bool aihook_font_load(const uint8_t *image, size_t size);

/* True after a successful aihook_font_load(). */
bool aihook_font_ready(void);
