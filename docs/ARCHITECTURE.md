# EEGO A4 模板架构

## 1. 分层

```text
应用 / 示例
  DiagnosticApp、examples/*
        │
        ├── PortraitCanvas / CpFontRenderer
        │
  EegoA4Support（安全启动与板级策略）
        │
  通用驱动
  EInkDisplay / InputManager / SDCardManager / Rtc /
  BatteryMonitor / FrontlightManager
        │
  BoardConfig::EEGO_A4（唯一板级 profile）
        │
  Arduino-ESP32 / ESP-IDF / ESP32-S3 硬件
```

应用层不应出现散落的引脚号。`BoardConfig::EEGO_A4` 是通用驱动读取的板级事实，
`EegoA4Hardware.h` 提供诊断和文档使用的具名常量。

## 2. 启动状态机

推荐只调用 `eego::beginStandardHardware()`。其顺序是：

1. 解除复位后仍可能保留的 deep-sleep GPIO hold。
2. 将 GPIO4 拉高，保持整机电源。
3. 初始化侧键并向 GSLX680 上传 36,696 字节固件表。
4. 安全探测可选 LM3630A；未应答时 GPIO12 回到低电平。
5. 根据 LM3630A 是否存在设置 GPIO11 充电极性和上下拉。
6. 初始化 PCF8563。
7. 在独立 HSPI 控制器上挂载 SD。
8. 初始化 UC8279C、EPD rail 和 framebuffer。

SD 必须先于显示初始化。Arduino 的全局 `SPI.begin()` 初始化后不会可靠地被另一
组引脚重新映射；显示与 SD 因此必须各自保持明确、独立的 SPI 总线所有权。

## 3. 总线所有权

| 总线 | 引脚 | 所有者 | 规则 |
|---|---|---|---|
| EPD SPI | SCLK42、MOSI45、CS21 | `EpdBus` | 应用不得直接操作 |
| SD SPI | SCLK39、MISO40、MOSI38、CS47 | `SDCardManager` 的静态 HSPI | 生命周期覆盖整个应用 |
| I²C0 | SDA2、SCL1、400 kHz | GSL、RTC、前光共享 | 单线程调用；扫描只做 ACK |
| USB | native USB CDC | `Serial` | 打开主机端口可能复位 |

驱动会重复执行兼容的 `Wire.begin(2, 1, 400000)`，但不要在另一个 FreeRTOS task
中并发访问同一 `Wire`。产品需要并发时应在应用层建立一个 I²C mutex。

## 4. 显示与坐标

UC8279C 控制器扫描 `768×600`，应用 framebuffer 是 `768×552`、1 bpp，
每行 96 字节；驱动在发送时附加 48 行白色 padding。

模板 UI 使用 `552×768` 竖屏逻辑坐标：

```text
nativeX = logicalY
nativeY = 551 - logicalX
```

`InputManager` 已完成官方 raw 标定并返回 panel-native 坐标。应用使用
`eego::normalizedTouchToLogical()` 或 `PortraitCanvas::touchToLogical()` 完成
最后一次旋转。不要再交换/镜像原始轴。

普通 UI 还必须服从实机标定的圆角内容安全区：

```text
物理外轮廓：x=0, y=0, w=552, h=768, R60
内容安全区：x=12, y=12, w=528, h=744, R48
普通 UI 区：x=28, y=28, w=496, h=712, R0
```

`PortraitCanvas` 默认逐像素裁剪到零圆角普通 UI 区；编译期会验证它的四角均在
R48 内容安全区内。全屏背景、屏幕波形/边缘测试和
触控边缘测试可短暂调用 `useFullPanel()`，但解释文字和控件必须立即恢复
`useUiContentRect()`。
详细布局规则和例外清单见
[SAFE_AREA.md](SAFE_AREA.md)。

## 5. 刷新策略

- Full：页面切换、黑白/灰阶测试、纠正残影。
- Fast：同一页面的小改动。
- Grayscale：两个 1-bit plane 驱动四灰阶。
- 连续 Fast 最多四次，第五次由 UC8279C 驱动自动提升为 Full。

调用阻塞式刷新前，显示 facade 会等待未完成的异步刷新。产品应用仍应避免多个
task 同时绘制 framebuffer；CrossPoint 的做法是把渲染、设置保存、截图和强制
刷新串行化。

## 6. 输入状态

侧键经过消抖，应用在每轮 `loop()` 调用一次 `input.update()`，随后读取
`wasPressed()` 等一次性事件。

GSLX680 的屏下键使用官方特殊 sentinel，不是屏幕坐标：

- 短按释放：`wasHomeKeyShortPressed()`；
- 持续 700 ms：`wasHomeKeyLongPressed()`。

电子纸刷新会阻塞数百毫秒。若产品 UI 必须捕获刷新期间的输入，可使用
`InputManager::beginAsync()` 和 `popPress()/popTouchTap()/popSwipe()`；启用后
主线程不能再调用 `update()`。

## 7. 电源模型

GPIO4 是主电源 latch。模板和示例默认永不休眠。显式深睡统一使用
`eego::enterTimedDeepSleep()`：

1. 关闭前光；
2. 等待显示刷新完成并让面板休眠；
3. 保持 GPIO4 为高；
4. 先设置 3–60 秒定时唤醒；
5. 再进入 deep sleep。

它是防止开发者把设备“睡死”的保守基线，不是最终量产低功耗策略。量产固件还
应测量 SD、I²C pull-up、GPIO6 和各 rail 的静态电流。

## 8. 哪一层该修改

| 需求 | 修改位置 |
|---|---|
| 新页面、菜单、业务 | 应用/示例 |
| 坐标旋转 | `PortraitCanvas` 或应用 layout，不改 GSL raw 标定 |
| 新的板级 revision | `BoardConfig::EEGO_A4` + 硬件文档 |
| 触摸协议/消抖 | `InputManager` |
| 刷新策略 | 应用 cadence；控制器序列才改 `Uc8279cA4Driver` |
| SD 文件 API | `SDCardManager` |
| 安全启动/睡眠 | `EegoA4Support` |
| 字体覆盖测试 | `CpFontRenderer`；产品排版仍用 CrossPoint renderer |

官方与第三方实现的选择理由见
[OEM_CROSSLINK_MAPPING.md](OEM_CROSSLINK_MAPPING.md)。
