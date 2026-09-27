# Read Pico（小纸 Pico）适配 — 未完成事项交接记录

> 状态：**已编译通过（2026-09-25），未烧写，未做真机验收。** 本文件记录剩下没做的事、
> 必须由人决策的岔路口、已知缺陷，以及恢复工作时的入口。设备事实与冻结接口规格见
> [read-pico.md](read-pico.md)（89,629 B）。
>
> 工作树 `D:\CrossSIGO\crossmux-upstream`（基线 80c67543, main），未提交任何 commit。
> 编译后才暴露的 4 个问题见第 8 节——其中 3 个正是"没跑评审门"的直接代价。

---

## 1. 一句话结论

SDK 板级、16 位并行显示路径、四个外设后端、CrossMux HAL 与 `readpico` 构建环境**都已写好并编译通过**
（`firmware.bin` 5.77 MB）。但仍然**没有任何真机验证**。距离"能用"只差两步：
**整片备份 + 烧写 → 上真机按验收清单跑**。所有"能亮、能翻页、能读卡"的说法目前都还不成立。

---

## 2. 已完成的部分

| 任务 | 内容 | 主要产出 |
|---|---|---|
| t1 | 硬件契约 + 接口冻结 | `docs/engineering/read-pico.md`（89,629 B / 1118 行） |
| t6 | 16 位并行总线 + E0470 波形 | `LgfxEpdConfig.h`、`LgfxEpdDriver.cpp`、`src/lut/E0470Waveforms.{h,cpp}`、`include/LgfxEpdWaveforms.h` |
| t5 | 板级 profile + 构建环境 | `BoardConfig.h`、新库 `libs/hardware/BoardReadPico/`（6 个文件）、`platformio.ini` |
| t7 | 四个外设后端 + PMU 时间缝隙 | `InputManager`（CST836U）、`Imu`（SC7A20H）、`BatteryMonitor`（CW32L010）、`Rtc`（PMU）、`SDCardManager`、`PowerManager` |
| t8 | HAL 集成 + 启动序列 | `lib/hal/`（8 个文件）、`src/main.cpp` |

改动规模（`git status` / `git diff --stat` 实测）：

- 父仓库：12 个文件，**+498 / −8**（含 `platformio.ini` +59）
- 子模块 `freeink-sdk`：16 个已跟踪文件 **+926 / −42**，另有 4 个未跟踪新路径
  （`LgfxEpdWaveforms.h`、`src/lut/E0470Waveforms.{h,cpp}`、`libs/hardware/BoardReadPico/`）
- 未跟踪：`docs/engineering/read-pico.md`、本文件

新增构建环境：`[readpico_hardware]` + `[env:readpico]`（`pio project config` 与 `--lint` 均 exit 0）。

---

## 3. 完全没做的事

### 3.1 三道计划内的门（任务已建但从未运行）

| 任务 | 内容 | 状态 |
|---|---|---|
| **t9** | 文档收口：`docs/engineering/device-variants.md` 增加设备条目、`README.md` 与 `README.zh-CN.md` 两语言同步增加设备行与构建命令、`docs/engineering/index.md` 注册 | **未做** |
| **t11** | 独立静态验证（接口一致性 / 引脚逐条对照官方证据 / 构建门控 / 零回归 / 洁净度） | **未做** |
| **t12** | 评审门（独立质量评审，verdict = pass / needs_revision / reject） | **未做** |

> **重要：这不等于"验证过了"。** 本轮的验收命令都是**写契约的人自己跑自己写的断言**，
> 属于自证，不是独立验证。t11/t12 的空缺意味着**没有任何第三方复核过这批 diff**。

### 3.2 编译：已完成；烧写与真机：未做

`pio run -e readpico` **构建成功**，`firmware.bin` = 6,046,672 B（5.77 MB）：

| 项 | 结果 |
|---|---|
| RAM | 23.3%（76,424 / 327,680 B） |
| Flash | **92.3%（6,046,171 / 6,553,600 B）— 只剩约 495 KB 余量** |
| 产物 | `firmware.bin`、`firmware.elf`、`bootloader.bin`（18,736 B）、`partitions.bin`（3,072 B） |

编译暴露并修掉了 4 个问题（第 8 节）。**仍未做**：整片 16 MiB 备份、烧写、真机验收。
验收清单在 `read-pico.md` §4（仍全部 `pending`）与 §5.4。

> **"能编译"不等于"能用"。** 面板是否亮、波形对不对、触摸坐标、三个键区、SD 挂载、
> 电池/RTC 读数、睡眠与唤醒——一个都没在真机上跑过。92.3% 的 Flash 占用也意味着
> 后续任何功能都会很快撞到 6.25 MB 槽的上限。

### 3.3 未做的记录性工作

- 未提交任何 commit（工作树保持脏状态，便于你 review）。
- 未把 `readpico` 加入 Nightly / OTA / Web 发布映射（**按范围本就不该做**，见 `read-pico.md` §3.9）。
- 未做整片 16 MiB 备份，未烧写。

---

## 4. 必须由人决策的岔路口

按优先级排序。**D-A 已在编译时定案；其余仍是开放的。**

### D-A ✅ 已解决：M5GFX 改为锁定 git tag `0.2.20`

契约冻结的 `m5stack/M5GFX @ 0.2.20` 在 PlatformIO Registry 里**不存在**（只有 `0.1.17 / 0.2.27 /
0.2.28 / 0.2.29`）。首次 `pio run -e readpico` 实测报
`UnknownPackageError: Could not find the package with 'm5stack/M5GFX @ 0.2.20' requirements`。

**处置**：改用 `https://github.com/m5stack/M5GFX.git#0.2.20`（实测装上 `M5GFX@0.2.20+sha.d8074bb`），
**不是**换 0.2.29——整套 LUT 语义结论（索引是目标电平、eraser 前置、step framebuffer 布局、
`_lut_2pixel` 步数预算）都是在 0.2.20 源码上核验的，换版本会**静默作废那份证据**。
`platformio.ini` 的注释已改写成"不要简化回 registry 版本"，并写明若要用 0.2.29，必须先重新核验
`Panel_EPD.{hpp,cpp}` 与 `Bus_EPD.{h,c}` 再更新文档。

### D-B ★ `pinPwr` 的替身引脚 GPIO0（硬件未验证）

`pin_pwr` 同时是 i80 的 `dc_gpio_num`，而 IDF 对负值直接 `ESP_ERR_INVALID_ARG` →
`Bus_EPD::init()` 返回 false → **静默死屏**。所以它必须是一个 ≥ 0 的真实 GPIO。
本板所有可用脚已被占用（1–18、21、38–48 是功能脚，19/20 原生 USB，26–37 flash/八线 PSRAM），
唯一没出现在参考固件里的是 **GPIO0**，已冻结为 `READPICO_EP_LGX_DUMMY_PIN = 0`。

**需要你确认**：GPIO0 在板上是 NC 还是接了 BOOT 按键/测试点？该脚从此被当作普通输出**长期拉低**
（启动 strapping 只在复位沿采样，不会误入下载模式，但若上面挂了按键，按键就废了）。

> `pinOe` 不需要替身：已在上游源码证实 `lgfx::pinMode(-1)` 安全（`common.cpp:361-364` 首行越界即 return），
> 故用 `PIN_UNASSIGNED`。这是 blocker B2 关闭的一半。

### D-C ★ 波形保真度：接受近似，还是另写驱动

**这是本轮最重要的技术发现。** LovyanGFX 的 `Panel_EPD` LUT 是「按**目标电平**取值的一维 16 项向量」
（4 B/帧），而厂商 E0470 表是 epdiy 的「**16×16 (from→to) 二维矩阵**」（64 B/帧）。
**忠实投影在数学上不存在** —— `from` 轴承载的逐转换保持/擦除斜坡没有对应物。

已落地的折中是「多数表决投影」：GC16 动作一致率 **94.1%**（尾 19 帧 100%），目标电平白推梯子**逐位一致**，
过驱/欠驱各 105 格（离散误差，非偏置）。文件头已明文写出该限制。

**你需要决定**：
- (a) 接受这个近似，上真机看鬼影/对比度能否接受；或
- (b) 认可它不够，改为在 SDK 里为 E0470 写一个专用驱动（不复用 LovyanGFX 的 LUT 模型），
  代价是新增一套 `Bus_EPD`/`Panel_EPD` 之外的并行刷新代码；
- 相关仍无解的一项（blocker B4）：LovyanGFX 把 XSTL 当 i80 **CS** 用，而 epdiy 用 LCD **DE** 驱动它；
  厂商的逐行时序（L_SL/L_BL/L_EL、CKV 高电平宽度、11090 µs 帧目标）Lgfx 路径表达不了。
  面板是否接受 CS 形式的起始脉冲，**只能上真机回答**。

### D-D 本板电源键在 CrossMux 里按不动

`input.power == PIN_UNASSIGNED`，且 CrossMux 没有 PMU 按键事件 seam。后果：
「长按电源键睡眠」与 `shortPwrBtn == FORCE_REFRESH` 手动刷新**都不会触发**，电源键只能依赖 PMU 自身行为。
自动睡眠超时仍可用。修法是在 SDK 增加 PMU 按键事件 seam 再在 app 侧接线（对应 blocker B10）。

### D-E 电池/充电状态被折叠掉两个标志位

`BoardReadPico::pmuBattery` 不返回 PMU 的 `flags bit2 charging_active`，也不返回 `soc_valid`。后果：
- 「插着电但已充满 / 未充电」无法与「正在充电」区分（现按项目既有口径：外部供电即视为充电）；
- `permille == 0` 无法区分「真的 0%」与「读数无效」（现用 mV 走 1S 曲线兜底）。

若 UI 需要区分，需扩展板级 hook（改 `BoardReadPico`，本轮已完成，改它需要新任务）。

### D-F 其余待定项（不阻塞编译）

| # | 事项 | 现状 |
|---|---|---|
| 1 | GL16 波形已落地但**刻意未接线**（48 步 = 24 KiB 内部 DMA，而 `epdModeFor()` 从不选 Half） | 若将来要用 Half 模式再接 |
| 2 | 未采纳原厂 120 MHz flash/PSRAM（温补被刻意禁用 + flash 型号相关 + 与 `firmware_tuned` 不兼容） | 代价是刷新比原厂固件慢，未测量 |
| 3 | 蜂鸣器未接线（`FREEINK_CAP_BUZZER` 为开，但 `readpico` 未 extend `[sound_feedback_hardware]`） | 不在本轮交付范围 |
| 4 | ~~本板没有任何来源发出 `Button::Back`~~ **已闭环**：中间触摸条键现为 `BTN_BACK`（`BoardReadPico.cpp` 的 `keyStripHook`），另有左缘滑动。注意阅读器标题栏点击（`MappedInputManager::wasHeaderTapBack`）曾一度对本板开放，**已回退**（它抢走了顶栏自身的点击，且不在需求内） | 两条返回路径；选择改由触摸承担（中间三分区点击开阅读器菜单） |
| 5 | SD 初始化仍在面板之前（任务要求「SD 最后」） | 有硬件理由：本板 SDMMC(38/42/44) 与 I2C(39/40)、EPD 总线无重叠；改它只会动到所有目标的共享 setup |
| 6 | 面板朝向结论：玻璃面 684×1216，帧缓冲/扫描序写作 1216×684 | §1.2 有推导，但**需真机确认**（`rotation`、`NO_FLIP` 都是起始值） |
| 7 | **readpico 专属大号 UI 字体：NotoSans 16pt**。用户已选定字号；**只生成一个字号**（实测系统里的 NotoSans 只有一档，不需要 medium/bold 多套）。做法：`convert-builtin-fonts.sh` 生成 → `builtinFonts/all.h` 用 `#if FREEINK_DEVICE_READPICO` 包住（其它 env 不编译、镜像里不出现）→ `fontIds.h` 加 ID → `uiScaleSpec()` 在 readpico 分支返回新 ID。**必须同时登记尺寸匹配的 CJK fallback**（内置 UI 字体只有 Ubuntu 拉丁 + Noto 希伯来/阿拉伯，中文靠 `GfxRenderer` 的 fallbackFontMap_），改完跑 `lib/EpdFont/scripts/verify-ui-noto-fonts.py` 回归。Flash 余量 522 KB，预计占 120–150 KB | 待开工 |
| 8 | `uiScale` 是**死值**：`UIScaleSpec`（`src/components/UIScale.h`）不读 `BoardConfig::ACTIVE.uiScale`；唯一的缩放机制 `GfxRenderer::sdCardFontScales_`（`GfxRenderer.h:100,202-206`）**没有任何调用点**。所以各板的 `uiScale = 1.2f` 从未生效（注释里"bump chrome to finger size"是空转） | 与第 7 项相关，是否一并修待定 |
| 9 | `listIconFor()`（`src/components/UiAppHelpers.h:82`）**只有 24 / 32 两套位图**，7 个调用点的尺寸是硬编码字面量；图标尺寸受行高约束（行高由正文字体行高派生）。所以"图标放大"不能单独做，要与第 7 项一起 | 并入第 7 项 |
| 10 | **PMU 电源键此前完全没有消费者**（用户报「短按实体按键关机并不生效」）。根因不是阈值也不是时序：`READ_PICO.input` 七个按键脚全是 `PIN_UNASSIGNED`（`BoardConfig.h:1876-1877`），所以 `InputManager::getPhysicalState()` 在本板**永远**不会置 `BTN_POWER`，`main.cpp:987-997` 那条唯一可达的关机路径不可能触发。已闭环：`keyStripHook()` 现在按原厂 `KEY_POLL_MS = 50` 轮询 PMU，把 `PMU_STATUS_KEY_PRESSED`（STATUS.flags bit 5，`raw[4..7]`）作为**电平**与 `1 << BTN_POWER` 一起上报；短按/长按是同一个比较（`CrossPointSettings.h:467-468` 对 `SLEEP` 返回 10 ms，否则 400 ms）。详见 `read-pico.md` §6.8a | 已闭环，待真机确认 |
| 11 | **EPUB 插图错位**（用户报「epub 的图片显示错位」）。根因：`DirectPixelWriter::writePixel()`（`DirectPixelWriter.h:160`）用 **`uint16_t`** 存帧缓冲字节索引，而本板帧缓冲是 152 B/行 × 684 行 = **103,968 B**，最大索引 103,967 远超 65,535 → 超出的全部回绕到靠前的行，图像内容被搬到约 `65536/152 = 431` 行之外。其他板恰好都没超（X4 最大 47,999、X3 最大 52,271），所以从未暴露；文字不受影响是因为 `GfxRenderer::drawPixel()` 本来就用的 `uint32_t`，这也正是症状看起来"只有图片错"的原因。已闭环：索引改 `uint32_t`（对其它板是**可证明无行为变化**的加宽，故未加 `#if`）。所有插图写入（`ImageBlock` / JPEG / PNG 三个转换器）都只经过这一个函数，全仓排查无同类缺陷。详见 `read-pico.md` §6.11 | 已闭环，待真机确认 |
| 12 | **16 灰阶适配（抗锯齿 / 图片）—— 待决策**。实测结论：**显示侧已经是 16 级**（epdiy 的 `g_fb4` 是 4bpp，GL16/GC16 波形都是 16 级），瓶颈全在上游数据是 2-bit。字体生成器其实**算出了 4-bit**（`fontconvert.py:348`）却在 `:368` 丢弃、降采样成 2-bit。整条链路：字形 2-bit → `mapTwoBitPixel` 两平面 → `epdiyLcdDrawGray` 映射为 **0→15 / 1→10 / 2→5 / 3→0 共 4 级**。<br>**Flash 是硬约束**：`nm` 实测 2-bit 字形位图 **1,281,113 B**（cjk_12 526 KB + cjk_10 372 KB + cjk_8 247 KB + ubuntu 136 KB + 其余），而当前 flash 余量只有 **661,329 B**。4-bit 化相当于在本板已链接的字形上再加约 1.28 MB（压缩后可能更少，但需实测）→ **整体 4-bit 装不下**。<br>可行切分：**(a) 图片走真 16 级 —— 零 flash 成本**，8-bit 亮度本来就在 RAM 里，不必抖动成 2-bit；**(b) 抗锯齿走 16 级 —— 必须 4-bit 字体**，需选一个子集（如只把阅读正文字体转 4-bit，或裁 CJK 覆盖/字号），并连带改字体格式版本、`.cpfont` SD 格式与渲染器（2 平面 → 4 平面）。 | **待用户决定取舍** |

---

## 5. 已知缺陷 / 建议修复（都在代码里，未修）

1. **幽灵按键**（SDK，中等）：`InputManager.cpp:1783-1790` 整帧读取失败时提前 `return 0`，
   没有调用 `cst836uDecodeFrame()`，于是 `BoardReadPico::setStripRawPoint()` 的邮箱保留上一次
   `down = true`，`keyStripHook()` 会持续上报一个「按住的键」，直到下一次成功解码。
   深睡路径已显式清零（`InputManager.cpp:1330`），故影响限于瞬时 I2C 故障。
2. **`LgfxEpdDriver.cpp:132`**：`new lgfx::LGFX_Sprite(&g_dev)` 无空指针检查，违反黄金规则 #2。
   **既有问题**，非本轮引入。
3. **穷尽 switch**：t7 已补齐 6 处（`InputManager.cpp:1322`、`Rtc.cpp:172/282`、`Imu.cpp:204/291/312`）。
   复查结论：其余受影响的 switch 都带 `default`，不应再有新增 `-Wswitch`。
4. **`MAX_FRAMEBUFFER_BYTES`** 的 Read Pico 项用中间常量包裹而非插进 13 层 `cmax()`（公开名不变，
   语义等价）—— 只是提醒别把它"优化"回去。

---

## 6. 计划本身的缺陷（记录，避免重犯）

1. **t8 的 outOfScope 写了 `lib/`，与 inScope 的 `lib/hal/` 自相矛盾** → 结项时 9 个 lib/hal 文件被判越界，
   只能列 `src/main.cpp`；完整清单写在该任务的 output 里，评审时请以 `git status` 为准。
2. **t7 的 inScope 漏了 `Rtc/`**，但 §3.6 冻结的 `Rtc::setPmuTimeHooks` 又必须落地 → 越界实现，
   已在 output 记录。若重来，`Rtc/` 应写进 t7 范围。
3. **三条 verify 命令写错**：`glob.glob('dir/*')` 会返回子目录对象，`io.open` 打开目录抛
   `PermissionError` → 断言必然失败。t5/t6/t7/t8 各有 1–3 条中招，均已用等价修正版重跑并在
   `commandsRun` 中留档。
4. **契约命令 `pio project config -e readpico` 不存在**：PlatformIO 6.1.19 的 `pio project config`
   没有 `-e`（实测 exit 2）。等价验证是 `--json-output` / `--lint`。
5. **写了 `objectives` 而非 `objective`** → 前三个任务被建成 `kind=work` 而非 `implementation`，
   不得不删掉重建（id 因此从 t2/t3/t4 变成 t5/t6/t7）。

---

## 7. 恢复工作时怎么做

```bash
cd D:/CrossSIGO/crossmux-upstream

# 1) 编译（已完成；重跑走缓存，很快）
pio run -e readpico
# 2) 配置解析（不编译，可随时跑）
pio project config --lint
# 3) 烧写前：整片 16 MiB 备份并核验，留存于设备之外
#    （CrossMux partitions.csv 与板厂 partitions_16M.csv 在 0xE000 冲突，
#     首次安装必须整表烧写：bootloader@0x0 / partitions@0x8000 / boot_app0@0xe000 / app@0x10000）
# 4) 真机验收：按 docs/engineering/read-pico.md §4 的表逐项打勾
```

> Windows 上跑 `pio` 前先设 `PYTHONIOENCODING=utf-8`（见第 8 节 F-4），否则输出线程会被
> GBK 编码打死、构建静默卡住。

编译时**曾经优先怀疑、实际全部通过**的几处（已被编译器证明，保留作回归参照）：

- `BoardConfig.h` 的合并：一致性 `#if`、`FREEINK_MCU_S3` 计算、4 条 `static_assert`、
  `BoardProfile` / `TouchConfig` / `AudioConfig` 的**位置初始化**（数组成员必须给全）。
- `LgfxEpdConfig` 追加成员后，`BoardT5S3` / `BoardPaperS3` 的**旧 brace 初始化器是否仍合法**
  （这是零回归的关键，也是本次最容易被改坏的地方）。
- `HalDisplay.h` 里 `static constexpr uint16_t DISPLAY_WIDTH = BoardConfig::READ_PICO.displayWidth;`
  这类**类内初始化**能否通过。
- `BoardReadPico.cpp` 新装的三个 hook 调用（`setButtonHook` / `setPmuBatteryHook` / `setPmuTimeHooks`）
  与库依赖声明是否对齐。
- 镜像大小能否放进取 6.25 MB 的 OTA 槽（readpico 比其它目标多一个 M5GFX）。

---

## 8. 编译阶段暴露并修掉的问题（4 个）

首次 `pio run -e readpico` 依次暴露 4 个阻塞项，全部已修，最终构建通过。

| # | 位置 | 症状 | 修法 | 性质 |
|---|---|---|---|---|
| **F-1** | `freeink-sdk/.../PowerManager/src/PowerManager.cpp:14` | `error: 'HostShutdownHook' does not name a type` | 匿名命名空间里用类内别名要限定：`PowerManager::HostShutdownHook` | 本轮新代码缺陷 |
| **F-2** | `freeink-sdk/.../BatteryMonitor/src/BatteryMonitor.cpp:245` | `error: 'percentageFromMillivolts' was not declared in this scope` | 匿名命名空间的自由函数 `readGaugeSoc()` 里要限定：`BatteryMonitor::percentageFromMillivolts(mv)`（该静态成员在 public 区，第 82 行） | 本轮新代码缺陷 |
| **F-3** | `src/network/FirmwareBoardTag.cpp:42` | `#error "no FREEINK_DEVICE_* flag set; cannot derive board name"`，连带第 53 行 `expected ',' or ';'` | 该文件用 `FREEINK_DEVICE_*` 链自动派生板名，补 `#elif FREEINK_DEVICE_READPICO` → `"readpico"`（与环境名同风格） | **本轮完全没碰到的集成点** |
| **F-4** | `platformio.ini` 的 `lib_deps` | `UnknownPackageError: ... 'm5stack/M5GFX @ 0.2.20'` | 改为 `https://github.com/m5stack/M5GFX.git#0.2.20` | 依赖不可解析（见 D-A） |

**F-1/F-2 是同一类问题的两次重犯**：给 SDK 加"seam"时新引入的嵌套类型（`PowerManager::HostShutdownHook`、
`BatteryMonitor::PmuBatteryHook` 系列）在命名空间作用域被不加限定地使用。这类错误**静态通读极难发现、
编译器一眼就报**——如果 t11（独立静态验证）或 t12（评审门）跑过，最坏也只是同样漏掉，因为两者都被要求
"不编译"。**这是"先不编译"这个约束的直接代价，值得记录下来。**

**F-3 更值得注意**：它是本轮 5 个任务里**没有任何一个任务被指到过的文件**。板名派生、发布标签这类
"横切集成点"不在任何单点任务的视野里——只有真正编一次才会浮出来。

### 环境层面的两个坑（非代码问题，但会让人以为构建挂了）

- **GBK 控制台崩溃**：PlatformIO 打印 i18n 语言表里的阿拉伯文时抛
  `UnicodeEncodeError: 'gbk' codec can't encode character '\u0627'`，输出读取线程死掉、构建**静默卡住**
  （60 秒无任何新输出）。修法：跑 `pio` 前设 `PYTHONIOENCODING=utf-8`（配合 `PYTHONUTF8=1`）。
- **Espressif 组件 git 缓存损坏**：`%LOCALAPPDATA%\Espressif\ComponentManager\Cache\b_git_a38114cd`
  是个不完整的裸仓库，且 `objects/` 下有 12 个只读对象文件；IDF 组件管理器每次都走"删掉重克隆"，
  而 Python `shutil.rmtree` 在 Windows 上删只读文件直接 `WinError 5 拒绝访问` → CMake 配置失败。
  修法：把该目录**重命名挪走**（可回退，不要直接删），让管理器重建。
  注：该目录最后写入时间是 **9/11**，属**既有问题**，不是本轮中断构建造成的。

---

## 9. 按官方文档 + port-device-bsp 做的合规检查（2026-09-25）

对照 https://dot.mindreset.tech/docs/read_0 三个页面（index / start / firmware）与官方示例仓库
`MindReset/read_pico_firmware` 逐项核查。**最要命的一条是 S3 特有的**：文档里没写，但板级代码写了。

### 9.1 已修：四处 S3 配置缺陷 + 两处代码缺陷

**配置（全部限定在 `[readpico_hardware]`，不动共享段）**

| # | 问题 | 证据 | 处置 |
|---|---|---|---|
| **A1** ★ | **控制台占用了 GPIO43/GPIO44**：ESP32-S3 的默认 UART0 就是 GPIO43(U0TXD) / GPIO44(U0RXD)；而本板 GPIO43 是 **CST836U 触摸 INT#**、GPIO44 是 **TF 卡 D0** | 官方 `read_pico_board.h` 原话：「CST836U INT，原理图 TXD0 / GPIO43」；官方 `sdkconfig.defaults` 用 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` 规避。我们实测旧构建产物 `sdkconfig.readpico` 是 `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` 且 `# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is not set` | 加 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` + `CONFIG_ESP_CONSOLE_UART_DEFAULT=n` |
| **A2** | S3 data cache 只有官方一半 | 官方 `sdkconfig.defaults` 设 `DATA_CACHE_64KB` + `LINE_64B`，注释写明「渲染线程逐行流式读 PSRAM 里的 framebuffer，cache 越大预取越不容易失效」；我们实测 32KB/32B | 加 `64KB=y` + `32KB=n`（显式互斥，避免两条并存）+ `LINE_64B=y`；只改 readpico，其它 S3 目标保持 32KB |
| **A3** | 内部 RAM 未保留 | 官方 `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`；我们实测 `=0`。而 `Panel_EPD::_lut_2pixel` 需要 ~56.5 KiB **连续内部 DMA**，且 `init_intenal()` 失败是**静默**的（死屏无任何提示） | 加 `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768` |
| **A4** | 主任务栈只有官方一半 | 官方 `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`；我们实测 4096。eego A4 的先例是一旦有网络步骤落到该任务上就需要加到 16 KB | 加 `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` |

**代码**

| # | 位置 | 问题 | 处置 |
|---|---|---|---|
| **F5** | `freeink-sdk/.../InputManager/src/InputManager.cpp` `pollCst836u()` 帧无效分支 | 读帧失败时提前 `return 0`，而板级 strip 邮箱（单点、只在成功解码时被改写）保留着上一次的 `down=true` → `keyStripHook()` 会**持续上报一个按住的电容键**，直到下一次成功读取为止。即总线卡死时出现"幽灵按键" | 在该分支显式发布一次释放：`BoardReadPico::setStripRawPoint(0, 0, false)`。真实接触会在下一帧成功时重新发布，所以无害。**屏幕接触状态仍按原设计保留**（瞬时 I2C 失败不等于抬手） |
| **F6** | `freeink-sdk/.../display/FreeInkDisplay/src/driver/LgfxEpdDriver.cpp` `allocCanvas()` | `new lgfx::LGFX_Sprite(&g_dev)` 是裸 `new` 且**立即解引用**：本 SDK 用 `-fno-exceptions`，`new` 失败会直接 `abort()`（不是返回 nullptr），违反 `AGENTS.md` 黄金法则 #2；而这块是 Lgfx 路径上最大的单笔分配（w×h @8bpp 于 PSRAM） | 改 `new (std::nothrow)` + 空检查 + `Serial.printf` 报错（该 SDK 层用 `Serial.printf` 做诊断，无 LOG_ERR 宏）；并补 `createSprite()` 后 `getBuffer()` 为空时的处理（置空指针，让下游 `if (!g_canvas)` 守卫真正生效，而不是解引用空缓冲）。顺带给两条灰度平面分配补失败日志 |
| **F7** ★★ | `freeink-sdk/.../BoardReadPico/src/{BoardReadPico.cpp,ReadPicoLgfxConfig.cpp}` + `include/{BoardReadPico.h,BoardReadPicoPins.h}` | **硬件安全缺陷。** 原实现 `pmuVcomMv()` 在读不到 PMU 出厂 VCOM 时**回退到硬编码 1290 mV**，`epdPowerOn()` 随即把它写进 SY7636A 并拉高 `VCOM_EN` —— 即**用猜出来的屏幕电压去驱动面板**。板厂交机明确警告：屏幕电压参数出厂已写入 PMU，**不得更改，否则永久损坏硬件** | 删掉回退：`pmuVcomMv()` 现在只读，失败或超出 `500..2500 mV / 10 的倍数` 一律返回 **`-1`**；`epdPowerOn()` 把 VCOM 读取提到**任何上电动作之前**，见到 `-1` 立即返回 false，一个轨都不抬（结果是黑屏 + 一行日志，可恢复；猜错电压不可恢复）。删除 `READPICO_VCOM_FALLBACK_MV` 宏以消除再次误用的可能。全端口**从不写** PMU 的 VCOM（`read_pico_pmu_vcom_set()` 按官方文档仅供产线页面，未被调用） |

### 9.2 明确不做（附理由，不是遗漏）

- **A5**：官方 `read_0/firmware.mdx` 有一条排查说明「**唤醒后无法识别 USB**」（换 USB Type-A 线 / 再睡眠唤醒一次 / 重启）——S3 原生 USB 的已知问题。这不是固件缺陷而是**操作排查步骤**，已在 `read-pico.md` 记录。
- **蜂鸣器**：`platformio.ini:612-613` 自己写着，板子"只有通过扬声器路径与电平校准的实机验收之后"才 opt-in `[sound_feedback_hardware]`。本端口**零实机验收**，现在打开就违背项目自己的规则。需要时就是那三行：`board_build.embed_files` + `-DCROSSPOINT_CAP_SOUND_FEEDBACK=1` + `AudioManager` 依赖。
- **拿起唤醒**：`HalPowerManager.cpp:89` 已明确不武装 GPIO1。要在 SDK `Imu` 里配 `CTRL3` INT1_CFG + AOI1/HPIS1，而这些寄存器语义**现有 `Imu` API 不覆盖**；盲写加速度计中断配置正是"不要臆造寄存器语义"的禁区，故留作需求。
- **16 灰阶只用到 4 级**：取决于下面 9.3 的路线取舍。
- **`CONFIG_COMPILER_OPTIMIZATION_PERF`**：官方用 `PERF`，本端口保持 `-Os`——镜像已占 6.25 MB 槽的 **92.3%**，盲改 `-O2` 有撑爆唯一 app 槽的风险。要改就与体积实测一起做。
- **`bin/ci-check` 未加 readpico**：该脚本第 21 行是"Building default and supported hardware firmware"，而 readpico 本轮是 build-only、无实机验收。把新 env 加进共享 CI 脚本（每人每次 CI +30 分钟）属维护者决策，不擅自改。skill 的要求（"新目标若不在脚本里就必须显式构建"）已通过本轮显式构建 `pio run -e readpico` 满足。

### 9.3 路线重估：官方示例可直接重用（等你拍）

官方示例 Apache-2.0、明确可复用。我实测了可复用性：

- `cst836u` / `sc7a20h` / `fca9555` / `sy7636a` / `read_pico_pmu` 的 include **只用** `driver/gpio.h`、`driver/i2c_master.h`、`esp_err.h` —— **IDF 5.5.2 全部具备，可原样吃进来**。而我们为同一件事手写了约 900 行 C++。
- 唯一被 IDF 版本卡住的是 `read_pico/read_pico_flash_hpm.c`（需 IDF v6 的 `esp_flash_chips/spi_flash_override.h`）；我们不跑 120 MHz，可直接排除。
- **显示路径才是真正要定的那一个**：官方 `epdiy` + `e0470_epaper_waveform` 能**原生**跑 16×16 (from→to) 波形，**直接绕开 B3「忠实投影在数学上不存在」**，并拿到真正的 16 灰阶；但 **epdiy 是 LGPL-3.0-or-later，而 CrossMux 是 MIT**，静态链接需满足 LGPL 的可重链接等义务。现行 LovyanGFX 路线是 FreeBSD（宽松）但波形是近似的。
  → **波形正确性 ↔ 许可干净，这个取舍只能你定。**

### 9.4 skill 流程项

| 项 | 状态 |
|---|---|
| 更新两版 README | ✅ 本次已补（设备行 + 构建命令 + 整表烧录说明 + Windows `PYTHONIOENCODING` 提示） |
| `docs/engineering/index.md` 注册 | ✅ 本次已补 |
| `read-pico.md` 与事实一致 | ✅ 本次已补（§4 第 5/6/8 行改 `passed` 并填实测数字；§3.7 的 M5GFX 改为 git tag 形式；补 A1/A2/A3 三行） |
| 新目标要显式构建 | ⚠️ `bin/ci-check:21` 枚举 7 个 env，**没有 readpico** |
| 共享目标零回归 | ✅ `default`（共享 X3/X4 C3 镜像）SUCCESS；⚠️ S3 兄弟目标仍 pending——`eego_a4` 因本机 git 全局代理指向未监听的 `127.0.0.1:7890` 而失败（非代码问题），可用 `GIT_CONFIG_GLOBAL` 指向无代理临时配置绕过 |
| Nightly/OTA 不纳入 | ✅ 符合预期 |

---

## 10. 一句话提醒

编译通过只证明**它能生成一个镜像**，不证明这个镜像在硬件上做对任何一件事。
`read-pico.md` §4 里的 `pending` 项——面板、波形、触摸、键区、SD、电池、RTC、睡眠——
**全部**仍需真机逐项验收。而像 A1 那种"配置与板级事实冲突"的问题，**只有拿官方文档逐项对才会浮出来**，
编译器不会报、静态通读也极难发现。在那之前，不要把这个构建当成"适配完成"。
