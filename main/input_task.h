/* main/input_task.h */
#pragma once

/* 机器人模式只扫描 GPIO6 游戏键；原 Hook 模式保留旧三键。CDC 处理见下。 */
void input_start(void);

/* CDC 行命令处理（由 usb_cdc 的 rx 回调调用）。 */
void input_cdc_handler(const char *line);
