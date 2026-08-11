# EEGO A4 硬件模板固件

[English](README.en.md) · [文档导航](docs/README.md) ·
[贡献指南](CONTRIBUTING.md) · [安全政策](SECURITY.md)

这是一个独立的 EEGO A4 硬件支持、诊断与固件开发基线。它不是日常阅读器
应用，而是一套已经在实体设备上验证的“能安全启动、能点屏、能逐项测试、能恢复”
模板：保留官方固件的完整触控标定与按键定义，并采用 CrossLink 固件中效果更好的
UC8279C 刷新策略。

当前版本为 `1.0.18`。项目与 EEGO、Xteink、CrossLink 及其权利人没有隶属或
背书关系；来源和许可边界见 [NOTICE.md](NOTICE.md)。

## 快速开始

需要 Python 3.10+、Git 和一根支持数据的 USB 线：

```sh
git clone <repository-url>
cd eego-a4-template-firmware
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python scripts/validate_project.py --quick
pio run -e eego_a4_quickstart
```

Windows PowerShell 激活虚拟环境：

```powershell
.\.venv\Scripts\Activate.ps1
```

然后按顺序阅读：

1. [从零开发指南](docs/GETTING_STARTED.md)
2. [五个可编译示例](examples/README.md)
3. [驱动与 API](docs/DRIVER_GUIDE.md)
4. [架构与总线所有权](docs/ARCHITECTURE.md)
5. [刷写与救砖](docs/RECOVERY.md)

## 已覆盖硬件

| 模块 | 已实现行为 |
|---|---|
| 屏幕 | UC8279C 初始化、Full、Fast、四灰阶、边界与残影测试 |
| 字体 | 从 SD 读取 CPFONT v4，绘制 8/10/12/14/16/18 pt 中英文字 |
| 触控 | 上传完整 GSLX680 固件，官方全范围标定，九宫格与边缘测试 |
| 输入 | GPIO 5/7/8 Up、Down、Power；屏下键短按与 700 ms 长按 |
| 存储 | microSD 挂载、临时文件写读校验和容量读取 |
| 时钟 | PCF8563 探测、VL 状态和时间读写 |
| 电源 | GPIO 4 电源保持、电池 ADC、GPIO 11 充电状态、安全深睡 |
| 无线 | 2.4 GHz Wi-Fi 扫描/临时联网，BLE 被动扫描 |
| 系统 | 16 MiB Flash、8 MiB PSRAM、原生 USB CDC、JSON 报告 |
| 可选前光 | 仅在 I²C `0x36` 确认 LM3630A 后短时低亮度测试 |

没有证据的模块不会被“猜测支持”。当前没有确认板载扬声器、麦克风、蜂鸣器、
LED、IMU 或环境传感器。完整事实、来源和置信度见
[硬件参考](docs/HARDWARE_REFERENCE.md)。

## 安全合同

- `GPIO4` 必须是第一项硬件操作并保持高电平。
- 默认永不自动休眠，也不会自动关闭电源保持。
- `sleep` 命令只允许 3–60 秒，并始终配置定时唤醒。
- 自动诊断不擦除 Flash、不格式化 SD、不保存 Wi-Fi 密码、不写未知 I²C 器件。
- SD 测试只创建、验证并删除 `/eego-a4-diagnostics.tmp`。
- 完整镜像会改写分区表；任何写入前都必须保存新的 16 MiB 整片备份。
- 构建、校验和打包命令不会自动刷写已连接设备。

实体机标定的显示合同：

```text
物理外轮廓：x=0,  y=0,  552×768, R60
圆角安全区：x=12, y=12, 528×744, R48
普通 UI 区：x=28, y=28, 496×712, R0
推荐标题起点：(32,32)
```

正文和控件必须使用 `PortraitCanvas::useUiContentRect()`；只有背景、显示边缘或
触控边缘测试可以临时越界。详细规范见 [SAFE_AREA.md](docs/SAFE_AREA.md)。

## 项目结构

```text
src/
  main.cpp                 诊断固件入口
  app/                     诊断菜单、状态机与串口协议
lib/
  EegoA4Support/           板级合同、安全启动、坐标、电源策略
  EegoA4Ui/                竖屏画布、安全裁剪、CPFONT renderer
  BoardConfig/             FreeInk 板级 profile
  FreeInkDisplay/          显示 facade、总线、UC8279C 与其他驱动
  InputManager/            侧键、GSLX680 和触控标定
  SDCardManager/           独立 HSPI SD
  Rtc/ BatteryMonitor/ FrontlightManager/
examples/                  五个独立可编译示例
scripts/                   校验、打包、安全刷写、截图与提取工具
docs/                      开发、硬件、调试、恢复和维护文档
```

`.pio/`、`release/` 和 `captures/` 都是生成目录，已从源码控制排除。二进制发布包
应作为 GitHub Release assets 发布，而不是长期提交到源码历史。

## 构建与校验

快速静态检查：

```sh
python scripts/validate_project.py --quick
```

完整回归会检查目录合同、版本、示例安全入口、UI 规则、Markdown 链接和开源
元数据，并编译诊断固件与五个示例：

```sh
python scripts/validate_project.py
```

单独构建：

```sh
pio run -e eego_a4_diagnostics
pio run -e eego_a4_quickstart
pio run -e eego_a4_storage_rtc_battery
pio run -e eego_a4_wifi_ble
pio run -e eego_a4_safe_sleep
pio run -e eego_a4_cpfont
```

生成当前版本发布包：

```sh
pio run -e eego_a4_diagnostics
python scripts/package.py
cd release/1.0.18
shasum -a 256 -c SHA256SUMS
```

打包器从 `EegoA4Hardware.h` 读取版本，创建 app/full 镜像、manifest、校验表和
安全刷写器；不会访问串口。

## 安全刷写

先在工程外准备备份目录。发布包的只读预检会检查 SHA-256、目标芯片、MAC、
Flash 容量和当前分区表：

```sh
python release/1.0.18/safe_flash_template.py \
  --package release/1.0.18 --port <PORT> --preflight-only
```

写入要求一个尚不存在的备份路径和预检显示的准确 MAC。分区表匹配时可 app-only：

```sh
python release/1.0.18/safe_flash_template.py \
  --package release/1.0.18 --port <PORT> --app-update \
  --backup <backup>/eego-a4-before-update.bin \
  --confirm-mac 00:00:00:00:00:00
```

OEM/未知分区或首次安装必须改用 `--first-install`。工具会先读取完整备份，再写入
并执行 `verify-flash`。不要把 app 镜像写到 `0x0`，也不要在未知分区表上手工写
`0x10000`。完整说明见 [RECOVERY.md](docs/RECOVERY.md)。

## 使用诊断固件

启动后显示 `EEGO A4 LAB`，默认不会自动休眠：

- Up / Down：移动选择
- Power：打开或重测
- 触摸菜单行：直接打开
- 屏下键短按：返回
- 屏下键长按：运行全部自动测试

单项测试会停留在详情页，不会自动跳到报告。触控、实体键、屏下键、屏幕残影和
充电三阶段需要人工确认；`run all` 不会伪造这些结果。

USB CDC 为 115200 baud：

```sh
pio device monitor -b 115200 -p <PORT>
```

常用命令：

```text
help
status
ui state
run all
run safe
font page 3
report json
screenshot
```

`screenshot` 返回实体设备 RAM 中完整的 52,992 字节 framebuffer。主机工具：

```sh
python scripts/capture_framebuffer.py --port <PORT> \
  --page-command 'font page 3' --name font-page-3
```

这能证明 renderer 的真实像素，但不能替代对实体面板刷新、残影和对比度的观察。
见 [Framebuffer 调试指南](docs/FRAMEBUFFER_DEBUGGING.md)。

## 文档与项目治理

- [完整文档导航](docs/README.md)
- [实体设备验证范围](docs/VALIDATION.md)
- [维护者与发布流程](docs/MAINTAINER_GUIDE.md)
- [贡献规范](CONTRIBUTING.md)
- [行为准则](CODE_OF_CONDUCT.md)
- [支持入口](SUPPORT.md)
- [安全政策](SECURITY.md)
- [变更记录](CHANGELOG.md)
- [第三方依赖声明](THIRD_PARTY_NOTICES.md)

## 许可证与来源

源代码按 [MIT License](LICENSE) 发布。仓库不包含官方或第三方完整固件，也不
包含字体包。GSLX680 固件表和 UC8279C LUT 属于从已记录哈希的二进制提取的
互操作数据；发布者必须单独评估其分发依据。详见 [NOTICE.md](NOTICE.md)。
