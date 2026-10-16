# HoloCubic「AI 灵动岛」电脑端（USB 串口版）

完整图文教程见仓库 `Docs/HoloCubic_AI状态_使用指南.md`（或同名 PDF）。这里是速查。

## 原理
AI 工具的 hook 运行 `holocube_notify.sh`，脚本把一行 JSON 通过 **USB 串口(115200)** 写给小电视：
```
{"agent":"claude","state":"thinking","text":"..."}
```
固件回复 `AIS:OK`。在菜单或其他 APP 里收到状态时，会自动切换进 AIStatus（Settings APP 除外）。

## 安装
```bash
mkdir -p ~/.holocube
cp holocube_notify.sh holocube_serial_send.py sanitize_text.py ai_cn16_charset.txt test_states.sh grok_wrapper.sh ~/.holocube/
chmod +x ~/.holocube/*.sh ~/.holocube/*.py
ls /dev/cu.*                                   # 找到小电视的串口，例如 /dev/cu.usbserial-0001
echo /dev/cu.usbserial-0001 > ~/.holocube_port # 可选；不写就自动探测 /dev/cu.usbserial-* 等
~/.holocube-pio/bin/python3 ~/.holocube/holocube_serial_send.py --check   # 应打印 AIS:PONG
~/.holocube-pio/bin/python3 ~/.holocube/holocube_serial_send.py --brightness 50  # 设亮度 0-100（保存，重启保持）
~/.holocube-pio/bin/python3 ~/.holocube/holocube_serial_send.py --brightness     # 查询亮度
~/.holocube/test_states.sh
```
需要带 pyserial 的 Python：脚本依次尝试 `~/.holocube-pio/bin/python3`、`~/.platformio/penv/bin/python3`、`python3`。

## 不会复位小电视
发送时 DTR/RTS 保持释放（pyserial: 打开前设 `dtr=False, rts=False`）。这样一般不会触发 ESP32 的自动复位电路【未在 Mac 实机验证】。如果每次发送屏幕都重启，请反馈。

## 注意
- **串口同一时间只能被一个程序打开**：开着 `pio device monitor` 或 VS Code 串口监视器时，发送会失败（静默丢弃，错误记录在 `$TMPDIR/holocube_notify.err`）。看日志时可暂时接受这一点。
- 脚本在后台发送，最多约 2 秒超时，不会卡住 Claude/Codex。
- 多个 hook 同时触发时用文件锁排队，排队超过 1.5 秒的消息会被丢弃。
- WiFi 模式：固件编译时设 `AI_STATUS_USE_WIFI 1`，电脑上设 `CUBE_IP`（或写 `~/.holocube_ip`），脚本就改走 HTTP。

## 接入 AI 工具
- Claude Code：把 `claude_settings_snippet.json` 合并进 `~/.claude/settings.json`。映射为 UserPromptSubmit→thinking、PreToolUse→working、Notification→waiting、Stop→done、StopFailure→error。
- Codex：把 `codex_config_snippet.toml` 那一行加到 `~/.codex/config.toml` 顶部。只有 done 一种状态。
- Grok：默认会读取 `~/.claude/settings.json` 的 hooks；或者用 `grok_wrapper.sh grok` 启动。

## 字库
固件内置 GB2312 一级 3755 字 + 常用符号 + 约 80 个 emoji。`sanitize_text.py` 会把字库外的 emoji 换成 `[文字]` 或删除，并按 UTF-8 字符边界截断到 340 字节。
