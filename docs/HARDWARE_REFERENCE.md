# EEGO A4 二进制逆向硬件参考

文档日期：2026-07-25。本文区分“由两份独立固件交叉确认”“仅由单一固件确认”“存在驱动但本机未装配”“没有证据”四类结论，避免把链接进固件的通用库误当成板载硬件。

## 1. 证据来源

| 镜像 | 大小 | SHA-256 | 主要用途 |
|---|---:|---|---|
| CrossLink full 1.0.8 | 16,777,216 | `79c4cdbae8fbb66e8ec586b61ac1209561bee83068627b69fd1f657d970c1483` | Boot、分区、NVS、assets、救砖基线 |
| CrossLink app 1.0.10 | 3,676,688 | `43264c93c8e371db6f2e44574027e50d9444dcff40cc6a645828d2558d70cf24` | 快刷/灰阶波形、外设初始化 |
| 官方 EEGO A4 1.2.7 | 5,547,200 | `37e121af158cf63ca4483d79d4e66a0537f153c7b0d1c0a02a7acc74753f059c` | 触控全范围标定、屏下键哨兵、按键极性 |

两份 app 镜像被按 ESP32-S3 Xtensa 代码恢复为 ELF 后在 Ghidra 中分析。引脚结论只有在初始化参数、寄存器访问和调用点能互相闭合时才记为“确认”；单纯出现的库字符串不算硬件证据。

### 置信度定义

- **A：交叉确认**：官方与第三方固件独立出现，或二进制与实机结果互证。
- **B：强证据**：存在清晰调用链/寄存器序列，但只在一个来源中完整恢复。
- **C：可选装配**：驱动和接线均存在，但当前实机不 ACK 或用户明确表示没有此硬件。
- **D：未知/无证据**：不应分配引脚，也不应主动写寄存器。

## 2. 硬件总表

| 子系统 | 已恢复信息 | 置信度 |
|---|---|---|
| SoC | ESP32-S3，Xtensa LX7，2.4 GHz Wi-Fi + BLE；无 Classic Bluetooth | A |
| Flash | 16 MiB，DIO；当前模板目标 80 MHz | A |
| PSRAM | 8 MiB Octal PSRAM | A，实体启动日志验证 |
| E-paper | UC8279C，20 MHz SPI mode 0，BUSY 低有效 | A |
| 主机 framebuffer | 768×552，1 bpp，52,992 字节 | A |
| 控制器扫描 | 768×600，主机数据后追加 48 行全白 | A |
| 触摸 | GSLX680，I²C `0x40`，轮询，无已知 IRQ/RESET 引脚 | A |
| 屏下电容键 | GSL 特殊点 `X=0x03a0,Y=0x1020` | A |
| 侧键 | Up GPIO 5 低有效；Down GPIO 7 低有效；Power GPIO 8 高有效 | A |
| microSD | 独立 SPI，20 MHz | A |
| RTC | PCF8563，I²C `0x51` | A |
| 电池 | GPIO 10 ADC，分压换算系数 1.559 | A |
| 充电状态 | GPIO 11；有 LM3630A 型号高有效，无 LM3630A 型号低有效 | A（极性分支）；功能已验收，未发布三阶段数字标定 |
| 电源保持 | GPIO 4，高有效，睡眠中保持 | A |
| 显示电源 | GPIO 6，高有效；休眠时低并保持 | A |
| 前光 | LM3630A `0x36`、enable GPIO 12；当前实机未装配 | C |
| IMU | 没有板载器件证据；仅安全探测常见 `0x6a/0x6b` | D |
| 温湿度 | 没有板载器件证据；仅安全探测常见 `0x44` | D |
| 音频/麦克风 | 无 codec、I²S/PDM 或功放接线证据 | D |
| LED/蜂鸣器 | 无板载接线证据 | D |

## 3. 完整引脚图

| GPIO | 方向/总线 | 功能 | 有效电平或参数 |
|---:|---|---|---|
| 1 | I²C SCL | GSLX680、PCF8563、可选 LM3630A | 400 kHz |
| 2 | I²C SDA | 同上 | 400 kHz |
| 4 | 输出 | 主电源保持/rail latch | 高有效；启动第一时间拉高 |
| 5 | 输入上拉 | Up | 低有效 |
| 6 | 输出 | E-paper 电源/rail enable | 高有效 |
| 7 | 输入上拉 | Down | 低有效 |
| 8 | 输入下拉 | Power | 高有效 |
| 10 | ADC | 电池分压采样 | 实际 mV = ADC mV × 1.559 |
| 11 | 输入上拉/下拉 | 充电状态 | `0x36` 前光在场时高有效；不在场时低有效 |
| 12 | 输出 | 可选 LM3630A enable | 高有效；未探测到 IC 时保持低 |
| 13 | 输出 | E-paper RESET | 高→低→高，20/10/100 ms |
| 14 | 输出 | E-paper D/C | 命令/数据选择 |
| 21 | 输出 | E-paper CS | 低有效 |
| 38 | SPI MOSI | microSD | 独立 HSPI |
| 39 | SPI SCLK | microSD | 20 MHz |
| 40 | SPI MISO | microSD | 20 MHz |
| 41 | 输入 | E-paper BUSY | 低有效 |
| 42 | SPI SCLK | E-paper | 20 MHz |
| 45 | SPI MOSI | E-paper | 20 MHz |
| 47 | 输出 | microSD CS | 低有效 |

未列出的 GPIO 均为未知，不能因为 ESP32-S3 有该管脚就认定已连接外设。

## 4. UC8279C 屏幕

### 4.1 几何与内存顺序

- 控制器配置为 `0x61: 03 00 02 58`，即 768×600。
- 固件 framebuffer 为 768×552，每行 96 字节，总计 52,992 字节。
- 写入 `DTM1 (0x10)` 或 `DTM2 (0x13)` 时，552 行按 `y=551..0` 逆序发送，再追加 48×96 字节 `0xff`。
- 产品页面常见的 768×528 是可视/遮罩区域描述，不等于控制器 RAM 合同。
- 模板逻辑界面为竖屏 552×768；映射为 `nativeX=logicalY`、`nativeY=551-logicalX`。

### 4.2 初始化序列

```text
00: 3f 4a
03: 20
01: 43 00 78 78 17        # 灰阶模式首字节为 03
06: 25 25 3c
82: 24                    # 灰阶模式为 20
30: 0f
61: 03 00 02 58           # 768×600
65: 00 00 20 00
e1: 02
```

RESET 时序为高 20 ms、低 10 ms、高 100 ms。`0x04` 上电，`0x12` 刷新，`0x02` 下电，`0x07 0xa5` 深睡。BUSY 为低有效。

### 4.3 波形来源与策略

| 模式 | 来源 | 数据 | SHA-256 |
|---|---|---|---|
| Fast | CrossLink 1.0.10 | 5×49 字节记录，每项只发送前 42 字节；`0x50=0xd7` | `6bcb79bc45c53071254275623cd3e99ba7e21c913b892f1731b835694e2773ca` |
| Full/Half | 官方与 CrossLink 共有 | 5×49 字节完整发送；`0x50=0x97` | `1ed90690ea30f78caf3e89c96693314e60023dcb9bc0257099d56fadc0ae1ed0` |
| 4 gray | CrossLink | 两个 52,992 字节 bit-plane，5×42 字节波形记录 | `c38ac9c0085d1fe35c50d3632e8d58474a59093a9b03ce25adc84929ffab731b` |

第三方快刷约 380 ms，但连续使用会积累残影。驱动最多允许连续四次 Fast，第五次自动提升到约 820 ms 的 Full。显式 Full/Half/Gray、初始化和深睡都会重置该计数。

## 5. GSLX680 触控与屏下键

### 5.1 固件上传

GSLX680 在 `0x40`，启动时需要完整上传控制器固件。官方 1.2.7 与 CrossLink 1.0.10 的表逐字节相同：

```text
36,696 bytes
4,587 个 {register,value} 记录
raw table SHA-256:
076ac8cb283d055660f79f7ea0e10a33533563065d6866c095ad3e74537f3051
```

本工程生成后的头文件因为 C++ 格式与注释而有不同文件哈希，不能拿头文件哈希替代原始表哈希。控制器从寄存器 `0x80` 读取 24 字节触点帧，最多解析 5 点；模板交互目前使用第一个触点。

### 5.2 精确标定

点 0 的 raw Y 在帧字节 4–5，raw X 在字节 6–7，均为小端。官方 1.2.7 全范围整数变换：

```text
portrait_x = min(raw_y, 680) * 551 / 680
portrait_y = (920 - min(raw_x, 920)) * 767 / 920
native_x   = portrait_y
native_y   = 551 - portrait_x
```

最后一次 `native_y` 镜像用于匹配竖屏 renderer 的顺时针变换，否则界面横向触控会左右颠倒。模板采用官方 1.2.7 的完整范围变换，覆盖全部 panel-native 坐标。

### 5.3 屏下电容键

单点帧中的特殊原始记录：

```text
rawXWord = 0x03a0
rawYWord = 0x1020
```

官方把它当作 `NotificationShade` 独立键。它必须在普通坐标的 12-bit mask 之前识别，否则会伪装成角落触点。模板中：

- 释放前不足 700 ms：短按事件
- 持续至少 700 ms：长按事件，仅发一次
- 菜单短按为返回，长按为运行自动测试；按键测试页同时验证两者

## 6. microSD

SD 卡拥有独立 SPI 总线：

```text
SCLK=39, MISO=40, MOSI=38, CS=47, 20 MHz
```

必须长期保留独立 `SPIClass(HSPI)`。Arduino 全局 `SPI.begin()` 已初始化后不会按第二组引脚重新映射；模板使用专用 SPI 实例，确保显示与 SD 始终各自拥有正确总线。

## 7. RTC、I²C 与可选前光

共享总线为 SDA 2、SCL 1、400 kHz。

### PCF8563

- 地址 `0x51`
- 时间从寄存器 `0x02` 开始，7 字节 BCD
- 秒寄存器 bit 7 是 VL/oscillator-stop；置位时 ACK 正常但时间不可相信
- 模板 `rtc set` 写入有效时间并清除 VL
- `CLKOUT (0x0d)` 在初始化时关闭

### LM3630A

二进制中恢复出：

```text
I2C address 0x36
enable GPIO12
register sequence 50,01,02,05,06,00,03,04
```

当前已验证机型没有 LM3630A，也没有前光。模板将其视为可选 BOM：GPIO 12 只短暂使能用于 ACK 探测；无 ACK 立即拉低。有 ACK 时才允许 `frontlight` 命令在低亮度下短时验证，随后强制关闭。

### 未确认传感器

官方/第三方镜像没有为 EEGO A4 建立 IMU 或温湿度器件调用链。本工程扫描整个 I²C 地址空间，并对 `0x44`、`0x6a`、`0x6b` 的 ACK 给出候选提示，但不读取芯片专属寄存器、不宣称型号，也不为未知模块切换 GPIO。

## 8. 电池、充电与电源

- GPIO 10 为 ADC 电池分压输入。
- `battery_mV = analogReadMilliVolts(10) × 1.559`。
- 电量百分比使用通用锂电压曲线估算，不是 fuel gauge 精确 SoC。
- GPIO 11 为充电状态输入，不是充电器控制接口。CrossLink 1.0.10 的
  `FUN_420c6958` 先调用 `FUN_420c646c` 探测 LM3630A：探测成功时按高有效
  读取，失败时按低有效读取。当前无前光实机属于后者。
- “信号有效”仍不能自动区分“正在充电”“外部电源存在”和“充电完成”。
  模板用 `charge capture battery|charging|full` 在 RAM 中保存三阶段样本，
  只有无电源/正在充电/已充满呈现预期的 inactive/active/inactive 图案时，
  才将三阶段语义标为通过。
  同一功能也在电池页映射为 Up/Down/Power 三个实体键和三个触摸按钮，因此
  “仅电池”阶段不依赖 USB 串口。
- GPIO 4 为整机电源保持，必须在慢初始化前拉高。
- GPIO 6 为 EPD rail；屏幕驱动管理它，深睡时可拉低并 hold。

模板默认没有空闲休眠。显式 `sleep N` 只接受 3–60 秒，在 GPIO 4 高电平 hold 后启用 RTC timer wake，再进入深睡。

## 9. 无线与 USB

ESP32-S3 原生提供 2.4 GHz 802.11 b/g/n 和 Bluetooth LE，不支持 Bluetooth Classic。模板：

- Wi-Fi 扫描不需要凭据，不保存 NVS。
- `wifi connect SSID|PASSWORD` 只在 RAM 中短时使用凭据，15 秒超时后断开并关闭 Wi-Fi。
- BLE 使用 5 秒被动扫描，不配对、不连接、不修改 bond。
- USB CDC 在启动时启用；收到主机命令才算完整 RX/TX 闭环通过。

## 10. 中文字体事实

中文不是屏幕控制器能力，而是字库与 renderer 能力：

- CrossLink `assets` 是 `ASET` 容器，其中 `system.ttf` 是 4,893,768 字节 MiSans，SHA-256 `08c4ca4f3c6b4e393475e7a6ef5ca2baec988c170c967a409f33eb9db5d96656`。
- CrossLink app 出现 `system.ttf`、`stb_truetype` 与 assets mapper 的实际引用。
- 官方 1.2.7 独立包含 `.eefont`/`.eefontpack` 和 `stbtt_*` 路径。
- CrossPoint 适配把 MiSans 转换为 8/10/12/14/16/18 pt 的 `.cpfont`，每档 18,736 glyph、2,045 Unicode interval，放在 SD `/fonts/MiSansA4`。
- CrossPoint 简体中文系统翻译为 426/426 条，无 fallback。

诊断模板包含独立最小 CPFONT v4 reader：控制标签仍用 ASCII
内建字体，以便 SD/字库损坏时显示错误；文字测试页则从 SD
`/fonts/MiSansA4` 实际读取 8/10/12/14/16/18 pt 的 Unicode
覆盖表、glyph metadata 和 2-bit bitmap，渲染英文、简体中文、混排与中文标点。诊断 reader
只实现 regular、LTR 和黑白阈值路径；产品 CrossPoint renderer 仍负责 fallback family、缓存、kerning、ligature、BiDi 和灰阶等完整能力。

当前验证基线已在实体机逐页读回三张 52,992 字节 framebuffer：六档字号的
英文/简体中文必需字符缺失数均为 0，bitmap I/O 错误为 0，三页布局均完整。
`screenshot` 命令使用分块重试传输整帧，并在 footer 发完后才恢复 USB CDC
的正常有限 TX 超时。普通状态/JSON 输出使用 50 ms，截图期间使用 250 ms；
最终 schema 2 JSON 报告在主机端完整解析，并携带屏幕、触控、按键、SD、
RTC、电池、充电极性、电源和可选前光的完整硬件合同。

## 11. 分区

OEM full 镜像的主 app 只有约 4 MiB，无法容纳当前 CrossPoint。模板与 A4 CrossPoint 使用相同的对称布局：

| 名称 | Offset | Size |
|---|---:|---:|
| nvs | `0x9000` | `0x5000` |
| otadata | `0xe000` | `0x2000` |
| app0 | `0x10000` | `0x640000` |
| app1 | `0x650000` | `0x640000` |
| spiffs | `0xc90000` | `0x360000` |
| coredump | `0xff0000` | `0x10000` |

首次变更布局必须写完整 16 MiB 镜像到 `0x0`；此后 app-only 更新才可写 `0x10000`。

## 12. 最终验证状态

已由实体机或日志验证：ESP32-S3/PSRAM、启动、电源保持、屏幕 SPI、全刷/快刷、SD、RTC、USB、CrossPoint 中文字体缓存与渲染、永不休眠设置。

触控左右方向、四边边界、侧键、屏下键短/长按、实体屏幕刷新与残影、充电行为
以及显式深睡/唤醒均已完成实体机验证；适用范围和证据边界见
[实体设备验证摘要](VALIDATION.md)。

本项目没有发布电池 ADC 绝对误差、GPIO 11 三阶段数字样本或深睡静态电流等数值规格；若未来需要对外承诺这些指标，仍须使用万用表、功耗仪和受控充电条件另行标定。GPIO 11 的型号相关极性分支已由第三方二进制确认。没有实机装配证据的传感器、音频、麦克风、LED 不应被标记为“待驱动”，而应保持“不存在/未知”。
