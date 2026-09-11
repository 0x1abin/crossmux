# CrossMux

[English](./README.md) | **简体中文**

**CrossMux** 是面向 ESP32 墨水屏设备的 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) 社区 fork。以阅读为核心，同时提供轻量应用、阅读分析、待机表盘和按需联网服务。

[固件发布](https://github.com/0x1abin/crossmux/releases) · [用户指南](./USER_GUIDE.md) · [参与贡献](./docs/contributing/README.md)

![CrossMux 运行在 Xteink 设备上](./docs/images/cover.jpg)

## 核心功能

- **阅读与书库**：EPUB、TXT、XTC/XTCH 和图片，章节导航、书签、词典、自定义字体、阅读背景，以及 KOReader 进度同步。
- **无线功能**：浏览器传书与设置、Calibre 无线连接、OPDS 下载、WebDAV 和设备 OTA 更新。
- **Apps 应用中心**：数独、五子棋、中国象棋、扫雷、2048、电子木鱼、Ugly Avatar 等轻量游戏与工具。[应用说明](./src/activities/apps/README.md)。
- **AirPage**：扫码上传内容，通过手动刷新或前台实时投送显示 BMP/JPEG 图片，也可将图片设为休眠画面。[操作与联网行为](./src/activities/apps/README.md#airpage)。
- **微信读书**：扫码登录、浏览书架、下载 EPUB 离线阅读和同步进度，在 China 内容区显示。[微信读书说明](./src/activities/apps/weread/README.md)。
- **阅读分析与待机**：阅读统计、热力图、档案与成就，以及时钟和老黄历表盘。[阅读分析说明](./src/activities/apps/reading-stats/README.md)。
- **语言与开发**：每个硬件目标使用包含 33 种 UI 语言的统一固件，并提供桌面模拟器辅助开发。

> **微信读书安全提示**：非公开 Web 协议可能变化。真机传输经过加密，但客户端不验证服务器证书与主机名，请仅在可信网络中使用。原生模拟器通过主机信任库验证证书，详见[传输说明](./docs/engineering/chinese-build.md#weread-transport)。

## 设备与发布渠道

| 设备 | 芯片 | 发布渠道 |
|---|---|---|
| Xteink X3 / X4（共用固件） | ESP32-C3 | Stable、Nightly |
| Seeed Sticky | ESP32-S3 | Nightly |
| Xteink X4 Pro | ESP32-S3 | Nightly |
| M5Stack Paper Mono | ESP32-S3 | Nightly |
| eego A4 | ESP32-S3 | Nightly |
| Murphy M4 | ESP32-S3 | Nightly |
| Waveshare ePaper 3.97 | ESP32-S3 | Nightly |

此表表示配置中的发布目标，不代表所有功能均已通过实机验收。每个 S3 目标使用独立固件。X4 Classic 仅提供构建目标，尚未加入公开发布与 OTA 索引。各设备限制见[设备变体说明](./docs/engineering/device-variants.md)。

X3/X4 稳定渠道使用 [Stable](https://github.com/0x1abin/crossmux/releases/tag/stable)，开发版本使用 [Nightly](https://github.com/0x1abin/crossmux/releases/tag/nightly)。[目标表](./scripts/nightly_targets.py) 定义渠道与产物名称；[发布架构](./docs/engineering/firmware-release.md) 说明打包与 OTA。当前源码版本及构建环境以 [platformio.ini](./platformio.ini) 为准。

## 安装固件

1. 打开 [CrossMux Releases](https://github.com/0x1abin/crossmux/releases)，选择渠道和准确设备型号，按该版本的产物链接与安装说明操作。X3/X4 共用固件，S3 必须按板型选择。
2. 更换固件前备份 SD 卡数据。使用匹配的安装包；只有应用部分的 `firmware.bin` 不是完整的首次安装镜像。
3. 对已有固件的 X3/X4，可使用[上游 CrossPoint 网页烧录器](https://crosspointreader.com/#flash-tools)的自定义二进制上传功能：选择 X3/X4，上传 **CrossMux** 应用固件。选择烧录器中的上游版本会安装 CrossPoint。
4. S3 的安装与恢复请遵循对应[设备文档](./docs/engineering/device-variants.md)和发布说明，不要套用 X3/X4 的烧录命令或偏移地址。

从源码构建并烧录 X3/X4 可使用下方的[开发命令](#开发快速开始)。已安装 CrossMux 时，设备 OTA 按型号、内容区和渠道选择固件；S3 目标没有 Stable 渠道。

### USB 锁定的 Xteink 设备

部分设备可能限制 USB 刷写。[上游 Xteink Unlocker](https://crosspointreader.com/#unlock-tool) 是独立工具，使用前请阅读其当前兼容性与恢复说明。不能从 CrossPoint 兼容性推断 CrossMux 可用于锁定设备；刷入不受支持的固件可能导致无法恢复。串口未出现时，也应检查数据线、端口和浏览器权限。

## 中文字体与内容区

每个硬件目标只构建一个统一语言固件。简体中文选择 China 内容区（`crossmux.cn`），其它 UI 语言选择 Global 内容区（`crossmux.com`）。切换 UI 语言会同步内容区和区域应用，包括微信读书与中国象棋。

UI 内置精简的 8/10/12pt 简体中文回退字体。内置阅读字体选项共用 12pt 离线回退；完整字族、其它字号、粗斜体和更广的 Unicode 覆盖使用 SD 卡 `.cpfont` 字体。内嵌字库是子集，生僻字或繁体字可能需要覆盖相应字形的 SD 字体。

可从 **设置 > 阅读器 > 管理字体** 下载字体，或将转换好的字体复制到 SD 卡。安装与转换见 [SD 卡字体](./docs/sd-card-fonts.md)，内嵌字体工具链见[中文支持](./docs/engineering/chinese-build.md)。常规构建无需重新生成字体。

## 开发快速开始

安装 PlatformIO Core（`pio`）和 Python 3；仓库已固定 pioarduino 平台版本。完整代码检查还需要 clang-format 21+、CMake 和 Ninja。环境设置见[开发入门](./docs/contributing/getting-started.md)。

```bash
git clone --recursive https://github.com/0x1abin/crossmux.git
cd crossmux

# 如果尚未初始化子模块：
git submodule update --init --recursive

# X3/X4 开发构建
pio run -e default

# X3/X4 统一语言稳定版构建
pio run -e gh_release

# 构建并烧录到已连接的 X3/X4
pio run -e gh_release -t upload
```

应用固件位于 `.pio/build/gh_release/firmware.bin`。其它板型使用[构建文档](./docs/engineering/build-system.md)中的对应环境。

### 桌面模拟器

安装 SDL2 与 curl（Linux 还需 OpenSSL 开发头文件），将 EPUB 放入 `fs_/books/`，然后运行：

```bash
pio run -e simulator -t run_simulator           # X4
pio run -e simulator_x3 -t run_simulator        # X3
pio run -e simulator_eego_a4 -t run_simulator   # eego A4
pio run -e simulator_murphy_m4 -t run_simulator # Murphy M4
```

[CrossMux 模拟器 fork](https://github.com/0x1abin/crosspoint-simulator) 的版本固定在 `platformio.ini` 中。它用于预览 UI 和输入流程，不能验证显示波形、耗电或实际硬件时序。

### 检查与调试

```bash
./bin/ci-check       # 完整代码检查，不改写源文件
pio device monitor  # 已连接设备的串口日志
```

针对性检查和增强串口监视器见[测试与调试](./docs/contributing/testing-debugging.md)。纯文档修改检查链接、命令和空白即可，无需构建固件。

## 文档与贡献

- [用户指南](./USER_GUIDE.md) · [Web 传书](./docs/webserver.md) · [Web API](./docs/webserver-endpoints.md)
- [项目范围](./SCOPE.md) · [社区治理](./GOVERNANCE.md) · [贡献指南](./docs/contributing/README.md)
- [Agent 指南](./AGENTS.md) · [工程文档](./docs/engineering/index.md) · [触屏与 UI](./docs/contributing/touch-and-ui.md)
- [缓存管理](./docs/engineering/cache-management.md) · [文件格式](./docs/file-formats.md)

请在 [CrossMux Issues](https://github.com/0x1abin/crossmux/issues) 反馈问题和提出改进，贡献 PR 以 **`0x1abin/crossmux:main`** 为目标，每个 PR 聚焦一件事并说明验证方式。代码中保留的 CrossPoint 类名和 SD 卡上的 `/.crosspoint` 数据目录是兼容细节，不代表应向上游仓库提交。该目录也保存设置与阅读进度，不要为了清理某本书的缓存而直接删除整个目录。

## 致谢

感谢 [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)、[Inx](https://github.com/obijuankenobiii/inx)、[cpr-vcodex](https://github.com/franssjz/cpr-vcodex) 及其贡献者，以及带来最初启发的 [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader)。

CrossMux 与 Xteink 及任何设备厂商均无隶属关系。上游工具与社区独立于本 fork。仓库许可证见 [LICENSE](./LICENSE)。
