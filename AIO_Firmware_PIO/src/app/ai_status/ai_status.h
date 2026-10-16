#ifndef APP_AI_STATUS_H
#define APP_AI_STATUS_H

#include "sys/interface.h"

// 开机自启时，在 Web 配置页 "开机自启的APP名字" 填写这个名字
#define AI_STATUS_APP_NAME "AIStatus"

// 1 = 同时开启 WiFi + HTTP(POST/GET /status :8080)；0 = 只用 USB 串口（默认）
#ifndef AI_STATUS_USE_WIFI
#define AI_STATUS_USE_WIFI 0
#endif
#define AI_STATUS_HTTP_PORT 8080

// 收到串口状态行时，是否自动切换进 AIStatus（Settings 应用占用串口时除外）
#ifndef AI_STATUS_AUTO_ENTER
#define AI_STATUS_AUTO_ENTER 1
#endif

extern APP_OBJ ai_status_app;

class AppController;
// 在 loop() 中每轮调用：读取 USB 串口上的 JSON 状态行（无论当前在哪个 APP）
void ai_status_serial_poll(AppController *sys);

#endif
