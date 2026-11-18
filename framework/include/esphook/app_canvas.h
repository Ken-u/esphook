#pragma once
/* Size of the framework's shared RGB565 canvas (app_ui_pixels()). Kept in its own header, free of LVGL, so the pure
 * drawing kits (app_gfx.h, app_stack.h) and their host tests do not need the UI headers. */
#define APP_CANVAS_W 160
#define APP_CANVAS_H 64
