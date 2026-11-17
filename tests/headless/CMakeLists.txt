cmake_minimum_required(VERSION 3.19)
project(esphook_headless C CXX)
get_filename_component(PROJECT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_SOURCE_DIR}" lvgl)
include("${PROJECT_DIR}/cmake/apps.cmake")
find_path(CJSON_INCLUDE cJSON.h PATH_SUFFIXES cjson REQUIRED)
find_library(CJSON_LIBRARY NAMES cjson libcjson.so.1 REQUIRED)
add_executable(apps_headless test_apps.c ${ESPHOOK_APP_SOURCES}
    "${PROJECT_DIR}/framework/core/app_manager.c"
    "${PROJECT_DIR}/framework/core/app_input.c"
    "${PROJECT_DIR}/framework/core/app_keys.c"
    "${PROJECT_DIR}/framework/core/app_hardware.c"
    "${PROJECT_DIR}/framework/services/display_protocol.c"
    "${PROJECT_DIR}/framework/services/radar_plot.c"
    "${PROJECT_DIR}/framework/ports/esp32/app_ui.c"
    "${PROJECT_DIR}/main/aihook_font_12.c" "${PROJECT_DIR}/main/aihook_font_16.c")
target_include_directories(apps_headless PRIVATE "${PROJECT_DIR}/framework/include" "${PROJECT_DIR}/framework/include/esphook"
    "${PROJECT_DIR}/main" "${PROJECT_DIR}/tests/ui_stubs" ${ESPHOOK_APP_INCLUDES} ${CJSON_INCLUDE})
target_link_libraries(apps_headless PRIVATE lvgl ${CJSON_LIBRARY} m)
target_compile_options(apps_headless PRIVATE -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer)
target_compile_definitions(apps_headless PRIVATE ESPHOOK_FONT_FILE="${PROJECT_DIR}/main/fontdata.bin")
if(ESPHOOK_APP_stock)
    target_compile_definitions(apps_headless PRIVATE ESPHOOK_TEST_STOCK)
endif()
if(ESPHOOK_APP_basketball)
    target_compile_definitions(apps_headless PRIVATE ESPHOOK_TEST_BASKETBALL)
endif()
if(ESPHOOK_APP_dino)
    target_compile_definitions(apps_headless PRIVATE ESPHOOK_TEST_DINO)
endif()
if(ESPHOOK_APP_ski)
    target_compile_definitions(apps_headless PRIVATE ESPHOOK_TEST_SKI)
endif()
if(ESPHOOK_APP_demo)
    target_compile_definitions(apps_headless PRIVATE ESPHOOK_TEST_DEMO)
endif()
target_link_options(apps_headless PRIVATE -fsanitize=address,undefined)
target_compile_options(lvgl PRIVATE -g -fsanitize=address,undefined -fno-omit-frame-pointer)
