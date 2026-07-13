/* main/input_task.h */
#pragma once

/* 初始化按键 GPIO，启动扫描任务。CDC 配网命令处理也在此（input_cdc_handler）。 */
void input_start(void);

/* CDC 行命令处理（由 usb_cdc 的 rx 回调调用）。 */
void input_cdc_handler(const char *line);
