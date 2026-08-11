# 可编译示例

这些示例不是孤立片段；根目录 `platformio.ini` 为每个示例提供了独立环境，因此
每次修改驱动后都能和诊断固件一起编译检查。

五个示例都显式调用 `canvas.useUiContentRect()`，正文和控件统一受
`x=28, y=28, 496×712, R0` 矩形内容区保护；该矩形完整内接于
`x=12, y=12, 528×744, R48`。Quickstart 的边缘触点标记是为了
验证触控全量范围而保留的明确例外；它绘制后会立刻恢复安全裁剪。开发新页面前
先阅读[安全显示区域规范](../docs/SAFE_AREA.md)，不要在示例中复制 12/60/48/28
magic number。

| 环境 | 源码 | 展示内容 |
|---|---|---|
| `eego_a4_quickstart` | `01_quickstart_display_input/main.cpp` | 安全启动、屏幕、触摸坐标、侧键与屏下键 |
| `eego_a4_storage_rtc_battery` | `02_storage_rtc_battery/main.cpp` | SD 专用 SPI、PCF8563、电池与动态充电极性 |
| `eego_a4_wifi_ble` | `03_wifi_ble_scan/main.cpp` | 2.4 GHz Wi-Fi 与 BLE 扫描、完整资源清理 |
| `eego_a4_safe_sleep` | `04_safe_deep_sleep/main.cpp` | 默认不休眠、显式 10 秒安全深睡与定时唤醒 |
| `eego_a4_cpfont` | `05_cpfont_chinese/main.cpp` | 从 SD 加载 CPFONT 并实际绘制中英文 |

在工程根目录执行：

```sh
pio run -e eego_a4_quickstart
pio run -e eego_a4_storage_rtc_battery
pio run -e eego_a4_wifi_ble
pio run -e eego_a4_safe_sleep
pio run -e eego_a4_cpfont
```

只在明确知道自己要替换诊断固件时才上传示例：

```sh
pio run -e eego_a4_quickstart -t upload --upload-port /dev/cu.usbmodemXXXX
```

示例上传同样会覆盖 app 分区。第一次接触设备时，应先按照
[`docs/GETTING_STARTED.md`](../docs/GETTING_STARTED.md) 完成整片备份，并优先
使用发布包的安全刷写器。示例环境用于学习和开发，不会生成完整 16 MiB 首装镜像。
