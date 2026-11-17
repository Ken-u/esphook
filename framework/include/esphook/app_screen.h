#pragma once
/* The 0.96" ST7735 80 x 160 modules are not all alike. Two flavours are common and the same wiring drives both:
 *
 *   plugin  colours need INVON (the firmware default, the board it was written on), RAM offsets x = 1, y = 26
 *   plain   colours must NOT be inverted, RAM offsets x = 0, y = 24
 *
 * Driven as the wrong one, a module shows every colour inverted and - because the window then runs one column past
 * the end of a 160 wide RAM - the picture slants a pixel more with every row. The panel tuning below is therefore
 * persistent and can be changed while the screen is showing, from the `screentest` app (keys) or from the PC (the
 * USB text commands parsed by app_screen_parse, `robot screentest ...`). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool invert;               /* ST7735 INVON/INVOFF: true for the "plugin" flavour */
    uint8_t x_gap, y_gap;      /* RAM offsets of the 160 x 80 window */
    uint8_t mhz;               /* SPI clock in use since power-up */
    uint8_t saved_mhz;         /* SPI clock the next power-up uses (changing it needs a power cycle) */
    bool configured;           /* a tuning is stored: false on a board that was never set up (the screentest app then runs
                                  first at boot, see app_descriptor_t::first_run), read-only for apply() */
} app_screen_t;

#define APP_SCREEN_GAP_MAX 63U
#define APP_SCREEN_MHZ_MAX 40U
#define APP_SCREEN_RAW_MAX 8U  /* one controller command: the command byte and up to seven parameters */

/* ---- services (framework/ports/esp32; thread safe, callable from the USB task) ------------------------------ */
/* What the panel uses now. */
void app_screen_read(app_screen_t *out);
/* Invert and offsets take effect on the next frame; false when a value is out of range. With `save` they are stored
 * (together with want->mhz, the clock the next power-up uses). Without it a power cycle returns to the saved ones. */
bool app_screen_apply(const app_screen_t *want, bool save);
/* Sends one raw command to the controller (bytes[0], then its parameters) and redraws; false when busy or malformed.
 * For experiments: a wrong register is undone by a power cycle. */
bool app_screen_raw(const uint8_t *bytes, size_t length);
/* Forgets the stored tuning: the firmware defaults apply now, `configured` turns false and the next boot starts with the
 * screen setup again. False when the display is busy with an earlier request. */
bool app_screen_forget(void);

/* ---- pure helpers (framework/core/app_screen.c) ------------------------------------------------------------- */
bool app_screen_valid(const app_screen_t *screen);
/* "plugin", "plain" or "custom": which flavour the invert/offset triple matches. */
const char *app_screen_variant(const app_screen_t *screen);
/* Sets invert and offsets of `*screen` to a flavour; false for an unknown name. */
bool app_screen_set_variant(app_screen_t *screen, const char *name);

typedef enum {
    APP_SCREEN_CMD_STATUS,
    APP_SCREEN_CMD_SET,   /* apply the fields flagged below, not stored */
    APP_SCREEN_CMD_SAVE,  /* store what is applied now (and the clock, when flagged) */
    APP_SCREEN_CMD_RESET, /* forget the stored tuning: defaults now, the setup runs again at the next boot */
    APP_SCREEN_CMD_RAW,
    APP_SCREEN_CMD_OPEN,  /* show the test pattern */
    APP_SCREEN_CMD_MENU,  /* leave it */
    APP_SCREEN_CMD_BAD    /* `error` says what is wrong */
} app_screen_cmd_kind_t;
typedef struct {
    app_screen_cmd_kind_t kind;
    bool has_invert, has_gap, has_mhz;
    app_screen_t value;
    uint8_t raw[APP_SCREEN_RAW_MAX];
    size_t raw_length;
    const char *error;
} app_screen_cmd_t;
/* Parses a `robot screentest ...` line. False when the line is not addressed to the screen test at all; true when
 * it is (check `kind`). Grammar:
 *   robot screentest [status]          what is applied and stored
 *   robot screentest plugin | plain    apply a flavour (invert and offsets); not stored
 *   robot screentest invert 0|1        apply the colour polarity
 *   robot screentest gap <x> <y>       apply the RAM offsets (0..63)
 *   robot screentest save              store what is applied
 *   robot screentest reset             forget the stored tuning (defaults now; the next boot sets the screen first)
 *   robot screentest mhz <1..40>       store the SPI clock (and what is applied); takes effect after a power cycle
 *   robot screentest raw <hex>...      one raw command, e.g. `raw 21` or `raw 2A 00 00 00 9F`
 *   robot screentest start | stop      show / leave the test pattern */
bool app_screen_parse(const char *line, app_screen_cmd_t *cmd);
