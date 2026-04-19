/*
 * TB6612motor_ESP32S3_WiFi.ino
 *
 * 主控  : ESP32-S3 N8R2 开发板
 * 驱动  : TB6612 D153C 双路驱动稳压模块
 * 电机  : 33GB-520-18.7F DC 12V × 2
 * 激光  : 18×45 980nm 30mW 100KHz 5V TTL 调制模组
 * 接收  : VS1838B（1838）红外接收管
 *
 * 功能：
 *   1. ESP32-S3 以 AP（热点）模式启动，手机连入后通过 WebSocket 双向通信
 *   2. 接收手机指令，独立控制左（Motor A）/ 右（Motor B）电机的转速与方向
 *   3. 接收手机指令，开关激光发射器（38 KHz TTL 载波）
 *   4. 统计 VS1838B OUT 引脚的 FALLING 沿次数（每次激光命中），并实时推送给手机
 *
 * ── GPIO 分配（与 硬件接线图.md 一致）──────────────────────────────
 *   GPIO 4  → TB6612 PWMA   左电机速度 PWM（LEDC）
 *   GPIO 5  → TB6612 AIN1   左电机方向1
 *   GPIO 6  → TB6612 AIN2   左电机方向2
 *   GPIO 7  → TB6612 PWMB   右电机速度 PWM（LEDC）
 *   GPIO 8  → TB6612 BIN1   右电机方向1
 *   GPIO 9  → TB6612 BIN2   右电机方向2
 *   GPIO 10 → TB6612 STBY   驱动使能（HIGH = 工作）
 *   GPIO 11 → 激光 TTL      38 KHz PWM 载波（LEDC）
 *   GPIO 12 ← VS1838B OUT   Active LOW 中断输入
 *
 * ── 依赖库（Arduino 库管理器安装）─────────────────────────────────
 *   "WebSockets" by Markus Sattler  (arduinoWebSockets ≥ 2.4.0)
 *   "ArduinoJson" by Benoit Blanchon (≥ 6.x)
 *
 * ── WebSocket 通信协议（JSON）──────────────────────────────────────
 *   手机 → ESP32（发送指令）：
 *     {"cmd":"motor","a":<-255~255>,"b":<-255~255>}   // 电机速度，负值反转，0=滑行（coast）
 *     {"cmd":"laser","on":<true|false>}               // 激光开关
 *     {"cmd":"stop"}                                  // 滑行停止（LOW LOW 惰行）
 *     {"cmd":"brake"}                                 // 制动停止（SHORT BRAKE 急停，推荐竞赛使用）
 *     {"cmd":"ir_reset"}                              // 清零 IR 计数器
 *     {"cmd":"get_ir"}                                // 主动请求当前计数
 *
 *   ESP32 → 手机（主动推送）：
 *     {"status":"connected","ip":"192.168.4.18","ssid":"Robot-ESP32"}
 *     {"ir_count":<uint32>}                           // 计数变化时实时推送
 *
 * ── 安全机制 ────────────────────────────────────────────────────────
 *   若超过 MOTOR_TIMEOUT_MS 毫秒未收到任何指令，自动停止双电机
 *   WebSocket 连接断开时立即停止双电机
 */

#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ════════════════════════════════════════════════════════════════
//  Wi-Fi AP 配置
// ════════════════════════════════════════════════════════════════
static const char* AP_SSID     = "Robot-ESP32-Barry";
static const char* AP_PASSWORD = "rockwellrobot1234";    // 至少 8 位

// ════════════════════════════════════════════════════════════════
//  引脚定义
// ════════════════════════════════════════════════════════════════
// 左电机（Motor A）
#define PWMA_PIN   4
#define AIN1_PIN   5
#define AIN2_PIN   6

// 右电机（Motor B）
#define PWMB_PIN   7
#define BIN1_PIN   8
#define BIN2_PIN   9

// TB6612 驱动使能
#define STBY_PIN   10

// 激光发射器 TTL 信号
#define LASER_PIN  11

// VS1838B 红外接收输出（Active LOW）
#define IR_PIN     12

// ════════════════════════════════════════════════════════════════
//  LEDC 通道 & 频率配置
//  arduino-esp32 v3.x API：ledcAttachChannel(pin, freq, res, channel)
//  写占空比使用 ledcWrite(pin, duty)，以引脚号而非通道号寻址
// ════════════════════════════════════════════════════════════════
#define LEDC_PWMA_CH     0      // 左电机 PWM 通道
#define LEDC_PWMB_CH     1      // 右电机 PWM 通道
#define LEDC_LASER_CH    2      // 激光 PWM 通道

#define MOTOR_PWM_FREQ   1000   // 电机载波频率 1 kHz
#define MOTOR_PWM_RES    8      // 8-bit 分辨率（duty 0~255）

#define LASER_FREQ       38000  // 激光载波 38 kHz（匹配 VS1838B 带通滤波器）
#define LASER_DUTY_ON    128    // 50% 占空比（128 / 256）

// ════════════════════════════════════════════════════════════════
//  WebSocket
// ════════════════════════════════════════════════════════════════
#define WS_PORT  81

WebSocketsServer wsServer(WS_PORT);

// ════════════════════════════════════════════════════════════════
//  电机安全超时
// ════════════════════════════════════════════════════════════════
#define MOTOR_TIMEOUT_MS  2000  // 2 秒无指令则停车

static unsigned long lastCmdTime = 0;

// ════════════════════════════════════════════════════════════════
//  VS1838B 中断计数（ISR 中使用 volatile）
// ════════════════════════════════════════════════════════════════
static volatile uint32_t irCount    = 0;
static volatile bool     irNewPulse = false;

void IRAM_ATTR onIRFalling() {
    // VS1838B 接收到 38 KHz 载波时 OUT 由 HIGH→LOW
    irCount++;
    irNewPulse = true;
}

// ════════════════════════════════════════════════════════════════
//  电机控制函数
//  pwm 范围：-255（满速反转）~ 0（停止）~ +255（满速正转）
//  方向逻辑参考 TB6612motor_ArduinoUNO_Demo.ino：
//    Motor A 正转：AIN1=HIGH, AIN2=LOW
//    Motor B 正转：BIN1=LOW,  BIN2=HIGH
// ════════════════════════════════════════════════════════════════
static void setMotorA(int pwm) {
    pwm = constrain(pwm, -255, 255);
    if (pwm > 0) {                          // 正转：AIN1=H, AIN2=L
        digitalWrite(AIN1_PIN, HIGH);
        digitalWrite(AIN2_PIN, LOW);
        ledcWrite(PWMA_PIN, (uint32_t)pwm);
    } else if (pwm < 0) {                   // 反转：AIN1=L, AIN2=H
        digitalWrite(AIN1_PIN, LOW);
        digitalWrite(AIN2_PIN, HIGH);
        ledcWrite(PWMA_PIN, (uint32_t)(-pwm));
    } else {                                // 滑行：AIN1=L, AIN2=L, PWM=0
        digitalWrite(AIN1_PIN, LOW);
        digitalWrite(AIN2_PIN, LOW);
        ledcWrite(PWMA_PIN, 0);
    }
}

static void setMotorB(int pwm) {
    pwm = constrain(pwm, -255, 255);
    if (pwm > 0) {                          // 正转：BIN1=H, BIN2=L
        digitalWrite(BIN1_PIN, HIGH);
        digitalWrite(BIN2_PIN, LOW);
        ledcWrite(PWMB_PIN, (uint32_t)pwm);
    } else if (pwm < 0) {                   // 反转：BIN1=L, BIN2=H
        digitalWrite(BIN1_PIN, LOW);
        digitalWrite(BIN2_PIN, HIGH);
        ledcWrite(PWMB_PIN, (uint32_t)(-pwm));
    } else {                                // 滑行：BIN1=L, BIN2=L, PWM=0
        digitalWrite(BIN1_PIN, LOW);
        digitalWrite(BIN2_PIN, LOW);
        ledcWrite(PWMB_PIN, 0);
    }
}

static void stopMotors() {
    // 滑行停止：AIN/BIN 全低，PWM=0（惰行，适合弧线转弯复位）
    setMotorA(0);
    setMotorB(0);
}

static void brakeMotors() {
    // 制动停止：AIN1=AIN2=HIGH，BIN1=BIN2=HIGH，PWM=0（短路制动，竞赛急停首选）
    digitalWrite(AIN1_PIN, HIGH);
    digitalWrite(AIN2_PIN, HIGH);
    ledcWrite(PWMA_PIN, 0);
    digitalWrite(BIN1_PIN, HIGH);
    digitalWrite(BIN2_PIN, HIGH);
    ledcWrite(PWMB_PIN, 0);
}

// ════════════════════════════════════════════════════════════════
//  激光控制
// ════════════════════════════════════════════════════════════════
static void setLaser(bool on) {
    ledcWrite(LASER_PIN, on ? LASER_DUTY_ON : 0);
}

// ════════════════════════════════════════════════════════════════
//  发送 IR 计数给指定客户端
// ════════════════════════════════════════════════════════════════
static void sendIRCount(uint8_t clientNum) {
    noInterrupts();
    uint32_t count = irCount;
    interrupts();

    Serial.printf("[IR] Send ir_count=%u → client #%u\n", count, clientNum);
    StaticJsonDocument<64> doc;
    doc["ir_count"] = count;
    char buf[64];
    serializeJson(doc, buf);
    wsServer.sendTXT(clientNum, buf);
}

// ════════════════════════════════════════════════════════════════
//  广播 IR 计数给所有已连接的客户端
// ════════════════════════════════════════════════════════════════
static void broadcastIRCount() {
    noInterrupts();
    uint32_t count = irCount;
    interrupts();

    Serial.printf("[IR] Broadcast ir_count=%u\n", count);
    StaticJsonDocument<64> doc;
    doc["ir_count"] = count;
    char buf[64];
    serializeJson(doc, buf);
    wsServer.broadcastTXT(buf);
}

// ════════════════════════════════════════════════════════════════
//  WebSocket 事件回调
// ════════════════════════════════════════════════════════════════
void onWebSocketEvent(uint8_t clientNum, WStype_t type,
                      uint8_t* payload, size_t length) {
    switch (type) {

        // ── 客户端连接 ────────────────────────────────────────
        case WStype_CONNECTED: {
            IPAddress ip = wsServer.remoteIP(clientNum);
            Serial.printf("[WS] Client #%u connected from %s\n",
                          clientNum, ip.toString().c_str());

            // 发送握手确认（包含 AP IP 和 SSID）
            StaticJsonDocument<128> doc;
            doc["status"] = "connected";
            doc["ip"]     = WiFi.softAPIP().toString();
            doc["ssid"]   = AP_SSID;
            char buf[128];
            serializeJson(doc, buf);
            wsServer.sendTXT(clientNum, buf);
            break;
        }

        // ── 客户端断开（制动停车，防止失控滑行）────────────
        case WStype_DISCONNECTED:
            Serial.printf("[WS] Client #%u disconnected\n", clientNum);
            brakeMotors();
            break;

        // ── 接收 JSON 文本指令 ────────────────────────────────
        case WStype_TEXT: {
            Serial.printf("[WS #%u] RX: %.*s\n", clientNum, (int)length, (char*)payload);
            StaticJsonDocument<128> doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) {
                Serial.printf("[WS] JSON parse error: %s\n", err.c_str());
                break;
            }

            const char* cmd = doc["cmd"];
            if (!cmd) break;

            // ── {"cmd":"motor","a":<-255~255>,"b":<-255~255>} ──
            if (strcmp(cmd, "motor") == 0) {
                int a = doc["a"] | 0;
                int b = doc["b"] | 0;
                setMotorA(a);
                setMotorB(b);
                lastCmdTime = millis();
                Serial.printf("[Motor] A=%d  B=%d\n", a, b);
            }
            // ── {"cmd":"laser","on":<true|false>} ──────────────
            else if (strcmp(cmd, "laser") == 0) {
                bool on = doc["on"] | false;
                setLaser(on);
                Serial.printf("[Laser] %s\n", on ? "ON" : "OFF");
            }
            // ── {"cmd":"stop"} ─────────────────────────────────
            else if (strcmp(cmd, "stop") == 0) {
                stopMotors();
                lastCmdTime = millis();
                Serial.println("[Motor] STOP(coast)");
            }
            // ── {"cmd":"brake"} ────────────────────────────────
            else if (strcmp(cmd, "brake") == 0) {
                brakeMotors();
                lastCmdTime = millis();
                Serial.println("[Motor] BRAKE");
            }
            // ── {"cmd":"ir_reset"} ────────────────────────────
            else if (strcmp(cmd, "ir_reset") == 0) {
                noInterrupts();
                irCount    = 0;
                irNewPulse = false;
                interrupts();
                Serial.println("[IR] Counter reset");
                sendIRCount(clientNum);
            }
            // ── {"cmd":"get_ir"} ─────────────────────────────
            else if (strcmp(cmd, "get_ir") == 0) {
                Serial.printf("[IR] get_ir ← client #%u\n", clientNum);
                sendIRCount(clientNum);
            }
            else {
                Serial.printf("[WS] Unknown cmd: \"%s\"\n", cmd);
            }
            break;
        }

        default:
            break;
    }
}

// ════════════════════════════════════════════════════════════════
//  setup
// ════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP32-S3 Robot Controller ===");

    // ── TB6612 方向控制引脚 ──────────────────────────────────
    pinMode(AIN1_PIN, OUTPUT);
    pinMode(AIN2_PIN, OUTPUT);
    pinMode(BIN1_PIN, OUTPUT);
    pinMode(BIN2_PIN, OUTPUT);
    pinMode(STBY_PIN, OUTPUT);

    // 初始状态：滑行（AIN/BIN 全低，等待首条指令）
    digitalWrite(AIN1_PIN, LOW);
    digitalWrite(AIN2_PIN, LOW);
    digitalWrite(BIN1_PIN, LOW);
    digitalWrite(BIN2_PIN, LOW);

    // 使能 TB6612 驱动器
    digitalWrite(STBY_PIN, HIGH);

    // ── LEDC：左电机 PWM ────────────────────────────────────
    ledcAttachChannel(PWMA_PIN, MOTOR_PWM_FREQ, MOTOR_PWM_RES, LEDC_PWMA_CH);
    ledcWrite(PWMA_PIN, 0);

    // ── LEDC：右电机 PWM ────────────────────────────────────
    ledcAttachChannel(PWMB_PIN, MOTOR_PWM_FREQ, MOTOR_PWM_RES, LEDC_PWMB_CH);
    ledcWrite(PWMB_PIN, 0);

    // ── LEDC：激光 38 KHz PWM ───────────────────────────────
    ledcAttachChannel(LASER_PIN, LASER_FREQ, MOTOR_PWM_RES, LEDC_LASER_CH);
    ledcWrite(LASER_PIN, 0);    // 默认关闭

    // ── VS1838B 中断（FALLING：OUT 由 HIGH→LOW = 接收到激光）
    pinMode(IR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(IR_PIN), onIRFalling, FALLING);

    // ── Wi-Fi AP 事件回调（记录手机 Wi-Fi 层连入/断开）────────
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
            Serial.printf("[WiFi] Station joined  MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                          info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                          info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                          info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
        } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
            Serial.printf("[WiFi] Station left    MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                          info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
                          info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
                          info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5]);
        }
    });

    // ── Wi-Fi AP 热点 ────────────────────────────────────────
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[WiFi] AP started\n  SSID : %s\n  IP   : %s\n  Port : %d\n",
                  AP_SSID,
                  WiFi.softAPIP().toString().c_str(),
                  WS_PORT);

    // ── WebSocket 服务器 ─────────────────────────────────────
    wsServer.begin();
    wsServer.onEvent(onWebSocketEvent);
    Serial.println("[WS] Server started");

    lastCmdTime = millis();
    Serial.println("[Init] Setup complete. Waiting for connections...");
}

// ════════════════════════════════════════════════════════════════
//  loop
// ════════════════════════════════════════════════════════════════
void loop() {
    // 处理 WebSocket 网络事件（必须频繁调用）
    wsServer.loop();

    // ── 安全超时：无指令则制动停车（短路制动，防竞赛失控）────
    if (millis() - lastCmdTime > MOTOR_TIMEOUT_MS) {
        Serial.println("[Motor] Timeout → auto BRAKE");
        brakeMotors();
        lastCmdTime = millis();     // 重置计时，避免每帧重复调用
    }

    // ── 推送 IR 计数（中断标志已置位则广播）────────────────
    if (irNewPulse) {
        noInterrupts();
        irNewPulse = false;
        interrupts();
        broadcastIRCount();
    }
}
