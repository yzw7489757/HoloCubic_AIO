// AI Status: 用 HoloCubic 当 AI 编程助手的 "灵动岛"
// USB 串口(默认, 115200, 与日志同一个口): 发送一行 JSON + '\n'
//   {"agent":"claude","state":"thinking","text":"..."}   -> 回复 "AIS:OK" 或 "AIS:ERR <原因>"
//   {"ping":1}                                           -> 回复 "AIS:PONG"
//   {"brightness":N} (0-100, 立即生效并写入 /sys.cfg)      -> 回复 "AIS:OK brightness=N"（不切换 APP）
//   {"get":"brightness"}                                 -> 回复 "AIS:brightness=N"
// WiFi (AI_STATUS_USE_WIFI=1 时): POST /status 同样的 JSON；GET /status 查询
#include "ai_status.h"
#include "ai_status_gui.h"
#include "sys/app_controller.h"
#include "common.h"
#include <ArduinoJson.h>
#if AI_STATUS_USE_WIFI
#include <WiFi.h>
#include <WebServer.h>
#define ALIVE_INTERVAL_MS 20000
#endif

#define UI_INTERVAL_MS 1000
#define SERIAL_LINE_MAX 640

// ---- 常驻数据：APP 外收到的状态也保留，进入 APP 时直接显示 ----
static AiAgent g_agents[AI_MAX_AGENTS];
static int g_cur = -1;
static volatile bool g_dirty = false;

struct AiRunData
{
#if AI_STATUS_USE_WIFI
    WebServer *http;
    bool server_started;
    unsigned long last_alive;
#endif
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
    while (lim > 0 && ((uint8_t)src[lim] & 0xC0) == 0x80) lim--; // 回退到字符边界
    memcpy(dst, src, lim);
    if (ellipsis)
    {
        memcpy(dst + lim, "...", 3);
        lim += 3;
    }
    dst[lim] = 0;
}

// 更新一个 agent 的状态；成功返回 NULL，否则返回错误原因
static const char *ai_apply(const char *agent, const char *st, const char *text)
{
    bool ok;
    AiState s = ai_state_from_str(st, &ok);
    if (!ok) return "bad state";
    int idx = -1, oldest = 0;
    for (int i = 0; i < AI_MAX_AGENTS; i++)
    {
        if (g_agents[i].used && !strncasecmp(g_agents[i].name, agent, sizeof(g_agents[i].name) - 1))
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        for (int i = 0; i < AI_MAX_AGENTS; i++)
        {
            if (!g_agents[i].used) { idx = i; break; }
            if (g_agents[i].update_ms < g_agents[oldest].update_ms) oldest = i;
        }
        if (idx < 0) idx = oldest;
        memset(&g_agents[idx], 0, sizeof(AiAgent));
        utf8_copy(g_agents[idx].name, sizeof(g_agents[idx].name), agent, false);
        g_agents[idx].used = true;
        g_agents[idx].since_ms = millis();
        g_agents[idx].state = s;
    }
    AiAgent &a = g_agents[idx];
    if (a.state != s) a.since_ms = millis();
    a.state = s;
    a.update_ms = millis();
    utf8_copy(a.text, sizeof(a.text), text, true);
    g_cur = idx;
    g_dirty = true;
    return NULL;
}

// 解析一行 JSON；返回 NULL 表示成功
static const char *ai_apply_json(const char *json, size_t len, bool *is_ping)
{
    *is_ping = false;
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, json, len)) return "bad json";
    if (doc.containsKey("ping"))
    {
        *is_ping = true;
        return NULL;
    }
    if (!doc.containsKey("agent") || !doc.containsKey("state")) return "need agent+state";
    return ai_apply(doc["agent"] | "agent", doc["state"] | "idle", doc["text"] | "");
}

// ---------------- USB 串口 ----------------
static char s_line[SERIAL_LINE_MAX];
static size_t s_len = 0;
static bool s_overflow = false;

// 串口控制命令：{"brightness":N} 设置并保存屏幕亮度；{"get":"brightness"} 查询。
// 返回 true 表示本行是控制命令（已回复），不会自动切换 APP
static bool handle_command(AppController *sys, const char *p, size_t len)
{
    if (!strstr(p, "\"brightness\"")) return false;
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, p, len))
    {
        Serial.println("AIS:ERR bad json");
        return true;
    }
    if (doc.containsKey("brightness"))
    {
        JsonVariant v = doc["brightness"];
        if (!v.is<int>() && !v.is<float>())
        {
            Serial.println("AIS:ERR brightness must be 0-100");
            return true;
        }
        long n = v.as<long>();
        if (n < 0 || n > 100)
        {
            Serial.println("AIS:ERR brightness must be 0-100");
            return true;
        }
        screen.setBackLight(n / 100.0); // 立即生效（与开机时 screen.init 用法相同）
        // 与 Web 设置页相同的保存路径：SET_PARAM backLight -> WRITE_CFG（写 /sys.cfg）
        char val[8];
        snprintf(val, sizeof(val), "%ld", n);
        sys->send_to(AI_STATUS_APP_NAME, CTRL_NAME, APP_MESSAGE_SET_PARAM, (void *)"backLight", (void *)val);
        sys->send_to(AI_STATUS_APP_NAME, CTRL_NAME, APP_MESSAGE_WRITE_CFG, NULL, NULL);
        Serial.printf("AIS:OK brightness=%ld\n", n);
        return true;
    }
    const char *g = doc["get"] | "";
    if (!strcmp(g, "brightness"))
    {
        Serial.printf("AIS:brightness=%u\n", (unsigned)sys->sys_cfg.backLight);
        return true;
    }
    Serial.println("AIS:ERR unknown command");
    return true;
}

static void handle_line(AppController *sys)
{
    s_line[s_len] = 0;
    // 只接受以 '{' 开头且含 "agent"/"ping"/"brightness" 的行（忽略其他任何输入/噪声）
    const char *p = s_line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '{') return;
    size_t len = s_len - (p - s_line);
    if (handle_command(sys, p, len)) return;
    if (!strstr(p, "\"agent\"") && !strstr(p, "\"ping\"")) return;
    bool is_ping;
    const char *err = ai_apply_json(p, len, &is_ping);
    if (err)
    {
        Serial.printf("AIS:ERR %s\n", err);
        return;
    }
    if (is_ping)
    {
        Serial.println("AIS:PONG");
        return;
    }
    Serial.println("AIS:OK");
#if AI_STATUS_AUTO_ENTER
    sys->app_enter(AI_STATUS_APP_NAME); // 已在本 APP 时不做任何事
#endif
}

void ai_status_serial_poll(AppController *sys)
{
    // Settings 应用自己用串口收数据，此时不抢
    const char *cur = sys->current_app_name();
    if (cur && !strcmp(cur, "Settings")) return;

    int guard = 512; // 每轮最多处理的字节数，避免卡住主循环
    while (Serial.available() > 0 && guard-- > 0)
    {
        int c = Serial.read();
        if (c < 0) break;
        if (c == '\n')
        {
            if (!s_overflow && s_len > 0)
            {
                if (s_line[s_len - 1] == '\r') s_len--;
                handle_line(sys);
            }
            s_len = 0;
            s_overflow = false;
        }
        else if (s_len < SERIAL_LINE_MAX - 1)
        {
            s_line[s_len++] = (char)c;
        }
        else
        {
            s_overflow = true; // 行太长：丢弃到下一个换行
        }
    }
}

// ---------------- WiFi / HTTP（可选） ----------------
#if AI_STATUS_USE_WIFI
static void send_json_state()
{
    DynamicJsonDocument doc(1536);
    doc["ok"] = true;
    doc["app"] = AI_STATUS_APP_NAME;
    doc["ip"] = WiFi.localIP().toString();
    doc["uptime_s"] = millis() / 1000;
    JsonArray arr = doc.createNestedArray("agents");
    for (int i = 0; i < AI_MAX_AGENTS; i++)
    {
        if (!g_agents[i].used) continue;
        JsonObject o = arr.createNestedObject();
        o["agent"] = g_agents[i].name;
        o["state"] = ai_state_name(g_agents[i].state);
        o["text"] = g_agents[i].text;
        o["elapsed_s"] = (millis() - g_agents[i].since_ms) / 1000;
    }
    String out;
    serializeJson(doc, out);
    rd->http->send(200, "application/json", out);
}

static void handle_post()
{
    if (!rd->http->hasArg("plain"))
    {
        rd->http->send(400, "application/json", "{\"ok\":false,\"err\":\"empty body\"}");
        return;
    }
    String body = rd->http->arg("plain");
    bool is_ping;
    const char *err = ai_apply_json(body.c_str(), body.length(), &is_ping);
    if (err)
    {
        rd->http->send(400, "application/json", String("{\"ok\":false,\"err\":\"") + err + "\"}");
        return;
    }
    send_json_state();
}

static void start_server()
{
    if (rd->server_started || WiFi.status() != WL_CONNECTED) return;
    rd->http = new WebServer(AI_STATUS_HTTP_PORT);
    rd->http->on("/status", HTTP_GET, send_json_state);
    rd->http->on("/status", HTTP_POST, handle_post);
    rd->http->onNotFound([]() { rd->http->send(404, "text/plain", "use GET/POST /status"); });
    rd->http->begin();
    rd->server_started = true;
    String ip = WiFi.localIP().toString();
    Serial.printf("[AIStatus] HTTP server: http://%s:%d/status\n", ip.c_str(), AI_STATUS_HTTP_PORT);
    ai_status_gui_set_ip(("USB | " + ip + ":" + String(AI_STATUS_HTTP_PORT)).c_str());
}
#endif

// ---------------- APP 生命周期 ----------------
static int ai_status_init(AppController *sys)
{
    rd = (AiRunData *)calloc(1, sizeof(AiRunData));
    rd->led_state = AI_STATE_NUM;
    ai_status_gui_init();
#if AI_STATUS_USE_WIFI
    ai_status_gui_set_ip("USB|WiFi..");
    sys->send_to(AI_STATUS_APP_NAME, CTRL_NAME, APP_MESSAGE_WIFI_CONN, NULL, NULL);
    rd->last_alive = millis();
#else
    ai_status_gui_set_ip("USB");
#endif
    g_dirty = true; // 显示 APP 外已收到的状态
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
#if AI_STATUS_USE_WIFI
    if (now - rd->last_alive > ALIVE_INTERVAL_MS)
    {
        sys->send_to(AI_STATUS_APP_NAME, CTRL_NAME,
                     WiFi.status() == WL_CONNECTED ? APP_MESSAGE_WIFI_ALIVE : APP_MESSAGE_WIFI_CONN,
                     NULL, NULL);
        rd->last_alive = now;
    }
    if (!rd->server_started) start_server();
    if (rd->server_started) rd->http->handleClient();
#endif
    if (g_dirty || now - rd->last_ui > UI_INTERVAL_MS)
    {
        g_dirty = false;
        ai_status_gui_render(g_agents, AI_MAX_AGENTS, g_cur, now);
        if (g_cur >= 0 && g_agents[g_cur].state != rd->led_state)
        {
            rd->led_state = g_agents[g_cur].state;
            set_led(rd->led_state);
        }
        rd->last_ui = now;
    }
    delay(5);
}

static void ai_status_background_task(AppController *sys, const ImuAction *act_info)
{
}

static int ai_status_exit_callback(void *param)
{
    if (rd)
    {
#if AI_STATUS_USE_WIFI
        if (rd->http)
        {
            rd->http->stop();
            delete rd->http;
        }
#endif
        free(rd);
        rd = NULL;
    }
    ai_status_gui_del();
    return 0;
}

static void ai_status_message_handle(const char *from, const char *to,
                                     APP_MESSAGE_TYPE type, void *message, void *ext_info)
{
#if AI_STATUS_USE_WIFI
    if (type == APP_MESSAGE_WIFI_CONN && rd)
    {
        Serial.print("[AIStatus] WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
        start_server();
    }
#endif
}

APP_OBJ ai_status_app = {AI_STATUS_APP_NAME, &app_ai_status, "AI agent status display\nVersion 0.2.2 (USB)\n",
                         ai_status_init, ai_status_process, ai_status_background_task,
                         ai_status_exit_callback, ai_status_message_handle};
