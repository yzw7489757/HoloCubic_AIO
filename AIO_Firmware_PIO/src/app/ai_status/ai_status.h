#ifndef APP_AI_STATUS_H
#define APP_AI_STATUS_H

#include "sys/interface.h"

// 开机自启时，在 Web 配置页 "开机自启的APP名字" 填写这个名字
#define AI_STATUS_APP_NAME "AIStatus"
// HTTP 端口：server 应用占用 80，这里用 8080 避免冲突
#define AI_STATUS_HTTP_PORT 8080

extern APP_OBJ ai_status_app;

#endif
