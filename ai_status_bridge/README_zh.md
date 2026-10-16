# HoloCubic「AI 灵动岛」使用教程（macOS + WiFi，零基础版）

让 HoloCubic 小电视实时显示 Claude Code / Codex / Grok 的状态：
`thinking`(蓝) `working`(橙) `waiting`(粉，等你确认) `done`(绿) `error`(红) `idle`(灰)。

原理：电脑上的 AI 工具在关键时刻运行 `holocube_notify.sh`，脚本用 curl 把
`{"agent","state","text"}` 发到小电视的 `http://<IP>:8080/status`。

---

## 1. 安装开发环境
1. 安装 [VS Code](https://code.visualstudio.com/)。
2. VS Code 左侧「扩展」里搜索 **PlatformIO IDE** 安装，装完重启 VS Code（第一次会自动下载工具链，要几分钟）。
3. USB 串口驱动：
   - 板子上是 **CP210x** 芯片：新版 macOS 一般免驱；不识别再装 Silicon Labs 的 CP210x VCP 驱动。
   - 是 **CH340/CH343**：macOS 较新版本一般自带驱动；不识别就装 WCH 官方 macOS 驱动，安装后到「系统设置 → 隐私与安全性」点允许，然后重启。
   - 一定要用**能传数据的 USB 线**（有些线只能充电）。

## 2. 打开工程
1. 解压 `holocubic_ai_status.zip`。
2. VS Code → File → Open Folder → 选择 **`AIO_Firmware_PIO`** 这个文件夹（不是上一级）。
3. 插上小电视，终端运行：
   ```bash
   ls /dev/cu.*
   ```
   找到类似 `/dev/cu.usbserial-0001`、`/dev/cu.wchusbserial*` 或 `/dev/cu.SLAB_USBtoUART` 的那一项。
4. 打开 `platformio.ini`，把
   ```ini
   upload_port = COM12
   ```
   改成你的端口，例如：
   ```ini
   upload_port = /dev/cu.usbserial-0001
   monitor_port = /dev/cu.usbserial-0001
   ```

## 3. 编译、烧录、看日志
- VS Code 底部蓝色状态栏：✓ = Build（编译），→ = Upload（烧录），🔌 = Serial Monitor。
- 或在终端（工程目录下）：
  ```bash
  pio run -e HoloCubic_AIO_Releases            # 编译
  pio run -e HoloCubic_AIO_Releases -t upload  # 烧录
  pio device monitor -b 115200                 # 串口日志（Ctrl+C 退出）
  ```
- 烧录卡在 `Connecting....`：按住板子 BOOT/IO0 键再点烧录；或把 `upload_speed` 改成 `115200` 再试。
- 本工程里 APP 开关在 `src/app/app_conf.h`。Flash 已用约 87%，以后加东西不够时把不用的 APP 宏改为 0。

## 4. 配置 WiFi（用固件自带的方法）
1. 小电视左右倾斜切换 APP，找到 **WebServer**，前倾进入。
2. 第一次没配过 WiFi 时，它会放出热点 **`HoloCubic_AIO`**（无密码），Mac 连上它；屏幕上会显示要访问的 IP 地址。
3. 浏览器打开该 IP → 系统设置，填写 **WiFi SSID_0 / 密码（必须是 2.4G WiFi）**，保存。
4. 可选：「开机自启的APP名字」填 **`AIStatus`**，以后一开机就直接进灵动岛。
5. 后仰（返回手势）退出 WebServer。

> 供电不足时连 WiFi 会重启，请用电脑后置 USB 口或靠谱的充电头。

## 5. 进入 AI Status、获取 IP
- 开机后菜单第一个就是 **AIStatus**（图标是一个胶囊+三个点），前倾进入。
- 屏幕底部会显示 `192.168.x.x:8080`；串口里也会打印：
  ```
  [AIStatus] HTTP server: http://192.168.x.x:8080/status
  ```
- 退出：和其它 APP 一样做「返回」手势（后仰）。
- 建议在路由器里给小电视做 DHCP 固定 IP，免得 IP 变化。

## 6. 在 Mac 上测试
```bash
# 健康检查
curl http://192.168.x.x:8080/status
# 发一个状态
curl -X POST http://192.168.x.x:8080/status \
  -H 'Content-Type: application/json' \
  -d '{"agent":"claude","state":"thinking","text":"hello cube"}'
```
屏幕应显示 claude / thinking / hello cube，RGB 灯变蓝。

## 7. 安装通知脚本
```bash
mkdir -p ~/.holocube
cp holocube_notify.sh sanitize_text.py ai_cn16_charset.txt grok_wrapper.sh test_states.sh ~/.holocube/
chmod +x ~/.holocube/*.sh
echo "192.168.x.x" > ~/.holocube_ip      # 换成你的小电视 IP（默认端口 8080）
~/.holocube/test_states.sh               # 屏幕会依次切换所有状态
```
也可以用环境变量 `export CUBE_IP=192.168.x.x` 覆盖。脚本在后台发送、2 秒超时，小电视关机也不会卡住 AI 工具。

## 8. 接入 Claude Code
把 `claude_settings_snippet.json` 里的 `"hooks"` 合并进 `~/.claude/settings.json`（文件不存在就直接复制过去；已存在就把 hooks 段合并进去，注意 JSON 逗号）。
映射：`UserPromptSubmit`→thinking，`PreToolUse`→working，`Notification`→waiting，`Stop`→done，`StopFailure`→error。
在 Claude Code 里输入 `/hooks` 可以确认已加载。

## 9. 接入 Codex CLI
把 `codex_config_snippet.toml` 的那一行加进 **`~/.codex/config.toml`**（放在文件最上面、任何 `[xxx]` 小节之前），把 `YOUR_NAME` 换成 `whoami` 的结果。
Codex 的 `notify` 目前只在**一轮回答结束**（`agent-turn-complete`）时触发，所以只会显示 `done`；没有「等待确认」事件。

## 10. 接入 Grok
- Grok CLI（`grok`）支持 hooks，并且**默认会读取 `~/.claude/settings.json` 里的 Claude hooks**。所以做完第 8 步，Grok 通常也会自动上报；脚本检测到环境变量 `GROK_HOOK_EVENT` 时会把 agent 名写成 `grok`。
- 也可以单独写到 `~/.grok/hooks/holocube.json`（格式与 Claude 相同），在 Grok 里 `/hooks` 或终端 `grok inspect` 查看是否加载。
- 不想用 hooks：`~/.holocube/grok_wrapper.sh grok` 启动，开始时显示 working，退出时显示 done/error（任何命令都能这样包）。

## 11. 常见问题
| 现象 | 排查 |
|---|---|
| `ls /dev/cu.*` 看不到设备 | 换数据线 / 装驱动 / 换 USB 口 |
| 烧录 `Failed to connect` | 按住 BOOT 再烧；降低 upload_speed；关闭占用串口的监视器 |
| 屏幕一直 `WiFi connecting...` | 先用 WebServer 配 2.4G WiFi；看串口日志；供电是否足够 |
| curl 超时 | Mac 和小电视是否同一网络；访客网络/AP 隔离会挡住；IP 是否变化；是否已进入 AIStatus APP（只有在该 APP 里才监听 8080） |
| 状态不更新 | 手动运行 `echo '{}' \| ~/.holocube/holocube_notify.sh working`；检查 `~/.holocube_ip`；Claude 中用 `/hooks` 查看 |
| 个别字显示为空白 | 固件内置字库为 GB2312 一级 3755 字 + 常用符号 + 约 80 个 emoji，二级生僻字不显示；不在字库的 emoji 由 sanitize_text.py 替换为 [文字] |
| 编译报 `arduino.h`/`EMOJI.h` 找不到 | 大小写问题，本包已修复（macOS 默认不区分大小写，一般不会遇到） |
