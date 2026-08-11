# EEGO A4 从零开发指南

本指南假设读者会使用终端、能看懂基础 C/C++，但没有 ESP32、PlatformIO 或
电子纸项目经验。照顺序执行即可完成“验证源码 → 编译 → 备份设备 → 修改示例 →
调试”的完整闭环。

## 1. 先认识交付内容

工程中有三类可运行代码：

1. `eego_a4_diagnostics`：完整、非破坏性的硬件诊断固件，也是默认环境。
2. `eego_a4_quickstart` 等五个示例：用于学习单项 API。
3. `release/<版本>/`：给最终用户安全刷写的 app/full 二进制，不是开发源码。

第一次开发应从 `eego_a4_quickstart` 开始。不要从 1,800 多行的
`DiagnosticApp.cpp` 复制代码，也不要直接修改 UC8279C LUT 或 GSLX680 固件表。

## 2. 安装工具

需要：

- Python 3.10 或更高版本；
- PlatformIO Core；
- Git（仅在获取或版本管理工程时需要）；
- 一根支持数据传输的 USB 线。

PlatformIO 官方支持通过 pip 安装：

```sh
python3 -m pip install -U platformio
pio --version
```

如果 macOS 上 `pio` 没有进入 `PATH`，常见实际路径是：

```sh
~/.platformio/penv/bin/pio --version
```

工程固定了 pioarduino 平台、Arduino-ESP32、SdFat、Adafruit GFX 和
NimBLE-Arduino 版本。不要在第一次构建前“顺手升级”依赖；先让基线编译通过。

## 3. 不连接设备也能完成的检查

进入工程根目录：

```sh
cd eego-a4-template-firmware
python3 scripts/validate_project.py --quick
pio run -e eego_a4_quickstart
```

完整检查会编译诊断固件和五个示例：

```sh
python3 scripts/validate_project.py
```

成功标准：

```text
Static validation PASS
Build validation PASS: 6 environment(s)
```

这个脚本还会检查：

- `EegoA4Hardware.h` 与打包脚本的版本是否一致；
- `platformio.ini` 是否包含全部环境；
- 每个示例是否使用安全电源保持和统一启动器；
- README、示例和 `docs/` 内的相对链接是否有效。

## 4. 第一次连接设备

### 4.1 找端口

macOS：

```sh
ls /dev/cu.usbmodem*
```

Linux 通常是 `/dev/ttyACM0`，Windows 通常是 `COM3` 一类端口。后文统一写成
`<PORT>`。

注意：在本项目的 macOS + ESP32-S3 native USB CDC 组合上，主机打开串口会
触发一次 MCU 复位，即使程序预先关闭 DTR/RTS。任何保存在 RAM 的触摸计数、
充电阶段样本或当前菜单页面都会清空；这不等于固件故障。

### 4.2 先备份整片 Flash

刷任何开发固件前必须保存 16 MiB：

```sh
python3 -m esptool --chip esp32s3 --port <PORT> chip-id
python3 -m esptool --chip esp32s3 --port <PORT> read-flash \
  0x0 0x1000000 eego-a4-before-development.bin
shasum -a 256 eego-a4-before-development.bin
```

Windows PowerShell 可用：

```powershell
Get-FileHash .\eego-a4-before-development.bin -Algorithm SHA256
```

把备份和摘要放到工程外的安全位置。恢复步骤见
[RECOVERY.md](RECOVERY.md)。

## 5. 运行而不是猜测硬件

如果设备已刷入模板，打开监视器：

```sh
pio device monitor -b 115200 -p <PORT>
```

常用命令：

```text
help
status
ui state
run all
report json
screenshot
```

`run all` 只运行可自动判断的项目。触摸九宫格、实体按键、实体屏幕残影、充电
三阶段和深睡电流必须由人或仪表完成，自动代码不会伪造这些结果。

## 6. 从最小示例开始开发

先阅读并编译：

```text
examples/01_quickstart_display_input/main.cpp
```

它展示了产品应用必须保留的最小骨架：

```cpp
void setup() {
  eego::holdPower();          // 必须是第一条硬件操作
  delay(250);
  eego::beginUsbSerial();

  const eego::BeginStatus hw =
      eego::beginStandardHardware(display, input, frontlight, rtc, sd);
  if (!hw.displayReady) {
    return;
  }
}
```

要新建自己的示例：

1. 复制 `examples/01_quickstart_display_input/` 为新目录；
2. 在 `platformio.ini` 新建一个 `[env:你的环境名]`；
3. 让 `build_src_filter` 只包含新目录的 `main.cpp`；
4. 每次修改后执行 `pio run -e 你的环境名`；
5. 稳定后再考虑上传。

不要直接删除默认诊断环境；它是验证硬件和回归驱动的基准。

## 7. 上传开发示例

直接 `pio -t upload` 只适用于设备已经安装本项目/CrossPoint 的对称 6.4 MiB
OTA 分区表。若设备仍是 OEM 布局，必须使用完整首装镜像和安全刷写器。

已确认分区表匹配时：

```sh
pio run -e eego_a4_quickstart -t upload --upload-port <PORT>
```

未知分区表、第一次安装或救砖时不要使用这条快捷命令。改用发布包中的：

```sh
python safe_flash_template.py --port <PORT> --preflight-only
```

安全刷写流程见根目录 [README](../README.md#烧写)。

## 8. 开发时必须保持的硬件契约

- `GPIO4`：整机电源保持，普通运行绝不能拉低。
- `GPIO6`：EPD rail，仅交给显示驱动管理。
- 显示 SPI：SCLK42/MOSI45/CS21/DC14/RST13/BUSY41。
- SD SPI：SCLK39/MISO40/MOSI38/CS47，必须使用独立、长期存活的
  `SPIClass(HSPI)`；不能改用已初始化的全局 `SPI`。
- I²C：SDA2/SCL1/400 kHz，由 GSLX680、PCF8563 和可选 LM3630A 共享。
- 触摸：应用只做 panel-native → portrait UI 旋转，不能再次镜像 raw X/Y。
- UI：物理圆角安全区为 `x=12, y=12, 528×744, R48`；正文和控件统一位于
  `x=28, y=28, 496×712, R0` 的矩形内容区。`PortraitCanvas` 默认裁剪，
  普通页面显式调用 `useUiContentRect()`；全屏背景/硬件测试例外须立即恢复
  该模式。
- GPIO11：带 LM3630A 时高有效，无 LM3630A 时低有效；统一启动器会自动选择。
- 默认永不休眠；任何示例深睡必须先设置定时唤醒。

完整引脚表见 [HARDWARE_REFERENCE.md](HARDWARE_REFERENCE.md)，API 用法见
[DRIVER_GUIDE.md](DRIVER_GUIDE.md)，布局边界见
[SAFE_AREA.md](SAFE_AREA.md)。

## 9. 推荐的修改—验证循环

每次只改一个层级：

1. UI/业务：只改自己的示例或应用。
2. 通用外设行为：改对应 `lib/<Driver>/`，同时更新示例。
3. 板级引脚/能力：改 `BoardConfig::EEGO_A4` 和 `EegoA4Hardware.h`。
4. 波形/触摸固件：只有在新二进制、原理图或实测证据出现时修改。

每次提交前执行：

```sh
python3 scripts/validate_project.py
python3 scripts/package.py
cd release/1.0.18
shasum -a 256 -c SHA256SUMS
```

发生问题先查 [TROUBLESHOOTING.md](TROUBLESHOOTING.md)，不要通过随机交换引脚、
翻转坐标或缩短 BUSY 等待来“碰运气”。
