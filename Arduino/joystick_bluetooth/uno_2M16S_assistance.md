# 2M16S & Arduino UNO（Digital PWM UNO）针脚与接口说明

> 参考图片：`2M16S_final.jpg` 和 `uno_final.jpg`

---

## 一、2 Motor && 16 Servo Drive Board（2M16S）

### 1.1 板卡概览示意图

```
                  ┌──────────────────────────────────────────────────────┐
                  │           2 Motor && 16 Servo Drive Board (2M16S)    │
                  │                                                      │
   电源输入 ──────►│ [VIN] [GND]  (7~12V DC 外部供电)                    │
                  │                                                      │
   电机A ─────────►│ [MA1] [MA2]   Motor A 输出（正负极）                 │
   电机B ─────────►│ [MB1] [MB2]   Motor B 输出（正负极）                 │
                  │                                                      │
   舵机1~16 ──────►│ [S1]~[S16]    16路舵机信号输出（PWM）               │
                  │              每路 3 针：GND / VCC(5V) / SIG          │
                  │                                                      │
   与UNO通信 ─────►│ [I2C: SDA/SCL] 或 UART [TX/RX]                    │
                  │  （默认 I2C 地址 0x40，可通过 A0~A4 跳线更改）        │
                  │                                                      │
   固件烧录 ──────►│ [ISP Header]: MOSI/MISO/SCK/RST/VCC/GND (6-pin)   │
                  │  或板载 Micro-USB / USB-C（视版本而定）              │
                  └──────────────────────────────────────────────────────┘
```

### 1.2 各接口与针脚详解

#### ① 电源接口（Power）

| 针脚 | 标识 | 说明 |
|------|------|------|
| 正极 | VIN  | 外部电源输入，7～12V DC（推荐 7.4V 锂电或 9V 适配器） |
| 负极 | GND  | 电源地（与逻辑地共地） |
| 板载输出 | 5V | 板载稳压输出，供舵机 VCC 使用（最大约 3A） |

> ⚠️ 若舵机数量多、电流大，建议外接独立 5V BEC 为舵机供电，避免烧毁稳压芯片。

#### ② 电机驱动输出（Motor Output）

| 接口 | 引脚 | 说明 |
|------|------|------|
| Motor A | MA1、MA2 | 控制 A 路直流电机，可正反转（H桥驱动） |
| Motor B | MB1、MB2 | 控制 B 路直流电机，可正反转（H桥驱动） |

- 驱动芯片一般为 **TB6612FNG** 或等效芯片
- 单路最大电流：约 1.2A（峰值 3.2A）
- 电机转速/方向由来自 UNO 的 PWM + DIR 信号控制

#### ③ 16路舵机接口（Servo 1 ~ 16）

每个舵机接口为 **3针排针**（从左到右）：

```
  GND  ●
  VCC  ●  (5V)
  SIG  ●  (PWM 信号，50Hz)
```

| 编号 | 功能 |
|------|------|
| S1 ~ S8  | 舵机通道 1～8 |
| S9 ~ S16 | 舵机通道 9～16 |

> 信号为标准 **50Hz PWM**，脉宽 500μs～2500μs 对应舵机 0°～180°。

#### ④ 与 Arduino UNO 通信接口

| 接口 | 针脚 | 说明 |
|------|------|------|
| I2C SDA | A4（UNO侧） | 数据线 |
| I2C SCL | A5（UNO侧） | 时钟线 |
| GND | GND | 共地（必须连接） |
| VCC | 5V | 逻辑电平供电（可选，若2M16S已独立供电则不需要） |

- 默认 **I2C 地址：0x40**
- 可通过板上 A0～A4 焊点/跳线修改地址（最多支持 32 个板级联）

#### ⑤ ISP 烧录接口（固件烧录用）

位于板上的 **6针 ISP 排针**，用于 AVR ISP 编程器烧录固件：

```
  ┌──────────────────┐
  │ MISO  VCC        │
  │ SCK   MOSI       │
  │ RST   GND        │
  └──────────────────┘
```

| 针脚 | 功能 |
|------|------|
| VCC  | 3.3V 或 5V 供电（由编程器提供） |
| GND  | 地 |
| MOSI | 主机数据输出→从机数据输入 |
| MISO | 从机数据输出→主机数据输入 |
| SCK  | SPI 时钟 |
| RST  | 复位（低电平有效） |

---

## 二、Arduino UNO（Digital PWM UNO）

### 2.1 板卡概览示意图

```
                ┌─────────────────────────────────────────────────────────┐
                │                  Arduino UNO (Digital PWM UNO)          │
                │                                                         │
  USB-B 供电/   │ ┌──────┐                                               │
  固件烧录 ─────►│ │USB-B │   (内置 CH340/ATmega16U2 作 USB-串口桥接)     │
                │ └──────┘                                               │
                │                                                         │
  DC 电源 ──────►│ [DC 2.1mm 插座]  7～12V                               │
                │                                                         │
                │  数字 IO：D0(RX) D1(TX) D2~D13                         │
                │  其中 PWM：D3★  D5★  D6★  D9★  D10★  D11★            │
                │                                                         │
                │  模拟 IO：A0 A1 A2 A3 A4(SDA) A5(SCL)                 │
                │                                                         │
                │  电源针脚：3.3V / 5V / GND / VIN                       │
                │                                                         │
                │  ISP Header（6-pin）：用于直接 AVR 编程                  │
                │  RST 按钮：手动复位                                      │
                └─────────────────────────────────────────────────────────┘
```

### 2.2 各接口与针脚详解

#### ① 数字 I/O 引脚（D0 ~ D13）

| 引脚 | 特殊功能 | 说明 |
|------|---------|------|
| D0 (RX) | UART 接收 | 与 USB 串口共用，烧录时勿占用 |
| D1 (TX) | UART 发送 | 与 USB 串口共用，烧录时勿占用 |
| D2  | 外部中断 INT0 | 数字输入/输出 |
| D3  ★ | PWM + INT1 | PWM 输出（490Hz） |
| D4  | — | 数字输入/输出 |
| D5  ★ | PWM | PWM 输出（980Hz） |
| D6  ★ | PWM | PWM 输出（980Hz） |
| D7  | — | 数字输入/输出 |
| D8  | — | 数字输入/输出 |
| D9  ★ | PWM | PWM 输出（490Hz） |
| D10 ★ | PWM / SS  | PWM 输出（490Hz）/ SPI 片选 |
| D11 ★ | PWM / MOSI | PWM 输出（490Hz）/ SPI 数据 |
| D12 | MISO | SPI 数据输入 |
| D13 | SCK / LED | SPI 时钟 / 板载 LED |

> ★ 表示支持 PWM 输出的引脚

#### ② 模拟输入引脚（A0 ~ A5）

| 引脚 | 功能 | 说明 |
|------|------|------|
| A0 | 模拟输入 | 10-bit ADC，0～5V |
| A1 | 模拟输入 | 同上 |
| A2 | 模拟输入 | 同上 |
| A3 | 模拟输入 | 同上 |
| A4 | 模拟输入 / **I2C SDA** | 与 2M16S 通信数据线 |
| A5 | 模拟输入 / **I2C SCL** | 与 2M16S 通信时钟线 |

#### ③ 电源引脚

| 针脚 | 说明 |
|------|------|
| 5V  | 输出 5V（由 USB 或板载稳压提供，最大 500mA） |
| 3.3V | 输出 3.3V（最大 50mA） |
| GND | 地（多个） |
| VIN | 外部电源输入（7～12V），与 DC 座共享 |
| RESET | 复位引脚（低电平触发） |

#### ④ USB-B 接口（固件烧录 & 串口通信）

- 连接电脑后通过 **Arduino IDE** 自动识别为串口（COMx / /dev/ttyUSB0 / /dev/tty.usbserial-xxx）
- 内置自动复位电路，烧录时 IDE 自动拉低 DTR 触发复位，无需手动按 RST
- 通信协议：**115200 baud**（默认）

#### ⑤ ISP 六针烧录接口

同 2M16S，引脚定义如下：

```
  ┌──────────────────┐
  │ MISO  VCC        │
  │ SCK   MOSI       │
  │ RST   GND        │
  └──────────────────┘
```

用于外部编程器（如 USBasp、AVRISP mkII）绕过 USB bootloader 直接烧录。

---

## 三、固件烧录方法

### 3.1 Arduino UNO 固件烧录

#### 方法一：USB-B 接口（推荐，最简单）

1. 用 **USB-A to USB-B** 数据线连接 UNO 与电脑
2. 打开 **Arduino IDE**
3. 选择：
   - 菜单 `工具 → 开发板 → Arduino AVR Boards → Arduino Uno`
   - 菜单 `工具 → 端口` → 选择对应串口（如 `/dev/tty.usbserial-xxxx` 或 `COMx`）
4. 打开固件文件 `UNO_firmware/UNO_firmware.ino`
5. 点击 **上传（→）** 按钮，等待 "Done uploading" 提示

```
电脑 USB-A  ──── USB-B 数据线 ────►  UNO USB-B 接口
                                     ↓
                               自动触发复位 → 烧录 Bootloader → 写入固件
```

#### 方法二：ISP 六针接口（需外部编程器）

1. 使用 **USBasp** 或 **AVRISP mkII** 编程器
2. 将编程器 6针 ISP 线连接至 UNO 板上的 ISP Header
3. Arduino IDE 选择：
   - `工具 → 编程器 → USBasp`（或对应型号）
   - `草图 → 通过编程器上传`（Ctrl+Shift+U）

---

### 3.2 2M16S 固件烧录

#### 方法一：通过板载 Micro-USB / USB-C 接口（若板载 USB 芯片支持）

1. 用 USB 线连接 2M16S 板与电脑
2. Arduino IDE 选择对应开发板（通常为 `Arduino Pro Mini` 或板载 MCU 型号）
3. 选择正确串口，打开 `2M16S_firmware/2M16S_firmware.ino`
4. 点击上传

#### 方法二：通过 ISP 六针接口烧录（推荐，确保稳定）

根据 `2M16S_final.jpg` 中可见的 **6针 ISP Header**：

```
  USBasp / AVRISP mkII
       ↓
  ┌──────────────────┐
  │ MISO  VCC  ──────┼──► 2M16S ISP Header
  │ SCK   MOSI ──────┼──►  (6-pin, 对准缺口方向)
  │ RST   GND  ──────┼──►
  └──────────────────┘
```

**步骤：**
1. 断开 2M16S 与 UNO 的 I2C 连接
2. 将外部编程器接至 2M16S 的 ISP Header（注意 VCC 方向，通常针1为 MISO，PIN1标记三角形）
3. 为 2M16S 提供独立电源（VIN 接 7~12V）或由编程器 VCC 供电（5V 时注意电流限制）
4. Arduino IDE 或 avrdude 命令行：
   ```bash
   avrdude -c usbasp -p atmega328p -U flash:w:2M16S_firmware.hex:i
   ```
5. 烧录完成后断开编程器，重新连接 I2C 至 UNO

#### 方法三：用另一块 Arduino UNO 作为 ISP 编程器

1. 先在 UNO 上烧录 `ArduinoISP` 示例草图（`文件 → 示例 → 11.ArduinoISP → ArduinoISP`）
2. 按如下连接 UNO → 2M16S ISP Header：

| UNO 引脚 | 2M16S ISP 针脚 |
|---------|--------------|
| D10    | RST          |
| D11 (MOSI) | MOSI    |
| D12 (MISO) | MISO    |
| D13 (SCK)  | SCK     |
| 5V     | VCC          |
| GND    | GND          |

3. Arduino IDE 中选择 `工具 → 编程器 → Arduino as ISP`
4. 选择 2M16S 对应目标芯片（如 `ATmega328P`）
5. `草图 → 通过编程器上传`

---

## 四、UNO 与 2M16S 接线汇总

```
  Arduino UNO              2M16S Board
  ┌──────────┐             ┌─────────────┐
  │ A4 (SDA) ├────────────►│ SDA         │
  │ A5 (SCL) ├────────────►│ SCL         │
  │ GND      ├────────────►│ GND         │
  │ 5V       ├────────────►│ VCC (逻辑)  │ ← 可选，2M16S 独立供电时不需要
  └──────────┘             │             │
                           │ VIN ◄───────┼── 外部 7~12V 电源（电机+舵机）
                           │ MA1/MA2 ────┼── 电机 A
                           │ MB1/MB2 ────┼── 电机 B
                           │ S1~S16 ─────┼── 舵机 1~16
                           └─────────────┘
```

---

## 五、注意事项

1. **共地（GND）必须连接**：UNO 与 2M16S 之间的 GND 必须相连，否则 I2C 通信异常。
2. **电源分离**：建议大电流（电机/舵机）由独立电源供给 VIN，逻辑 VCC 可由 UNO 5V 提供。
3. **烧录前断开电机**：烧录固件时建议断开电机，防止误触发。
4. **ISP 接口方向**：ISP Header 针1通常以白色/三角形标记，接错会损坏器件。
5. **I2C 地址冲突**：若连接多块 2M16S，需通过焊盘配置不同 I2C 地址（默认 0x40）。
6. **USB-B 烧录时勿占用 D0/D1**：这两个引脚与 USB 串口复用，会导致烧录失败。
