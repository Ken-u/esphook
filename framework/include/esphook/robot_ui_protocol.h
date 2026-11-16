#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROBOT_UI_JSON_MAX 1024
#define ROBOT_UI_TEXT_MAX 192

typedef enum { UI_AUTO, UI_FACE, UI_TEXT, UI_RADAR } robot_ui_mode_t;
typedef enum { FACE_AUTO, FACE_NEUTRAL, FACE_HAPPY, FACE_SLEEPY, FACE_SAD, FACE_CUSTOM } robot_expression_t;
typedef struct {
    robot_ui_mode_t mode;
    robot_expression_t expression;
    char text[ROBOT_UI_TEXT_MAX + 1];
    char left[36], right[36], mouth[21];
    uint32_t eye_color, mouth_color, text_color;
    unsigned ttl_ms, range_mm;
    bool blink;
} robot_ui_t;

robot_ui_t robot_ui_default(void);
/* Parses a complete replacement frame; failure never changes *out. */
bool robot_ui_parse(const char *json, size_t len, robot_ui_t *out, char *error, size_t error_len);
const char *robot_ui_mode_name(robot_ui_mode_t mode);

/* Shared UTF-8 validation for other versioned protocols. */
bool robot_ui_valid_utf8(const char *text);
