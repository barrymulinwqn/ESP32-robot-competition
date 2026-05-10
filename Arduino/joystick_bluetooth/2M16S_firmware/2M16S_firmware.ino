/*
 * 2M16S_firmware.ino
 *
 * 主控板  : 2 Motor && 16 Servo Drive Board（2M16S）
 *           MCU: ATmega328P @ 16 MHz（Arduino UNO 兼容，出厂已烧录 optiboot）
 * 手柄解码: YFRobot 2015 游戏手柄解码器（PS2 四线接口）
 * 电机    : 33GB-520-18.7F DC 12V × 2（板载 H 桥直驱）
 * 通信对端: Digital PWM UNO（激光发射 + VS1838B 命中检测）
 *
 * ── 功能 ─────────────────────────────────────────────────────────────────
 *   1. 通过 PS2X_lib 读取蓝牙游戏手柄（经 YFRobot 2015 解码器转为 PS2 协议）
 *   2. 对摇杆数据做死区过滤 + 差速计算，驱动左（M1）/ 右（M2）电机
 *   3. L1 按下 → 制动急停（SHORT BRAKE）；L1 释放 → 恢复摇杆控制
 *   4. 通过硬件 UART TX（115200 bps）向 Digital PWM UNO 发送手柄状态帧
 *      UNO 据此控制激光开关（L2 键）并上报命中次数
 *   5. 可选：接收 UNO 回传的激光命中计数（uint32，小端字节序）
 *
 * ── 通信帧格式（二进制，无需字符串解析，抗干扰强）────────────────────────
 *   发往 UNO（7 字节）：
 *     [0xAA][LY][LX][RX][BTN][CHK][0x55]
 *     LY  = 左摇杆 Y（0~255，中位 128）
 *     LX  = 左摇杆 X（0~255，中位 128）
 *     RX  = 右摇杆 X（0~255，中位 128）
 *     BTN = bit0:L1  bit1:L2  bit2:R1  bit3:R2
 *     CHK = LY ^ LX ^ RX ^ BTN（异或校验）
 *
 *   来自 UNO（6 字节，可选）：
 *     [0xBB][IR3][IR2][IR1][IR0][CHK]
 *     IR3~IR0 = uint32 命中计数（大端字节序）
 *     CHK = IR3 ^ IR2 ^ IR1 ^ IR0
 *
 * ── PS2 引脚（2M16S 板载硬连接，不可更改）────────────────────────────────
 *   Pin 10 (CLK) → YFRobot CLK
 *   Pin 11 (CS)  → YFRobot CS
 *   Pin 12 (CMD) → YFRobot CMD
 *   Pin 13 (DAT) ← YFRobot DAT   ※ 与板载 LED 共用，通信时 LED 会轻微闪烁，属正常现象
 *
 * ── 电机 H 桥引脚（典型 2M16S 版本，烧录前请核实原理图或丝印）────────────
 *   Pin 4  → M1 DIR A  |  Pin 5 → M1 DIR B  |  Pin 6 (PWM) → M1 使能
 *   Pin 7  → M2 DIR A  |  Pin 8 → M2 DIR B  |  Pin 9 (PWM) → M2 使能
 *
 * ── UART（2M16S 硬件串口）────────────────────────────────────────────────
 *   Pin 1 (TX) → UNO Pin 4（SoftwareSerial RX，115200 bps）
 *   Pin 0 (RX) ← UNO Pin 5（SoftwareSerial TX，可选，回传命中数）
 *   ⚠️  烧录固件时必须断开 Pin 0 / Pin 1 的外部连线，否则上传失败
 *
 * ── 依赖库（Arduino IDE 库管理器安装）──────────────────────────────────────
 *   "PS2X_lib" by Bill Porter（若搜索不到请手动安装 .zip）
 *   Wire（内置，用于 PCA9685，本固件暂不使用）
 */

#include <PS2X_lib.h>

// ════════════════════════════════════════════════════════════════
//  引脚定义
//  ⚠️ 电机引脚为主流 2M16S 版本的典型映射，不同厂商可能不同。
//     烧录前务必对照板子原理图或丝印标注验证。
// ════════════════════════════════════════════════════════════════

// PS2 接口（硬连接，不可修改）
#define PS2_CLK_PIN   10
#define PS2_CS_PIN    11
#define PS2_CMD_PIN   12
#define PS2_DAT_PIN   13   // ※ 与板载 LED 共用引脚

// 左电机（Motor 1 / M1）
#define M1A_PIN    4   // 方向控制端 A
#define M1B_PIN    5   // 方向控制端 B
#define M1_PWM_PIN 6   // 速度 PWM（Timer0 OC0A，不影响 millis/delay）

// 右电机（Motor 2 / M2）
#define M2A_PIN    7   // 方向控制端 A
#define M2B_PIN    8   // 方向控制端 B
#define M2_PWM_PIN 9   // 速度 PWM（Timer1 OC1A）

// ════════════════════════════════════════════════════════════════
//  通信参数
// ════════════════════════════════════════════════════════════════

#define UART_BAUD       115200U  // 与 UNO SoftwareSerial 波特率一致

// 发往 UNO 的帧头/帧尾
#define TX_FRAME_START  0xAA
#define TX_FRAME_END    0x55

// 来自 UNO 的帧头
#define RX_FRAME_START  0xBB
#define RX_FRAME_LEN    6        // [0xBB][IR3][IR2][IR1][IR0][CHK]

// ════════════════════════════════════════════════════════════════
//  控制参数
// ════════════════════════════════════════════════════════════════

#define DEADZONE          15     // 摇杆死区（归一化到 ±100 后）
#define LOOP_PERIOD_MS    20U    // 主循环周期 20 ms（50 Hz）
#define PS2_RETRY_DELAY  500U    // PS2 初始化失败后的重试间隔（ms）

// ════════════════════════════════════════════════════════════════
//  全局变量
// ════════════════════════════════════════════════════════════════

static PS2X    ps2x;
static bool    ps2Ready   = false;
static uint32_t irCount   = 0;      // 来自 UNO 的命中计数（可选）

// IR 接收帧缓冲
static uint8_t  rxBuf[RX_FRAME_LEN];
static uint8_t  rxBufIdx = 0;

// ════════════════════════════════════════════════════════════════
//  电机控制函数
//  pwm 范围：-255（满速反转）~ 0（滑行停止）~ +255（满速正转）
// ════════════════════════════════════════════════════════════════

static void setMotor1(int pwm) {
    pwm = constrain(pwm, -255, 255);
    if (pwm > 0) {
        digitalWrite(M1A_PIN, HIGH);
        digitalWrite(M1B_PIN, LOW);
        analogWrite(M1_PWM_PIN, (uint8_t)pwm);
    } else if (pwm < 0) {
        digitalWrite(M1A_PIN, LOW);
        digitalWrite(M1B_PIN, HIGH);
        analogWrite(M1_PWM_PIN, (uint8_t)(-pwm));
    } else {
        // 滑行停止（COAST）：DIR A/B 均置 LOW，PWM = 0（惰行）
        digitalWrite(M1A_PIN, LOW);
        digitalWrite(M1B_PIN, LOW);
        analogWrite(M1_PWM_PIN, 0);
    }
}

static void setMotor2(int pwm) {
    pwm = constrain(pwm, -255, 255);
    if (pwm > 0) {
        digitalWrite(M2A_PIN, HIGH);
        digitalWrite(M2B_PIN, LOW);
        analogWrite(M2_PWM_PIN, (uint8_t)pwm);
    } else if (pwm < 0) {
        digitalWrite(M2A_PIN, LOW);
        digitalWrite(M2B_PIN, HIGH);
        analogWrite(M2_PWM_PIN, (uint8_t)(-pwm));
    } else {
        digitalWrite(M2A_PIN, LOW);
        digitalWrite(M2B_PIN, LOW);
        analogWrite(M2_PWM_PIN, 0);
    }
}

// 制动停止（SHORT BRAKE）：DIR A/B 同时 HIGH，PWM = 0
// 电机两端短路，产生反向制动力矩，停车迅速——竞赛急停首选
static void brakeMotors() {
    digitalWrite(M1A_PIN, HIGH);
    digitalWrite(M1B_PIN, HIGH);
    analogWrite(M1_PWM_PIN, 0);
    digitalWrite(M2A_PIN, HIGH);
    digitalWrite(M2B_PIN, HIGH);
    analogWrite(M2_PWM_PIN, 0);
}

// 滑行停止（COAST）：DIR A/B 均 LOW，PWM = 0（惰行自然减速）
static void coastMotors() {
    setMotor1(0);
    setMotor2(0);
}

// ════════════════════════════════════════════════════════════════
//  差速驱动计算
//
//  输入：PS2 摇杆原始值（0~255，中位 128）
//    ry  = 左摇杆 Y（128→0 方向为前进）
//    rx  = 左摇杆 X（0→128 方向为左转）
//    rx2 = 右摇杆 X（用于原地转）
//
//  算法：
//    归一化 speed / turn → -100 ~ +100
//    左摇杆：差速弧线行驶 → left = speed - turn, right = speed + turn
//    右摇杆：原地转（优先级高于左摇杆）→ left = -turn2, right = +turn2
//    最终 PWM = constrain(值 * 255 / 100, -255, 255)
// ════════════════════════════════════════════════════════════════

static void driveByJoystick(uint8_t ry, uint8_t rx, uint8_t rx2) {
    // 归一化：-100（满速反向）~ 0 ~ +100（满速正向）
    int speed = ((128 - (int)ry)  * 100) / 128;   // 正 = 前进，负 = 后退
    int turn  = (((int)rx  - 128) * 100) / 128;   // 正 = 右，  负 = 左
    int turn2 = (((int)rx2 - 128) * 100) / 128;   // 右摇杆：正 = 右原地转

    int left, right;

    if (abs(turn2) > DEADZONE) {
        // 右摇杆主导：原地转（左右电机速度等大反向）
        left  = -turn2;
        right = +turn2;
    } else if (abs(speed) < DEADZONE && abs(turn) < DEADZONE) {
        // 双摇杆归中：滑行停止，直接返回
        coastMotors();
        return;
    } else {
        // 左摇杆：差速弧线行驶
        if (abs(turn) < DEADZONE) turn = 0;    // 左摇杆 X 死区
        if (abs(speed) < DEADZONE) speed = 0;  // 左摇杆 Y 死区
        left  = speed - turn;
        right = speed + turn;
    }

    // 映射到 PWM -255 ~ +255（保持比例限幅）
    int maxVal = max(abs(left), abs(right));
    if (maxVal > 100) {
        left  = left  * 100 / maxVal;
        right = right * 100 / maxVal;
    }
    left  = left  * 255 / 100;
    right = right * 255 / 100;

    setMotor1(constrain(left,  -255, 255));
    setMotor2(constrain(right, -255, 255));
}

// ════════════════════════════════════════════════════════════════
//  向 Digital PWM UNO 发送手柄状态帧（7 字节二进制）
//  格式：[0xAA][LY][LX][RX][BTN][CHK][0x55]
//  BTN: bit0=L1  bit1=L2  bit2=R1  bit3=R2
//  CHK: LY ^ LX ^ RX ^ BTN（异或校验，UNO 侧验证后丢弃错误帧）
// ════════════════════════════════════════════════════════════════

static void sendFrameToUNO(uint8_t ly, uint8_t lx, uint8_t rx,
                           bool l1, bool l2, bool r1, bool r2) {
    uint8_t btn = ((uint8_t)l1       )
                | ((uint8_t)l2 << 1U )
                | ((uint8_t)r1 << 2U )
                | ((uint8_t)r2 << 3U );
    uint8_t chk = ly ^ lx ^ rx ^ btn;

    Serial.write(TX_FRAME_START);
    Serial.write(ly);
    Serial.write(lx);
    Serial.write(rx);
    Serial.write(btn);
    Serial.write(chk);
    Serial.write(TX_FRAME_END);
}

// ════════════════════════════════════════════════════════════════
//  接收 UNO 回传的激光命中帧（可选，非阻塞）
//  格式：[0xBB][IR3][IR2][IR1][IR0][CHK]  大端字节序 uint32
// ════════════════════════════════════════════════════════════════

static void receiveIRFrame() {
    while (Serial.available() > 0) {
        uint8_t b = (uint8_t)Serial.read();

        if (rxBufIdx == 0) {
            // 等待帧头；收到非帧头字节直接丢弃
            if (b != RX_FRAME_START) continue;
        }

        rxBuf[rxBufIdx++] = b;

        if (rxBufIdx >= RX_FRAME_LEN) {
            rxBufIdx = 0;
            // 校验：XOR of bytes 1~4
            uint8_t chk = rxBuf[1] ^ rxBuf[2] ^ rxBuf[3] ^ rxBuf[4];
            if (chk == rxBuf[5]) {
                irCount = ((uint32_t)rxBuf[1] << 24)
                        | ((uint32_t)rxBuf[2] << 16)
                        | ((uint32_t)rxBuf[3] <<  8)
                        |  (uint32_t)rxBuf[4];
            }
            // 校验失败：静默丢弃，等待下一帧
        }
    }
}

// ════════════════════════════════════════════════════════════════
//  PS2 初始化（带重试机制）
// ════════════════════════════════════════════════════════════════

static void initPS2() {
    // config_gamepad(clk, cmd, cs, dat, pressures, rumble)
    // pressures = false（不需要按键压力值，减少通信量）
    // rumble    = false（不驱动震动马达）
    int err = ps2x.config_gamepad(
        PS2_CLK_PIN, PS2_CMD_PIN,
        PS2_CS_PIN,  PS2_DAT_PIN,
        false, false
    );
    ps2Ready = (err == 0);
}

// ════════════════════════════════════════════════════════════════
//  setup
// ════════════════════════════════════════════════════════════════

void setup() {
    // ── 电机引脚初始化（上电默认滑行停止）────────────────────────
    pinMode(M1A_PIN,    OUTPUT);
    pinMode(M1B_PIN,    OUTPUT);
    pinMode(M1_PWM_PIN, OUTPUT);
    pinMode(M2A_PIN,    OUTPUT);
    pinMode(M2B_PIN,    OUTPUT);
    pinMode(M2_PWM_PIN, OUTPUT);
    coastMotors();

    // ── 硬件 UART：与 Digital PWM UNO 通信 ──────────────────────
    // ⚠️ 烧录时必须断开 Pin 0(RX) / Pin 1(TX) 的外部导线
    Serial.begin(UART_BAUD);

    // ── PS2 初始化：等待解码器上电稳定后再配置 ──────────────────
    delay(300);
    initPS2();
}

// ════════════════════════════════════════════════════════════════
//  loop（严格 50 Hz / 20 ms 周期）
// ════════════════════════════════════════════════════════════════

void loop() {
    static uint32_t lastLoopMs  = 0;
    static uint32_t lastRetryMs = 0;

    uint32_t now = millis();

    // ── 限频：20 ms 一帧，等待不足时直接返回 ───────────────────
    if (now - lastLoopMs < LOOP_PERIOD_MS) return;
    lastLoopMs = now;

    // ── PS2 未就绪：定时重试初始化，同时保持停车 ───────────────
    if (!ps2Ready) {
        if (now - lastRetryMs >= PS2_RETRY_DELAY) {
            lastRetryMs = now;
            initPS2();
        }
        coastMotors();
        return;
    }

    // ── 读取手柄数据 ─────────────────────────────────────────────
    // read_gamepad(rumble, rumble_power) — 两参数均为 false/0
    ps2x.read_gamepad(false, 0);

    uint8_t ly  = ps2x.Analog(PSS_LY);   // 左摇杆 Y（0~255，中位 128）
    uint8_t lx  = ps2x.Analog(PSS_LX);   // 左摇杆 X
    uint8_t rxA = ps2x.Analog(PSS_RX);   // 右摇杆 X

    bool l1 = ps2x.Button(PSB_L1);       // L1 ← 制动急停
    bool l2 = ps2x.Button(PSB_L2);       // L2 ← 激光发射（由 UNO 响应）
    bool r1 = ps2x.Button(PSB_R1);       // R1（预留扩展）
    bool r2 = ps2x.Button(PSB_R2);       // R2（预留扩展）

    // ── 电机控制 ─────────────────────────────────────────────────
    if (l1) {
        brakeMotors();                    // L1 按下：制动急停（SHORT BRAKE）
    } else {
        driveByJoystick(ly, lx, rxA);    // 摇杆差速驱动
    }

    // ── 向 Digital PWM UNO 发送手柄状态帧（7 字节）─────────────
    sendFrameToUNO(ly, lx, rxA, l1, l2, r1, r2);

    // ── 接收 UNO 回传的激光命中计数（非阻塞，可选）─────────────
    receiveIRFrame();
    // irCount 现在保存最新命中次数，可按需使用（如：蜂鸣器提示）
}
