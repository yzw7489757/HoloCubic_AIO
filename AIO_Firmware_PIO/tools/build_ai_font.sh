#!/bin/bash
# 重新生成内置中文字体 src/app/ai_status/ai_cn16.c（const 数据，编译进 Flash）
# 依赖: npm i -g lv_font_conv ; pip install fonttools
# 字体: NotoSansSC[wght].ttf 与 NotoEmoji[wght].ttf (Google Fonts, OFL)。可变字体默认是 Thin，
#       先用 fontTools.varLib.instancer 固定为 wght=500 / 400 得到 *-static.ttf。
# sc.txt / em.txt: 各字体负责的字符（ASCII + GB2312 一级 3755 字 + 中文标点 + 符号 + ~80 个 emoji）
# 修改字符集后，同步更新 ai_status_bridge/ai_cn16_charset.txt（= sc.txt + em.txt）
set -e
D="$(cd "$(dirname "$0")" && pwd)"
lv_font_conv --font NotoSansSC-static.ttf --symbols "$(cat "$D/sc.txt")" \
             --font NotoEmoji-static.ttf  --symbols "$(cat "$D/em.txt")" \
             --size 16 --bpp 4 --format lvgl --lv-font-name ai_cn16 --lv-include lvgl.h \
             -o "$D/../src/app/ai_status/ai_cn16.c"     # 默认启用压缩(需 LV_USE_FONT_COMPRESSED 1，已开启)
sed -i.bak 's/^    .fallback = NULL,/    .fallback = \&lv_font_montserrat_14,/' "$D/../src/app/ai_status/ai_cn16.c"
rm -f "$D/../src/app/ai_status/ai_cn16.c.bak"
