# EEGO A4 开发排错

先记录固件版本、app SHA、串口日志和复现步骤。屏幕类问题同时保存 framebuffer
和实体面板观察，方法见 [FRAMEBUFFER_DEBUGGING.md](FRAMEBUFFER_DEBUGGING.md)。

| 症状 | 最常见原因 | 正确处理 |
|---|---|---|
| USB 接着能运行，拔线就关机 | GPIO4 没有最先拉高 | `setup()` 第一条硬件操作调用 `eego::holdPower()` |
| framebuffer 正确，实体屏幕还是旧画面 | SD 和 EPD 错用了同一个/被重映射的 SPI 对象 | 使用 `SDCardManager`；不要把 EEGO SD 改为全局 `SPI` |
| 屏幕全白或无 framebuffer | PSRAM/分配失败，或写错目标板 | 检查 N16R8 board、8 MiB PSRAM、`framebufferReady()` |
| 刷新后残影越来越重 | 长期只请求 Fast | 保留自动 4×Fast→Full；页面切换主动 Full |
| 触摸左右颠倒 | 应用再次镜像了已标定坐标 | 只调用 `normalizedTouchToLogical()` |
| 四角点不到 | 使用 CrossLink 的窄短轴标定，或把 raw 当 logical | 保留官方 1.2.7 GSL 变换和当前 profile |
| 屏下键触发角落按钮 | 在截获 sentinel 前去掉了 12-bit 高位 | 使用 `InputManager` 的 short/long API，不自行解析 frame |
| 按键偶尔丢失 | 主循环被电子纸刷新阻塞 | 需要时启用 `beginAsync()`；启用后不要再调用 `update()` |
| 中文变方框 | SD 缺少 CPFONT、路径/字号不对 | 检查 `/fonts/MiSansA4/MiSansA4_{8,10,12,14,16,18}.cpfont` |
| ASCII 正常但中文页空白 | 把 Adafruit 内建字体误当中文字体 | 使用 `CpFontRenderer` 示例或 CrossPoint 完整 renderer |
| SD 初始化后屏幕不工作 | 初始化顺序或 SPI 生命周期被改 | 使用 `beginStandardHardware()`，保持 SD 在 display 之前 |
| RTC `begin()` 成功但 `now()` 失败 | PCF8563 VL 标志表明时间无效 | 用户明确设置时间后再读取；不要把默认值当真 |
| 电池百分比跳动 | ADC 电压曲线不是 fuel gauge | 滤波显示并标注估值；需要精度就增加真实计量芯片 |
| 带前光版充电状态反了 | GPIO11 极性未随 LM3630A 探测更新 | 使用 `configureChargeStatus()`/标准启动器 |
| 无前光机 GPIO12 被拉高 | 应用绕过了 runtime probe | 只通过 `FrontlightManager`，先检查 `present()` |
| BLE 扫描后内存持续减少 | 未清结果或未 deinit | `clearResults()` 后 `NimBLEDevice::deinit(true)` |
| Wi‑Fi 测试改变了用户配置 | 使用了擦除 AP 的 disconnect 参数 | 测试使用 `disconnect(true, false)`，不持久化密码 |
| 打开串口后页面回到菜单 | native USB CDC 打开触发 MCU 复位 | 一个会话内完成切页、`ui state` 和截图 |
| 单项测试立即到报告页 | 页面状态机未保持等待输入 | 参照 `DiagnosticApp` 的 `View` 和 `waiting_for_input=1` |
| app 刷完不启动 | 把 app-only 镜像写到 `0x0` 或分区表不匹配 | 首装用完整镜像；app-only 仅写 `0x10000` 且先校验分区 |
| 看似“睡死” | 深睡前没有 timer wake 或 GPIO4 hold | 只使用 3–60 秒的 `enterTimedDeepSleep()` |

## I²C 排错

EEGO A4 正常无前光版本应至少看到：

```text
0x40 GSLX680
0x51 PCF8563
```

带前光版本还可能有 `0x36 LM3630A`。扫描只发送空 payload ACK。不要对未知地址
随意写 `who-am-I` 寄存器；相同地址可能对应不同器件，写操作可能有副作用。

## 串口排错

1. 确认数据线而不是充电线。
2. 关闭其他占用 `<PORT>` 的 monitor。
3. 重新列出端口；ESP32-S3 复位时设备名可能短暂消失。
4. 使用 115200 baud。
5. 不要让日志永久等待 `while (!Serial)`；模板最多等待 1.5 秒。

## 构建排错

先执行：

```sh
python3 scripts/validate_project.py --quick
pio run -e eego_a4_quickstart
```

若依赖状态异常，可删除 PlatformIO 的该环境构建缓存后重编，但不要删除
`release/`、设备备份或整个工程。不要把“升级到最新版平台”当成第一排错步骤；
当前版本已经锁定并验证。

## 何时回退

如果出现持续 boot loop、USB 不枚举、分区表未知或完整镜像误写，停止继续尝试
随机 offset，按 [RECOVERY.md](RECOVERY.md) 使用已保存的 16 MiB 备份恢复。
