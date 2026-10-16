#include "ai_status_gui.h"
#include <Arduino.h>
#include <string.h>

static lv_obj_t *scr = NULL;
// 内置中文字体（ai_cn16.c，lv_font_conv 生成，const 数据留在 Flash，不占 RAM）
// 内容: ASCII + GB2312 一级 3755 字 + 中文标点 + 常用符号 + ~80 个黑白 emoji；缺字回退 montserrat_14
LV_FONT_DECLARE(ai_cn16);
static lv_obj_t *lbl_agent, *dot, *lbl_state, *lbl_text, *lbl_elapsed, *lbl_list, *lbl_ip;

static const char *k_names[AI_STATE_NUM] = {"idle", "thinking", "working", "waiting", "done", "error"};
static const uint32_t k_colors[AI_STATE_NUM] = {0x808080, 0x3D7BFF, 0xFFB000, 0xFF40C0, 0x20D060, 0xFF3030};

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

static lv_obj_t *mk_label(const lv_font_t *f, lv_color_t c, lv_coord_t y, lv_coord_t w)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    lv_label_set_text(l, "");
    return l;
}

void ai_status_gui_init(void)
{
    scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    const lv_font_t *f_name = &ai_cn16;
    const lv_font_t *f_small = &ai_cn16;

    lbl_agent = mk_label(f_name, lv_color_white(), 6, 230);

    dot = lv_obj_create(scr);
    lv_obj_set_size(dot, 26, 26);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 14, 42);

    lbl_state = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_30, 0);
    lv_obj_align(lbl_state, LV_ALIGN_TOP_LEFT, 50, 38);
    lv_label_set_text(lbl_state, "");

    lbl_text = mk_label(f_small, lv_color_hex(0xDDDDDD), 78, 228);
    lv_label_set_long_mode(lbl_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(lbl_text, 58); // 约3行，超出部分被裁掉

    lbl_elapsed = mk_label(&lv_font_montserrat_20, lv_color_hex(0xAAAAAA), 138, 230);
    lbl_list = mk_label(f_small, lv_color_hex(0x999999), 166, 230);
    lv_obj_set_height(lbl_list, 50);
    lbl_ip = mk_label(&lv_font_montserrat_14, lv_color_hex(0x6080A0), 218, 236);
    lv_label_set_text(lbl_ip, "USB serial 115200");
}

void ai_status_gui_set_ip(const char *line)
{
    if (scr) lv_label_set_text(lbl_ip, line);
}

static void fmt_elapsed(char *buf, size_t n, unsigned long s)
{
    if (s < 60) snprintf(buf, n, "%lus", s);
    else if (s < 3600) snprintf(buf, n, "%lum%02lus", s / 60, s % 60);
    else snprintf(buf, n, "%luh%02lum", s / 3600, (s / 60) % 60);
}

void ai_status_gui_render(const AiAgent *a, int n, int cur, unsigned long now)
{
    if (!scr) return;
    if (cur < 0 || !a[cur].used)
    {
        lv_label_set_text(lbl_agent, "AI Status");
        lv_label_set_text(lbl_state, "no data");
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x404040), 0);
        lv_obj_set_style_text_color(lbl_state, lv_color_hex(0x808080), 0);
        lv_label_set_text(lbl_text, "waiting for USB...\n{agent,state,text}");
        lv_label_set_text(lbl_elapsed, "");
        lv_label_set_text(lbl_list, "");
        return;
    }
    const AiAgent &c = a[cur];
    lv_color_t col = lv_color_hex(ai_state_color(c.state));
    lv_label_set_text(lbl_agent, c.name);
    lv_label_set_text(lbl_state, ai_state_name(c.state));
    lv_obj_set_style_text_color(lbl_state, col, 0);
    lv_obj_set_style_bg_color(dot, col, 0);
    lv_label_set_text(lbl_text, c.text);
    char e[24];
    fmt_elapsed(e, sizeof(e), (now - c.since_ms) / 1000);
    lv_label_set_text(lbl_elapsed, e);

    char list[256] = {0};
    size_t off = 0;
    for (int i = 0; i < n; i++)
    {
        if (!a[i].used || i == cur) continue;
        char t[16];
        fmt_elapsed(t, sizeof(t), (now - a[i].since_ms) / 1000);
        off += snprintf(list + off, sizeof(list) - off, "%s: %s %s\n",
                        a[i].name, ai_state_name(a[i].state), t);
        if (off >= sizeof(list)) break;
    }
    lv_label_set_text(lbl_list, list);
}

void ai_status_gui_del(void)
{
    if (scr)
    {
        lv_obj_clean(scr);
        scr = NULL;
    }

}
