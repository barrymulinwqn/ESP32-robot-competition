/**
 * @file    esp32_robot_controller.ino
 * @brief   YFRobot 2015 手柄解码器 + TB6612 D153C 双电机控制
 * @board   ESP32-S3 N8R2（Arduino-ESP32 v3.x）
 *
 * ─── 硬件接线（参考 ../esp32_decoder.md）────────────────────────
 *
 *  YFRobot 2015 解码器 → ESP32-S3：
 *    VCC → D153C 3.3V 输出（⚠️ 必须 3.3V，勿接 5V）
 *    GND → 公共 GND
 *    CS  → GPIO 15
 *    CLK → GPIO 16
 *    CMD → GPIO 17
 *    DAT → GPIO 18（DAT ↔ VCC 之间接 10kΩ 上拉电阻）
 *
 *  ESP32-S3 → TB6612 D153C：
 *    GPIO  4 → PWMA   GPIO  5 → AIN1   GPIO  6 → AIN2
 *    GPIO  7 → PWMB   GPIO  8 → BIN1   GPIO  9 → BIN2
 *    GPIO 10 → STBY
 *
 *  TB6612 D153C → 电机：
 *    AO1/AO2 → 左电机（33GB-520-18.7F DC 12V）
 *    BO1/BO2 → 右电机（33GB-520-18.7F DC 12V）
 *
 *  D153C 供电：
 *    Vin+  ← 18650 电池组正极（~7.4V，经 ON/OFF 开关）
 *    -     ← 电池负极 / 公共 GND
 *    5V 输出 → ESP32-S3 5V 引脚（D153C 为 ESP32 供电）
 *
 * ─── 控制逻辑────────────────────────────────────────────────────
 *
 *  所有控制通过【左摇杆】实现（LX / LY，范围 0~255，中心 = 128）
 *
 *    LY < 96            → 前进方向
 *    LY > 160           → 后退方向
 *    LX < 96            → 左方向
 *    LX > 160           → 右方向
 *    LX 和 LY 均在死区   → 滑行停止
 *
 *  组合映射（见 esp32_decoder.md 第十节）：
 *    前 + 中   → 前进       后 + 中   → 后退
 *    左 + 中   → 原地左转   右 + 中   → 原地右转
 *    前 + 左   → 左前弧线   前 + 右   → 右前弧线
 *    后 + 左   → 左后弧线   后 + 右   → 右后弧线
 *
 * ─── 依赖────────────────────────────────────────────────────────
 *  无外部库，PS2 协议完全自实现（软件 SPI，无需 PS2X_lib）
 * ────────────────────────────────────────────────────────────────
 */

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

// ============================================================
//  LEDC PWM 配置（Arduino-ESP32 v3.x API）
//  v3.x 直接以引脚号绑定通道，无需手动分配 channel 编号
// ============================================================
static constexpr uint32_t LEDC_FREQ = 20000;  // 20 kHz，超声波频率，消除电机啸叫
static constexpr uint8_t  LEDC_RES  = 8;      // 8-bit（占空比 0~255）

// ============================================================
//  摇杆死区阈值（PS2 坐标 0~255，中心 = 128）
// ============================================================
static constexpr uint8_t JOY_FWD_THR  = 96;   // LY < 96  → 前进方向
static constexpr uint8_t JOY_BWD_THR  = 160;  // LY > 160 → 后退方向
static constexpr uint8_t JOY_LEFT_THR = 96;   // LX < 96  → 左方向
static constexpr uint8_t JOY_RIGHT_THR = 160; // LX > 160 → 右方向

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

// ============================================================
//  全局状态
// ============================================================
static uint8_t ps2_buf[PS2_FRAME_LEN];  // PS2 收发缓冲区
static bool    ps2_connected = false;   // 解码器连接状态

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
    ledcWrite(PIN_PWMA, pwm);  // v3.x：以引脚号写入占空比
}

static inline void motorB(uint8_t bin1, uint8_t bin2, uint8_t pwm) {
    digitalWrite(PIN_BIN1, bin1);
    digitalWrite(PIN_BIN2, bin2);
    ledcWrite(PIN_PWMB, pwm);  // v3.x：以引脚号写入占空比
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

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {}  // 等待串口，最多 2s
    Serial.println("\n========================================");
    Serial.println(" ESP32-S3 Robot Controller  v1.0");
    Serial.println(" YFRobot 2015 + TB6612 D153C");
    Serial.println("========================================");

    // ── TB6612 D153C 初始化 ────────────────────────────────
    pinMode(PIN_AIN1, OUTPUT);
    pinMode(PIN_AIN2, OUTPUT);
    pinMode(PIN_BIN1, OUTPUT);
    pinMode(PIN_BIN2, OUTPUT);
    pinMode(PIN_STBY, OUTPUT);

    // 先制动停止，再使能驱动器（防止上电瞬间电机抖动）
    brakeStop();
    digitalWrite(PIN_STBY, HIGH);

    // 配置 LEDC PWM（Arduino-ESP32 v3.x：ledcAttach 一步绑定引脚+频率+分辨率）
    ledcAttach(PIN_PWMA, LEDC_FREQ, LEDC_RES);
    ledcAttach(PIN_PWMB, LEDC_FREQ, LEDC_RES);
    Serial.printf("[TB6612] STBY=HIGH, PWM %.0fkHz 8-bit  OK\n", LEDC_FREQ / 1000.0f);

    // ── YFRobot 2015 PS2 解码器初始化 ──────────────────────
    pinMode(PIN_PS2_CS,  OUTPUT);
    pinMode(PIN_PS2_CLK, OUTPUT);
    pinMode(PIN_PS2_CMD, OUTPUT);
    pinMode(PIN_PS2_DAT, INPUT_PULLUP);  // 上拉（板外 10kΩ 上拉为主，内部上拉做兜底）

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
    } else {
        Serial.println("  TIMEOUT");
        Serial.println("[PS2]   ⚠ Check: VCC=3.3V, DAT pull-up, wiring");
        Serial.println("[PS2]   Will keep retrying in main loop...");
    }

    Serial.println("----------------------------------------");
    Serial.println("[Ready] Left joystick → motor control");
    Serial.println("  LY<96  = forward   LY>160 = backward");
    Serial.println("  LX<96  = left      LX>160 = right");
    Serial.println("  deadzone [96,160] = coast stop");
    Serial.println("========================================\n");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    bool valid = ps2_poll();

    // ── 连接状态变化通知 ──────────────────────────────────
    if (!valid) {
        if (ps2_connected) {
            Serial.println("[PS2] ⚠ Connection lost — motors stopped");
            ps2_connected = false;
        }
        brakeStop();  // 失联时制动停止，保证安全
        delay(200);
        return;
    }
    if (!ps2_connected) {
        Serial.println("[PS2] ✓ Reconnected");
        ps2_connected = true;
    }

    // ── 读取左摇杆 ────────────────────────────────────────
    const uint8_t lx = ps2_buf[PS2_IDX_LX];  // 0=最左  128=中心  255=最右
    const uint8_t ly = ps2_buf[PS2_IDX_LY];  // 0=最前  128=中心  255=最后

    const bool fwd     = (ly < JOY_FWD_THR);           // LY < 96
    const bool bwd     = (ly > JOY_BWD_THR);            // LY > 160
    const bool lft     = (lx < JOY_LEFT_THR);           // LX < 96
    const bool rgt     = (lx > JOY_RIGHT_THR);          // LX > 160
    const bool ly_mid  = (!fwd && !bwd);
    const bool lx_mid  = (!lft && !rgt);

    // ── 调试输出（每 25 帧打印一次，约 500ms 间隔）────────
    static uint8_t dbg_cnt = 0;
    if (++dbg_cnt >= 25) {
        dbg_cnt = 0;
        Serial.printf("[Joy] LX=%3u  LY=%3u\n", lx, ly);
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
