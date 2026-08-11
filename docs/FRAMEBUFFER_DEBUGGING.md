# 实体设备 Framebuffer 回读调试

本文记录一种对电子纸固件非常有效的调试方法：通过 USB 从正在运行的实体设备
回读其真实 framebuffer，再在主机上保存为 raw 和 PNG。它不是模拟器截图，也
不是相机拍摄屏幕，而是 MCU 当前准备交给显示驱动的逐像素内容。

## 1. 能证明什么

Framebuffer 回读发生在实体 EEGO A4 上，数据来自设备 RAM。因此它可以证明：

- 固件实际停留在哪个页面；
- renderer 最终生成了哪些像素；
- 中文、英文、数字和符号是否缺字、乱码或变成方框；
- 文字和控件是否越界、重叠或被裁切；
- 旋转前的 768×552 显示缓冲区是否完整；
- USB 能否无短写地传回完整的 52,992 字节。

它不能证明实体电子纸已经正确呈现这些像素。以下问题仍需要肉眼、相机或仪表：

- SPI 数据是否真正到达面板；
- EPD rail、RESET、BUSY 和刷新命令是否正确；
- 面板是否仍保留上次关机画面；
- 快刷残影、黑白均匀性、灰阶层次、闪烁和坏点；
- 屏幕表面实际方向是否与外壳一致。

因此应把它称为“实体设备 framebuffer 回读”，而不是“实体屏幕照片”。

## 2. 诊断分层

同一个页面同时比较 framebuffer 和实体面板，可以快速缩小故障范围：

| Framebuffer | 实体面板 | 优先检查 |
|---|---|---|
| 错误 | 错误 | 页面状态、字体、布局、renderer、坐标旋转 |
| 正确 | 错误或仍是旧画面 | EPD SPI、CS/DC、rail、RESET、BUSY、波形、刷新命令 |
| 正确 | 正确但残影严重 | Fast/Full cadence、LUT、温度、清屏策略 |
| 正确 | 正确但触摸错位 | GSL raw 标定、logical/native 变换、手势层 |
| 无法完整读回 | 未知 | USB CDC 超时、短写、主机未按声明长度持续读取 |

若 framebuffer 正确、实体面板却保留旧画面，应优先检查显示 SPI 与 SD SPI 的
总线所有权，而不是先修改字体或布局 renderer。

## 3. EEGO A4 二进制协议

串口为 115200 baud。发送：

```text
screenshot
```

设备返回：

```text
SCREENSHOT_START:52992
<恰好 52992 个原始二进制字节>
SCREENSHOT_END
```

其中：

```text
52992 = 768 × 552 ÷ 8
```

数据为 1 bpp、每行 96 字节、MSB first；bit 0 表示黑，bit 1 表示白。
这是 controller-landscape 顺序。按 768×552 解码后顺时针旋转 90°，即可得到
用户方向的 552×768 竖屏图。

主机必须先解析长度，再读取恰好这么多字节。不能把图像数据按行读取，也不能
尝试 UTF-8 解码，因为 framebuffer 内部可以自然出现 `0x0a`、`0x0d` 或看似
`SCREENSHOT_END` 的字节组合。只有读满声明长度后才能解析 footer。

## 4. 最小主机采集脚本

依赖：

```sh
python3 -m pip install pyserial pillow
```

示例：

```python
from pathlib import Path

import serial
from PIL import Image

PORT = "/dev/cu.usbmodem2101"
RAW_PATH = Path("eego-a4-screen.raw")
PNG_PATH = Path("eego-a4-screen.png")


def read_exact(stream: serial.Serial, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = stream.read(size - len(data))
        if not chunk:
            raise TimeoutError(f"short framebuffer: {len(data)}/{size}")
        data.extend(chunk)
    return bytes(data)


with serial.Serial(PORT, 115200, timeout=2, write_timeout=2) as device:
    device.reset_input_buffer()
    device.write(b"screenshot\n")
    device.flush()

    while True:
        line = device.readline()
        if line.startswith(b"SCREENSHOT_START:"):
            size = int(line.split(b":", 1)[1])
            break

    raw = read_exact(device, size)
    footer = device.readline().strip()
    if footer != b"SCREENSHOT_END":
        raise RuntimeError(f"bad screenshot footer: {footer!r}")

RAW_PATH.write_bytes(raw)

landscape = Image.frombytes("1", (768, 552), raw)
portrait = landscape.rotate(-90, expand=True)
portrait.save(PNG_PATH)
```

如果先用 `font page 3`、`run battery` 等命令切换页面，应等待设备报告该页面
刷新完成，再执行 `ui state` 和 `screenshot`。不要在异步 render task 正在写
framebuffer 时并发采集。

### USB CDC 打开端口可能复位设备

在本项目使用的 macOS 主机和这台 EEGO A4 上，主机重新打开 native USB CDC
端口会触发 MCU 复位；即使 pyserial 对象在 `open()` 前已经设置
`dtr=False`、`rts=False`，仍观察到了完整启动日志和 UI 回到菜单。不要把“只发
`ui state`”误认为一定是无侵入查询。

可靠做法是只打开一次串口，等待启动完成，然后在同一个会话中依次执行：

```text
切换到目标页面
等待刷新完成
ui state
screenshot
```

完成全部采集后再关闭端口，不要在步骤之间反复打开。触控次数、按键次数、充电
阶段 capture mask 等 RAM-only 状态会在这类复位后清空；测试记录应单独留存，
不能根据复位后的 `manual_required` 字段推断复位前的实体操作状态。

## 5. 固件侧实现要点

模板的 `screenshot` 命令遵守以下规则：

1. 从显示驱动获取 framebuffer 指针和精确长度。
2. 先输出纯文本长度 header。
3. 以小块循环写入，并重试 USB CDC 短写。
4. 只在截图期间临时提高 TX timeout。
5. 写满全部像素后输出 footer 并 `flush`。
6. footer 排空后才恢复普通日志使用的较短 timeout。

一次 `Serial.write()` 的返回值不等于整帧完整发送；ESP32-S3 native USB FIFO
忙时可能发生短写。主机若只等待 footer 而不验证字节数，也可能把损坏截图误当成
成功。

协议实现位于 `src/app/DiagnosticApp.cpp`。模板仓库提供独立
`scripts/capture_framebuffer.py`，它会严格读取声明长度、校验 footer，并
保存 raw、PBM 和旋转后的 PNG：

```sh
python scripts/capture_framebuffer.py --port <PORT> \
  --page-command 'font page 3' --name font-page-3
```

可重复 `--page-command`，让同一个串口会话在截图前完成多个页面命令。该工具
把短读、异常 framebuffer 长度和丢 footer 都视为失败，而不是生成一张可能
截断的图片。输出默认位于已被 Git 忽略的 `captures/`。

## 6. 建议的证据留存

每次可复现问题至少保留：

```text
固件版本与 app SHA-256
设备 MAC 或资产编号
触发页面的串口命令
ui state 输出
原始 .raw framebuffer
旋转后的 .png
raw 与 PNG 的 SHA-256
同一时刻的实体屏幕照片或肉眼结论
```

raw 是最重要的原始证据；PNG 编码器版本变化可能造成 PNG 文件哈希不同，即使
像素完全相同。比较回归结果时应优先对 raw 执行逐字节比较。

公开的 1.0.18 实体设备合成图和摘要位于
[验证摘要](VALIDATION.md#ui-证据)。它证明中英文与安全区 framebuffer 正确，
仍不能替代对实体面板残影和刷新结果的观察。
