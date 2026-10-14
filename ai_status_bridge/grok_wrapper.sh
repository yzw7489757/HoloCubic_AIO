#!/bin/bash
# 可选：不想用 hooks 时，用包装器运行任意 CLI 并在开始/结束时上报
# 用法: grok_wrapper.sh grok [参数...]     或  grok_wrapper.sh <任意命令>
N="$HOME/.holocube/holocube_notify.sh"
AG=$(basename "$1")
"$N" working "$AG" "running"
"$@"; rc=$?
if [ $rc -eq 0 ]; then "$N" done "$AG" "exit 0"; else "$N" error "$AG" "exit $rc"; fi
exit $rc
