#!/bin/bash
# holocube_notify.sh <state> [agent] [text]
#   state: idle|thinking|working|waiting|done|error
#   agent: claude|codex|grok|...（默认 claude；在 Grok 的 hook 里自动识别为 grok）
#   text : 可选。不传时尝试从 stdin 的 hook JSON 中提取（Claude/Grok hooks 会把 JSON 写到 stdin）
# Codex notify 用法: holocube_notify.sh --codex '<json>'   (Codex 把 JSON 作为最后一个参数传入)
#
# 设备地址: 环境变量 CUBE_IP（如 192.168.1.50 或 192.168.1.50:8080），
#           否则读取 ~/.holocube_ip 文件第一行。
# 原则: 永远不阻塞、不报错、不影响 AI 工具，失败就静默退出 0。

STATE="$1"; AGENT="${2:-claude}"; TEXT="$3"
[ -n "$GROK_HOOK_EVENT" ] && [ -z "$2" ] && AGENT="grok"

json_get() { # json_get <json> <key>... -> 取第一个存在的字段
  local j="$1"; shift
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$j" "$@" <<'P' 2>/dev/null
import json,sys
try: d=json.loads(sys.argv[1])
except Exception: sys.exit(0)
for k in sys.argv[2:]:
    v=d.get(k) if isinstance(d,dict) else None
    if v:
        if isinstance(v,list): v=" ".join(map(str,v))
        print(str(v).replace("\n"," ")[:200]); break
P
  elif command -v jq >/dev/null 2>&1; then
    for k in "$@"; do v=$(printf '%s' "$j" | jq -r --arg k "$k" '.[$k] // empty' 2>/dev/null); [ -n "$v" ] && { printf '%s' "${v:0:200}" | tr '\n' ' '; break; }; done
  fi
}

if [ "$1" = "--codex" ]; then
  AGENT="codex"; STATE="done"
  TEXT=$(json_get "$2" last-assistant-message)
elif [ -z "$TEXT" ] && [ ! -t 0 ]; then
  # 读 stdin（最多等 1 秒，macOS 自带 bash 3.2 也支持），避免卡住
  IN=""; IFS= read -r -d '' -t 1 IN || true
  [ -n "$IN" ] && TEXT=$(json_get "$IN" message last_assistant_message tool_name toolName prompt)
fi
[ -z "$STATE" ] && exit 0

# 文本清洗（emoji 替换、UTF-8 安全截断）；没有 python3 就原样发送，固件端也会按字符边界截断
SAN="$(dirname "$0")/sanitize_text.py"
if [ -n "$TEXT" ] && [ -f "$SAN" ] && command -v python3 >/dev/null 2>&1; then
  T2=$(python3 "$SAN" "$TEXT" 2>/dev/null) && TEXT="$T2"
fi

IP="${CUBE_IP:-$(head -n1 "$HOME/.holocube_ip" 2>/dev/null | tr -d ' \r\n')}"
[ -z "$IP" ] && exit 0
case "$IP" in *:*) ;; *) IP="$IP:8080";; esac

esc() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' | tr -d '\000-\037'; }
BODY="{\"agent\":\"$(esc "$AGENT")\",\"state\":\"$(esc "$STATE")\",\"text\":\"$(esc "$TEXT")\"}"

# 后台发送，1s 连接超时 / 2s 总超时，脚本立即返回
( curl -s -o /dev/null --connect-timeout 1 --max-time 2 \
    -H 'Content-Type: application/json' -d "$BODY" "http://$IP/status" >/dev/null 2>&1 & ) >/dev/null 2>&1
exit 0
