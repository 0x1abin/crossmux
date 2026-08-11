# EEGO A4 全硬件诊断指南

## 1. 推荐测试顺序

1. 保持 USB 连接，打开 115200 波特率串口。
2. 确认屏幕出现 `EEGO A4 LAB`，而不是保留刷写前画面。
3. 运行 `run overview`，保存 `report json` 输出。
4. 运行 `run all` 完成不需要人体操作的自动测试。
5. 单独完成 `run touch` 和 `run buttons`。
6. 运行 `run fonts`，翻完 3 页，核对中英文、六档字号和标点。
7. 插拔 USB 电源，分别执行 `run battery`，观察充电脚变化。
8. 肉眼检查全刷、快刷、灰阶和连续刷新残影。
9. 只有需要验证低功耗时，最后才执行 `sleep 5`。

发生异常时先保存串口日志，不要立即重复烧写。屏幕内容静止不代表 MCU 没运行：墨水屏断电后会保留旧画面，USB 日志是区分“启动失败”和“屏幕总线失败”的关键。

## 2. 屏幕菜单

| 菜单 | 内容 | 通过标准 |
|---|---|---|
| Run all automated tests | Display、内存、电池、SD、RTC、I²C、前光探测、Wi-Fi、BLE、USB | 无 FAIL；交互项仍需手测 |
| Hardware overview | 启动能力、RTC、I²C、内存与电池摘要 | 核心器件均识别 |
| Touch grid test | 3×3 九宫格 | 9 格全部命中，左右/上下与手指一致 |
| Buttons + front key | 三侧键、屏下键短/长按 | 五项全部命中 |
| E-paper waveforms | R60/12 px/R48 + 28 px 矩形内容区、Full、Fast、四灰阶、黑/白场 | 圆角贴合、矩形完整、无错位，灰阶可分辨 |
| Fonts: Chinese + English | SD MiSansA4 8/10/12/14/16/18 pt，中英文、数字、混排、标点 | 六文件可读、必需中英文字形无缺失、三页实际渲染正常 |
| SD card + RTC | 临时文件 R/W/verify/remove，RTC 时间 | 文件校验通过；RTC 无 VL 或设时后通过 |
| Battery + charging | 电压、估算百分比、ADC、充电脚 | 电压合理；插拔电源时充电状态符合实机 |
| Wi-Fi radio scan | 2.4 GHz 扫描 | 扫描 API 成功；附近无 AP 时 0 个也可接受 |
| Bluetooth LE scan | 5 秒被动扫描 | 扫描 API 成功；附近无 advertiser 时 0 个也可接受 |
| I2C, sensors + memory | 全总线 ACK、可选前光、1 MiB PSRAM 图案 | `0x40`、`0x51` 存在；内存图案通过 |

除 `Run all` 和 `Hardware overview` 本来就需要显示总报告外，所有单项自动测试都会停留在专属详情页。详情页触摸 `BACK` 返回、`RETEST` 重测；屏下键等同 Back，Power 等同 Retest。页面不会超时，也不会把触摸转发给隐藏的主菜单。

`E-paper waveforms` 是六阶段人工流程：

1. 逻辑画布最外沿的安全区域：贴边 12 px 黑色边框、外轮廓
   `R=60 px`，内侧圆角安全区 `R=48 px`，并显示 28 px、496×712 的零圆角
   普通 UI 矩形；
2. Full 网格和边界；
3. Fast 棋盘与差分残影；
4. 四灰阶 bit-plane；
5. 黑场均匀性；
6. 白场与坏点。

Power、Down 或触摸进入下一阶段，Up 返回上一阶段，屏下键退出。最后一次继续操作才进入持久化的 Display Result 页。
边框宽度和圆角半径按 552×768 逻辑像素计算，旋转到面板时保持一比一；
黑带内侧的白色 R48 区域是物理安全上限；正文和控件统一放在其中的
28 px、496×712 零圆角矩形。背景和必须覆盖面板的硬件测试可以越界，但必读
内容不能越过矩形；完整规则见
[安全显示区域规范](SAFE_AREA.md)。该页会停留，应比较四个物理
角是否都与曲线贴合。可输入 `screen safe` 直接打开。

`Fonts: Chinese + English` 也是不会自动跳页的三阶段流程：

1. MiSansA4 8/10/12 pt 的英文与中文；
2. MiSansA4 14/16/18 pt 的英文与中文；
3. 14 pt 英文句子、简体中文、中英混排、数字、符号和中文标点。

Power、Down 或普通屏幕触摸进入下一页，Up 返回上一页，屏下键退出；第三页按 Power 只确认完成，内容仍保留。该页真实读取 SD
`/fonts/MiSansA4/*.cpfont`，控制标题才使用内建 ASCII。若字体文件缺失，仍会用内建字体显示明确错误，不会黑屏。

状态字符：

```text
+ PASS
! WARN
X FAIL
- NOT_FITTED
> RUNNING
? NOT_RUN
```

`NOT_FITTED` 对当前 EEGO A4 的前光是正确结果，不是故障。`WARN` 通常表示需要人工确认、RTC 尚未设时、USB 控制台尚未收到命令，或扫描未发现附近设备。

## 3. 串口命令

### 通用

```text
help
status
ui state
report json
screenshot
menu
reboot
```

- `status` 输出人类可读结果。
- `ui state` 输出当前真实页面状态和 `waiting_for_input`，用于验证页面没有自动跳转。
- `report json` 每次输出一行 JSON，适合重定向到测试记录。
- `screenshot` 返回当前完整 1-bit framebuffer，供主机保存和像素级复核。
- `menu` 返回主屏。
- `reboot` 软件重启，不改设置。

所有命令也接受 `CMD:` 前缀，例如 `CMD:screenshot`，便于复用 CrossPoint
主机调试工具。

### Framebuffer 截图

`screenshot` 的二进制协议为：

```text
SCREENSHOT_START:52992
<恰好 52992 字节>
SCREENSHOT_END
```

52992 = 768 × 552 ÷ 8。原始字节是 UC8279C controller-landscape 顺序；
主机按 768×552 的 1-bit 图像解释后顺时针旋转 90°，得到用户看到的
552×768 竖屏画面。传输按小块重试，并只在截图期间提高 USB CDC TX
超时到 250 ms，随后恢复为普通状态/JSON 输出使用的有限 50 ms；主机必须持续
读取至声明长度，再解析 footer。CrossPoint 的
`scripts/debugging_monitor.py` 已同时识别 800×480 与 EEGO A4 768×552
framebuffer。

这种方法读取的是连接实体机 MCU RAM 中的真实 framebuffer，不是模拟器，
但也不是相机拍到的实体面板。它能定位 renderer、字体和布局问题，不能单独证明
SPI 传输、刷新波形或残影正常。完整的分层诊断方法、主机采集脚本和证据留存
规范见 [实体设备 Framebuffer 回读调试](FRAMEBUFFER_DEBUGGING.md)。

### 测试

```text
run all
run overview
run display
run safe
run fonts
font page 1
font page 2
font page 3
run gray
run touch
run buttons
run sd
run battery
run rtc
run wifi
run ble
run i2c
run frontlight
run memory
```

### 屏幕

```text
screen full
screen safe
screen fast
screen gray
screen checker
screen black
screen white
```

`screen safe` 使用 Full 并停留在“贴边 12 px、外轮廓 R60、内轮廓 R48、
28 px 零圆角 UI 矩形”的安全区域页。
`screen black` 和 `screen white` 使用 Full，适合观察坏点/行列缺失。
`screen fast` 使用轻量
快刷；驱动内部会在第五次连续 Fast 自动 Full 清残影。

### 字体

```text
run fonts
font page 1
font page 2
font page 3
```

`run fonts` 先校验六个 CPFONT v4 文件、英文/简体中文必需字形覆盖以及字形 bitmap
读取，再停在第 1 页。`font page N` 可直接重开指定页。期望 SD 路径：

```text
/fonts/MiSansA4/MiSansA4_8.cpfont
/fonts/MiSansA4/MiSansA4_10.cpfont
/fonts/MiSansA4/MiSansA4_12.cpfont
/fonts/MiSansA4/MiSansA4_14.cpfont
/fonts/MiSansA4/MiSansA4_16.cpfont
/fonts/MiSansA4/MiSansA4_18.cpfont
```

每档当前应为 18,736 glyph、2,045 Unicode interval。测试 PASS 证明文件结构、必需中英文字形和 bitmap I/O
成功。第 2 页的 18 pt 英文页面样例为避免越过右边框而缩短，完整
`0123456789` 覆盖仍显示在第 3 页。字体是否在实体面板上视觉正常仍需翻完
三页人工确认。

### Wi-Fi

```text
wifi connect SSID|PASSWORD
```

连接测试只持续到获得地址或 15 秒超时。SSID/密码不会写 NVS，结束后 `WiFi.disconnect` 并关闭 radio。SSID 中可以包含空格，但当前简易协议不支持包含 `|` 的 SSID。

### RTC

```text
rtc set 2026-07-25T23:30:00
```

写入 PCF8563 的 BCD 时间并清除 VL。命令只检查基本字段范围，不做每月天数/闰年完整校验，请输入真实日期。

### 可选前光

```text
frontlight 10 50
```

参数为亮度和暖色比例，均为 0–100。只有 LM3630A 在 `0x36` ACK 后才执行；驱动约 1.2 秒后自动关闭。当前已知零售机没有前光，预期结果是 `NOT_FITTED`。

### 深睡

```text
sleep 5
```

参数会被限制到 3–60 秒。进入睡眠前屏幕 controller deep-sleep、GPIO 4 保持高、timer wake 启用。自动测试永远不会调用此命令。

## 4. 分模块判读

### 屏幕

自动 `PASS` 只代表 framebuffer 分配成功且驱动调用完成，无法替代肉眼检查。必须确认：

- 开机后实体屏发生刷新，不是旧画面；
- 竖屏方向正确，没有 90°/镜像错误；
- 外边框四边完整；
- 棋盘格没有整行/整列断裂；
- 四灰阶图从左到右能看到四种 bit-plane 结果；
- 连续四次 Fast 后，第五次明显执行更彻底的清残影刷新。

若串口显示 refresh 完成而屏幕不动，优先检查“SD 与屏幕错误复用全局 SPI”的问题。本模板已用独立 HSPI 修复。

### 触控

九宫格必须从手指实际点击的位置点亮。特别检查：

- 左上不会点亮右上；
- 左下不会点亮右下；
- 四角和左右边缘可达；
- 普通屏幕触摸不会被误判为屏下键；
- 屏下键不会在九宫格角落产生幽灵触点。

日志同时输出 logical 和 native normalized 坐标，可用于回归：

```text
[TOUCH] logical=(x,y) native_norm=(nx,ny) cells=0x...
```

### 实体键与屏下键

Up/Down 是低有效，Power 是高有效。三键都应只命中对应行。屏下键：

- 快速按下释放只命中 SHORT；
- 持续超过 700 ms 命中 HOLD；
- 长按事件只发生一次。

### SD

测试不会格式化卡，临时文件必须经历写入、读回完全相等、删除三个步骤。失败日志会显示三个布尔值。没有插卡时 FAIL 是正常现场条件，但不能据此否定 SPI 引脚。

### RTC

`PCF8563 ACK but VL...` 表示芯片存在但后备电源中断过，使用 `rtc set` 后重测。设时后立即重启并读取，时间应继续前进。

### 电池与充电

电压是分压 ADC 估计值。单节锂电通常应落在约 3.0–4.3 V；明显为 0、接近 ADC 满量程或随屏幕刷新大幅跳变都要进一步测量。请记录：

```text
仅电池供电：mV / ADCraw / CHG_GPIO11
USB 已插入：mV / ADCraw / CHG_GPIO11
充满后：    mV / ADCraw / CHG_GPIO11
```

每个阶段稳定后可以直接执行：

```text
charge capture battery
charge capture charging
charge capture full
report json
```

也可以完全脱离 USB 串口操作 `Battery + charging` 页面：Up 保存“仅电池”、
Down 保存“正在充电”、Power 保存“已充满”，屏下键返回；底部三个触摸按钮
执行相同操作。这样拔掉 USB 后仍能采集 battery 阶段。

样本只保存在 RAM，`charge reset` 可清空。无前光型号的二进制极性规则为
低有效，因此预期三阶段图案是 inactive/active/inactive；有 LM3630A 的型号
使用相反电平。这样才能最终确认 GPIO 11 是“正在充电”还是“外部电源存在/
充电完成”的具体语义。

### I²C 与传感器

核心期望地址：

```text
0x40 GSLX680
0x51 PCF8563
0x36 LM3630A（仅可选前光机型）
```

扫描到其他地址先记录，不应仅凭地址认定器件型号。`0x44`、`0x6a`、`0x6b` 只是候选提示。

### Wi-Fi/BLE

“扫描完成且 0 个设备”仍说明 radio/API 正常；若要验证射频接收，测试场地必须有已知 AP 或 BLE beacon。BLE 测试只覆盖 LE。ESP32-S3 不具备 Classic Bluetooth，不能把 Classic 扫描失败当作板故障。

### USB

屏幕启动并不代表 USB RX/TX 已闭环。打开 monitor，输入 `status`；固件收到任一命令后 USB 项变为 PASS。若设备端口根本不出现，先尝试进入 ESP32-S3 ROM download mode，再按恢复文档处理。

## 5. 建议保存的测试记录

每块机器至少保存：

```text
日期与机器编号
固件 SHA-256
完整启动日志
run all 日志
report json
九宫格命中结果
按键五项结果
USB 插拔前后电池读数
屏幕 Full/Fast/Gray 肉眼结果
字体三页中英文/字号/标点肉眼结果
I2C 地址列表
Wi-Fi/BLE 已知信号源结果
深睡唤醒与静态电流（若测试）
```
