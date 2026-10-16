#!/bin/bash
# holocube_notify.sh <state> [agent] [text]
#   state: idle|thinking|working|waiting|done|error
#   agent: claude|codex|grok|...（默认 claude；在 Grok 的 hook 里自动识别为 grok）
#   text : 可选。不传时尝试从 stdin 的 hook JSON 中提取（Claude/Grok hooks 会把 JSON 写到 stdin）
# Codex notify 用法: holocube_notify.sh --codex '<json>'   (Codex 把 JSON 作为最后一个参数传入)
#
# 默认通过 USB 串口发送（115200）。端口: CUBE_PORT 环境变量 > ~/.holocube_port > 自动探测 /dev/cu.usbserial-*
# 如果设置了 CUBE_IP（或存在 ~/.holocube_ip），改走 WiFi HTTP（固件需 AI_STATUS_USE_WIFI=1）。
# 原则: 永远不阻塞、不报错、不影响 AI 工具：发送在后台进行，脚本立即退出 0。

DIR="$(cd "$(dirname "$0")" && pwd)"
STATE="$1"; AGENT="${2:-claude}"; TEXT="$3"
[ -n "$GROK_HOOK_EVENT" ] && [ -z "$2" ] && AGENT="grok"

# 选 python：优先 PlatformIO 自带的（含 pyserial）
PY=""
for c in "$HOME/.holocube-pio/bin/python3" "$HOME/.platformio/penv/bin/python3" python3; do
  if command -v "$c" >/dev/null 2>&1; then PY="$c"; break; fi
done

json_get() { # json_get <json> <key>... -> 取第一个存在的字段
  [ -z "$PY" ] && return 0
  "$PY" - "$@" <<'P' 2>/dev/null
import json,sys
try: d=json.loads(sys.argv[1])
except Exception: sys.exit(0)
for k in sys.argv[2:]:
    v=d.get(k) if isinstance(d,dict) else None
    if v:
        if isinstance(v,list): v=" ".join(map(str,v))
        print(str(v).replace("\n"," ")[:300]); break
P
}

if [ "$1" = "--codex" ]; then
  AGENT="codex"; STATE="done"
  TEXT=$(json_get "$2" last-assistant-message)
elif [ -z "$TEXT" ] && [ ! -t 0 ]; then
  IN=""; IFS= read -r -d '' -t 1 IN || true   # 最多等 1 秒（macOS bash 3.2 可用）
  [ -n "$IN" ] && TEXT=$(json_get "$IN" message last_assistant_message tool_name toolName prompt)
fi
[ -z "$STATE" ] && exit 0
[ -z "$PY" ] && exit 0

# 文本清洗（emoji 替换、UTF-8 安全截断）
if [ -n "$TEXT" ] && [ -f "$DIR/sanitize_text.py" ]; then
  T2=$("$PY" "$DIR/sanitize_text.py" "$TEXT" 2>/dev/null) && TEXT="$T2"
fi

# 用 python 生成合法 JSON（转义安全）
LINE=$("$PY" -c 'import json,sys;print(json.dumps({"agent":sys.argv[1],"state":sys.argv[2],"text":sys.argv[3]},ensure_ascii=False))' \
       "$AGENT" "$STATE" "$TEXT" 2>/dev/null) || exit 0

IP="${CUBE_IP:-$(head -n1 "$HOME/.holocube_ip" 2>/dev/null | tr -d ' \r\n')}"
if [ -n "$IP" ]; then
  case "$IP" in *:*) ;; *) IP="$IP:8080";; esac
  ( curl -s -o /dev/null --connect-timeout 1 --max-time 2 -H 'Content-Type: application/json' \
      --data-binary "$LINE" "http://$IP/status" >/dev/null 2>&1 & ) >/dev/null 2>&1
else
  # 后台发送；python 端自带超时（锁等待 1.5s + 读回复 0.6s）
  ( "$PY" "$DIR/holocube_serial_send.py" "$LINE" >/dev/null 2>>"${TMPDIR:-/tmp}/holocube_notify.err" & ) >/dev/null 2>&1
fi
exit 0
