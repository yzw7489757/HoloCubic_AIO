#include "ai_status_gui.h"
#include <Arduino.h>
#include <string.h>

// v0.2.2 布局（240x240，全息棱镜下高对比，无暗灰字）：
//   y 4..26    头部：agent 名（过长循环滚动）            右侧：已用时间
//   y 30..64   状态：彩色圆点 + 大号状态字                右侧：USB 小标签
//   y 70..     消息：填满中间区域，超出时自动上下滚动（lv_anim，非阻塞）
//   y 198..238 底部：其他 agent 彩色小卡片（名字 + 状态）；无其他 agent 时消息区占满
LV_FONT_DECLARE(ai_cn16);

#define MSG_Y 70
#define MSG_H_FULL (238 - MSG_Y)
#define MSG_H_CHIPS (194 - MSG_Y)
#define CHIP_MAX 3

static lv_obj_t *scr = NULL;
static lv_obj_t *lbl_agent, *lbl_elapsed, *dot, *lbl_state, *lbl_conn;
static lv_obj_t *msg_box, *lbl_text;
static lv_obj_t *chips[CHIP_MAX], *chip_lbl[CHIP_MAX];
static char s_shown_text[sizeof(((AiAgent *)0)->text)];
static char s_shown_name[sizeof(((AiAgent *)0)->name)];
static int s_shown_box_h = -1;

static const char *k_names[AI_STATE_NUM] = {"idle", "thinking", "working", "waiting", "done", "error"};
static const uint32_t k_colors[AI_STATE_NUM] = {0xB0B0B0, 0x4D8BFF, 0xFFB000, 0xFF40C0, 0x20E070, 0xFF3838};

const char *ai_state_name(AiState s) { return (s < AI_STATE_NUM) ? k_names[s] : "?"; }
uint32_t ai_state_color(AiState s) { return (s < AI_STATE_NUM) ? k_colors[s] : 0xFFFFFF; }

AiState ai_state_from_str(const char *s, bool *ok)
{
    *ok = false;
    if (!s) return AI_IDLE;
    for (int i = 0; i < AI_STATE_NUM; i++)
        if (!strcasecmp(s, k_names[i])) { *ok = true; return (AiState)i; }
    return AI_IDLE;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *mk_plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void ai_status_gui_init(void)
{
    scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lbl_agent = mk_label(scr, &ai_cn16, 0xFFFFFF);
    lv_obj_set_width(lbl_agent, 150);
    lv_label_set_long_mode(lbl_agent, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(lbl_agent, LV_ALIGN_TOP_LEFT, 8, 6);

    lbl_elapsed = mk_label(scr, &lv_font_montserrat_20, 0xE8E8E8);
    lv_obj_set_width(lbl_elapsed, 80);
    lv_obj_set_style_text_align(lbl_elapsed, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(lbl_elapsed, LV_ALIGN_TOP_RIGHT, -6, 4);

    dot = lv_obj_create(scr);
    lv_obj_set_size(dot, 22, 22);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 8, 37);

    lbl_state = mk_label(scr, &lv_font_montserrat_30, 0xFFFFFF);
    lv_obj_align(lbl_state, LV_ALIGN_TOP_LEFT, 38, 31);

    lbl_conn = mk_label(scr, &lv_font_montserrat_14, 0x70B0FF);
    lv_obj_set_width(lbl_conn, 60);
    lv_label_set_long_mode(lbl_conn, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_conn, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(lbl_conn, LV_ALIGN_TOP_RIGHT, -6, 42);
    lv_label_set_text(lbl_conn, "USB");

    msg_box = mk_plain(scr); // 裁剪容器，label 在里面移动
    lv_obj_set_pos(msg_box, 8, MSG_Y);
    lv_obj_set_size(msg_box, 224, MSG_H_FULL);
    lbl_text = mk_label(msg_box, &ai_cn16, 0xFFFFFF);
    lv_obj_set_width(lbl_text, 224);
    lv_label_set_long_mode(lbl_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(lbl_text, 2, 0);
    lv_obj_set_pos(lbl_text, 0, 0);

    for (int i = 0; i < CHIP_MAX; i++)
    {
        lv_obj_t *c = lv_obj_create(scr);
        lv_obj_set_size(c, 74, 40);
        lv_obj_set_pos(c, 6 + i * 77, 198);
        lv_obj_set_style_radius(c, 6, 0);
        lv_obj_set_style_border_width(c, 2, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_30, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *l = mk_label(c, &ai_cn16, 0xFFFFFF);
        lv_obj_set_width(l, 68);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(l, -2, 0);
        lv_obj_center(l);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        chips[i] = c;
        chip_lbl[i] = l;
    }
    s_shown_text[0] = 0;
    s_shown_name[0] = 0;
    s_shown_box_h = -1;
}

void ai_status_gui_set_ip(const char *line)
{
    if (scr) lv_label_set_text(lbl_conn, line);
}

static void fmt_elapsed(char *buf, size_t n, unsigned long s)
{
    if (s < 60) snprintf(buf, n, "%lus", s);
    else if (s < 3600) snprintf(buf, n, "%lum%02lus", s / 60, s % 60);
    else snprintf(buf, n, "%luh%02lum", s / 3600, (s / 60) % 60);
}

static void anim_y_cb(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)v); }

// 设置消息文本并（重新）启动滚动：顶部停 2s -> 匀速上滚 -> 底部停 2s -> 回滚 -> 循环
static void set_message(const char *txt, int box_h)
{
    lv_anim_del(lbl_text, anim_y_cb);
    lv_obj_set_height(msg_box, box_h);
    lv_label_set_text(lbl_text, txt);
    lv_obj_set_y(lbl_text, 0);
    lv_obj_update_layout(lbl_text);
    lv_coord_t over = lv_obj_get_height(lbl_text) - box_h;
    if (over <= 0) return;
    uint32_t t = (uint32_t)over * 40; // 约 25 px/s
    if (t < 800) t = 800;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_text);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_values(&a, 0, -over);
    lv_anim_set_time(&a, t);
    lv_anim_set_delay(&a, 2000);
    lv_anim_set_playback_delay(&a, 2000);
    lv_anim_set_playback_time(&a, t / 2 < 600 ? 600 : t / 2);
    lv_anim_set_repeat_delay(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void update_message(const char *txt, int box_h)
{
    if (box_h == s_shown_box_h && !strcmp(txt, s_shown_text)) return; // 未变化：不打断滚动
    strncpy(s_shown_text, txt, sizeof(s_shown_text) - 1);
    s_shown_text[sizeof(s_shown_text) - 1] = 0;
    s_shown_box_h = box_h;
    set_message(txt, box_h);
}

static void update_name(const char *n)
{
    if (!strcmp(n, s_shown_name)) return; // 不重复 set_text，避免循环滚动被重置
    strncpy(s_shown_name, n, sizeof(s_shown_name) - 1);
    s_shown_name[sizeof(s_shown_name) - 1] = 0;
    lv_label_set_text(lbl_agent, n);
}

void ai_status_gui_render(const AiAgent *a, int n, int cur, unsigned long now)
{
    if (!scr) return;
    if (cur < 0 || !a[cur].used)
    {
        update_name("AI Status");
        lv_label_set_text(lbl_state, "no data");
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x606060), 0);
        lv_obj_set_style_text_color(lbl_state, lv_color_hex(0xC0C0C0), 0);
        lv_label_set_text(lbl_elapsed, "");
        for (int i = 0; i < CHIP_MAX; i++) lv_obj_add_flag(chips[i], LV_OBJ_FLAG_HIDDEN);
        update_message("waiting for USB...\n{\"agent\",\"state\",\"text\"}", MSG_H_FULL);
        return;
    }
    const AiAgent &c = a[cur];
    lv_color_t col = lv_color_hex(ai_state_color(c.state));
    update_name(c.name);
    lv_label_set_text(lbl_state, ai_state_name(c.state));
    lv_obj_set_style_text_color(lbl_state, col, 0);
    lv_obj_set_style_bg_color(dot, col, 0);
    char e[24];
    fmt_elapsed(e, sizeof(e), (now - c.since_ms) / 1000);
    lv_label_set_text(lbl_elapsed, e);

    int k = 0;
    for (int i = 0; i < n && k < CHIP_MAX; i++)
    {
        if (!a[i].used || i == cur) continue;
        lv_color_t cc = lv_color_hex(ai_state_color(a[i].state));
        lv_obj_set_style_border_color(chips[k], cc, 0);
        lv_obj_set_style_bg_color(chips[k], cc, 0);
        char t[48];
        snprintf(t, sizeof(t), "%.9s\n%s", a[i].name, ai_state_name(a[i].state));
        lv_label_set_text(chip_lbl[k], t);
        lv_obj_clear_flag(chips[k], LV_OBJ_FLAG_HIDDEN);
        k++;
    }
    for (int i = k; i < CHIP_MAX; i++) lv_obj_add_flag(chips[i], LV_OBJ_FLAG_HIDDEN);
    update_message(c.text, k ? MSG_H_CHIPS : MSG_H_FULL);
}

void ai_status_gui_del(void)
{
    if (scr)
    {
        if (lbl_text) lv_anim_del(lbl_text, anim_y_cb);
        lv_obj_clean(scr);
        scr = NULL;
    }
}
