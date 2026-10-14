// 流式 SD 字体：不把整个字体读入 RAM（lv_font_load 在 v8 会整体加载，约 600KB+，ESP32 放不下）。
// RAM 中只保留：码点表(4B*N≈16KB) + 小型 LRU 字形缓存(AI_FONT_CACHE 条)。
// 文件格式见 tools/c2aif.py（AIF1）。
#include "ai_status_font.h"
#include <Arduino.h>
#include <string.h>

#define AI_FONT_CACHE 48

struct AifCacheEnt
{
    uint32_t cp;
    uint32_t stamp;
    uint8_t *rec; // rec_size 字节
};

struct AifFont
{
    lv_fs_file_t file;
    bool opened;
    uint16_t count;
    uint16_t rec_size;
    uint8_t bpp;
    uint32_t *cps;
    uint32_t data_off;
    uint8_t *pool;
    AifCacheEnt cache[AI_FONT_CACHE];
    uint32_t tick;
    lv_font_t font;
};

static AifFont *g_aif = NULL;

static int aif_find(AifFont *a, uint32_t cp)
{
    int lo = 0, hi = a->count - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) >> 1;
        if (a->cps[mid] == cp) return mid;
        if (a->cps[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static uint8_t *aif_get_rec(AifFont *a, uint32_t cp)
{
    a->tick++;
    int victim = 0;
    for (int i = 0; i < AI_FONT_CACHE; i++)
    {
        if (a->cache[i].stamp && a->cache[i].cp == cp)
        {
            a->cache[i].stamp = a->tick;
            return a->cache[i].rec;
        }
        if (a->cache[i].stamp < a->cache[victim].stamp) victim = i;
    }
    int idx = aif_find(a, cp);
    if (idx < 0) return NULL;
    AifCacheEnt &e = a->cache[victim];
    uint32_t rd = 0;
    if (lv_fs_seek(&a->file, a->data_off + (uint32_t)idx * a->rec_size, LV_FS_SEEK_SET) != LV_FS_RES_OK ||
        lv_fs_read(&a->file, e.rec, a->rec_size, &rd) != LV_FS_RES_OK || rd != a->rec_size)
    {
        e.stamp = 0;
        return NULL;
    }
    e.cp = cp;
    e.stamp = a->tick;
    return e.rec;
}

static bool aif_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next)
{
    AifFont *a = (AifFont *)font->dsc;
    if (!a || !a->opened) return false;
    if (letter == 0xFE0F || letter == 0x200D) // emoji 变体选择符/连接符：零宽
    {
        dsc->adv_w = 0; dsc->box_w = 0; dsc->box_h = 0; dsc->ofs_x = 0; dsc->ofs_y = 0;
        dsc->bpp = a->bpp; dsc->is_placeholder = 0;
        return true;
    }
    uint8_t *r = aif_get_rec(a, letter);
    if (!r) return false; // 交给 fallback 字体
    uint16_t adv = r[0] | (r[1] << 8);
    dsc->adv_w = (adv + 8) >> 4;
    dsc->box_w = r[2];
    dsc->box_h = r[3];
    dsc->ofs_x = (int8_t)r[4];
    dsc->ofs_y = (int8_t)r[5];
    dsc->bpp = a->bpp;
    dsc->is_placeholder = 0;
    return true;
}

static const uint8_t *aif_glyph_bitmap(const lv_font_t *font, uint32_t letter)
{
    AifFont *a = (AifFont *)font->dsc;
    if (!a || !a->opened) return NULL;
    uint8_t *r = aif_get_rec(a, letter); // 通常刚被 dsc 调用缓存，命中
    return r ? r + 8 : NULL;
}

void ai_font_close(void)
{
    if (!g_aif) return;
    if (g_aif->opened) lv_fs_close(&g_aif->file);
    free(g_aif->cps);
    free(g_aif->pool);
    free(g_aif);
    g_aif = NULL;
}

const lv_font_t *ai_font_open(const char *path, const lv_font_t *fallback)
{
    ai_font_close();
    AifFont *a = (AifFont *)calloc(1, sizeof(AifFont));
    if (!a) return NULL;
    uint8_t h[16];
    uint32_t rd = 0;
    if (lv_fs_open(&a->file, path, LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        Serial.printf("[AIStatus] font %s not found, use built-in font\n", path);
        free(a);
        return NULL;
    }
    a->opened = true;
    g_aif = a;
    if (lv_fs_read(&a->file, h, 16, &rd) != LV_FS_RES_OK || rd != 16 || memcmp(h, "AIF1", 4))
    {
        Serial.println("[AIStatus] bad font header");
        ai_font_close();
        return NULL;
    }
    a->count = h[4] | (h[5] << 8);
    a->bpp = h[8];
    a->rec_size = h[10] | (h[11] << 8);
    a->data_off = 16 + 4u * a->count;
    size_t need = 4u * a->count + (size_t)a->rec_size * AI_FONT_CACHE;
    // 留足余量给 WiFi/WebServer/LVGL
    if (ESP.getFreeHeap() < need + 40 * 1024 || a->bpp == 0 || a->rec_size < 8)
    {
        Serial.printf("[AIStatus] font needs %u B, free heap %u B -> skip\n", (unsigned)need, ESP.getFreeHeap());
        ai_font_close();
        return NULL;
    }
    a->cps = (uint32_t *)malloc(4u * a->count);
    a->pool = (uint8_t *)malloc((size_t)a->rec_size * AI_FONT_CACHE);
    if (!a->cps || !a->pool ||
        lv_fs_read(&a->file, a->cps, 4u * a->count, &rd) != LV_FS_RES_OK || rd != 4u * a->count)
    {
        Serial.println("[AIStatus] font load failed");
        ai_font_close();
        return NULL;
    }
    for (int i = 0; i < AI_FONT_CACHE; i++) a->cache[i].rec = a->pool + (size_t)i * a->rec_size;

    a->font.get_glyph_dsc = aif_glyph_dsc;
    a->font.get_glyph_bitmap = aif_glyph_bitmap;
    a->font.line_height = h[6];
    a->font.base_line = h[7];
    a->font.subpx = LV_FONT_SUBPX_NONE;
    a->font.underline_position = (int8_t)h[12];
    a->font.underline_thickness = h[13];
    a->font.dsc = a;
    a->font.fallback = fallback;
    Serial.printf("[AIStatus] CN font loaded: %u glyphs, RAM %u B, free heap %u B\n",
                  a->count, (unsigned)need, ESP.getFreeHeap());
    return &a->font;
}
