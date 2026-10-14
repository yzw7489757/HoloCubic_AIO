#ifndef APP_AI_STATUS_FONT_H
#define APP_AI_STATUS_FONT_H

#include "lvgl.h"

// SD 卡上的中文字体（AIF1 流式格式，由 lv_font_conv 生成后转换）
// SD 卡路径: /font/ai_cn16.bin   ->  LVGL 路径 "S:/font/ai_cn16.bin"
#define AI_FONT_PATH "S:/font/ai_cn16.bin"

// 成功返回字体指针，失败返回 NULL（调用方回退到内置字体）
const lv_font_t *ai_font_open(const char *path, const lv_font_t *fallback);
void ai_font_close(void);

#endif
