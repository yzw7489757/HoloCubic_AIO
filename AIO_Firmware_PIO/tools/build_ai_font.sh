#!/bin/bash
# 重新生成 SD 卡中文字体 放置到内存卡/font/ai_cn16.bin
# 依赖: npm i -g lv_font_conv ; pip install fonttools
# 字体: NotoSansSC[wght].ttf 与 NotoEmoji[wght].ttf (Google Fonts, OFL)，先用 fontTools instancer
#       固定为 wght=500 / 400 得到 *-static.ttf（可变字体默认是 Thin）。
# sc.txt / em.txt: 分别由各字体覆盖的字符（GB2312 一级 3755 字 + ASCII + 中文标点 + 符号 + 约 80 个 emoji）
set -e
lv_font_conv --font NotoSansSC-static.ttf --symbols "$(cat sc.txt)" \
             --font NotoEmoji-static.ttf  --symbols "$(cat em.txt)" \
             --size 16 --bpp 4 --no-compress --no-prefilter --format lvgl --lv-font-name ai_cn16 -o ai_cn16.c
python3 "$(dirname "$0")/c2aif.py" ai_cn16.c ai_cn16.bin
