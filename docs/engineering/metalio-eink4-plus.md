# Metalio E-Ink 4 Plus 适配记录

最后核对日期 2026-11-18。Metalio E-Ink 4 Plus 已通过原厂同机恢复、S31 独立构建及使用临时 FreeInk SDK 的 BSP 联编。完整 CrossMux 固件和面板实测待完成。

## 硬件事实与资料

| 项目 | 已有证据 | 真机待核对 |
| --- | --- | --- |
| 整机 | 实机「关于」页显示 `metalio-e-ink4-plus`，原厂固件 `2.0.79`，编译时间 `Nov 11 2026 20:06:41`（页面未标时区）。厂商有同版本公开源码提交 `14aa1d06`。 | PCB 版号、屏排线，以及实机二进制与公开构建的差异。 |
| 平台 | 实机「关于」页显示 ESP32-S31 双核、16.0 MB Flash、16.0 MB PSRAM。ROM 确认 ESP32-S31 rev v0.0 和 16 MiB Flash。已读回启动分区表、原厂应用及 ROM 恢复入口。厂商文档注明 Octal PSRAM，使用 ESP-IDF 6.1。 | CrossMux 的 PSRAM 初始化、可用内存和运行稳定性。 |
| 屏幕 | 本机 2.0.79 备份匹配原厂 `E0470A14-AF 34Pin` 初始化标记及 GC／GL／DU 表，源文件为 1216×684、8 位并行输出、TPS65185 和 TCA9555。厂商 README 另记 ED047TC2 系列。 | PCB 与实物排线丝印、VCOM 实际电压、波形使用范围及上电／掉电时序。 |
| 输入及存储 | 厂商参考为 FT6336U 触控、盖板虚拟键、通过 TCA9555 读取的按键、4-bit SDMMC。 | 坐标、方向、共享复位和电源、按键映射、存储读写及缺卡恢复。 |
| 电源及前光 | 厂商参考包含 BQ27220 电量计、CX25601N 充电器、PCF8563 RTC、双路前光以及 USB Serial/JTAG 和 USB HS MSC。用户确认双路前光纳入首版。 | 实机前光配置、亮度和色温调节，休眠熄灯和唤醒恢复。待核实剩余电量、充电、外部供电和 USB 调试各自的信号来源，以及休眠／唤醒、欠压保存和电池统计能力。电流与续航仍待测量。 |

- [厂商中文说明](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/main/README_zn.md)
- [厂商引脚配置](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/main/main/boards/metalio-e-ink4-plus/config.h)
- [厂商屏幕板级驱动](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/main/main/boards/metalio-e-ink4-plus/display/epd_board_metalio_eink4_plus.c)
- [kiikoread 独立实现及其版本说明](https://github.com/wegooo-cell/Metalio-S31-kiikoread)

## 基线与实现顺序

- 当前工作分支 `codex/metalio-eink4-plus-fork` 基于 `ar0c/crossmux:main` 的 `edf43c5efbf2667f01344a978333ad5b92c80efc`，`platformio.ini` 声明固件 `1.6.5`。该分支的推送远程为 `fork`。当前 `.gitmodules` 指向 `ar0c/freeink-sdk`，gitlink 固定为 `ab5c45b01c230266f144caad7ffeb25037b64bfd`，嵌套 Lucide 固定为 `c81680e066f45b640743ca78ae36cdedda3f0318`，均已回读。
- 原上游参考基线 `0x1abin/crossmux:main` 为 `efee672b612c8e2319f27b4f2a2a861d2e627229`（固件 `2.0.0`），原分支 `codex/metalio-eink4-plus` 和 SDK pin `24195eb26201f193f1b25a21c3360e90791ba2d2` 供对照。个人 fork 的现有独立 `main` 工作树保持原状。
- CrossMux 固定的 `pioarduino` 55.03.311 对应 Arduino 3.3.11／ESP-IDF 5.5.5；厂商固件使用 ESP-IDF 6.1。先确认 ESP32-S31 独立构建入口、工具链与组件兼容性，然后核对固定 FreeInk SDK 的并行屏、GPIO、存储和电源能力。已有 S3 的 `metalio_eink4` 仅用于对照 CrossMux 接口，Plus 有独立的硬件和镜像身份。编译与最小串口启动通过后，再按供电依赖接入屏幕、输入、存储、阅读和休眠。
- S31 探针已使用 [Arduino-ESP32 4.0.0-RC1](https://github.com/espressif/arduino-esp32/releases/tag/4.0.0-RC1) 编译，实际镜像报告 ESP-IDF `v6.1-803-g94639ab7251`。应用依赖仍待验证。[Espressif Arduino PR #12677](https://github.com/espressif/arduino-esp32/pull/12677) 跟踪 S31 支持。默认 Waveshare 目标继续使用 [pioarduino 55.03.311](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.311)。本机原生 ESP-IDF 6.2 用于独立的工具链验证，FreeInk SDK gitlink 保持固定。
- GitHub CLI 已验证 `ar0c` 登录，已有 `ar0c/crossmux` fork，父仓库为 `0x1abin/crossmux`，当前账号有推送权限。
- 首版目标是显示刷新、触控／翻页键、microSD／EPUB、休眠唤醒和双路前光。原上游参考基线通过 `lib/hal/HalFrontlight.cpp` 管理板级前光，`src/main.cpp` 在睡眠前关闭前光并在启动时读取恢复设置。参考 SDK `24195eb2` 已有 FT6336U 和双路 PWM 前光能力（`libs/hardware/BoardConfig/include/BoardConfig.h:1998`、`:2103`、`:2625`）。参考并行屏适配器 `EpdiyLcdDriver` 使用 S3 的 `LCD_CAM`、GDMA 与 RMT（`libs/display/EpdiyLcd/library.json:5`，`src/epdiy/src/output_lcd/lcd_driver.c:574`），S31 后端和当前 fork 可复用接口需独立验证。

## S31 构建与板级接入

- `platformio.ini` 的 `metalio_eink4_plus_platform` 将 [pioarduino 61.04.00-RC1](https://github.com/pioarduino/platform-espressif32/releases/tag/61.04.00-RC1) 单独固定到 S31 验证目标。该版本使用 Arduino 4.0.0-RC1 与 ESP-IDF 6.1，包含 16 MB Flash／Octal PSRAM 的 `esp32-s31-coreboard-1`。此板配置只用于 CPU 和编译链验证。探针使用默认 QIO／OPI 配置与 USB Serial/JTAG；CoreBoard 整包的应用起址为 `0x10000`，只作离线构建产物。Metalio 原厂启动应用起址为 `0x80000`。
- 当前固定 SDK `ab5c45b0` 的 `BoardConfig.h:115` 未定义 S31 系列，`EpdiyLcd/src/epdiy/src/output_common/render_method.h:7` 将并行屏后端限制在 S3。可保留 `FreeInkDisplay.cpp:278` 的 `EpdiyLcdDriver` 刷新接口，在 SDK 内为 S31 增加独立板级配置及输出。原厂 2.0.79 已有 S31 输出参考。1216×684 紧密排布的单个 1 bpp 缓冲为 103,968 字节，单个 4 bpp 缓冲为 415,872 字节，尚无 DMA 或峰值分配的实机测量。 [原厂 S31 输出驱动](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/boards/metalio-e-ink4-plus/display/output_lcd/lcd_driver.c#L73)
- 原厂 2.0.79 的 I²C SDA/SCL 为 GPIO0/1。FT6336U 地址 `0x38`、中断 GPIO5，复位由 TCA9555 P1.2 控制并与其他外设共享。双路前光候选 PWM GPIO6/7，四线 SDMMC 的 CLK/CMD 为 GPIO24/25，D0–D3 为 GPIO20–23。 [原厂固定引脚配置](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/boards/metalio-e-ink4-plus/config.h#L42)
- 在本机已校验的 2.0.79 Flash 备份中，解析 `ota_0` ESP 镜像的载入段。原厂 `k_epd_bus_pins` 对应的 16 个小端 32 位值 `8,9,10,11,12,13,14,15,16,17,18,4,35,40,19,37` 在段 0 的 Flash 偏移 `0xec870` 唯一匹配。旧版注释中的 XSTL GPIO19／MODE GPIO36 组合无匹配。由此确认本机原厂镜像的 XSTL GPIO4／MODE GPIO19，PCB 丝印、屏号、电压测量及实板时序仍待确认。 [原厂引脚表定义](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/boards/metalio-e-ink4-plus/display/epd_board_metalio_eink4_plus.c#L411)
- 原厂软件在 `screen:black_depth` 存储索引 0／1／2，分别请求 VCOM 幅值 1200／1600／2000 mV，键缺失时用索引 0。本机 2.0.79 备份未设置该项，因此恢复镜像的预期请求值为 1200 mV。仅将此值用于建立本机原厂对照，实板输出、面板标称 VCOM 和当前运行期间的值仍待确认。 [原厂黑度映射](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/display/epd_refresh.cc#L58) · [原厂设置读取](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/display/epd_refresh.cc#L586)
- BQ27220、CX25601N 和 PCF8563 属原厂硬件参考。电池统计继续以供电、充电、USB 三者已确认的区间为准，且纳入欠压保存与重启回读验收。当前电流、功耗、休眠时基和供电状态的能力仍待验证。

### SDK 联编与面板证据

- 在已校验的 `ota_0` 载入段中独立比对固定厂商提交的字节。初始化标记 `EPDiy init E0470A14-AF 34Pin` 见 Flash 偏移 `0xaf094`，GC 表的 3008 字节显式初值、GL 表 3072 字节及 DU 表 1280 字节分别在 `0x118e38`、`0x11822c` 和 `0x119a38` 唯一匹配。该结果确认原厂软件选用的面板及原始表，实际排线和时序仍待验证。 [原厂显示初始化](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/boards/metalio-e-ink4-plus/display/metalio_epd.c#L31) · [原厂面板定义](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/boards/metalio-e-ink4-plus/display/displays.c#L76)
- 临时 SDK 的 `FREEINK_DEVICE_METALIO_EINK4_PLUS` 提供独立 S31 配置、1216×684 PSRAM 帧缓冲、四线 SDMMC、BQ27220 参数和 GPIO6/7 双路前光。前光沿用原厂 20 kHz、10 位、总 PWM 上限 10%。触控共享复位、TCA9555 按键、电源管理及实际 SD 挂载仍待接入或验证。 [原厂前光频率](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/boards/metalio-e-ink4-plus/periph/frontlight.h#L44) · [原厂前光限幅](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/boards/metalio-e-ink4-plus/periph/frontlight.cc#L23)
- S31 后端采用原生 8 位 RGB、AXI GDMA 和 RMT，按每字节高位优先打包 2 位驱动值。扫描超时、中途失败和缺行均返回错误，禁止推进软件刷新基准。S31 调用必须提供匹配的面板定义、波形和电源钩子。适配探针仅链接这些入口。原厂运行时波形加工、实机扫描同步和时间预算仍待验证。
- 两个 64 槽内部 SRAM 线队列存放 38,912 字节像素输出，两个 8 行 DMA 缓冲共 4,864 字节，另外还有任务栈、信号量、描述符和临时转换区。联编镜像报告静态 RAM 42,852 字节、程序 403,542 字节。峰值堆内存、最终应用分区余量和功耗待测。

联编使用 [FreeInk SDK Draft PR #1](https://github.com/ar0c/freeink-sdk/pull/1) 的 `codex/metalio-eink4-plus-s31`，提交 `8764655de14f9ce2f5051304bbdab01608af48fa`。以下命令在个人 fork 的 CrossMux 功能分支根目录执行，SDK 工作树须干净。根仓库中已提交的 gitlink 保持原样。

```sh
git submodule update --init --recursive
git -C freeink-sdk fetch https://github.com/ar0c/freeink-sdk.git refs/heads/codex/metalio-eink4-plus-s31
git -C freeink-sdk switch --detach 8764655de14f9ce2f5051304bbdab01608af48fa
git -C freeink-sdk submodule update --init --recursive
pio run -e metalio_eink4_plus_sdk -e metalio_eink4_plus_platform -j 3
python -m esptool image-info .pio/build/metalio_eink4_plus_sdk/firmware.bin
```

上游 CrossMux 的固定 SDK 为 `0x1abin/freeink-sdk` 的 `24195eb2`，与本次验证用的个人 SDK 分叉。上游草稿通过临时 SDK checkout 评审联编入口，正式集成需要将 SDK 变更移植到上游对应基线并按该固定引用验证。

## 复现与验收状态

| 验证项 | 状态 | 证据或下一步 |
| --- | --- | --- |
| Fork 基线与工作树 | 通过 | `git ls-remote --exit-code fork refs/heads/main` 核对 `edf43c5e`，在独立工作区建立 `codex/metalio-eink4-plus-fork`，核对工程记录哈希未变。 |
| 固定 SDK 源码 | 通过 | 使用个人 fork 的已核实本地缓存，`git submodule status --recursive` 回读 FreeInk SDK `ab5c45b0`、Lucide `c81680e`，与各级 gitlink 相同。 |
| 现有 Windows 工具版本 | 通过 | PlatformIO Core 6.2.0、Python 3.13.14、esptool 5.5.0。原生 ESP-IDF 6.2.0，提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`。在当前 shell 将已安装的 `idf6.2_py3.13_env/Scripts` 加到 PATH 首位，再执行 IDF 的 `export.ps1`。 |
| 原生 S31 最小构建 | 通过 | 在忽略的 `build/metalio-s31-toolchain/hello_world/` 复制官方 `examples/get-started/hello_world`，执行 `idf.py -B ../out -DIDF_TARGET=esp32s31 reconfigure`、`cmake --build ../out --parallel 3` 和 `python -m esptool image-info ../out/hello_world.bin`。配置和编译均返回 0，镜像识别为 ESP32-S31（Chip ID 32），150240 字节，SHA-256 `f787ac946a5970d7acb3eeb28bdcc78c1c802bced141ce9a3b435bae3b70fb2c`。日志 `build/metalio-s31-toolchain/build.log`。仅离线验证。 |
| Arduino S31 平台探针 | 通过（离线） | `pio run -e metalio_eink4_plus_platform -j 3` 仅编译 `src/debug/MetalioEink4PlusPlatform.cpp`。检查芯片目标、Arduino 主版本及 RGB LCD、RMT、SDMMC、LEDC 能力。`python -m esptool image-info .pio/build/metalio_eink4_plus_platform/firmware.bin` 确认芯片 ID 32、16 MB Flash、镜像哈希有效；346,304 字节，SHA-256 `3d42d4db4b6c5dfe9659b5cd1324e4bf7bc8f2cc407fb5f6b9e250dca4046132`。本次未刷写探针。 |
| S31 BSP 联编 | 通过（临时 SDK） | `pio run -e metalio_eink4_plus_sdk -e metalio_eink4_plus_platform -j 3` 通过。`esptool image-info` 确认 SDK 探针为 S31，镜像 SHA-256 `d4577727702b571a56c0170083618a2eedf2e8a988f95c7935084cd3d1c1ce1c`。 |
| SDK 显示宿主回归 | 通过 | 在 Ubuntu 运行 `test_s31_packing.py`、`test_transactions.py`、`test_waveform_trim.py`。覆盖高位优先打包、掩码及现有 Read Pico 的刷新事务、资源失败和原有波形一致性。 |
| CrossMux S31 主程序和 FreeInk | 待验证 | 面板后端及板级配置接入后再构建。 |
| CI 选择与发布回归 | 通过 | `python -m unittest discover -s scripts/tests -p test_ci_plan.py` 通过 12 项，`python -m unittest discover -s scripts/tests -p test_nightly_release.py` 通过 39 项。默认目标及发布矩阵均为 Waveshare 3.97。 |
| 探针 C++ 格式 | 通过 | `clang-format --dry-run --Werror src/debug/MetalioEink4PlusPlatform.cpp`，版本 21.1.8。 |
| 完整固件回归与 CI | 待验证 | 完整 S31 应用可构建后运行 `./bin/ci-check` 与受影响目标回归，共享层变化覆盖 X3/X4 基线。 |
| USB 调试端口归属 | 通过 | 11 月 18 日 15:12:13（新加坡时间），用户唤醒后 `COM26` 重新枚举。用 `pnputil /enum-devices /connected /bus USB /properties` 核对接口的 USB 父设备 `303a:1001`，其序列标识与同次拍摄的「关于」页 MAC 一致。仅保存匹配结果。 |
| 原厂恢复镜像的 VCOM 请求值 | 通过（离线） | 核对完整备份 SHA-256 和实际分区表后，用 ESP-IDF 6.2 的 `components/nvs_flash/nvs_partition_tool/nvs_parser.py` 在内存解析 `0x9000` 起的 16 KiB NVS。校验有效页头及目标记录状态，只查 `screen: black_depth`。该项缺失，原厂 2.0.79 使用默认索引 0，即请求 1200 mV。 |
| 原厂运行中的 VCOM | 待验证 | 再次唯一匹配 `COM26` 后，以 115200、DTR/RTS 关闭的只读监听方式接收 15,560 字节。90 秒内没有匹配到原厂 `TPS PowerOn ok vcom=… mV`。电压未测量。 |
| CrossMux 串口传输与休眠复连 | 待验证 | 已观察到 Plus 的 USB 端口在原厂固件运行时消失，并于唤醒后枚举。ROM 传输已在原厂恢复中验证。CrossMux 运行日志和休眠复连仍待验证。 |
| 原厂同机恢复 | 通过 | 已完成 2.0.79 整档备份、同机写回、全量摘要校验和硬件复位。用户确认原厂界面、2.0.79 版本、触控和书架正常。 |
| CrossMux 真机验收 | 待验证 | 待核实板号及排线、验证启动、显示、触控和按键、SD 与阅读、双路前光、休眠唤醒、堆内存与功耗。 |
| SDK 和 CrossMux PR | 待验证 | SDK 已建 Draft，CrossMux 依赖集成待验证。上游草稿采用其 `main` 的独立分支，当前复现需临时 SDK checkout。维护者协调、正式依赖固定及验收均待完成。 |

## 原厂 2.0.79 恢复预检

- [原厂 2.0.79 源码](https://github.com/CloudZao/Metalio-E-INK4-Plus/commit/14aa1d06d15a46435f234aa14f3cffa478ed2802) 固定为 `14aa1d06d15a46435f234aa14f3cffa478ed2802`。GitHub 公开 Releases 和 Tags 为空。[官方烧录目录](https://burner.cloudzao.com/en/registry) 的检索快照列有 CloudZao 官方发布的 Plus `2.0.74`（15.65 MB）。在线下载、镜像段、板修订适配及校验值均未核实。该记录是降级候选，未取得同版本 `2.0.79` 出厂整包。
- RickyOS 的[安装工作台](https://rickyaistudio.com/install.html)提供完整 Flash 备份选项，其[说明](https://rickyaistudio.com/licenses.html)表明恢复刷写暂不支持。kiikoread 的 [Metalio S31 文档](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/main/docs/BOARDS.md)描述它自己的镜像和分区。两者均未提供已核实的本机原厂 2.0.79 恢复包。
- [同版本分区表](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/partitions/v1/16m.csv) 含 `ota_0` 和 `ota_1`（各 5 MiB）、`otadata`、NVS、`model`、`resources` 与 `font_data`。应用起址是 `0x80000`、`0x580000`。同版本 [sdkconfig](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/sdkconfig#L995) 未启用启动失败自动回滚。上述是公开构建值，真机分区和安全状态须独立读取。CrossMux 现有 `partitions.csv` 的应用起址不同，Plus 镜像不能沿用现有目标的偏移。
- [厂商 TF 卡升级](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/README_zn.md) 使用应用镜像并依赖现有更新程序。原厂引导、分区表或资源被替换后，恢复需要适配该板的 USB 整包，或本机的完整 Flash 备份及经过核对的 ROM 恢复入口。
- [2.0.79 板级配置](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/14aa1d06d15a46435f234aa14f3cffa478ed2802/main/boards/metalio-e-ink4-plus/config.h#L114) 将 BOOT 接到 GPIO61。[乐鑫 ROM 说明](https://docs.espressif.com/projects/esptool/en/latest/esp32s31/advanced-topics/boot-mode-selection.html) 要求复位时该引脚为低。整机按键进入 ROM 的方式和 ROM 端口仍需实测。
- 启动实机备份前，验证 Plus 的 ROM 下载模式、端口归属和 `esptool --no-stub get-security-info`。如允许读取，在同一 ROM 会话确认 Flash 容量后，使用 `read-flash` 从 `0` 读完整 Flash。若实读为 16 MiB，备份应为 16,777,216 字节。检查分区表完整性、选中应用的 `2.0.79` 版本、镜像校验和和整档 SHA-256，再用设备读回或 `verify-flash` 复核。microSD 书库、进度和设置另备份。备份只保存在本地且不提交。
### 原厂 2.0.79 同机恢复实测

- 2026-11-18 通过 Windows `COM26` 的 ESP32-S31 原生 USB Serial/JTAG 进入 ROM。设备返回 ESP32-S31 rev v0.0，Flash ID 确认为 16 MiB，安全标志 `0x0`，Secure Boot 和 Flash 加密均关闭。
- 使用 esptool 5.5.0 从地址 `0x0` 读取 16,777,216 字节，`verify-flash` 与实机摘要匹配。离线校验引导与应用镜像、分区表 MD5 和 `otadata`，启动槽为 `ota_0`（`0x80000`），应用为 `Metalio-E-Ink4-Plus` 2.0.79，编译时间 `Nov 11 2026 20:06:41`，IDF `v6.1-dirty`。
- 以 `write-flash --flash-mode keep --flash-size keep --flash-freq keep 0 <已验证的同机备份>` 写回全部 16 MiB，耗时 79.4 秒。独立执行 `verify-flash 0 <备份>`，整档摘要匹配后发送硬件复位。没有独立擦除步骤，未操作 eFuse 或 microSD。此次验证对同一台设备、同一份备份有效。
- 包含设备配置的备份私存于 Windows `Documents/EmbeddedBackups/Metalio-E-Ink4-Plus/2026-11-18-stock-2.0.79/`。整档 SHA-256、读取、离线检查与写回日志均随备份保存在本地。用户实机确认原厂界面、2.0.79 版本、触控和书架正常。
