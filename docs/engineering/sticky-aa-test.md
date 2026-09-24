# Sticky 三档抗锯齿实验固件

本实验针对阅读翻页中“黑白正文先出现，字边随后变平滑”的中间态。普通纯文字
EPUB/TXT 页会先准备正文和灰阶，再提交一次合并波形。三档为黑／灰／白，两个
抗锯齿灰度合并为一个中间灰。Sticky 实机已确认纯文字翻页无独立黑白正文过渡。

## 构建与边界

```sh
pio run -e sticky_aa_test
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_sticky_combined_aa.py
```

`sticky_aa_test` 继承 Sticky 硬件配置，启用 `FREEINK_STICKY_COMBINED_AA=1`，版本
为 `1.6.0-sticky-aa-test2`，设备标识仍为 `sticky`。其他构建环境和 Nightly 清单
不启用实验开关。不能将 Paper Mono 的整机固件刷到 Sticky。

SDK 复用 PaperMonoDriver 的三档合并算法，Sticky 分支保留 X+/Y− 扫描和原始
字节顺序、40 MHz 默认 SPI、黑白 `0xFF` / 清理 `0xF7`、灰阶前独立 `0xC0`
电源稳定，以及灰阶边框 `0x80`。窗口绘制在此实验中回退到整帧更新。
手动、启动和周期清屏仍可先显示黑白页，再执行灰阶，这是有意保留的清理流程。
只有 EPUB/TXT 无图片、无阅读背景的文字抗锯齿页选择三档合并。含图页、封面、
图片背景及其他灰阶内容走原 Sticky SSD1677 驱动，保留原四档图片灰阶和清理波形。
两条路径切换时重新初始化屏幕和物理基线；首张跨路径页面会多一次必要清理。

独立校准值位于 SDK 的 `src/lut/StickyCombinedAa.h`（相对于
`libs/display/FreeInkDisplay/`）：5 ms 帧周期，激励／灰／黑时长 16／24／32 帧，
电压寄存器值 `17 41 A8 32 30` 沿用 Sticky 当前灰阶配置。这些是实验初值；只根据
实机记录调整，不能把 Paper Mono 的光学测量结果视为 Sticky 的测量结果。

八个 48,000 字节缓冲共占用 384,000 字节 PSRAM，在初始化分配并由单例驱动终身
复用。任务栈无法容纳且这些状态需跨帧保留；不在翻页循环分配。部分分配失败会全部
释放，启动选择原 Sticky 驱动并输出失败日志。阅读器保留已有条带工作区／TXT 黑白
快照开销。Sticky 不使用 Paper Mono 的 180° 旋转工作区。

SDK 在每次灰阶正文开始时清理旧暂存数据。HAL 的 `cancelGrayscale()` 丢弃尚未提交
的正文和灰阶，重新允许下一次绘制；阅读器切换 Activity 时调用。灰阶不完整／工作区
分配失败则显示完整黑白页，避免留下未提交页面。

## 安装与回退

适用于已经运行 CrossMux、使用当前双 OTA 分区布局的 Sticky：

1. 在电脑上解压交付包，运行 `sha256sum -c SHA256SUMS` 验证文件。
2. 将 `sticky-aa-test2.bin` 和 `sticky-rollback.bin` 复制到 SD 卡。
3. 设备设置中进入 **SD 卡固件更新**，选择 `sticky-aa-test2.bin` 并确认。
4. 重启后确认版本为 `1.6.0-sticky-aa-test2`，开启文字抗锯齿，使用浅色阅读背景。
5. 回退时使用相同的 SD 卡固件更新入口，选择 `sticky-rollback.bin`。

回退文件由同一工作区的 `sticky-gh_release` 构建，关闭实验驱动，保留原 Sticky
显示路径；不是从设备读取的完整闪存备份。首次安装和其他分区布局应走项目原有
Sticky 安装流程。测试版仅烧录已核对身份的 Sticky，不推送 Nightly。

启动日志应出现 `Sticky text-only combined AA: ready (384000 bytes PSRAM)`。
若出现 `allocation failed; original driver only`，本次启动走原驱动，
不能据此评估合并抗锯齿。普通文字翻页日志应出现 `SSD1677 update: stages=1`；
这只证明软件提交次数，不替代视觉验收。

## 实机记录

2026-09-24：指定 Sticky（MAC `44:b1:76:b0:66:74`）已烧录
`1.6.0-sticky-aa-test2`，写入校验通过；用户反馈实测正常。尚无逐项翻页耗时、
100 页残影和内存趋势的量化记录，后续可按以下项目复测。

对照原版和实验版，使用同一本书、字体、字号、方向、刷新频率和环境温度；EPUB、
TXT 各连续翻页 100 次。分别记录按键到首次变化、首次变化到最终稳定的时间，
以及是否仍出现独立的黑白正文。可用高速录像结合串口分阶段日志定位。

检查灰色边缘、笔画黑度、白底、边框和逐页残影；补测旋转、菜单返回、夜间模式、
手动清屏、周期清屏、休眠唤醒和快速翻页。记录内部堆剩余、最大连续块和 PSRAM
余量，不能出现持续下降。含图页面应与原 Sticky 驱动保持相同的四阶灰度和清理效果。

验收：普通文字页不独立提交黑白正文；文字灰边可辨识、底色正常、残影不持续加重。
驱动主机测试和固件编译通过只算软件验证；图片质量和跨路径清理以 Sticky 实机反馈为准。

## Subsequent default-enabled implementation

The rebased SSD1677 rollout is documented in [ssd1677-text-aa.md](ssd1677-text-aa.md).
The acceptance recorded above applies to the archived test2 image, not automatically
to the new revision. Ordinary Sticky now defaults to text-only combined AA; build
`sticky_aa_rollback` to restore the original path on the new baseline.
