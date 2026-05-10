# 硬件清单与固件烧录分析

## 硬件清单

| 硬件 | 型号 / 规格 | 数量 | 备注 |
|------|------------|------|------|
| 主控板 | ESP32-S3 N8R2（8MB Flash，2MB PSRAM） | 1 | WiFi 方案主控 |
| 电机驱动模块 | TB6612 D153C（双路驱动 + 板载稳压） | 1 | 纯硬件驱动，无 MCU |
| 主控电机驱动板 | 2 Motor && 16 Servo Drive Board（2M16S） | 1 | 蓝牙方案主控，内置 H 桥 |
| 辅助控制板 | Digital PWM UNO（Arduino UNO 兼容） | 1 | 蓝牙方案辅助板（激光控制） |
| 电机 | 33GB-520-18.7F DC 12V 320RPM ±10% | 2 | 左右驱动轮 |
| 电池 | 18650 锂离子电池 2 节串联 | 1 组 | ~7.4V / 5550mWh（WiFi 方案） |
| 电池 | 18650 锂电池 3 节串联 | 1 组 | ~11.1V（蓝牙方案，满足 12V 电机） |
| 激光发射器 | 18×45 980nm 30mW 100KHz 5V TTL 调制 近红外模组 | 1 | 库位: E1L5 |
| 激光接收器 | VS1838B（1838）红外接收管 | 1 | 38 kHz 解调，Active LOW |
| 蓝牙手柄解码器 | YFRobot 2015（CLK/CS/CMD/DAT 四线 PS2 接口） | 1 | 仅蓝牙方案使用 |

---

## 系统控制方案

本项目支持两种独立的控制方案，硬件组成和固件需求不同：

### 方案一：WiFi App 控制（主方案）

```
Android App（WebSocket）
        │ Wi-Fi（ESP32 AP 热点）
        ▼
ESP32-S3 N8R2
        │ GPIO（PWM + 方向信号）
        ▼
TB6612 D153C（纯硬件电机驱动）
        ├── 左电机 Motor A
        └── 右电机 Motor B
```

### 方案二：蓝牙游戏手柄控制（备选方案）

```
蓝牙游戏手柄
        │ 蓝牙
        ▼
YFRobot 2015 解码器（PS2 协议）
        │ PS2 四线（CLK/CS/CMD/DAT）
        ▼
2M16S 主控板（内置 H 桥）
        ├── 左电机 M1（直驱）
        ├── 右电机 M2（直驱）
        └── UART TX → Arduino UNO（Digital PWM UNO）
                            ├── Pin 9  → 激光发射器（38 kHz PWM）
                            └── Pin 2  ← VS1838B OUT（命中中断）
```

---

## 固件烧录需求分析（专家建议）

### ✅ 结论速览

| 硬件 | 是否需要烧录固件 | 原因 |
|------|:---------------:|------|
| **TB6612 D153C** | ❌ 不需要 | 纯硬件电机驱动芯片，无 MCU |
| **ESP32-S3 N8R2** | ✅ 必须烧录 | 方案一主控，负责 WiFi/WebSocket/电机/激光全部逻辑 |
| **2M16S** | ✅ 必须烧录 | 方案二主控，含 ATmega MCU，需运行差速控制固件 |
| **Arduino UNO** | ✅ 必须烧录 | 方案二辅助板，需运行激光发射 + 命中检测固件 |

---

### 1. TB6612 D153C — 不需要烧录

**TB6612FNG 是纯硬件 H 桥电机驱动芯片，内部无任何可编程 MCU 或存储器。**

- 它只是一个受信号控制的功率开关器件：接收来自主控板的 `PWMA/PWMB`（PWM 速度信号）、`AIN1/AIN2/BIN1/BIN2`（方向信号）、`STBY`（使能信号），然后驱动电机。
- D153C 版本额外集成了板载稳压模块（为主控提供 5V），但这同样是纯硬件电路，无固件。
- **烧录对象是上游主控板（ESP32-S3 或 2M16S），而非 TB6612。**

---

### 2. ESP32-S3 N8R2 — 必须烧录（方案一）

**固件文件**：`Arduino/TB6612motor_ESP32S3_WiFi/TB6612motor_ESP32S3_WiFi.ino`

ESP32-S3 是方案一的唯一主控，承担所有逻辑：
- 以 AP 热点模式启动，监听 WebSocket 连接（端口 81）
- 解析 Android App 发送的 JSON 指令（`motor` / `laser` / `brake` / `ir_reset` 等）
- 通过 LEDC（硬件 PWM）控制 TB6612 的 PWMA/PWMB 引脚，实现无级调速
- 控制激光发射器（38 kHz 载波 TTL 信号）
- 通过 GPIO 12 中断统计 VS1838B 激光命中次数，实时推送给 App
- 安全超时机制：超过 2 秒无指令则自动停车

> 不烧录此固件，ESP32-S3 无法启动热点，App 无法连接，电机和激光均无法工作。

---

### 3. 2M16S — 必须烧录（方案二主控）

**2 Motor && 16 Servo Drive Board（2M16S）板载一颗 ATmega 系列 MCU（Arduino 兼容），自带内置 H 桥电路直接驱动两路直流电机。**

2M16S 固件需实现：
- 通过 PS2 库（`PS2X_lib`）从 YFRobot 2015 解码器读取摇杆轴值（0~255，中位 128）及按键状态
- 对摇杆数据做死区过滤和差速计算，输出左右电机 PWM 及方向
- 通过 UART TX 向 Arduino UNO 发送手柄状态帧（115200 bps）
- 响应 L1 键触发制动停车（AIN=HIGH HIGH，PWM=0）

> ⚠️ **当前仓库中尚未包含 2M16S 固件源码，需另行开发或获取。**

---

### 4. Arduino UNO（Digital PWM UNO）— 必须烧录（方案二辅助板）

**在蓝牙方案中，Arduino UNO 作为激光子系统的专用控制器，需烧录独立固件。**

UNO 固件需实现：
- 通过 SoftwareSerial（Pin 4）接收来自 2M16S 的手柄状态帧，解析激光按键（L2）
- 在 Pin 9（Timer1 OC1A Fast PWM）产生 **38 kHz** PWM 载波，驱动激光发射器 TTL 信号
- 在 Pin 2（INT0 外部中断）检测 VS1838B 下降沿（激光命中），累计命中计数
- 可选：通过 SoftwareSerial（Pin 5）将命中次数回传给 2M16S

> ⚠️ **`Arduino/TB6612motor_ArduinoUNO_Demo.ino` 不是此处所需固件。** 该文件是独立的 TB6612 接线验证 Demo（电机循环正转/反转），仅用于开发初期验证电机和 TB6612 硬件接线是否正确，**不包含** SoftwareSerial 接收、激光 PWM 输出、VS1838B 中断计数等功能，**不可直接用于蓝牙方案的 UNO 辅助控制器。**
>
> ⚠️ **当前仓库中尚未包含 UNO 激光控制器固件源码，需另行开发。**

---

## GPIO 引脚分配（方案一：ESP32-S3 + TB6612）

| GPIO | 方向 | 功能 | 连接设备 |
|:----:|:----:|:----:|:--------:|
| 4 | 输出 | LEDC PWM → TB6612 PWMA | 左电机速度 |
| 5 | 输出 | 方向控制 → TB6612 AIN1 | 左电机方向1 |
| 6 | 输出 | 方向控制 → TB6612 AIN2 | 左电机方向2 |
| 7 | 输出 | LEDC PWM → TB6612 PWMB | 右电机速度 |
| 8 | 输出 | 方向控制 → TB6612 BIN1 | 右电机方向1 |
| 9 | 输出 | 方向控制 → TB6612 BIN2 | 右电机方向2 |
| 10 | 输出 | 驱动使能 → TB6612 STBY | HIGH = 工作 |
| 11 | 输出 | LEDC 38 kHz PWM → 激光 TTL | 激光发射器 |
| 12 | 输入 | 外部中断（FALLING） ← VS1838B OUT | 激光命中检测 |

---

## 2M16S 固件烧录详细步骤

### 板载硬件简介

2M16S（2 Motor && 16 Servo Drive Board）的核心规格：

| 组件 | 型号 / 规格 |
|------|------------|
| 主控 MCU | ATmega328P（与 Arduino UNO 完全兼容） |
| 时钟 | 16 MHz 晶振 |
| 逻辑电压 | 5V |
| USB 转串口芯片 | CH340G（主流版本）或 CH341 / CP2102（部分版本） |
| USB 连接器 | Micro-USB（新版）或 Type-B（旧版，与 UNO 相同） |
| 电机驱动 | 板载 H 桥（L298N 或同类芯片），直驱 M1 / M2 |
| 舵机扩展 | PCA9685（I2C，地址 0x40），16 路 PWM，50 Hz |
| PS2 接口 | 硬连接至 Pin 10（CLK）/ 11（CS）/ 12（CMD）/ 13（DAT） |

> **关键前提**：2M16S 的 ATmega328P 出厂已预装 Arduino UNO Bootloader（optiboot），因此可直接通过 USB 烧录，**无需** ISP 编程器。

---

### 第一步：安装 USB 驱动

#### macOS

1. 前往 WCH 官方下载 CH340 macOS 驱动：  
   `http://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html`
2. 解压后运行 `CH34xVCPDriver.pkg` 安装包。
3. 安装完成后进入：**系统设置 → 隐私与安全性 → 安全性**，找到被阻止的 WCH 驱动扩展，点击 **允许**，然后重启。
4. 重启后插入 USB 线，在终端确认端口出现：
   ```bash
   ls /dev/cu.usbserial*
   # 正常应出现类似：/dev/cu.usbserial-14210
   ```

#### Windows

1. Windows 10/11 通常自动安装 CH340 驱动；若设备管理器中显示感叹号，前往下载手动安装：  
   `http://www.wch-ic.com/downloads/CH341SER_EXE.html`
2. 安装后在 **设备管理器 → 端口（COM 和 LPT）** 中确认出现 `USB-SERIAL CH340 (COMx)`。

---

### 第二步：安装 Arduino IDE

推荐使用 **Arduino IDE 2.x**（官网 `https://www.arduino.cc/en/software`）。

---

### 第三步：安装依赖库

在 Arduino IDE 中打开 **工具 → 管理库**，逐一搜索并安装：

| 库名 | 作者 | 用途 | 版本要求 |
|------|------|------|----------|
| `PS2X_lib` | Bill Porter | 读取 PS2 游戏手柄摇杆与按键 | ≥ 1.8 |
| `Adafruit PWM Servo Driver Library` | Adafruit | 控制 PCA9685（舵机扩展，可选） | ≥ 2.4 |
| `Wire` | Arduino 内置 | I2C 通信，驱动 PCA9685 | 内置，无需额外安装 |
| `SoftwareSerial` | Arduino 内置 | UART 发送手柄状态帧给 UNO | 内置，无需额外安装 |

> `PS2X_lib` 在库管理器中可能搜索不到旧版，可从 GitHub 手动下载 zip 并通过 **项目 → 加载库 → 添加 .ZIP 库** 安装：  
> `https://github.com/mstrens/PSX_Arduino_library`（兼容替代）或原版 `https://github.com/brokentoaster/PS2X_lib`

---

### 第四步：Arduino IDE 开发板配置

进入 **工具** 菜单，按如下配置：

| 选项 | 值 |
|------|----|
| **开发板** | `Arduino UNO`（ATmega328P） |
| **处理器** | `ATmega328P` |
| **端口** | 选择 CH340 对应的串口（macOS: `/dev/cu.usbserial-xxxx`；Windows: `COMx`） |
| **程序员** | `AVRISP mkII`（保持默认，USB 上传时不使用此选项） |

> ⚠️ **不要** 选择 `ATmega328P (Old Bootloader)`，2M16S 出厂使用的是标准 optiboot，波特率 115200。

---

### 第五步：理解 2M16S 板载 H 桥引脚映射

2M16S 的 H 桥控制引脚通常**硬连接**在板内，Arduino Sketch 直接操作对应 IO 引脚即可（无需外接 TB6612）：

| 功能 | Arduino 引脚 | 说明 |
|:----:|:------------:|------|
| 左电机方向 A | **Pin 4** | M1A 控制端 1 |
| 左电机方向 B | **Pin 5** | M1B 控制端 2 |
| 左电机速度 | **Pin 6**（PWM） | Motor 1 PWM |
| 右电机方向 A | **Pin 7** | M2A 控制端 1 |
| 右电机方向 B | **Pin 8** | M2B 控制端 2 |
| 右电机速度 | **Pin 9**（PWM） | Motor 2 PWM |

> ⚠️ **以上引脚为主流 2M16S 版本的典型映射，不同厂商版本可能不同。** 首次烧录前，务必对照手中板子的原理图或丝印标注确认实际引脚。若无法获取原理图，可用万用表导通挡逐一比对 M1A/M1B/M2A/M2B 端子与 ATmega328P 引脚的连接。

---

### 第六步：编写 2M16S 固件（框架说明）

> ⚠️ **当前仓库尚未包含 2M16S 固件源码**，以下为需实现的代码框架，可在 `Arduino/joystick_bluetooth/` 目录下新建 `2M16S_firmware/2M16S_firmware.ino` 进行开发。

```cpp
// ── 依赖库 ──────────────────────────────────────────────────────
#include <PS2X_lib.h>         // PS2 手柄读取
#include <SoftwareSerial.h>   // 向 UNO 发送手柄状态帧

// ── 引脚定义（以主流 2M16S 版本为例，烧录前务必核实）──────────
// PS2 接口（硬连接）
#define PS2_CLK  10
#define PS2_CS   11
#define PS2_CMD  12
#define PS2_DAT  13

// 左电机（Motor 1）
#define M1A_PIN   4   // 方向 A
#define M1B_PIN   5   // 方向 B
#define M1_PWM    6   // 速度 PWM

// 右电机（Motor 2）
#define M2A_PIN   7   // 方向 A
#define M2B_PIN   8   // 方向 B
#define M2_PWM    9   // 速度 PWM

// 与 Digital PWM UNO 通信
#define UNO_TX_PIN  1   // 硬件 UART TX（Pin 1）→ UNO SoftSerial RX (Pin 4)
// 或使用 SoftwareSerial：SoftwareSerial unoSerial(255, 3); // RX 不用，TX=Pin3

// ── 常量 ─────────────────────────────────────────────────────────
#define DEADZONE      15    // 摇杆死区（归一化后 ±15 以内视为 0）
#define TURN_LIMIT   100    // 转向系数最大值

// ── 全局对象 ──────────────────────────────────────────────────────
PS2X ps2x;

// ── 电机控制 ──────────────────────────────────────────────────────
// pwm: -255（满速反转）~ 0（停止）~ +255（满速正转）
void setMotor1(int pwm) {
    pwm = constrain(pwm, -255, 255);
    if (pwm > 0)       { digitalWrite(M1A_PIN, HIGH); digitalWrite(M1B_PIN, LOW);  analogWrite(M1_PWM, pwm);  }
    else if (pwm < 0)  { digitalWrite(M1A_PIN, LOW);  digitalWrite(M1B_PIN, HIGH); analogWrite(M1_PWM, -pwm); }
    else               { digitalWrite(M1A_PIN, LOW);  digitalWrite(M1B_PIN, LOW);  analogWrite(M1_PWM, 0);    }
}

void setMotor2(int pwm) {
    pwm = constrain(pwm, -255, 255);
    if (pwm > 0)       { digitalWrite(M2A_PIN, HIGH); digitalWrite(M2B_PIN, LOW);  analogWrite(M2_PWM, pwm);  }
    else if (pwm < 0)  { digitalWrite(M2A_PIN, LOW);  digitalWrite(M2B_PIN, HIGH); analogWrite(M2_PWM, -pwm); }
    else               { digitalWrite(M2A_PIN, LOW);  digitalWrite(M2B_PIN, LOW);  analogWrite(M2_PWM, 0);    }
}

void brakeMotors() {
    // 短路制动（HIGH HIGH，PWM=0）
    digitalWrite(M1A_PIN, HIGH); digitalWrite(M1B_PIN, HIGH); analogWrite(M1_PWM, 0);
    digitalWrite(M2A_PIN, HIGH); digitalWrite(M2B_PIN, HIGH); analogWrite(M2_PWM, 0);
}

// ── 差速计算 ───────────────────────────────────────────────────────
// ry: 左摇杆 Y（0~255，中位 128）；rx: 左摇杆 X；rx2: 右摇杆 X
void driveByJoystick(byte ry, byte rx, byte rx2) {
    int speed = ((128 - (int)ry) * 100) / 128;   // 正=前进，负=后退
    int turn  = (((int)rx - 128) * 100) / 128;   // 正=右，负=左
    int turn2 = (((int)rx2 - 128) * 100) / 128;  // 右摇杆原地转

    int left, right;
    if (abs(turn2) > DEADZONE) {          // 右摇杆：原地转
        left  = -turn2;
        right = +turn2;
    } else {                              // 左摇杆：差速弧线
        if (abs(speed) < DEADZONE && abs(turn) < DEADZONE) {
            setMotor1(0); setMotor2(0);
            return;
        }
        left  = speed - turn;
        right = speed + turn;
    }
    // 归一化后映射到 PWM
    left  = constrain(left  * 255 / 100, -255, 255);
    right = constrain(right * 255 / 100, -255, 255);
    setMotor1(left);
    setMotor2(right);
}

// ── setup ──────────────────────────────────────────────────────────
void setup() {
    pinMode(M1A_PIN, OUTPUT); pinMode(M1B_PIN, OUTPUT); pinMode(M1_PWM, OUTPUT);
    pinMode(M2A_PIN, OUTPUT); pinMode(M2B_PIN, OUTPUT); pinMode(M2_PWM, OUTPUT);
    setMotor1(0); setMotor2(0);

    Serial.begin(115200);   // 硬件 UART TX → UNO SoftSerial RX

    // PS2 初始化：pressures=false，rumble=false
    int err = ps2x.config_gamepad(PS2_CLK, PS2_CMD, PS2_CS, PS2_DAT, false, false);
    if (err != 0) {
        // PS2 初始化失败（手柄未连接），在此可循环重试或发出蜂鸣提示
    }
}

// ── loop ───────────────────────────────────────────────────────────
void loop() {
    ps2x.read_gamepad(false, 0);   // 读取手柄数据

    // L1 急停制动
    if (ps2x.Button(PSB_L1)) {
        brakeMotors();
    } else {
        driveByJoystick(
            ps2x.Analog(PSS_LY),   // 左摇杆 Y
            ps2x.Analog(PSS_LX),   // 左摇杆 X
            ps2x.Analog(PSS_RX)    // 右摇杆 X
        );
    }

    // 向 Digital PWM UNO 发送手柄状态帧（JSON，115200 bps）
    // 包含 L2 激光按键状态，供 UNO 控制激光发射
    Serial.print("{\"ly\":");  Serial.print(ps2x.Analog(PSS_LY));
    Serial.print(",\"lx\":");  Serial.print(ps2x.Analog(PSS_LX));
    Serial.print(",\"rx\":");  Serial.print(ps2x.Analog(PSS_RX));
    Serial.print(",\"l2\":");  Serial.print(ps2x.Button(PSB_L2) ? 1 : 0);
    Serial.println("}");

    delay(20);   // 50 Hz 控制频率
}
```

---

### 第七步：上传固件

#### 上传前检查清单

- [ ] USB 数据线已连接 2M16S 与电脑
- [ ] **电机电源（VM 端子）断开**（烧录时无需电机电源，避免大电流干扰）
- [ ] Arduino IDE 开发板选择 `Arduino UNO`，端口选择 CH340 串口
- [ ] 如果 2M16S 板上有与 Pin 0（RX）/ Pin 1（TX）连接的外部器件（如本方案中的 UNO），烧录时须**断开外部串口线**，否则 bootloader 握手信号被外设拉拽，导致上传失败

#### 上传操作

1. 在 Arduino IDE 中点击 **上传**（→ 图标）或按 `Ctrl+U` / `Cmd+U`。
2. IDE 底部输出区域显示编译进度，编译完成后自动发送复位信号（DTR 脉冲），ATmega328P 进入 bootloader 模式。
3. 等待显示 `avrdude done. Thank you.` 即上传成功。
4. 若上传失败（`stk500_recv(): programmer is not responding`），常见原因及处理：

| 错误现象 | 原因 | 解决方法 |
|----------|------|----------|
| `programmer is not responding` | Pin 0/1 被外接设备占用 | 断开 UNO 串口线后重试 |
| `programmer is not responding` | 端口选择错误 | 重新确认设备管理器中的串口号 |
| `device signature = 0x000000` | CH340 驱动未安装 | 重新安装 CH340 驱动并重启 |
| `avrdude: stk500_getsync()` | 开发板选错型号 | 确认选择 `Arduino UNO`（ATmega328P） |
| macOS 上传失败 | macOS 系统阻止 CH340 内核扩展 | 在 系统设置→隐私与安全 中允许并重启 |

---

### 第八步：上传后验证

1. 上传完成后**重新接上外部串口线**（2M16S TX → UNO Pin 4）。
2. 打开 Arduino IDE **串口监视器**（波特率 115200），连接 2M16S USB。
3. 拨动游戏手柄摇杆，串口监视器应持续输出类似：
   ```json
   {"ly":128,"lx":128,"rx":128,"l2":0}
   {"ly":100,"lx":140,"rx":128,"l2":0}
   {"ly":80,"lx":128,"rx":128,"l2":1}
   ```
4. 重新接上电机电源（VM），左右摇杆控制车体运动，L1 急停，L2 激光（UNO 侧响应）。

---

### 常见问题汇总

| 问题 | 可能原因 | 处理方法 |
|------|----------|----------|
| 手柄连接后 PS2 初始化失败 | YFRobot 2015 解码器未通电 | 检查 VCC/GND 及 5V 供电电流 |
| 电机方向与预期相反 | H 桥方向引脚定义与实际不符 | 互换 `M1A_PIN` / `M1B_PIN` 赋值，或物理对调电机线 |
| UNO 收不到串口帧 | 2M16S TX 与 UNO Pin 4 未共地 | 确认两板 GND 已短接至同一汇流点 |
| 上传时 CH340 端口频繁消失 | USB 线质量差或接触不良 | 更换质量较好的 Micro-USB 数据线 |
| macOS 找不到 `/dev/cu.usbserial-*` | CH340 驱动内核扩展被阻止 | 安全设置中允许扩展并重启 Mac |








