# EEGO A4 驱动与模板使用说明

本文是 API 手册；第一次使用请先完成
[从零开发指南](GETTING_STARTED.md)，可直接运行的完整代码位于
[examples/](../examples/README.md)。

## 1. 启动顺序

推荐入口是统一启动器：

```cpp
void setup() {
  eego::holdPower();  // 第一条硬件操作
  delay(250);
  eego::beginUsbSerial();

  const eego::BeginStatus hardware =
      eego::beginStandardHardware(display, input, frontlight, rtc, sd);
  if (!hardware.displayReady) {
    return;
  }
}
```

它执行已经在实体机验证的顺序：

```text
GPIO4 power latch
→ GSLX680
→ 可选 LM3630A 探测
→ GPIO11 动态充电极性
→ PCF8563
→ SD 专用 HSPI
→ UC8279C
```

不要先等待 USB 数秒再拉高 GPIO4，也不要自行交换 SD/display 初始化顺序。
完整实现位于 `lib/EegoA4Support/`。

## 2. BoardConfig

编译必须设置：

```ini
-DFREEINK_DEVICE_EEGO_A4=1
```

它选择 `BoardConfig::EEGO_A4`，从一个 profile 注入屏幕、按键、触控、SD、
电池、RTC、电源和可选前光参数。应用层不要重复散落 magic pin number；若
硬件 revision 改变，应先改 profile 和
`lib/EegoA4Support/include/EegoA4Hardware.h`，再改测试期望。

## 3. 屏幕

### 初始化与黑白绘制

```cpp
EInkDisplay display(eego::EPD_SCLK, eego::EPD_MOSI, eego::EPD_CS,
                    eego::EPD_DC, eego::EPD_RESET, eego::EPD_BUSY);
// beginStandardHardware(...) 会调用 display.begin()

uint8_t* fb = display.getFrameBuffer();
if (!fb) {
  // PSRAM/分配失败
}

display.clearScreen(0xff);  // white
// 修改 768×552、1bpp framebuffer
display.displayBuffer(EInkDisplay::FULL_REFRESH, false);
```

framebuffer 每行 96 字节，bit 7 对应该字节最左像素，`0` 是黑、`1` 是白。驱动负责行逆序和 48 行 controller padding，不要在应用 framebuffer 中自行增加到 600 行。

### 竖屏逻辑坐标

模板的 `PortraitCanvas` 把 552×768 UI 坐标转换为面板 native 768×552：

```text
nativeX = logicalY
nativeY = 551 - logicalX
```

它继承 `Adafruit_GFX`，可直接调用 `drawRect`、`print` 等 API。若换用其他 renderer，必须保持触摸映射与显示旋转互为逆变换。

### 安全内容区

实机标定的物理外轮廓是 R60。正文和控件必须向内避让 12 px，使用
`x=12, y=12, w=528, h=744, R48` 的圆角安全区。普通 UI 再使用
`x=28, y=28, w=496, h=712, R0` 的统一矩形内容区；28 来自最大内接矩形所需的
27 px 最小边距向上对齐到 4 px 网格。`PortraitCanvas` 默认启用矩形裁剪，普通
页面应显式保留：

```cpp
canvas.useUiContentRect();
canvas.clear();  // 背景仍可清满整个 framebuffer
canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
canvas.print("TITLE");
```

只有全屏背景或面板/触控边缘测试可以短暂调用 `canvas.useFullPanel()`，绘制后
必须立即恢复 `canvas.useUiContentRect()`。裁剪只是兜底，布局不能故意让半个字
或按钮落到矩形外。完整几何、顶部/底部排版建议和例外清单见
[SAFE_AREA.md](SAFE_AREA.md)。

### 刷新模式

```cpp
display.displayBuffer(EInkDisplay::FAST_REFRESH, false);
display.displayBuffer(EInkDisplay::HALF_REFRESH, false);
display.displayBuffer(EInkDisplay::FULL_REFRESH, false);
```

在 A4 驱动中 HALF 与 FULL 都使用完整清理波形。Fast 连续上限是 4，下一次自动提升为 Full。屏幕长期内容更新时仍应在界面切换或定期执行 Full。

### 四灰阶

```cpp
const size_t n = display.getBufferSize();  // 52992
uint8_t* lsb = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
uint8_t* msb = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

// 填充每像素的两个 bit-plane
display.copyGrayscaleBuffers(lsb, msb);
display.displayGrayBuffer(false);
display.cleanupGrayscaleBuffers(display.getFrameBuffer());
```

CrossLink 路径使用 MSB→DTM1、LSB→DTM2。两个 plane 共约 106 KiB，驱动内部还会持有副本，应用应从 PSRAM 分配。

## 4. 触控

```cpp
InputManager input;
input.begin();

void loop() {
  input.update();
  float nx, ny;
  if (input.wasTouchTap(nx, ny)) {
    int16_t logicalX = 0;
    int16_t logicalY = 0;
    eego::normalizedTouchToLogical(nx, ny, logicalX, logicalY);
  }
}
```

`nx,ny` 是 panel-native 归一化坐标，不是竖屏 UI 坐标。GSL backend 已经应用官方 raw 标定和 native-Y 反射，应用只负责当前 renderer 旋转。不要在应用层再次镜像 raw X/Y。

可用事件：

```cpp
input.wasTouchPressedAt(nx, ny);
input.isTouchHeldAt(nx, ny);
input.wasTouchTap(nx, ny);
input.wasSwipe(x0, y0, x1, y1);
input.wasHomeKeyShortPressed();
input.wasHomeKeyLongPressed();
```

屏下特殊键不是普通屏幕坐标；backend 在 12-bit mask 前截获。

如果页面刷新期间也不能丢输入，可以使用后台队列：

```cpp
input.beginAsync();
uint8_t button;
while (input.popPress(button)) {
  // route button
}
```

一旦调用 `beginAsync()`，后台 task 就拥有采样状态；主线程不能再调用
`input.update()`/`wasPressed()`。完整同步示例见
[`01_quickstart_display_input`](../examples/01_quickstart_display_input/main.cpp)。

## 5. 实体按键

```cpp
input.update();
if (input.wasPressed(InputManager::BTN_UP)) { /* GPIO5 */ }
if (input.wasPressed(InputManager::BTN_DOWN)) { /* GPIO7 */ }
if (input.wasPressed(InputManager::BTN_POWER)) { /* GPIO8 */ }
```

Up/Down 使用 `INPUT_PULLUP` 和低有效，Power 使用 `INPUT_PULLDOWN` 和高有效。事件已经消抖；不要同时在应用中直接轮询同一 GPIO 并再次产生键事件。

## 6. SD

```cpp
SDCardManager& sd = SDCardManager::getInstance();
// beginStandardHardware(...) 已经调用 sd.begin()
if (sd.ready()) {
  sd.writeFile("/test.txt", "hello");
  String text = sd.readFile("/test.txt");
  sd.remove("/test.txt");
}
```

EEGO A4 的 SD 与 EPD 引脚完全独立，SDK 使用静态 `SPIClass(HSPI)` 保持专用控制器生命周期。不要把 SD 改成全局 `SPI` 后再启动屏幕。

## 7. RTC

```cpp
Rtc rtc;
if (rtc.begin()) {
  Rtc::DateTime now;
  if (rtc.now(now)) {
    // valid time
  } else {
    // 可能 VL flag，先设时
  }
}
```

PCF8563 使用 7-byte BCD。`Rtc::now` 在 VL flag 置位时故意返回 false，防止把失效时间当真。

## 8. 电池与充电

```cpp
BatteryMonitor battery;
BatteryMonitor::Status s = battery.readStatus();
```

`Status` 区分 `supported`、`millivoltsKnown`、`percentageKnown` 和 `chargingKnown`。百分比是基于电压曲线的估值；需要精准 SoC 时必须确认并增加真正的 fuel-gauge 硬件，不能仅改算法假装精确。

EEGO A4 的 GPIO11 极性与可选前光装配相关：LM3630A `0x36` 应答时高有效，
不应答时低有效。`beginStandardHardware()` 会同时更新 GPIO 上下拉和
`BoardConfig::ACTIVE.batteryChargeActiveHigh`；只改 pinMode 而不更新 profile
会让 `BatteryMonitor` 在带前光版本上读反。

诊断层仍要求 `charge capture battery|charging|full` 三阶段样本，不能仅凭一个
瞬时电平宣称充电功能通过。完整示例见
[`02_storage_rtc_battery`](../examples/02_storage_rtc_battery/main.cpp)。

## 9. 可选前光

```cpp
FrontlightManager light;
light.begin();
if (light.present()) {
  light.setColorTemperature(50);
  light.setBrightness(10);
  delay(1000);
  light.off();
}
```

`begin` 会把 GPIO 12 短暂拉高并探测 `0x36`。无 ACK 时 `present()==false`，后续 API 是安全 no-op。不要绕过 `present()` 直接声明该机型有前光。

## 10. Wi-Fi、BLE 与共存

Wi-Fi 直接使用 Arduino `WiFi`。测试后应：

```cpp
WiFi.scanDelete();
WiFi.disconnect(true, false);
WiFi.mode(WIFI_OFF);
```

BLE 使用 NimBLE-Arduino。模板每次测试 `init`、扫描、清结果、`deinit(true)`，避免常驻内存。ESP32-S3 只支持 BLE。

```cpp
NimBLEDevice::init("EEGO-A4");
NimBLEScan* scan = NimBLEDevice::getScan();
scan->setActiveScan(false);
NimBLEScanResults found = scan->getResults(5000);  // NimBLE 2.x: milliseconds
scan->clearResults();
NimBLEDevice::deinit(true);
```

NimBLE-Arduino 2.x 的扫描时长单位是毫秒，阻塞式扫描入口是 `getResults()`。
完整资源清理见
[`03_wifi_ble_scan`](../examples/03_wifi_ble_scan/main.cpp)。

## 11. I²C 总线共享

GSLX680、PCF8563 和可选 LM3630A 共用 `Wire`：

```cpp
Wire.begin(2, 1, 400000);
Wire.setTimeOut(50);
```

驱动都使用相同 pins/frequency，重复 `Wire.begin` 可以保持配置，但产品代码更适合由统一 bus owner 初始化一次。扫描只做空 payload ACK；未知地址不应随意写“who-am-I”寄存器，因为不同器件同地址时写操作可能有副作用。

## 12. 电源与深睡

正常运行期间不要拉低 GPIO 4。安全的定时深睡顺序：

```cpp
eego::enterTimedDeepSleep(display, frontlight, 10);
```

helper 会等待刷新、关闭前光、保持 GPIO4、先设置 timer wake，再进入 deep sleep，
并把时间限制在 3–60 秒。唤醒后 `eego::holdPower()` 会解除 hold 并重新拉高。
完整示例见
[`04_safe_deep_sleep`](../examples/04_safe_deep_sleep/main.cpp)。

真正产品休眠还需根据实测决定 GPIO6、SD、I²C pull-up 等 rail 状态；模板 helper
只提供不易把设备永久睡死的保守基线。

## 13. CPFONT 中英文字体诊断

模板的 `CpFontRenderer` 是独立 CPFONT v4 reader，不依赖 CrossPoint
活动/主题层。它读取 CPFONT v4 global header 和 regular style TOC，把 Unicode interval
覆盖索引放入 PSRAM，按需从 SD 读取 16-byte glyph metadata 与 1/2-bit packed bitmap，再直接画入
`PortraitCanvas`。

```cpp
CpFontRenderer font(sd, canvas);
if (font.load("/fonts/MiSansA4/MiSansA4_14.cpfont")) {
  uint16_t missing = 0;
  uint16_t ioErrors = 0;
  font.drawText(24, 120, "English 中文字体测试", &missing, &ioErrors);
}
```

诊断 reader 的边界是 regular、LTR、单行、黑白输出；它用于证明 SD→Unicode
覆盖→glyph metadata→bitmap→A4 framebuffer 的最小链路。阅读器产品代码需要继续使用 CrossPoint
完整 renderer，以获得缓存、fallback、kerning、ligature、BiDi、换行布局与灰阶抗锯齿。

可编译页面见
[`05_cpfont_chinese`](../examples/05_cpfont_chinese/main.cpp)。

## 14. 扩展新硬件 revision

新增能力时遵循：

1. 从新官方镜像、原理图或实测至少获得一条直接证据。
2. 先只做只读/ACK 探测。
3. 将引脚和 capability 放入 BoardProfile，不散落在 UI。
4. 为测试添加 PASS/WARN/FAIL/NOT_FITTED 语义。
5. 串口报告必须包含原始值，便于跨机器比较。
6. 自动测试不得执行格式化、永久配对、保存凭据、无限睡眠或未知 GPIO 驱动。
7. 更新硬件文档的证据来源和置信度。
