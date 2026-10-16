#ifndef APP_AI_STATUS_GUI_H
#define APP_AI_STATUS_GUI_H

#include "lvgl.h"

#define AI_MAX_AGENTS 4

enum AiState
{
    AI_IDLE = 0,
    AI_THINKING,
    AI_WORKING,
    AI_WAITING,
    AI_DONE,
    AI_ERROR,
    AI_STATE_NUM
};

struct AiAgent
{
    char name[24];
    char text[361]; // 最多约 360 字节 UTF-8
    AiState state;
    unsigned long since_ms; // 进入当前状态的时间
    unsigned long update_ms;
    bool used;
};

const char *ai_state_name(AiState s);
uint32_t ai_state_color(AiState s); // 0xRRGGBB
AiState ai_state_from_str(const char *s, bool *ok);

void ai_status_gui_init(void);
void ai_status_gui_set_ip(const char *line);
void ai_status_gui_render(const AiAgent *agents, int n, int cur, unsigned long now_ms);
void ai_status_gui_del(void);

extern const lv_img_dsc_t app_ai_status;

#endif
