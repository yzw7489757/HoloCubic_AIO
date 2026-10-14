// AI Status: 用 HoloCubic 当 AI 编程助手的 "灵动岛"
// HTTP: POST /status {"agent":"claude","state":"thinking","text":"..."}   GET /status
#include "ai_status.h"
#include "ai_status_gui.h"
#include "sys/app_controller.h"
#include "common.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#define ALIVE_INTERVAL_MS 20000
#define UI_INTERVAL_MS 1000

struct AiRunData
{
    WebServer *http;
    AiAgent agents[AI_MAX_AGENTS];
    int cur;
    bool server_started;
    bool dirty;
    unsigned long last_alive;
    unsigned long last_ui;
    AiState led_state;
};

static AiRunData *rd = NULL;

static void set_led(AiState s)
{
    uint32_t c = ai_state_color(s);
    uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    // 固定颜色：min==max, step=0；退出 APP 时控制器会恢复用户的 RGB 设置
    RgbParam p = {LED_MODE_RGB, r, g, b, r, g, b, 0, 0, 0, 150, 150, 0, 100};
    set_rgb_and_run(&p, RUN_MODE_TASK);
}

// UTF-8 安全拷贝：最多 cap-1 字节，不截断多字节字符；被截断时尽量追加 "..."
static void utf8_copy(char *dst, size_t cap, const char *src, bool ellipsis)
{
    size_t n = strlen(src);
    if (n < cap)
    {
        memcpy(dst, src, n + 1);
        return;
    }
    size_t lim = cap - 1 - (ellipsis ? 3 : 0);
    // 回退到字符边界（不能停在 10xxxxxx 续字节上）
    while (lim > 0 && ((uint8_t)src[lim] & 0xC0) == 0x80) lim--;
    memcpy(dst, src, lim);
    if (ellipsis)
    {
        memcpy(dst + lim, "...", 3);
        lim += 3;
    }
    dst[lim] = 0;
}

static void send_json_state()
{
    DynamicJsonDocument doc(1024);
    doc["ok"] = true;
    doc["app"] = AI_STATUS_APP_NAME;
    doc["ip"] = WiFi.localIP().toString();
    doc["uptime_s"] = millis() / 1000;
    JsonArray arr = doc.createNestedArray("agents");
    for (int i = 0; i < AI_MAX_AGENTS; i++)
    {
        if (!rd->agents[i].used) continue;
        JsonObject o = arr.createNestedObject();
        o["agent"] = rd->agents[i].name;
        o["state"] = ai_state_name(rd->agents[i].state);
        o["text"] = rd->agents[i].text;
        o["elapsed_s"] = (millis() - rd->agents[i].since_ms) / 1000;
    }
    String out;
    serializeJson(doc, out);
    rd->http->send(200, "application/json", out);
}

static void handle_get() { send_json_state(); }

static void handle_post()
{
    if (!rd->http->hasArg("plain"))
    {
        rd->http->send(400, "application/json", "{\"ok\":false,\"err\":\"empty body\"}");
        return;
    }
    DynamicJsonDocument doc(768);
    if (deserializeJson(doc, rd->http->arg("plain")))
    {
        rd->http->send(400, "application/json", "{\"ok\":false,\"err\":\"bad json\"}");
        return;
    }
    const char *agent = doc["agent"] | "agent";
    const char *st = doc["state"] | "idle";
    const char *text = doc["text"] | "";
    bool ok;
    AiState s = ai_state_from_str(st, &ok);
    if (!ok)
    {
        rd->http->send(400, "application/json",
                       "{\"ok\":false,\"err\":\"state must be idle/thinking/working/waiting/done/error\"}");
        return;
    }
    // 找已有 agent，否则占空位，否则替换最久没更新的
    int idx = -1, oldest = 0;
    for (int i = 0; i < AI_MAX_AGENTS; i++)
    {
        if (rd->agents[i].used && !strncasecmp(rd->agents[i].name, agent, sizeof(rd->agents[i].name) - 1))
        { idx = i; break; }
    }
    if (idx < 0)
    {
        for (int i = 0; i < AI_MAX_AGENTS; i++)
        {
            if (!rd->agents[i].used) { idx = i; break; }
            if (rd->agents[i].update_ms < rd->agents[oldest].update_ms) oldest = i;
        }
        if (idx < 0) idx = oldest;
        memset(&rd->agents[idx], 0, sizeof(AiAgent));
        utf8_copy(rd->agents[idx].name, sizeof(rd->agents[idx].name), agent, false);
        rd->agents[idx].used = true;
        rd->agents[idx].since_ms = millis();
        rd->agents[idx].state = s;
    }
    AiAgent &a = rd->agents[idx];
    if (a.state != s) a.since_ms = millis();
    a.state = s;
    a.update_ms = millis();
    // 截断：超过 sizeof(text)-1 字节时按 UTF-8 字符边界截断并加 "..."
    utf8_copy(a.text, sizeof(a.text), text, true);
    rd->cur = idx;
    rd->dirty = true;
    Serial.printf("[AIStatus] %s -> %s : %s\n", a.name, ai_state_name(s), a.text);
    send_json_state();
}

static void start_server()
{
    if (rd->server_started || WiFi.status() != WL_CONNECTED) return;
    rd->http = new WebServer(AI_STATUS_HTTP_PORT);
    rd->http->on("/status", HTTP_GET, handle_get);
    rd->http->on("/status", HTTP_POST, handle_post);
    rd->http->onNotFound([]() { rd->http->send(404, "text/plain", "use GET/POST /status"); });
    rd->http->begin();
    rd->server_started = true;
    String ip = WiFi.localIP().toString();
    Serial.printf("[AIStatus] HTTP server: http://%s:%d/status\n", ip.c_str(), AI_STATUS_HTTP_PORT);
    ai_status_gui_set_ip((ip + ":" + String(AI_STATUS_HTTP_PORT)).c_str());
}

static int ai_status_init(AppController *sys)
{
    rd = (AiRunData *)calloc(1, sizeof(AiRunData));
    rd->cur = -1;
    rd->led_state = AI_STATE_NUM;
    ai_status_gui_init();
    ai_status_gui_render(rd->agents, AI_MAX_AGENTS, rd->cur, millis());
    // 用系统现有流程连接 WiFi（成功后回调 message_handle）
    sys->send_to(AI_STATUS_APP_NAME, CTRL_NAME, APP_MESSAGE_WIFI_CONN, NULL, NULL);
    rd->last_alive = millis();
    return 0;
}

static void ai_status_process(AppController *sys, const ImuAction *act_info)
{
    if (RETURN == act_info->active)
    {
        sys->app_exit();
        return;
    }
    unsigned long now = millis();
    if (now - rd->last_alive > ALIVE_INTERVAL_MS)
    {
        // 心跳：防止控制器因空闲关掉 WiFi；断线时重新请求连接
        sys->send_to(AI_STATUS_APP_NAME, CTRL_NAME,
                     WiFi.status() == WL_CONNECTED ? APP_MESSAGE_WIFI_ALIVE : APP_MESSAGE_WIFI_CONN,
                     NULL, NULL);
        rd->last_alive = now;
    }
    if (!rd->server_started) start_server();
    if (rd->server_started) rd->http->handleClient();

    if (rd->dirty || now - rd->last_ui > UI_INTERVAL_MS)
    {
        ai_status_gui_render(rd->agents, AI_MAX_AGENTS, rd->cur, now);
        if (rd->cur >= 0 && rd->agents[rd->cur].state != rd->led_state)
        {
            rd->led_state = rd->agents[rd->cur].state;
            set_led(rd->led_state);
        }
        rd->dirty = false;
        rd->last_ui = now;
    }
    delay(5);
}

static void ai_status_background_task(AppController *sys, const ImuAction *act_info)
{
    // 当前版本控制器并不调用 background_task；保活放在 process 里
    sys->send_to(AI_STATUS_APP_NAME, CTRL_NAME, APP_MESSAGE_WIFI_ALIVE, NULL, NULL);
}

static int ai_status_exit_callback(void *param)
{
    if (rd)
    {
        if (rd->http)
        {
            rd->http->stop();
            delete rd->http;
        }
        free(rd);
        rd = NULL;
    }
    ai_status_gui_del();
    return 0;
}

static void ai_status_message_handle(const char *from, const char *to,
                                     APP_MESSAGE_TYPE type, void *message, void *ext_info)
{
    if (type == APP_MESSAGE_WIFI_CONN && rd)
    {
        Serial.print("[AIStatus] WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
        start_server();
    }
}

APP_OBJ ai_status_app = {AI_STATUS_APP_NAME, &app_ai_status, "AI agent status display\nVersion 0.1.0\n",
                         ai_status_init, ai_status_process, ai_status_background_task,
                         ai_status_exit_callback, ai_status_message_handle};
