/*
 * UNO_firmware.ino
 *
 * 控制板  : Digital PWM UNO（Arduino UNO / ATmega328P @ 16 MHz）
 * 通信对端: 2M16S 主控板（通过 SoftwareSerial 发来手柄状态帧）
 * 激光    : 18×45 980nm 30mW 100KHz 5V TTL 调制模组（接 Pin 9）
 * 接收    : VS1838B 红外接收管（接 Pin 2）
 *
 * ── 功能 ─────────────────────────────────────────────────────────────────
 *   1. 通过 SoftwareSerial（Pin 4 RX / 115200 bps）接收 2M16S 手柄状态帧
 *   2. 解析帧中 L2 按键位 → 控制激光发射器（38 kHz Timer1 PWM，Pin 9）
 *   3. 通过 INT0（Pin 2）下降沿中断统计 VS1838B 激光命中次数
 *   4. 命中计数发生变化时，通过 SoftwareSerial（Pin 5 TX）向 2M16S 回传命中帧
 *
 * ── GPIO 分配 ─────────────────────────────────────────────────────────────
 *   Pin 2  ← VS1838B OUT  (INT0，Active LOW，下降沿 = 命中)
 *   Pin 4  ← 2M16S TX     (SoftwareSerial RX，115200 bps)
 *   Pin 5  → 2M16S RX     (SoftwareSerial TX，115200 bps)
 *   Pin 9  → 激光 TTL     (Timer1 OC1A，Fast PWM，38 kHz，50% 占空比)
 *   Pin 0/1               (硬件 UART 保留给 USB 调试，烧录时无需断线)
 *
 * ── Timer1 @ 38 kHz 计算 ─────────────────────────────────────────────────
 *   模式  : Fast PWM，TOP = ICR1（Mode 14）
 *   时钟  : 无分频（CS10=1）
 *   ICR1  = F_CPU / F_laser - 1 = 16,000,000 / 38,000 - 1 = 421
 *   实际频率 = 16,000,000 / (421+1) = 37,914 Hz ≈ 38 kHz ✓（误差 0.23%）
 *   OCR1A = ICR1 / 2 = 210（50% 占空比，最大发射功率）
 *   激光 ON : 将 OC1A 接入 Pin 9（TCCR1A |= _BV(COM1A1)）
 *   激光 OFF: 断开 OC1A，Pin 9 强制 LOW（TCCR1A &= ~_BV(COM1A1)）
 *
 * ── 通信帧格式（与 2M16S_firmware.ino 完全对应）────────────────────────────
 *   接收帧 —— 来自 2M16S（7 字节二进制）：
 *     [0xAA] [LY] [LX] [RX] [BTN] [CHK] [0x55]
 *     LY  = 左摇杆 Y（0~255，中位 128）
 *     LX  = 左摇杆 X（0~255，中位 128）
 *     RX  = 右摇杆 X（0~255，中位 128）
 *     BTN = bit0:L1  bit1:L2  bit2:R1  bit3:R2
 *     CHK = LY ^ LX ^ RX ^ BTN（异或校验）
 *     末字节 0x55 为帧尾，用于双重同步
 *
 *   发送帧 —— 回传 2M16S（6 字节二进制，命中计数变化时触发）：
 *     [0xBB] [IR3] [IR2] [IR1] [IR0] [CHK]
 *     IR3~IR0 = uint32 命中计数（大端字节序）
 *     CHK = IR3 ^ IR2 ^ IR1 ^ IR0
 *
 * ── 依赖库 ─────────────────────────────────────────────────────────────────
 *   SoftwareSerial（Arduino 内置，无需额外安装）
 *
 * ── 烧录注意 ───────────────────────────────────────────────────────────────
 *   本固件使用 SoftwareSerial（Pin 4 / Pin 5），硬件 UART（Pin 0 / Pin 1）
 *   保留给 USB 调试。烧录时无需断开任何外部导线，这是相比 2M16S 的优势。
 *   确认端口选择 "Arduino UNO"，开发板 ATmega328P，即可直接上传。
 */

#include <SoftwareSerial.h>
#include <avr/interrupt.h>

// ════════════════════════════════════════════════════════════════
//  引脚定义
// ════════════════════════════════════════════════════════════════

#define IR_PIN      2    // VS1838B OUT → INT0（Active LOW，下降沿 = 命中）
#define SS_RX_PIN   4    // SoftwareSerial RX ← 2M16S TX
#define SS_TX_PIN   5    // SoftwareSerial TX → 2M16S RX
#define LASER_PIN   9    // 激光 TTL → Timer1 OC1A（38 kHz PWM）

// ════════════════════════════════════════════════════════════════
//  Timer1 常量（38 kHz Fast PWM，无分频）
//  F_laser = 16,000,000 / (ICR1 + 1)  →  ICR1=421 → 37,914 Hz
// ════════════════════════════════════════════════════════════════

#define LASER_ICR1   421U   // TOP 值
#define LASER_OCR1A  210U   // 50% 占空比（≈ ICR1 / 2）

// ════════════════════════════════════════════════════════════════
//  通信帧常量（与 2M16S_firmware.ino 对应）
// ════════════════════════════════════════════════════════════════

#define RX_FRAME_START  0xAA   // 来自 2M16S 帧头
#define RX_FRAME_END    0x55   // 来自 2M16S 帧尾
#define RX_FRAME_LEN    7      // 完整帧长度

#define TX_FRAME_START  0xBB   // 发往 2M16S 帧头
#define TX_FRAME_LEN    6

// BTN 字节位掩码
#define BTN_L1   (1U << 0)
#define BTN_L2   (1U << 1)
#define BTN_R1   (1U << 2)
#define BTN_R2   (1U << 3)

// ════════════════════════════════════════════════════════════════
//  通信超时：若超过此时间未收到帧，关闭激光（安全保护）
// ════════════════════════════════════════════════════════════════

#define FRAME_TIMEOUT_MS  500U   // 500 ms 无帧 → 关激光

// ════════════════════════════════════════════════════════════════
//  全局变量
// ════════════════════════════════════════════════════════════════

static SoftwareSerial ssPort(SS_RX_PIN, SS_TX_PIN);

// VS1838B 命中计数（ISR 中更新，主循环中读取）
static volatile uint32_t irCount    = 0;
static volatile bool     irNewHit   = false;   // 有新命中标志

// 上一次发送给 2M16S 的计数（用于检测变化）
static uint32_t lastSentCount = 0;

// 接收帧缓冲
static uint8_t  rxBuf[RX_FRAME_LEN];
static uint8_t  rxBufIdx = 0;

// 激光当前状态
static bool laserOn = false;

// 最后一次收到有效帧的时间（用于超时保护）
static uint32_t lastFrameMs = 0;

// ════════════════════════════════════════════════════════════════
//  VS1838B 命中中断（ISR）
//
//  VS1838B OUT 为 Active LOW：
//    - 检测到 38 kHz 载波时 OUT 由 HIGH → LOW（下降沿）
//    - 下降沿即表示激光命中一次
//  注意：中断期间 SoftwareSerial 如正在接收字节则会暂时屏蔽中断
//        (~87 µs/字节)，偶发漏计在激光命中计数场景中可接受
// ════════════════════════════════════════════════════════════════

ISR(INT0_vect) {
    irCount++;
    irNewHit = true;
}

// ════════════════════════════════════════════════════════════════
//  激光控制
//
//  激光 ON  : 将 Timer1 OC1A 连接到 Pin 9，输出 38 kHz 载波
//  激光 OFF : 断开 OC1A，Pin 9 强制 LOW（无信号，VS1838B 不响应）
// ════════════════════════════════════════════════════════════════

static void laserEnable(bool on) {
    if (on == laserOn) return;   // 状态未变，无需操作
    laserOn = on;

    if (on) {
        // 连接 OC1A 到 Pin 9：COM1A1=1（非反转模式）
        TCCR1A |= _BV(COM1A1);
    } else {
        // 断开 OC1A：COM1A1=0，然后强制 Pin 9 LOW
        TCCR1A &= ~_BV(COM1A1);
        digitalWrite(LASER_PIN, LOW);
    }
}

// ════════════════════════════════════════════════════════════════
//  向 2M16S 发送命中计数帧（6 字节）
//  格式：[0xBB][IR3][IR2][IR1][IR0][CHK]  大端字节序 uint32
// ════════════════════════════════════════════════════════════════

static void sendIRFrame(uint32_t count) {
    uint8_t ir3 = (uint8_t)(count >> 24);
    uint8_t ir2 = (uint8_t)(count >> 16);
    uint8_t ir1 = (uint8_t)(count >>  8);
    uint8_t ir0 = (uint8_t)(count      );
    uint8_t chk = ir3 ^ ir2 ^ ir1 ^ ir0;

    ssPort.write(TX_FRAME_START);
    ssPort.write(ir3);
    ssPort.write(ir2);
    ssPort.write(ir1);
    ssPort.write(ir0);
    ssPort.write(chk);
}

// ════════════════════════════════════════════════════════════════
//  处理一帧完整的 2M16S 来帧（已通过校验）
// ════════════════════════════════════════════════════════════════

static void processFrame(const uint8_t *frame) {
    // frame[0]=0xAA  frame[1]=LY  frame[2]=LX  frame[3]=RX
    // frame[4]=BTN   frame[5]=CHK  frame[6]=0x55
    uint8_t btn = frame[4];
    bool    l2  = (btn & BTN_L2) != 0;

    laserEnable(l2);
    lastFrameMs = millis();
}

// ════════════════════════════════════════════════════════════════
//  非阻塞接收帧（逐字节状态机）
//
//  状态机说明：
//    rxBufIdx == 0 : 等待帧头 0xAA
//    rxBufIdx 1~5  : 累积有效载荷字节
//    rxBufIdx == 6 : 等待帧尾 0x55，同时做 CHK 校验
//    任何错误 → 重置 rxBufIdx，重新搜索帧头
// ════════════════════════════════════════════════════════════════

static void receiveFrames() {
    while (ssPort.available() > 0) {
        uint8_t b = (uint8_t)ssPort.read();

        if (rxBufIdx == 0) {
            // 搜索帧头
            if (b != RX_FRAME_START) continue;
            rxBuf[0] = b;
            rxBufIdx = 1;
            continue;
        }

        rxBuf[rxBufIdx++] = b;

        if (rxBufIdx < RX_FRAME_LEN) continue;   // 帧未收完，继续等待

        // 收到完整 7 字节：验证帧尾 + CHK
        rxBufIdx = 0;   // 立即复位，准备下一帧

        if (rxBuf[6] != RX_FRAME_END) {
            // 帧尾不符：丢弃，可能帧同步偏移
            continue;
        }

        uint8_t chk = rxBuf[1] ^ rxBuf[2] ^ rxBuf[3] ^ rxBuf[4];
        if (chk != rxBuf[5]) {
            // 校验失败：静默丢弃
            continue;
        }

        processFrame(rxBuf);
    }
}

// ════════════════════════════════════════════════════════════════
//  Timer1 初始化：Fast PWM Mode 14（TOP = ICR1），无分频，38 kHz
//  Timer 持续运行，通过 COM1A1 连接/断开 OC1A（Pin 9）
// ════════════════════════════════════════════════════════════════

static void initTimer1() {
    // 先关闭 OC1A 输出
    TCCR1A = _BV(WGM11);                             // Mode 14: WGM11=1, WGM10=0
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);   // Mode 14: WGM13=1, WGM12=1; CS10=1 无分频
    ICR1   = LASER_ICR1;
    OCR1A  = LASER_OCR1A;
    // OC1A 未连接（COM1A1=0），Pin 9 保持 LOW
    digitalWrite(LASER_PIN, LOW);
    pinMode(LASER_PIN, OUTPUT);
}

// ════════════════════════════════════════════════════════════════
//  INT0 初始化：Pin 2 下降沿触发（VS1838B Active LOW 命中检测）
// ════════════════════════════════════════════════════════════════

static void initINT0() {
    pinMode(IR_PIN, INPUT);            // 无上拉（VS1838B OUT 自带上拉至 VCC）
    EICRA  = _BV(ISC01);               // ISC01=1, ISC00=0 → 下降沿触发
    EIMSK |= _BV(INT0);               // 使能 INT0
}

// ════════════════════════════════════════════════════════════════
//  setup
// ════════════════════════════════════════════════════════════════

void setup() {
    // ── 硬件 UART：USB 调试（可在串口监视器查看命中次数）──────────
    Serial.begin(115200);
    Serial.println(F("[UNO] Laser controller ready."));

    // ── SoftwareSerial：与 2M16S 通信 ────────────────────────────
    ssPort.begin(115200);
    ssPort.listen();   // 激活接收监听（同一时刻只有一个 SoftwareSerial 可监听）

    // ── Timer1：38 kHz 激光载波 ──────────────────────────────────
    initTimer1();

    // ── INT0：VS1838B 命中检测 ────────────────────────────────────
    initINT0();

    // ── 全局中断使能 ──────────────────────────────────────────────
    sei();

    lastFrameMs = millis();
}

// ════════════════════════════════════════════════════════════════
//  loop（非阻塞，无 delay）
// ════════════════════════════════════════════════════════════════

void loop() {
    // ── 1. 非阻塞接收 2M16S 手柄帧 ──────────────────────────────
    receiveFrames();

    // ── 2. 通信超时保护：长时间无帧则关激光 ─────────────────────
    if ((millis() - lastFrameMs) >= FRAME_TIMEOUT_MS) {
        laserEnable(false);
    }

    // ── 3. 检测命中计数变化，回传给 2M16S ────────────────────────
    //    读取 volatile 变量时关中断，避免 32-bit 读取撕裂
    uint32_t count;
    bool     newHit;
    cli();
    count  = irCount;
    newHit = irNewHit;
    if (newHit) irNewHit = false;
    sei();

    if (newHit && count != lastSentCount) {
        lastSentCount = count;
        sendIRFrame(count);

        // USB 串口调试输出
        Serial.print(F("[UNO] IR hit count: "));
        Serial.println(count);
    }
}
