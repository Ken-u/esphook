/* 屏参数（PLUGIN 变体 0.96" ST7735 80×160 横屏），屏幕测试工程 screen-test 验证通过。
 */
/* MADCTL = MY|MV|BGR = 0x68 */
#define ST7735_MADCTL      0x68
/* 显示颜色反转（INVON 0x21） */
#define ST7735_NEEDS_INVON 1
/* 横屏 rotation1 窗口偏移：xstart=1(rowstart), ystart=26(colstart) */
#define ST7735_WIN_XSTART  1
#define ST7735_WIN_YSTART  26