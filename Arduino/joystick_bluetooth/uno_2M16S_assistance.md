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
   与UNO通信 ─────►│ [I2C: SDA/SCL]                                     │
                  │  （默认 I2C 地址 0x40，可通过 A0~A4 跳线更改）        │
                  │                                                      │
   固件烧录 ──────►│ ⚠️ 板上无 USB-B 也无 ISP Header，须用 USB-to-TTL   │
                  │    串口适配器连接板上 TX/RX/RST/GND 引脚单独烧录    │
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

> ⚠️ **注意**：经核对 `2M16S_final.jpg`，该板**没有 USB-B 接口，也没有 ISP Header 烧录接口**。2M16S 拥有独立的 ATmega328P MCU，**必须单独烧录**，不能通过叠插 UNO 的 USB-B 接口一并写入（UNO 的 USB-B 只能访问 UNO 自身的 MCU）。唯一可行的烧录方式为：使用 **USB-to-TTL 串口适配器**连接 2M16S 板上的 TX/RX/RST/GND 引脚，通过板载 optiboot Bootloader 上传固件。详见第三章 3.2 节。

---

## 二、Arduino UNO（Digital PWM UNO）

### 2.1 板卡概览示意图

```
                ┌─────────────────────────────────────────────────────────┐
                │                  Arduino UNO (Digital PWM UNO)          │
                │                                                         │
  USB-B 供电/   │ ┌──────┐                                               │
  固件烧录 ─────►│ │USB-B │   (内置 CH340 作 USB-串口桥接)                │
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
| D0 (RX) | UART 接收 | 与 USB 串口共用；本项目中未接外部设备，烧录时无干扰 |
| D1 (TX) | UART 发送 | 与 USB 串口共用；本项目中未接外部设备，烧录时无干扰 |
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
| A4 | 模拟输入 / I2C SDA | 本项目中仅用作模拟输入，未使用 I2C |
| A5 | 模拟输入 / I2C SCL | 本项目中仅用作模拟输入，未使用 I2C |

> ⚠️ **本项目 UNO 与 2M16S 之间通过 SoftwareSerial（Pin 4 RX / Pin 5 TX）通信，并非 I2C。** A4/A5 在本项目中未使用。

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
- **本固件使用 SoftwareSerial（Pin 4 / Pin 5）与 2M16S 通信**，硬件 UART（D0/D1）保留给 USB 调试，**烧录时无需断开任何外部连线**

#### ⑤ ISP 六针烧录接口

引脚定义如下：

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

> ⚠️ **macOS 用户须先安装 CH340 驱动**：macOS 12（Monterey）及以上版本默认不含 CH340 驱动，接上 USB-B 线后端口不会出现。请先从 WCH 官网下载并安装 `CH34x_Install_V1.x.pkg`（[下载地址](https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html)），安装后重启系统再连接 UNO。

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
                        IDE 拉低 DTR → 自动复位 → 激活 Bootloader → 写入固件
```

> ✅ **无需断开任何连线**：本固件使用 SoftwareSerial（Pin 4/5），D0/D1 不被占用，连着外部电路也可正常烧录。

#### 方法二：ISP 六针接口（需外部编程器）

1. 使用 **USBasp** 或 **AVRISP mkII** 编程器
2. 将编程器 6针 ISP 线连接至 UNO 板上的 ISP Header
3. Arduino IDE 选择：
   - `工具 → 编程器 → USBasp`（或对应型号）
   - `草图 → 通过编程器上传`（Ctrl+Shift+U）

---

### 3.2 2M16S 固件烧录

#### 📋 烧录方式选择说明

经核对 `2M16S_final.jpg`，2M16S 板上：
- ❌ **无 USB-B 接口**：无法像 UNO 一样直接插 USB 线烧录
- ❌ **无 ISP Header**：无法使用 USBasp/AVRISP mkII 等编程器
- ❌ **无法通过叠插 UNO 的 USB-B 接口烧录**：UNO 的 USB-B 只能访问 UNO 自身的 ATmega328P，**不能跨板写入 2M16S 上独立的 MCU**
- ✅ **唯一可行方法：USB-to-TTL 串口适配器**（CH340/CP2102/FT232 均可），通过板上 UART 引脚（TX/RX）配合 optiboot Bootloader 烧录

> ℹ️ **2M16S 出厂已预烧录 optiboot Bootloader**，支持通过串口 Bootloader 方式上传 `.ino` 固件，原理与 UNO 完全相同，只是没有板载 USB 桥接芯片，需外接 USB-to-TTL 适配器替代。

#### 为什么 USB-to-TTL 方法是唯一实用选择？

| 对比项 | 通过 UNO USB-B 烧录 2M16S | USB-to-TTL 串口适配器 |
|--------|--------------------------|---------------------|
| 是否可行 | ❌ **完全不可行**，UNO USB-B 只能烧录 UNO 自身 MCU | ✅ **唯一正确方法** |
| 所需硬件 | — | USB-to-TTL 适配器（CH340G 最常见，约 ¥5～¥30） |
| 操作难度 | — | 低，4 根杜邦线接好，步骤与烧录 UNO 相同 |
| 是否需断线 | — | 需暂时断开 2M16S 与 UNO 之间的 TX/RX 连线 |
| 自动复位 | — | DTR/RTS 引脚串接 100nF 电容实现自动复位；无该引脚则手动按 RST |

**结论：USB-to-TTL 串口适配器是烧录 2M16S 的唯一实用方法。**

---

#### ⚠️ 烧录前必须断开与 UNO 的连线

2M16S 的 **Pin 0（RX）/ Pin 1（TX）** 在运行时连接 UNO，烧录时这两根线会干扰 Bootloader 握手，**必须先断开**：

| 需断开的连线 | 说明 |
|------------|------|
| 2M16S Pin 1 (TX) ↔ UNO Pin 4 | 运行时发数据给 UNO，烧录时干扰 RX |
| 2M16S Pin 0 (RX) ↔ UNO Pin 5 | 运行时收 UNO 数据，烧录时干扰 TX |

#### 烧录步骤（USB-to-TTL 适配器）

1. **断开** 2M16S 与 UNO 之间 Pin 0/1 的连线
2. 按如下接线连接 USB-to-TTL 适配器与 2M16S：

| USB-TTL 适配器引脚 | 2M16S 引脚 | 说明 |
|------------------|----------|------|
| TX               | Pin 0 (RX) | 适配器发 → 2M16S 收 |
| RX               | Pin 1 (TX) | 2M16S 发 → 适配器收 |
| GND              | GND        | 共地（必须连接） |
| DTR 或 RTS（可选） | RST（串接 100nF 电容） | 实现自动复位（推荐） |

```
电脑 USB-A  ── USB-to-TTL 适配器 ──► TX → 2M16S Pin0(RX)
                                     RX ← 2M16S Pin1(TX)
                                     GND ── 2M16S GND
                                     DTR ─[100nF]─► 2M16S RST
                                          ↓
                           DTR 自动拉低 RST → 激活 optiboot → 写入固件
```

> 若 USB-to-TTL 适配器无 DTR/RTS 引脚，上传进度条出现的瞬间**手动快速按一下 2M16S 的 RST 按钮**触发复位。

3. 打开 **Arduino IDE**，选择：
   - `工具 → 开发板 → Arduino AVR Boards → Arduino Uno`（ATmega328P @ 16 MHz）
   - `工具 → 端口` → 选择 USB-to-TTL 适配器对应的串口（如 `/dev/tty.usbserial-xxxx` 或 `COMx`）
4. 打开固件文件 `2M16S_firmware/2M16S_firmware.ino`
5. 安装依赖库：`工具 → 管理库` → 搜索并安装 **`PS2X_lib`**（by Bill Porter）
6. 点击 **上传（→）**，等待 "Done uploading"
7. 烧录完成后，**重新连接** 2M16S Pin 0/1 至 UNO Pin 5/4

---

## 四、UNO 与 2M16S 接线汇总

```
  Arduino UNO（Digital PWM UNO）        2M16S Board
  ┌──────────────┐                      ┌─────────────────┐
  │ Pin 4 (SS_RX)│◄─────────────────────┤ Pin 1 (TX)      │  手柄状态帧 →
  │ Pin 5 (SS_TX)├─────────────────────►│ Pin 0 (RX)      │  ← 命中计数帧
  │ Pin 2 (INT0) │◄── VS1838B OUT        │                 │
  │ Pin 9 (PWM)  ├──► 激光 TTL           │ VIN ◄───────────┼── 外部 7~12V
  │ GND          ├─────────────────────►│ GND             │
  │ 5V           ├─────────────────────►│ VCC（逻辑）      │ ← 可选
  └──────────────┘                      │ MA1/MA2 ────────┼── 电机 A
                                        │ MB1/MB2 ────────┼── 电机 B
                                        │ Pin10←CLK       │
                                        │ Pin11←CS        │ ← YFRobot 2015
                                        │ Pin12←CMD       │   手柄解码器
                                        │ Pin13←DAT       │
                                        └─────────────────┘
```

> **通信说明**：UNO 使用 `SoftwareSerial`（Pin 4 RX / Pin 5 TX，115200 bps）接收 2M16S 发来的手柄状态帧，并回传激光命中计数。**两板之间不使用 I2C，A4/A5 在本项目中未占用。**

---

## 五、注意事项

1. **共地（GND）必须连接**：UNO 与 2M16S 之间的 GND 必须相连，否则 SoftwareSerial 串口通信异常（电平参考错位）。
2. **电源分离**：建议大电流（电机/舵机）由独立电源供给 VIN，逻辑 VCC 可由 UNO 5V 提供。
3. **烧录前断开电机**：烧录固件时建议断开电机，防止误触发。
4. **ISP 接口方向**：ISP Header 针1通常以白色/三角形标记，接错会损坏器件。
5. **USB-B 烧录时勿占用 D0/D1**：这两个引脚与 USB 串口复用，会导致烧录失败。
