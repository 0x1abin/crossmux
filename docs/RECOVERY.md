# EEGO A4 安全刷写与救砖

EEGO A4 使用 16 MiB ESP32-S3 Flash。完整镜像写入 `0x0` 会覆盖 bootloader、
分区、应用和数据；app-only 写入 `0x10000` 只适用于本项目的分区表。不要把
“设备还能被 USB 识别”误当成“可以跳过备份”。

## 1. 第一次写入前

在工程外创建专用备份目录，并确认端口：

```sh
pio device list
python3 -m esptool --chip esp32s3 --port <PORT> flash-id
python3 -m esptool --chip esp32s3 --port <PORT> read-flash \
  0x0 0x1000000 <backup>/eego-a4-original.bin
```

检查文件恰好为 16,777,216 字节，并保存摘要：

```sh
shasum -a 256 <backup>/eego-a4-original.bin \
  > <backup>/eego-a4-original.bin.sha256
shasum -a 256 -c <backup>/eego-a4-original.bin.sha256
```

Windows PowerShell 可使用：

```powershell
Get-FileHash <backup>\eego-a4-original.bin -Algorithm SHA256
```

不要把整片备份放进 Git 仓库、云端公开 Issue 或发布包；它可能包含 NVS、
设备身份和用户数据。至少保留两份独立副本。

## 2. 使用安全刷写器

先构建并打包：

```sh
pio run -e eego_a4_diagnostics
python scripts/package.py
```

只读预检：

```sh
python release/<version>/safe_flash_template.py \
  --package release/<version> --port <PORT> --preflight-only
```

它会校验：

- 发布包 `SHA256SUMS`；
- ESP32-S3 身份、MAC 和 16 MiB Flash；
- 当前分区表；
- full image 的固定长度。

真正写入还强制要求一个尚不存在的 16 MiB 备份路径和准确重复预检显示的 MAC。
分区表匹配时才能 app-only：

```sh
python release/<version>/safe_flash_template.py \
  --package release/<version> --port <PORT> --app-update \
  --backup <backup>/eego-a4-before-update.bin \
  --confirm-mac 00:00:00:00:00:00
```

OEM、未知分区或第一次安装：

```sh
python release/<version>/safe_flash_template.py \
  --package release/<version> --port <PORT> --first-install \
  --backup <backup>/eego-a4-before-first-install.bin \
  --confirm-mac 00:00:00:00:00:00
```

工具会在写入后运行 `verify-flash`。缺少任何保护条件时应修正条件，不要修改脚本
绕过 guard。

## 3. 镜像含义

- `eego-a4-template-full-16mb.bin`：从 `0x0` 写入，包含 bootloader、分区表、
  boot_app0 和 app，其余未用区域为 `0xff`。
- `eego-a4-template-app.bin`：从 `0x10000` 写入，要求已安装与包内逐字节一致的
  `partitions.bin`。

不要把 app 镜像写到 `0x0`，也不要在未知分区表上把它写到 `0x10000`。

## 4. 从整片备份恢复

先离线验证备份摘要，再连接设备：

```sh
shasum -a 256 -c <backup>/eego-a4-original.bin.sha256
python3 -m esptool --chip esp32s3 --port <PORT> flash-id
python3 -m esptool --chip esp32s3 --port <PORT> --baud 921600 \
  write-flash 0x0 <backup>/eego-a4-original.bin
python3 -m esptool --chip esp32s3 --port <PORT> --baud 921600 \
  verify-flash 0x0 <backup>/eego-a4-original.bin
```

完整恢复是有意覆盖整片 Flash 的破坏性操作。再次确认文件属于当前设备或你明确
选择的基线，不要只凭文件名判断。

## 5. USB 端口不出现

按顺序排查：

1. 换一根确认支持数据的线和主机 USB 端口。
2. 关闭占用端口的 monitor、IDE 和截图工具。
3. 重新枚举端口，不复用过期设备名。
4. 让 ESP32-S3 进入 ROM download mode；具体按键组合取决于外壳接线。
5. 运行 `esptool ... flash-id`，识别后先尝试读取 Flash。
6. 只有已保存并验证恢复镜像后才执行写入。

不要把 `erase-flash` 当作第一步；它会销毁仍可读取的 NVS、校准和恢复证据。

## 6. MCU 运行但屏幕不刷新

若串口显示刷新完成，面板仍保留关机前画面：

- 检查 GPIO 6 EPD rail；
- 检查 SCLK42 / MOSI45 / CS21 / DC14；
- 检查 SD 是否错误复用了全局 SPI；
- 检查 BUSY41 是否能回到空闲；
- 检查 RESET13 时序；
- 读取 framebuffer，区分 renderer 和物理面板链路。

电子纸断电后保留旧图是正常特性，不等于新固件没有运行。使用
[`FRAMEBUFFER_DEBUGGING.md`](FRAMEBUFFER_DEBUGGING.md) 分层判断。

## 7. 电源保持与深睡

设备启动数秒即消失时，先确认 `eego::holdPower()` 是 `setup()` 的第一项硬件
操作。深睡 GPIO hold 会跨复位保留，唤醒后必须解除 hold 并重新配置 GPIO4
高输出。

开发阶段只使用 `eego::enterTimedDeepSleep()`；它先设置 3–60 秒定时唤醒，再
进入 deep sleep。不要测试没有恢复条件的无限期深睡。

## 8. 恢复后验证

恢复或刷写后至少检查：

```sh
python3 -m esptool --chip esp32s3 --port <PORT> flash-id
pio device monitor -b 115200 -p <PORT>
```

然后确认启动、电源保持、屏幕、输入、SD 和 USB。整片镜像在首次启动后可能因
NVS 或 OTA 状态变化而改变；若需要精确整片哈希，应在首次启动前完成读回。
