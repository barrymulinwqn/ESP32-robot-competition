/**
 * @file    esp32_robot_controller.ino
 * @brief   YFRobot 2015 手柄解码器 + TB6612 D153C 双电机控制
 *          + 激光发射（38 kHz TTL）+ VS1838B 激光接收 + WS2812B LED
 * @board   ESP32-S3 N8R2（Arduino-ESP32 v3.x）
 *
 * ─── 硬件接线（参考 ../esp32_decoder.md）────────────────────────
 *
 *  YFRobot 2015 解码器 → ESP32-S3：
 *    VCC → D153C 3.3V 输出（⚠️ 必须 3.3V，勿接 5V）
 *    GND → 公共 GND
 *    CS  → GPIO 15   CLK → GPIO 16
 *    CMD → GPIO 17   DAT → GPIO 18（DAT ↔ VCC 接 10kΩ 上拉）
 *
 *  ESP32-S3 → TB6612 D153C：
 *    GPIO  4 → PWMA   GPIO  5 → AIN1   GPIO  6 → AIN2
 *    GPIO  7 → PWMB   GPIO  8 → BIN1   GPIO  9 → BIN2
 *    GPIO 10 → STBY
 *
 *  激光发射器（18×45 980nm 30mW，100kHz TTL）：
 *    VCC → D153C 5V 输出
 *    GND → 公共 GND
 *    TTL → GPIO 11（IRremote 生成的 38 kHz NEC 载波突发）
 *
 *  VS1838B 激光接收器：
 *    VCC → D153C 3.3V 输出
 *    GND → 公共 GND
 *    OUT → GPIO 12（Active LOW，INPUT_PULLUP）
 *
 *  WS2812B LED 灯条：
 *    5V  → D153C 5V 输出
 *    GND → 公共 GND
 *    DIN → GPIO 13
 *
 *  D153C 供电：
 *    Vin+  ← 18650 电池组正极（~7.4V，经 ON/OFF 开关）
 *    -     ← 电池负极 / 公共 GND
 *    5V 输出 → ESP32-S3 5V 引脚（D153C 为 ESP32 供电）
 *
 * ─── 控制逻辑────────────────────────────────────────────────────
 *
 *  左摇杆（LX/LY，0~255，中心=128，死区 96~160）：运动控制
 *    LY<96  前进  LY>160  后退  LX<96  左  LX>160  右（组合见下）
 *
 *  L2 短按（按下 < 500ms 后松开）：发送一帧 NEC 激光数据
 *  L2 长按（持续 ≥ 500ms）：每 300ms 自动循环发送一帧 NEC
 *  R2 按下：WS2812B 全部恢复绿色（重置命中计数）
 *
 * ─── VS1838B 命中逻辑──────────────────────────────────────────
 *  接收到 38 kHz 激光 → OUT 变低 → 下一个 LED 变红（从左到右）
 *  上电初始：全部绿色；R2 按下：全部恢复绿色
 *
 * ─── 依赖────────────────────────────────────────────────────────
 *  FastLED >= 3.6.0（工具 → 管理库 → 搜索 FastLED 安装）
 *  IRremote >= 4.6.0（工具 → 管理库 → 搜索 IRremote 安装）
 *  PS2 协议完全自实现（软件 SPI，无需 PS2X_lib）
 * ────────────────────────────────────────────────────────────────
 */

#define DECODE_NEC
#define EXCLUDE_EXOTIC_PROTOCOLS
#define EXCLUDE_UNIVERSAL_PROTOCOLS
#define NO_LED_FEEDBACK_CODE

#include <FastLED.h>
#include <IRremote.hpp>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <string.h>

// ============================================================
//  WiFi 配置（修改为实际 SSID / 密码）
// ============================================================
static constexpr char WIFI_SSID[]    = "your_ssid";      // ← 修改
static constexpr char WIFI_PASS[]    = "your_password";  // ← 修改
static constexpr char HIT_API_URL[]  = "https://mock.test.com/hit";
static constexpr char tankid[]       = "tank_1";
static constexpr uint8_t tankCode    = 0x01;
static const char* const teamtankids[] = {"tank_1", "tank_2", "tank_3"};

struct TankIdentity {
    uint8_t     tankCode;
    const char* tankId;
};

static const TankIdentity tank_registry[] = {
    {0x01, "tank_1"},
    {0x02, "tank_2"},
    {0x03, "tank_3"},
    {0x04, "tank_4"},
};

// ============================================================
//  引脚定义
// ============================================================

// TB6612 D153C
static constexpr uint8_t PIN_PWMA = 4;   // 左电机（A）速度 PWM
static constexpr uint8_t PIN_AIN1 = 5;   // 左电机（A）方向控制 1
static constexpr uint8_t PIN_AIN2 = 6;   // 左电机（A）方向控制 2
static constexpr uint8_t PIN_PWMB = 7;   // 右电机（B）速度 PWM
static constexpr uint8_t PIN_BIN1 = 8;   // 右电机（B）方向控制 1
static constexpr uint8_t PIN_BIN2 = 9;   // 右电机（B）方向控制 2
static constexpr uint8_t PIN_STBY = 10;  // 驱动器使能（HIGH=工作，LOW=休眠）

// YFRobot 2015 PS2 解码器
static constexpr uint8_t PIN_PS2_CS  = 15;  // 片选（Active LOW）
static constexpr uint8_t PIN_PS2_CLK = 16;  // 时钟（空闲高）
static constexpr uint8_t PIN_PS2_CMD = 17;  // 命令输出（MOSI，空闲高）
static constexpr uint8_t PIN_PS2_DAT = 18;  // 数据输入（MISO，需 10kΩ 上拉至 VCC）

// 激光发射器（TTL 调制，38 kHz，接 D153C 5V）
static constexpr uint8_t PIN_LASER_TX = 11;
// VS1838B 激光接收器（OUT，Active LOW，接 D153C 3.3V）
static constexpr uint8_t PIN_LASER_RX = 12;
// WS2812B LED 灯条数据线（接 D153C 5V 供电）
static constexpr uint8_t PIN_LED_DATA = 13;

// ============================================================
//  LEDC PWM 配置（Arduino-ESP32 v3.x API）
//  v3.x 直接以引脚号绑定通道，无需手动分配 channel 编号
// ============================================================
static constexpr uint32_t LEDC_FREQ      = 20000;  // 电机 PWM 20 kHz（超声波，消除啸叫）
static constexpr uint8_t  LEDC_RES       = 8;      // 8-bit 分辨率（0~255）
// ============================================================
//  WS2812B LED 配置
// ============================================================
static constexpr uint8_t NUM_LEDS       = 10;  // 灯条 LED 总数
static constexpr uint8_t LED_BRIGHTNESS = 60;  // 亮度（0~255，60≈23%，护眼）
// 单机调试时可设为 true，让每次 L2 发射直接模拟一次命中；比赛时必须保持 false。
static constexpr bool LOCAL_LED_TEST_ON_FIRE = false;

// ============================================================
//  摇杆死区阈值（PS2 坐标 0~255，中心 = 128）
// ============================================================
static constexpr uint8_t JOY_FWD_THR   = 96;   // LY < 96  → 前进方向
static constexpr uint8_t JOY_BWD_THR   = 160;  // LY > 160 → 后退方向
static constexpr uint8_t JOY_LEFT_THR  = 96;   // LX < 96  → 左方向
static constexpr uint8_t JOY_RIGHT_THR = 160;  // LX > 160 → 右方向

// ============================================================
//  PS2 协议时序（软件 SPI，LSB-first）
// ============================================================
static constexpr uint8_t PS2_CLK_HALF_US = 10;   // 半周期 10µs → ~50kHz 时钟
static constexpr uint8_t PS2_BYTE_GAP_US = 20;   // 字节间隔 20µs
static constexpr uint8_t PS2_FRAME_LEN   = 9;    // 每帧 9 字节

// 响应帧字节偏移
static constexpr uint8_t PS2_IDX_ID   = 1;  // 设备 ID（0x73=模拟, 0x41=数字）
static constexpr uint8_t PS2_IDX_ACK  = 2;  // 应答（正常为 0x5A）
static constexpr uint8_t PS2_IDX_BTN1 = 3;  // 按键高字节
static constexpr uint8_t PS2_IDX_BTN2 = 4;  // 按键低字节
static constexpr uint8_t PS2_IDX_RX   = 5;  // 右摇杆 X
static constexpr uint8_t PS2_IDX_RY   = 6;  // 右摇杆 Y
static constexpr uint8_t PS2_IDX_LX   = 7;  // 左摇杆 X（0=最左, 255=最右）
static constexpr uint8_t PS2_IDX_LY   = 8;  // 左摇杆 Y（0=最前, 255=最后）

static constexpr uint8_t PS2_ID_ANALOG  = 0x73;  // 模拟手柄模式（摇杆数据有效）
static constexpr uint8_t PS2_ID_DIGITAL = 0x41;  // 数字模式（摇杆数据无意义）
static constexpr uint8_t PS2_ACK_BYTE   = 0x5A;

// PS2 BTN2 按键掩码（Active LOW：0 = 按下）
// BTN2 字节：[□][×][○][△][R1][L1][R2][L2]
//             bit7 bit6 bit5 bit4 bit3 bit2 bit1 bit0
static constexpr uint8_t BTN_L2 = 0x01;  // L2：BTN2 bit0
static constexpr uint8_t BTN_R2 = 0x02;  // R2：BTN2 bit1

// ============================================================
//  HTTP 任务（FreeRTOS）
// ============================================================
static constexpr uint8_t HTTP_QUEUE_LEN = 4;   // 最多缓冲 4 次待发请求
static QueueHandle_t     http_queue      = nullptr;

/**
 * httpTask — 运行于 Core 0，阻塞等待队列信号
 * 每次收到信号即向 HIT_API_URL 发送 HTTP POST
 * 与主循环完全解耦，不阻塞电机 / 激光控制
 */
static void httpTask(void* /*arg*/) {
    uint8_t sig;
    for (;;) {
        // 阻塞等待 laserOn() 投递的信号
        if (xQueueReceive(http_queue, &sig, portMAX_DELAY) != pdTRUE) continue;

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[HTTP]  WiFi 未连接，跳过 POST");
            continue;
        }

        WiFiClientSecure client;
        // ⚠️ 仅测试环境跳过证书校验；生产环境请改用 client.setCACert()
        client.setInsecure();

        HTTPClient http;
        if (http.begin(client, HIT_API_URL)) {
            char payload[96];
            snprintf(payload, sizeof(payload),
                     "{\"event\":\"laser_fired\",\"tankid\":\"%s\"}",
                     tankid);
            http.addHeader("Content-Type", "application/json");
            int code = http.POST(reinterpret_cast<uint8_t*>(payload), strlen(payload));
            if (code > 0) {
                Serial.printf("[HTTP]  POST %s → HTTP %d\n", HIT_API_URL, code);
            } else {
                Serial.printf("[HTTP]  POST 失败: %s\n", HTTPClient::errorToString(code).c_str());
            }
            http.end();
        } else {
            Serial.println("[HTTP]  http.begin() 失败");
        }
    }
}

// ============================================================
//  全局状态
// ============================================================
static uint8_t ps2_buf[PS2_FRAME_LEN];  // PS2 收发缓冲区
static bool    ps2_connected = false;   // 解码器连接状态
static uint32_t ps2_last_valid_ms = 0;
static uint32_t ps2_last_invalid_log_ms = 0;
static uint16_t ps2_invalid_frames = 0;

// 无效帧可能只是蓝牙解码器的瞬时丢帧；连续超时才执行失联保护。
static constexpr uint32_t PS2_LOSS_TIMEOUT_MS = 250;
static constexpr uint32_t PS2_INVALID_LOG_INTERVAL_MS = 1000;

// WS2812B LED 数组（FastLED 管理）
static CRGB   leds[NUM_LEDS];
static int8_t hit_count = 0;  // 已命中（变红）的 LED 数量，0 ~ NUM_LEDS

// 激光状态机
enum LaserState : uint8_t {
    LASER_IDLE,      // 空闲
    LASER_FIRING,    // 正在发射（38 kHz 输出中）
    LASER_COOLDOWN,  // 连发冷却期（等待下次发射）
};
static LaserState laser_state      = LASER_IDLE;
static uint32_t   laser_timer_ms   = 0;     // 当前状态进入时刻
static bool       l2_prev          = false; // 上一帧 L2 是否按下
static uint32_t   l2_press_time_ms = 0;     // L2 本次按下的起始时刻
static bool       laser_auto       = false; // true = 正处于长按连发模式

static constexpr uint32_t LASER_FRAME_GUARD_MS = 200;  // NEC 帧后的最短状态保护时间（ms）
static constexpr uint32_t LASER_PERIOD_MS   = 300;  // 连发总周期（ms）
static constexpr uint32_t LASER_COOLDOWN_MS = LASER_PERIOD_MS - LASER_FRAME_GUARD_MS;  // 100 ms
static constexpr uint32_t LASER_LONG_MS     = 500;  // 长按判定阈值（ms）

// VS1838B 命中检测
static uint32_t last_hit_ms = 0;
static uint8_t  last_hit_tankcode = 0;
static bool     has_last_hit = false;

static constexpr uint32_t HIT_COOLDOWN_MS = 600;  // 同一命中最短间隔（ms）
static constexpr uint8_t  TEAM_TANK_COUNT   = sizeof(teamtankids) / sizeof(teamtankids[0]);
static constexpr uint8_t  TANK_REGISTRY_LEN = sizeof(tank_registry) / sizeof(tank_registry[0]);
static constexpr uint16_t LASER_IR_ADDRESS  = 0x42;

static char    last_rx_tankid[16] = "";
static uint8_t last_rx_tankcode   = 0;
static bool    motor_pwm_ready     = false;
static bool    r2_prev             = false;

static const char* lookupTankIdByCode(uint8_t code) {
    for (uint8_t i = 0; i < TANK_REGISTRY_LEN; ++i) {
        if (tank_registry[i].tankCode == code) {
            return tank_registry[i].tankId;
        }
    }
    return nullptr;
}

static bool isTeamTankId(const char* candidate) {
    for (uint8_t i = 0; i < TEAM_TANK_COUNT; ++i) {
        if (strcmp(candidate, teamtankids[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool isTeamTankCode(uint8_t code) {
    const char* resolved_tankid = lookupTankIdByCode(code);
    return resolved_tankid != nullptr && isTeamTankId(resolved_tankid);
}

// ============================================================
//  PS2 底层：单字节收发（CLK 空闲高，LSB-first）
//
//  时序：
//    CLK 拉低 → 写 CMD 位 → 等待半周期
//    CLK 拉高 → 读 DAT 位 → 等待半周期
// ============================================================
static uint8_t ps2_xfer_byte(uint8_t cmd_byte) {
    uint8_t dat_byte = 0;
    for (int bit = 0; bit < 8; bit++) {
        digitalWrite(PIN_PS2_CLK, LOW);
        digitalWrite(PIN_PS2_CMD, (cmd_byte >> bit) & 1 ? HIGH : LOW);
        delayMicroseconds(PS2_CLK_HALF_US);

        digitalWrite(PIN_PS2_CLK, HIGH);
        if (digitalRead(PIN_PS2_DAT)) {
            dat_byte |= (1u << bit);
        }
        delayMicroseconds(PS2_CLK_HALF_US);
    }
    return dat_byte;
}

// ============================================================
//  PS2 帧轮询：发送 9 字节命令，接收 9 字节响应
//  返回 true  = 收到有效模拟帧（ID=0x73, ACK=0x5A）
//  返回 false = 通信失败或控制器处于数字模式
// ============================================================
static bool ps2_poll() {
    // 标准 PS2 查询命令帧
    static const uint8_t CMD[PS2_FRAME_LEN] = {
        0x01, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    // 拉低 CS，激活通信
    digitalWrite(PIN_PS2_CS, LOW);
    delayMicroseconds(PS2_BYTE_GAP_US);

    for (uint8_t i = 0; i < PS2_FRAME_LEN; i++) {
        ps2_buf[i] = ps2_xfer_byte(CMD[i]);
        delayMicroseconds(PS2_BYTE_GAP_US);
    }

    // 释放 CS
    digitalWrite(PIN_PS2_CS, HIGH);
    delayMicroseconds(PS2_BYTE_GAP_US);

    // 校验：首字节固定 0xFF，ACK 字节固定 0x5A，ID 为模拟模式
    return (ps2_buf[0] == 0xFF) &&
           (ps2_buf[PS2_IDX_ACK] == PS2_ACK_BYTE) &&
           (ps2_buf[PS2_IDX_ID]  == PS2_ID_ANALOG);
}

// ============================================================
//  电机控制辅助函数
//
//  电机 A = 左电机，电机 B = 右电机
//  ain1/ain2 取值：HIGH(1) 或 LOW(0)
//  pwm 取值：0~255
// ============================================================
static inline void motorA(uint8_t ain1, uint8_t ain2, uint8_t pwm) {
    digitalWrite(PIN_AIN1, ain1);
    digitalWrite(PIN_AIN2, ain2);
    if (motor_pwm_ready) {
        ledcWrite(PIN_PWMA, pwm);  // v3.x：以引脚号写入占空比
    }
}

static inline void motorB(uint8_t bin1, uint8_t bin2, uint8_t pwm) {
    digitalWrite(PIN_BIN1, bin1);
    digitalWrite(PIN_BIN2, bin2);
    if (motor_pwm_ready) {
        ledcWrite(PIN_PWMB, pwm);  // v3.x：以引脚号写入占空比
    }
}

// ── 运动控制函数（对应 esp32_decoder.md 第十一节真值表）──────────

// 前进：A 正转，B 正转
static void driveForward() {
    motorA(HIGH, LOW, 255);
    motorB(HIGH, LOW, 255);
}

// 后退：A 反转，B 反转
static void driveBackward() {
    motorA(LOW, HIGH, 255);
    motorB(LOW, HIGH, 255);
}

// 左前弧线：A 停止（惰行），B 正转
static void driveLeftForward() {
    motorA(LOW, LOW,  0);
    motorB(HIGH, LOW, 255);
}

// 右前弧线：A 正转，B 停止（惰行）
static void driveRightForward() {
    motorA(HIGH, LOW, 255);
    motorB(LOW, LOW,  0);
}

// 左后弧线：A 停止（惰行），B 反转
static void driveLeftBackward() {
    motorA(LOW, LOW,  0);
    motorB(LOW, HIGH, 255);
}

// 右后弧线：A 反转，B 停止（惰行）
static void driveRightBackward() {
    motorA(LOW, HIGH, 255);
    motorB(LOW, LOW,  0);
}

// 原地左转：A 反转，B 正转
static void spinLeft() {
    motorA(LOW, HIGH,  200);
    motorB(HIGH, LOW,  200);
}

// 原地右转：A 正转，B 反转
static void spinRight() {
    motorA(HIGH, LOW,  200);
    motorB(LOW,  HIGH, 200);
}

// 制动停止：双路短接制动（快速停止）
static void brakeStop() {
    motorA(HIGH, HIGH, 0);
    motorB(HIGH, HIGH, 0);
}

// 滑行停止：双路惰行（自然减速）
static void coastStop() {
    motorA(LOW, LOW, 0);
    motorB(LOW, LOW, 0);
}

static void motorInitSafe() {
    pinMode(PIN_STBY, OUTPUT);
    digitalWrite(PIN_STBY, LOW);

    pinMode(PIN_AIN1, OUTPUT);
    pinMode(PIN_AIN2, OUTPUT);
    pinMode(PIN_BIN1, OUTPUT);
    pinMode(PIN_BIN2, OUTPUT);
    digitalWrite(PIN_AIN1, LOW);
    digitalWrite(PIN_AIN2, LOW);
    digitalWrite(PIN_BIN1, LOW);
    digitalWrite(PIN_BIN2, LOW);

    const bool left_pwm_ready  = ledcAttach(PIN_PWMA, LEDC_FREQ, LEDC_RES);
    const bool right_pwm_ready = ledcAttach(PIN_PWMB, LEDC_FREQ, LEDC_RES);
    motor_pwm_ready = left_pwm_ready && right_pwm_ready;

    if (motor_pwm_ready) {
        brakeStop();
    }
}

// ============================================================
//  激光控制（IRremote NEC）
// ============================================================

static void ledsAddHit();

// 开启激光：发送 NEC 数据帧（address 固定，command=tankCode），并通知 HTTP 任务上报
static inline void laserOn() {
    IrSender.sendNEC(LASER_IR_ADDRESS, tankCode, 0);
    Serial.printf("[LaserTX] NEC address=0x%02X, tankCode=0x%02X\n",
                  LASER_IR_ADDRESS, tankCode);

    if (LOCAL_LED_TEST_ON_FIRE) {
        ledsAddHit();
    }

    // 非阻塞投递：若队列已满则丢弃（不影响实时控制）
    const uint8_t sig = 1;
    if (http_queue != nullptr) {
        xQueueSend(http_queue, &sig, 0);
    }
}

// IRremote 发送函数返回时该帧已完成，这里无需额外关断
static inline void laserOff() {
}

/**
 * updateLaser() — 每帧调用，处理 L2 短按 / 长按 / 连发逻辑
 *
 * 短按（按下 < 500ms 后松开）：发送一帧 NEC
 * 长按（持续 >= 500ms）：每 300ms 循环发送一帧 NEC
 *
 * @param l2_down  当前帧 L2 是否按下（Active LOW 已转换：true = 按下）
 */
static void updateLaser(bool l2_down) {
    const uint32_t now = millis();

    // 记录 L2 下降沿时刻
    if (l2_down && !l2_prev) {
        l2_press_time_ms = now;
    }

    switch (laser_state) {

        case LASER_IDLE:
            if (l2_down) {
                // 长按判定：持续按下超过阈值则进入连发模式
                if ((now - l2_press_time_ms) >= LASER_LONG_MS) {
                    laser_auto    = true;
                    laser_state   = LASER_FIRING;
                    laser_timer_ms = now;
                    laserOn();
                }
            } else if (!l2_down && l2_prev) {
                // 上升沿：短按松开（未触发连发）
                if (!laser_auto) {
                    laser_state   = LASER_FIRING;
                    laser_timer_ms = now;
                    laserOn();
                }
                laser_auto = false;  // 起圈复位标志
            }
            break;

        case LASER_FIRING:
            if ((now - laser_timer_ms) >= LASER_FRAME_GUARD_MS) {
                laserOff();
                if (laser_auto && l2_down) {
                    // 进入冷却期等待下次循环
                    laser_state   = LASER_COOLDOWN;
                    laser_timer_ms = now;
                } else {
                    // 单次发射完成，或长按已松开
                    laser_auto  = false;
                    laser_state = LASER_IDLE;
                }
            }
            break;

        case LASER_COOLDOWN:
            if (!l2_down) {
                // 冒冷却期松开—中止连发
                laser_auto  = false;
                laser_state = LASER_IDLE;
            } else if ((now - laser_timer_ms) >= LASER_COOLDOWN_MS) {
                // 冷却完成，再次发射
                laser_state   = LASER_FIRING;
                laser_timer_ms = now;
                laserOn();
            }
            break;
    }

    l2_prev = l2_down;
}

// ============================================================
//  WS2812B LED 控制
// ============================================================

// 初始化：全部设为绿色
static void ledsInit() {
    hit_count = 0;
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
}

// 记录一次命中：下一个 LED 变红
static void ledsAddHit() {
    if (hit_count < (int8_t)NUM_LEDS) {
        leds[hit_count] = CRGB::Red;
        FastLED.show();
        hit_count++;
        Serial.printf("[LED]   Hit! %d/%d LEDs red\n", hit_count, (int)NUM_LEDS);
    }
}

// ============================================================
//  VS1838B 命中检测（每帧调用）
// ============================================================

/**
 * updateHitDetect() — 使用 IRremote 接收 NEC 数据帧
 * 业务地址固定为 LASER_IR_ADDRESS，command 作为 tankCode
 * 本地将 tankCode 映射为 tankid，并复用 teamtankids 做敌我判断
 */
static void updateHitDetect() {
    if (!IrReceiver.decode()) {
        return;
    }

    const IRData& ir_data = IrReceiver.decodedIRData;

    if (ir_data.protocol != NEC || ir_data.address != LASER_IR_ADDRESS) {
        IrReceiver.resume();
        return;
    }

    const uint32_t now_ms = millis();
    const uint8_t received_tankcode = static_cast<uint8_t>(ir_data.command & 0xFF);
    const char*   received_tankid   = lookupTankIdByCode(received_tankcode);

    last_rx_tankcode = received_tankcode;
    if (received_tankid != nullptr) {
        strncpy(last_rx_tankid, received_tankid, sizeof(last_rx_tankid) - 1);
        last_rx_tankid[sizeof(last_rx_tankid) - 1] = '\0';
    } else {
        strncpy(last_rx_tankid, "unknown", sizeof(last_rx_tankid) - 1);
        last_rx_tankid[sizeof(last_rx_tankid) - 1] = '\0';
    }

    if (received_tankid == nullptr) {
        Serial.printf("[LaserRX] Unknown tankCode=0x%02X, ignore\n",
                      received_tankcode);
        IrReceiver.resume();
        return;
    }

    if (isTeamTankCode(received_tankcode)) {
        Serial.printf("[LaserRX] Friendly/self tankCode=0x%02X tankid=%s, ignore\n",
                      received_tankcode, last_rx_tankid);
        IrReceiver.resume();
        return;
    }

    const bool different_tank = !has_last_hit || received_tankcode != last_hit_tankcode;
    const bool cooldown_done = !has_last_hit ||
        static_cast<int32_t>(now_ms - last_hit_ms) >= static_cast<int32_t>(HIT_COOLDOWN_MS);
    if (different_tank || cooldown_done) {
        last_hit_ms = now_ms;
        last_hit_tankcode = received_tankcode;
        has_last_hit = true;
        Serial.printf("[LaserRX] Enemy tankCode=0x%02X tankid=%s\n",
                      received_tankcode, last_rx_tankid);
        ledsAddHit();
    }

    IrReceiver.resume();
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {}  // 等待串口，最多 2s
    Serial.println("\n========================================");
    Serial.println(" ESP32-S3 Robot Controller  v2.0");
    Serial.println(" YFRobot 2015 + TB6612 D153C");
    Serial.println(" Laser TX/RX + WS2812B LED");
    Serial.println("========================================");

    // ── 电机安全初始化：先关闭 STBY，再进行可能阻塞的启动工作 ──
    motorInitSafe();

    // ── HTTP 队列 + 任务初始化 ──────────────────────────────
    http_queue = xQueueCreate(HTTP_QUEUE_LEN, sizeof(uint8_t));
    if (http_queue != nullptr) {
        // Core 0：主循环在 Core 1，HTTP 不与电机控制竞争
        xTaskCreatePinnedToCore(httpTask, "httpTask", 8192, nullptr, 1, nullptr, 0);
        Serial.println("[HTTP]  Task started on Core 0");
    } else {
        Serial.println("[HTTP]  Queue allocation failed; POST disabled");
    }

    // ── WiFi 连接 ──────────────────────────────────────────
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi]  Connecting to \"%s\"...", WIFI_SSID);
    {
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
            Serial.print(".");
            delay(300);
        }
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("  OK  IP=%s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("  TIMEOUT  (HTTP 上报将被跳过)");
    }

    Serial.printf("[TB6612] STBY=LOW, PWM %.0fkHz 8-bit %s\n",
                  LEDC_FREQ / 1000.0f, motor_pwm_ready ? "OK" : "FAILED");

    // ── 激光发射 / 接收初始化（IRremote NEC）──────────────
    IrSender.begin(PIN_LASER_TX);
    IrReceiver.begin(PIN_LASER_RX, DISABLE_LED_FEEDBACK);
    Serial.printf("[Laser]  TX=GPIO%d, NEC address=0x%02X, tankCode=0x%02X  OK\n",
                  PIN_LASER_TX, LASER_IR_ADDRESS, tankCode);
    Serial.printf("[LaserRX] RX=GPIO%d, IRremote NEC decode  OK\n", PIN_LASER_RX);

    // ── WS2812B LED 灯条初始化 ───────────────────
    FastLED.addLeds<WS2812B, PIN_LED_DATA, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(LED_BRIGHTNESS);
    ledsInit();
    Serial.printf("[LED]    WS2812B x%d, GPIO%d, brightness=%d  OK\n",
                  NUM_LEDS, PIN_LED_DATA, LED_BRIGHTNESS);

    // ── YFRobot 2015 PS2 解码器初始化 ──────────────
    pinMode(PIN_PS2_CS,  OUTPUT);
    pinMode(PIN_PS2_CLK, OUTPUT);
    pinMode(PIN_PS2_CMD, OUTPUT);
    pinMode(PIN_PS2_DAT, INPUT_PULLUP);  // 外部 10kΩ 上拉为主，内部上拉做兼底

    digitalWrite(PIN_PS2_CS,  HIGH);   // CS 空闲高
    digitalWrite(PIN_PS2_CLK, HIGH);   // CLK 空闲高
    digitalWrite(PIN_PS2_CMD, HIGH);   // CMD 空闲高

    Serial.print("[PS2]   Waiting for YFRobot 2015 decoder");
    delay(500);  // 等待解码器和手柄蓝牙配对完成

    // 尝试初始化，最多等待 5 秒（50 次 × 100ms）
    for (int i = 0; i < 50; i++) {
        if (ps2_poll()) {
            ps2_connected = true;
            break;
        }
        Serial.print(".");
        delay(100);
    }

    if (ps2_connected) {
        Serial.println("  OK");
        Serial.println("[PS2]   Analog mode confirmed (ID=0x73)");
        ps2_last_valid_ms = millis();
        if (motor_pwm_ready) {
            digitalWrite(PIN_STBY, HIGH);
        }
    } else {
        Serial.println("  TIMEOUT");
        Serial.println("[PS2]   ⚠ Check: VCC=3.3V, DAT pull-up, wiring");
        Serial.println("[PS2]   Will keep retrying in main loop...");
    }

    Serial.println("----------------------------------------");
    Serial.println("[Ready] Left joystick → motor control");
    Serial.println("  LY<96  = forward   LY>160 = backward");
    Serial.println("  LX<96  = left      LX>160 = right");
    Serial.println("  L2 short press = NEC laser frame");
    Serial.println("  L2 long press  = auto NEC laser (300ms cycle)");
    Serial.println("  R2 press       = reset LEDs to green");
    Serial.printf("[Laser]  tankid=%s  tankCode=0x%02X\n", tankid, tankCode);
    Serial.println("========================================\n");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    const bool valid = ps2_poll();
    const uint32_t now_ms = millis();

    // ── 连接状态变化通知 ──────────────────────────────────
    if (!valid) {
        ++ps2_invalid_frames;

        if (ps2_connected && (now_ms - ps2_last_valid_ms >= PS2_LOSS_TIMEOUT_MS)) {
            Serial.printf("[PS2] ⚠ Connection lost after %u invalid frames — motors/laser stopped\n",
                          ps2_invalid_frames);
            ps2_connected = false;
            ps2_invalid_frames = 0;
            digitalWrite(PIN_STBY, LOW);
            brakeStop();
            laserOff();
            laser_state = LASER_IDLE;
            laser_auto  = false;
            l2_prev     = false;
            r2_prev     = false;
        } else if (now_ms - ps2_last_invalid_log_ms >= PS2_INVALID_LOG_INTERVAL_MS) {
            ps2_last_invalid_log_ms = now_ms;
            Serial.printf("[PS2] Invalid frame #%u: %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                          ps2_invalid_frames,
                          ps2_buf[0], ps2_buf[1], ps2_buf[2], ps2_buf[3], ps2_buf[4],
                          ps2_buf[5], ps2_buf[6], ps2_buf[7], ps2_buf[8]);
        }

        // 短暂丢帧期间保持上一帧电机输出，避免单帧错误造成间歇性停机。
        delay(2);
        return;
    }

    ps2_last_valid_ms = now_ms;
    ps2_invalid_frames = 0;
    if (!ps2_connected) {
        Serial.println("[PS2] ✓ Reconnected");
        ps2_connected = true;
    }
    if (motor_pwm_ready) {
        digitalWrite(PIN_STBY, HIGH);
    }

    // ── 读取摇杆与按键 ────────────────────────────────────
    const uint8_t lx   = ps2_buf[PS2_IDX_LX];   // 0=最左  128=中心  255=最右
    const uint8_t ly   = ps2_buf[PS2_IDX_LY];   // 0=最前  128=中心  255=最后
    const uint8_t btn2 = ps2_buf[PS2_IDX_BTN2]; // Active LOW：0=按下

    // L2 / R2（Active LOW：bit 为 0 表示按下）
    const bool l2_down = !(btn2 & BTN_L2);
    const bool r2_down = !(btn2 & BTN_R2);

    // ── R2：边沿检测，按下时重置 LED ──────────────────────
    if (r2_down && !r2_prev) {
        ledsInit();
        Serial.println("[LED]   R2 pressed — LEDs reset to green");
    }
    r2_prev = r2_down;

    // ── 激光状态机更新 ────────────────────────────────────
    updateLaser(l2_down);

    // ── VS1838B 命中检测 ──────────────────────────────────
    updateHitDetect();

    // ── 左摇杆运动判定 ────────────────────────────────────
    const bool fwd    = (ly < JOY_FWD_THR);   // LY < 96
    const bool bwd    = (ly > JOY_BWD_THR);   // LY > 160
    const bool lft    = (lx < JOY_LEFT_THR);  // LX < 96
    const bool rgt    = (lx > JOY_RIGHT_THR); // LX > 160
    const bool ly_mid = (!fwd && !bwd);
    const bool lx_mid = (!lft && !rgt);

    // ── 调试输出（每 25 帧打印一次，约 500ms 间隔）────────
    static uint8_t dbg_cnt = 0;
    if (++dbg_cnt >= 25) {
        dbg_cnt = 0;
        Serial.printf("[Joy] LX=%3u  LY=%3u  BTN2=0x%02X  Laser=%d\n",
                      lx, ly, btn2, (int)laser_state);
    }

    // ── 动作判定（优先级从上到下）─────────────────────────
    if      (lx_mid  && ly_mid)  { coastStop();          }  // 死区 → 滑行停止
    else if (fwd     && lx_mid)  { driveForward();       }  // 纯前进
    else if (bwd     && lx_mid)  { driveBackward();      }  // 纯后退
    else if (lft     && ly_mid)  { spinLeft();           }  // 原地左转
    else if (rgt     && ly_mid)  { spinRight();          }  // 原地右转
    else if (fwd     && lft)     { driveLeftForward();   }  // 左前弧线
    else if (fwd     && rgt)     { driveRightForward();  }  // 右前弧线
    else if (bwd     && lft)     { driveLeftBackward();  }  // 左后弧线
    else if (bwd     && rgt)     { driveRightBackward(); }  // 右后弧线
    else                          { coastStop();          }  // 兜底

    delay(20);  // 50 Hz 控制频率
}
