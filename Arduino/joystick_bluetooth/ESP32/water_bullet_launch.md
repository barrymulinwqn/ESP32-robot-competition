# 水弹发射装置接线与控制说明

> 适用主控：ESP32-S3 N8R2（配合现有 `esp32_robot_controller.ino` v2.0 扩展）

---

## 目录

1. [功能概述](#一功能概述)
2. [工作原理](#二工作原理)
3. [所需硬件与费用](#三所需硬件与费用)
4. [供电与电流评估](#四供电与电流评估)
5. [电气接线](#五电气接线)
6. [ESP32 GPIO 占用](#六esp32-gpio-占用)
7. [PS2 手柄控制映射](#七ps2-手柄控制映射)
8. [代码修改说明](#八代码修改说明)
9. [注意事项](#九注意事项)

---

## 一、功能概述

采用 **飞轮式发射机构**：两个橡胶飞轮高速反向旋转，水弹从轮间通过时被摩擦力加速弹出；
一个微型舵机（SG90）驱动拨弹转盘，每次送入一颗水弹进入发射通道。

- PS2 **R1 键**：按住旋转飞轮预热
- PS2 **○（圆圈键）短按**：发射一颗水弹
- PS2 **○（圆圈键）长按**：每 500ms 自动循环发射

---

## 二、工作原理

```
        弹仓（漏斗）
            │ 水弹自重下落
            ▼
  ┌─── 拨弹转盘（SG90 驱动）───┐
  │     每转动一格送出一颗       │
  └──────────────────────────┘
            │
            ▼
  ←  飞轮A（顺时针）  水弹  飞轮B（逆时针）  →
       5V DC 130 电机          5V DC 130 电机
            │
            ▼
         ● 弹出
```

- 飞轮需约 0.5s 预热才能达到有效发射速度（约 8–12 m/s）
- 水弹直径 7–8 mm 软凝水弹（硬珠不适用）

---

## 三、所需硬件与费用

| 编号 | 硬件 | 规格/型号 | 数量 | 参考价格（RMB） |
|:----:|------|----------|:----:|:--------------:|
| 1 | 飞轮电机（含橡胶飞轮轮毂） | 130型 DC 3–6V 带 φ28mm 橡胶轮 | 2 | ¥8–15 /个 |
| 2 | 飞轮电机驱动模块 | L9110S 双路 H 桥（800mA/路） | 1 | ¥5–8 |
| 3 | 拨弹舵机 | SG90 9g 微型舵机（180°） | 1 | ¥10–15 |
| 4 | 拨弹转盘 | 3D 打印或水弹枪零件（孔径约 8mm） | 1 | ¥0–15 |
| 5 | 弹仓漏斗 | 3D 打印或改装自小型水弹枪弹仓 | 1 | ¥10–25 |
| 6 | 水弹（凝水弹） | 7–8mm 软水晶弹，泡水膨胀后使用 | 1 袋（约 1000颗） | ¥5–10 |
| 7 | 固定支架 | 铝合金 L 型件或 3D 打印安装架 | 1 套 | ¥10–20 |
| 8 | 杜邦线 / 连接线 | 公母/公公，20cm | 若干 | ¥3–5 |

**估算总费用：¥51 – ¥113 RMB**（3D 打印件自制可取低端值）

---

## 四、供电与电流评估

| 负载 | 供电 | 典型电流 | 峰值电流 |
|------|------|---------|---------|
| 飞轮电机 A | 5V（TB6612 D153C） | 300mA | 600mA（堵转） |
| 飞轮电机 B | 5V（TB6612 D153C） | 300mA | 600mA（堵转） |
| SG90 拨弹舵机 | 5V（TB6612 D153C） | 100mA | 300mA（卡顿） |
| **新增小计** | **5V** | **~700mA** | **~1.5A** |
| 原有负载（主驱动电机等） | — | ~1A | ~3A |
| **总计** | | **~1.7A** | **~4.5A** |

> ⚠ TB6612 D153C 的 5V 稳压输出额定电流通常 ≤ 2A，建议使用 **7.4V 2S 锂电池（2000mAh 以上，20C 放电）**，或单独为飞轮电机加一路 UBEC（5V/3A）。

---

## 五、电气接线

### 5.1 L9110S 飞轮驱动模块接线

> 两个飞轮方向相反：电机 A 顺时针、电机 B 逆时针，水弹从正上方落入两轮间被弹出。

| L9110S 引脚 | 连接至 | 说明 |
|------------|--------|------|
| VCC | 5V（TB6612 D153C 5V 输出） | 电机供电 |
| GND | GND（共地） | — |
| A-IA | ESP32 **GPIO 14** | 电机 A PWM 速度控制（duty 0–255） |
| A-IB | **GND**（直接接地） | 电机 A 方向固定为顺时针 |
| B-IA | **GND**（直接接地） | 电机 B 方向固定为逆时针 |
| B-IB | ESP32 **GPIO 21** | 电机 B PWM 速度控制（duty 0–255） |
| MOTOR-A | 左飞轮电机（M+ / M−） | — |
| MOTOR-B | 右飞轮电机（M+ / M−） | — |

### 5.2 SG90 拨弹舵机接线

| SG90 引脚 | 线色 | 连接至 | 说明 |
|----------|------|--------|------|
| VCC | 红 | 5V | 舵机供电 |
| GND | 棕 / 黑 | GND | 共地 |
| Signal | 橙 / 黄 | ESP32 **GPIO 41** | 50Hz PWM，脉宽 500–2500µs |

### 5.3 接线总图（文字示意）

```
TB6612 D153C
  5V ─────┬────────── L9110S VCC
           ├────────── SG90 VCC (红)
           └── (原有 WS2812B / 激光发射器)
  GND ────┬────────── L9110S GND
           ├────────── SG90 GND (棕)
           └── ESP32 GND

ESP32-S3 N8R2
  GPIO 14 ────────── L9110S A-IA  (飞轮A PWM)
  GPIO 21 ────────── L9110S B-IB  (飞轮B PWM)
  GPIO 41 ────────── SG90 Signal  (拨弹舵机)
  GND     ────────── L9110S A-IB  (飞轮A方向固定LOW)
  GND     ────────── L9110S B-IA  (飞轮B方向固定LOW)
```

---

## 六、ESP32 GPIO 占用

以下为新增引脚，与现有固件不冲突：

| GPIO | 方向 | 功能 | 说明 |
|:----:|------|------|------|
| **14** | 输出 | LEDC PWM | L9110S A-IA，飞轮电机 A 速度（5kHz，8bit） |
| **21** | 输出 | LEDC PWM | L9110S B-IB，飞轮电机 B 速度（5kHz，8bit） |
| **41** | 输出 | LEDC PWM | SG90 Signal，拨弹舵机（50Hz，16bit） |

**现有 GPIO 占用（参考，勿冲突）**

| GPIO | 已用功能 |
|:----:|---------|
| 4,5,6 | 左驱动电机（PWMA, AIN1, AIN2） |
| 7,8,9 | 右驱动电机（PWMB, BIN1, BIN2） |
| 10 | TB6612 STBY |
| 11 | 激光发射器 TTL（38kHz LEDC） |
| 12 | VS1838B OUT（INPUT_PULLUP） |
| 13 | WS2812B DIN（FastLED） |
| 15,16,17,18 | PS2（CS, CLK, CMD, DAT） |
| 26–32 | Flash（禁用） |
| 35–37 | PSRAM（禁用） |

---

## 七、PS2 手柄控制映射

| 按键 | 操作方式 | 功能 | 备注 |
|------|---------|------|------|
| **R1** | 持续按住 | 飞轮电机全速旋转 | 松开后飞轮自然减速 |
| **R1** | 松开 | 飞轮停止（duty=0） | — |
| **○（圆圈）** | 短按（< 500ms） | 拨弹一次，发射 1 颗水弹 | 需 R1 先按住预热飞轮 |
| **○（圆圈）** | 长按（≥ 500ms） | 每 500ms 自动拨弹，循环发射 | 松开即停 |

> **推荐操作顺序**：先按住 R1 等待约 0.5s（飞轮预热），再按 ○ 发射。

---

## 八、代码修改说明

在 `esp32_robot_controller.ino` 中新增以下内容（基于 Arduino-ESP32 **v3.x** API）：

### 8.1 宏定义（在 `// === Motor Pins ===` 区块后追加）

```cpp
// === 水弹发射器 ===
#define PIN_FW_A        14    // 飞轮电机A PWM（L9110S A-IA）
#define PIN_FW_B        21    // 飞轮电机B PWM（L9110S B-IB）
#define PIN_FEEDER      41    // 拨弹舵机 Signal

#define FW_FREQ         5000  // 飞轮PWM频率 5kHz
#define FW_RES          8     // 8bit 分辨率（0–255）
#define FW_DUTY_FULL    220   // 全速占空比（约86%，可按实际调整）

#define SERVO_FREQ      50    // 舵机标准频率 50Hz（周期20ms）
#define SERVO_RES       16    // 16bit 分辨率（0–65535）
// 50Hz + 16bit：1计数 = 20ms/65535 ≈ 0.305µs
#define SERVO_NEUTRAL   4915  // 中位 1.5ms ≈ 4915 counts
#define SERVO_FIRE      6553  // 拨弹位置 2.0ms ≈ 6553 counts（按实际弹仓调整）

#define FEEDER_SHOT_MS  200   // 舵机转至拨弹位置保持时间（ms）
#define FEEDER_LONG_MS  500   // 长按判断阈值（ms）
#define FEEDER_PERIOD_MS 500  // 自动连射间隔（ms）
```

### 8.2 全局变量（在 laser/LED 变量区块后追加）

```cpp
// 水弹发射状态
bool     fw_spinning     = false;
uint32_t feeder_last_ms  = 0;
bool     feeder_firing   = false;
uint32_t circle_press_ms = 0;
bool     circle_was_down = false;
```

### 8.3 setup() 中初始化（在 FastLED 初始化之后）

```cpp
// 水弹发射器初始化
ledcAttach(PIN_FW_A,    FW_FREQ,    FW_RES);
ledcAttach(PIN_FW_B,    FW_FREQ,    FW_RES);
ledcAttach(PIN_FEEDER,  SERVO_FREQ, SERVO_RES);
ledcWrite(PIN_FW_A,   0);
ledcWrite(PIN_FW_B,   0);
ledcWrite(PIN_FEEDER, SERVO_NEUTRAL);
Serial.println("[WATER] Flywheel + Feeder initialized");
```

### 8.4 loop() 中控制逻辑（在 PS2 按键解析区块中追加）

```cpp
// --- 飞轮控制（R1）---
bool r1_down = !(btn2 & BTN_R1);   // BTN_R1 = 0x10（Active LOW）
if (r1_down) {
    if (!fw_spinning) {
        ledcWrite(PIN_FW_A, FW_DUTY_FULL);
        ledcWrite(PIN_FW_B, FW_DUTY_FULL);
        fw_spinning = true;
    }
} else {
    if (fw_spinning) {
        ledcWrite(PIN_FW_A, 0);
        ledcWrite(PIN_FW_B, 0);
        fw_spinning = false;
    }
}

// --- 拨弹控制（○ 圆圈键）---
bool circle_down = !(btn2 & BTN_CIRCLE);  // BTN_CIRCLE = 0x20（Active LOW）
uint32_t now = millis();

if (circle_down && !circle_was_down) {
    circle_press_ms = now;              // 记录按下时刻
    circle_was_down = true;
}
if (!circle_down && circle_was_down) {
    // 短按：发射一颗
    if ((now - circle_press_ms) < FEEDER_LONG_MS) {
        ledcWrite(PIN_FEEDER, SERVO_FIRE);
        feeder_last_ms = now;
        feeder_firing  = true;
    }
    circle_was_down = false;
}
if (circle_down && circle_was_down) {
    // 长按：循环发射
    if ((now - circle_press_ms) >= FEEDER_LONG_MS) {
        if (now - feeder_last_ms >= FEEDER_PERIOD_MS) {
            ledcWrite(PIN_FEEDER, SERVO_FIRE);
            feeder_last_ms = now;
            feeder_firing  = true;
        }
    }
}
// 舵机回位
if (feeder_firing && (now - feeder_last_ms >= FEEDER_SHOT_MS)) {
    ledcWrite(PIN_FEEDER, SERVO_NEUTRAL);
    feeder_firing = false;
}
```

> `BTN_R1` 和 `BTN_CIRCLE` 的掩码需与现有固件中 PS2 字节解析保持一致，请参阅 `esp32_decoder.md` 第十节按键字节定义。

---

## 九、注意事项

| 编号 | 注意事项 |
|:----:|---------|
| 1 | **飞轮预热**：发射前至少预热 0.5s，飞轮未达转速时拨弹会导致水弹无力或卡弹 |
| 2 | **水弹规格**：使用 7–8mm 直径凝水弹（提前泡水膨胀），过大/过干均会卡弹 |
| 3 | **5V 电流余量**：飞轮双电机峰值约 1.2A，若 TB6612 D153C 5V 输出不足，需单独加 UBEC（5V/3A） |
| 4 | **舵机行程校准**：`SERVO_FIRE` 值需根据实际拨弹转盘角度调整（用 `ledcWrite` 逐步测试） |
| 5 | **防水**：水弹含水，安装时确保 ESP32、L9110S、舵机控制线远离弹道出口，必要时加防护罩 |
| 6 | **电机方向**：L9110S 的 A-IB 和 B-IA 接 GND 固定方向；若安装后飞轮转向反了，交换对应电机的 M+/M− 接线即可 |
| 7 | **比赛规则**：参赛前确认赛事规则是否允许水弹发射装置及水弹使用 |
