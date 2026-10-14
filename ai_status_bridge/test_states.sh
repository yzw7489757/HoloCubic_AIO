#!/bin/bash
# 依次发送所有状态，用来测试屏幕和灯
N="$(dirname "$0")/holocube_notify.sh"
for s in idle thinking working waiting done error; do echo "-> $s"; "$N" $s claude "test state $s"; sleep 2; done
"$N" working codex "codex is editing files"; sleep 1
"$N" waiting grok "grok needs approval"
