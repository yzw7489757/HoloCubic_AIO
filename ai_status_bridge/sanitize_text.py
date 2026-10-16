#!/usr/bin/env python3
# 用法: sanitize_text.py "<文本>" [charset文件]
# 把字体里没有的 emoji 换成 [中文说明] 或删除；按 UTF-8 字符边界截断到 340 字节
import sys, os
t = sys.argv[1] if len(sys.argv) > 1 else ""
cf = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "ai_cn16_charset.txt")
try:
    cs = set(open(cf, encoding="utf-8").read())
except Exception:
    cs = None
M = {"✕": "✖", "✗": "✖", "✘": "✖", "➔": "→", "➜": "→", "☑": "✅", "😄": "😀", "😃": "😀", "🙂": "😊",
     "😁": "😀", "🤣": "😂", "😢": "😭", "😤": "😡", "😠": "😡", "❎": "❌", "⏹": "■", "👉": "→", "👈": "←",
     "👆": "↑", "👇": "↓", "🎊": "[庆祝]", "🥳": "[庆祝]", "💥": "[爆炸]", "🐢": "[慢]", "⚡": "[快]",
     "📊": "[图表]", "📈": "[上升]", "📉": "[下降]", "🧪": "[测试]", "🔗": "[链接]", "🌐": "[网络]",
     "📷": "[图片]", "🎨": "[设计]", "🧹": "[清理]", "🏗": "[构建]", "🚨": "[警报]", "🛑": "[停止]",
     "📋": "[清单]", "📚": "[文档]", "🗂": "[目录]", "🆘": "[求助]", "💾": "[保存]", "🔀": "[合并]", "🌱": "[新]"}
o = []
for ch in t:
    if ch in "\ufe0f\u200d":
        continue
    ch = M.get(ch, ch)
    if cs is not None and len(ch) == 1 and ch not in cs:
        c = ord(ch)
        if c >= 0x1F000 or 0x2600 <= c < 0x2C00:   # 字库没有的 emoji/符号: 删除
            ch = ""
        elif c in (0x0A, 0x0D, 0x09):
            ch = " "
        # 其他不在字库的汉字（二级字等）保留，固件会显示为空白
    o.append(ch)
t = "".join(o)
b = t.encode("utf-8")
if len(b) > 340:
    b = b[:337]
    t = b.decode("utf-8", "ignore") + "..."   # ignore 会丢掉被截断的半个字符
sys.stdout.write(t)
