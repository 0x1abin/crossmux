# CrossMux

[English](./README.md) | **简体中文**

**CrossMux** 是 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 的社区 fork：在原有电子书阅读体验之上，新增了一个 Apps 应用中心（小游戏 / 小工具）、更丰富的待机表盘，以及一套完整的简体中文固件。

**版本：** CrossMux 1.3.0（基于 CrossPoint Reader 1.3.0）

**运行设备：** 基于 ESP32-C3 的 Xteink [X4](https://www.xteink.com/products/xteink-x4) 与 [X3](https://www.xteink.com/products/xteink-x3)。

> 本文档面向中文用户，重点说明 CrossMux 的中文相关功能与简体中文固件的编译方式。完整的英文说明请见 [README.md](./README.md)。

![CrossMux 运行在 Xteink 设备上](./docs/images/cover.jpg)

---

## CrossMux 相比上游新增了什么

- **Apps 应用中心**（首页的 `Apps` 菜单）：2048、扫雷、数独、五子棋、中国象棋、康威生命游戏，以及一个程序化生成的「Ugly Avatar」头像生成器。
- **微信读书 Copilot**：浏览书架、笔记与书评，支持 SD 卡离线缓存与批量同步。
- **待机表盘**：手绘风格的「潦草时钟」与中式老黄历表盘，另有可选的 4 级灰度增强与反色显示模式。
- **简体中文固件**（`gh_release_cn`）：中文 UI + i18n、内嵌 CJK 字体、面向中文的 EPUB 排版（断词与禁则等）。详见下方 [编译简体中文固件](#编译简体中文固件)。
- **桌面 / WebAssembly 模拟器**：可在电脑上开发与预览 UI。

上游 CrossPoint 的全部能力（EPUB 2/3 渲染、多格式支持、无线传书、OPDS、OTA 等）在 CrossMux 中同样可用，详见 [English README](./README.md#what-can-crosspoint-do)。

---

## 编译简体中文固件

CrossMux 提供一个专用的简体中文构建环境 `gh_release_cn`，产出**仅中文**的固件：简体中文 UI + i18n、内嵌 CJK 点阵字体、面向中文的 EPUB 排版，以及中国象棋 / 微信读书等应用。全新设备首次开机即直接进入中文界面。

### 前置条件

- [pioarduino](https://github.com/pioarduino/pioarduino)，或 VS Code + pioarduino 插件
- Python 3.8+
- `clang-format` 21（仅提交代码时需要）
- 支持数据传输的 USB-C 数据线

### 获取源码

```bash
git clone --recursive https://github.com/0x1abin/crossmux.git
cd crossmux

# 如果克隆时漏掉了 --recursive：
git submodule update --init --recursive
```

### 构建与烧录

仓库已内置 CJK 字体头文件，常规构建无需任何额外的字体处理步骤：

```bash
# 仅构建中文固件
pio run -e gh_release_cn

# 构建并烧录到已连接的设备
pio run -e gh_release_cn -t upload
```

构建产物位于 `.pio/build/gh_release_cn/firmware.bin`，也可以通过网页烧录器上传（见下方 [烧录固件](#烧录固件)）。

### 重新生成 CJK 字体（可选）

只有在**修改字符集**或**更新内嵌字体**时，才需要重新生成 CJK 点阵字体头文件。完整的字体工具链、字符集策略与 Flash 空间预算说明见 [docs/engineering/chinese-build.md](./docs/engineering/chinese-build.md)。

---

## 中文阅读注意事项

简体中文固件在有限的 Flash 空间内做了取舍，使用时请注意：

- **内嵌字体覆盖现代汉语 3500 常用字**（《现代汉语常用字表》）。生僻字、古字、繁体字、部分人名地名用字可能在阅读器中显示为 □。
- **不支持繁体中文**：本构建为简体专用（`ZH_CN`），繁体字形不在任何字库中。
- **大号字下中文正文可能留白**：16pt / 18pt（阅读器的 LARGE / EXTRA_LARGE）仅内嵌 UI 所需的小字集，这两档字号是为英文 EPUB 调优的。读中文请切到 MEDIUM 档。
- **无 CJK 粗体 / 斜体字形**：粗体 / 斜体会回退为常规字重。若需要更多字重，可在 SD 卡上加载自定义字体。
- 需要扩充字符覆盖范围时，参见 [docs/engineering/chinese-build.md](./docs/engineering/chinese-build.md) 中的「Expanding character coverage」。

---

## 烧录固件

### 网页烧录器（推荐）

1. 用 USB-C 连接设备到电脑，并唤醒 / 解锁设备。
2. 打开 https://crosspointreader.com/#flash-tools ，选择设备型号（X3 或 X4），点击「Custom .bin」上传你构建出的 `firmware.bin`。

### 命令行

1. 安装 [`esptool`](https://github.com/espressif/esptool)：

   ```bash
   pip install esptool
   ```

2. 用 USB-C 连接设备，找到串口（Linux 连接后运行 `dmesg`；macOS 可用 `log stream --predicate 'subsystem == "com.apple.iokit"' --info`）。
3. 烧录：

   ```bash
   esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
   ```

   请把 `/dev/ttyACM0` 换成你的实际串口。

### USB 锁定设备说明

部分通过第三方渠道（如 AliExpress）购买的 Xteink 设备出厂即锁定了 USB 刷写。若你的设备被锁定，需要先使用官方的 **Xteink Unlocker** 工具（https://crosspointreader.com/#unlock-tool ）解锁后才能刷写。**直接从 xteink.com 购买的设备不需要此工具。**

> ⚠️ 解锁工具仅官方支持 CrossPoint 与 CrossInk 固件。在已锁定的设备上刷入不受支持的固件，可能导致设备永久变砖或无可恢复路径，操作前务必阅读 [英文 README 的完整警告](./README.md#usb-locked-devices-xteink-unlocker)。

---

## 自定义 SD 卡字体

无需重新刷写固件，即可把你自己的 TTF/OTF 转换成可从 SD 卡加载的 `.cpfont` 字体：

1. 打开 https://crosspointreader.com/fonts ，找到「SD-card font builder」表单。
2. 上传最多四种字形（常规、粗体、斜体、粗斜体），设置字族名、字号与 Unicode 范围。
3. 下载生成的 `.cpfont` 文件，复制到 SD 卡的 `/fonts/你的字体/`（或 `/.fonts/你的字体/` 以隐藏目录）。
4. 在设备的字体设置中选择该字体。

> 提示：通过这种方式加载的字体可以补充内置 3500 字之外的字形（包括粗体 / 斜体），适合需要更大字符覆盖或更多字重的中文阅读场景。

---

## 文档

- [用户指南（英文）](./USER_GUIDE.md)
- [简体中文固件构建深度文档](./docs/engineering/chinese-build.md)
- [Web 服务使用说明](./docs/webserver.md) · [Web 接口](./docs/webserver-endpoints.md)
- [项目范围 SCOPE](./SCOPE.md) · [贡献文档](./docs/contributing/README.md)

---

## 开发快速开始

```bash
# 构建并烧录（默认英文构建）
pio run --target upload

# 提交 PR 前的检查
./bin/clang-format-fix
pio check -e default
pio run -e default
```

调试日志（先 `python3 -m pip install pyserial colorama matplotlib`）：

```bash
# Linux
python3 scripts/debugging_monitor.py
# macOS（替换为你的串口）
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

更多开发细节见 [English README](./README.md#development-quick-start) 与 [CLAUDE.md](./CLAUDE.md) 中链接的工程文档。

---

CrossMux / CrossPoint Reader **与 Xteink 及任何设备厂商均无隶属关系**。

特别感谢 [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader) 项目的启发，以及上游 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 社区。
