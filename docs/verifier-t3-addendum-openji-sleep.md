# t3 补充判定 — "开机即睡"到底走哪个路径（回答 captain 的矛盾）

关键新输入：**captain 已恢复 SDK 闩锁（eego 限定、powerDownRailsForSleep 重加 latch0/1 hold），COM14 仍"开机即睡"（allowSleepAt≈4508ms 进 Sleep，无 "Auto-sleep triggered" 日志）。**
这直接否定"闩锁是开机即睡根因"——闩锁只决定入睡后是否掉电，不决定是否主动入睡。

---

## (a) 4508ms 进 Sleep 走哪个 enterDeepSleep —— 唯一可行：loop() 电源键"长按"路径 main.cpp:726-732

用排除法锁死（实机日志 boot_repro2 时间线）：

| enterDeepSleep 调用点 | 判定 | 证据 |
|---|---|---|
| setup `case AfterUSBPower: startDeepSleep`(main.cpp:481) | **排除** | 日志 `[ACT] Entering InxRecent`(2423ms)、InxRecent 多次 GFX 渲染(2611/3165ms)、`Wait complete A4 refresh`(3574ms)——设备已完整跑进 loop() 并多帧渲染；setup-time 入睡不可能渲染 InxRecent。且未见日志 "Wakeup reason: After USB Power" |
| setup `case PowerButton: verifyPowerButtonWakeup→startDeepSleep`(470) | **排除** | 未见日志 "Verifying power button press duration"；wakeupReason 走 AfterFlash/Other（无日志分支），resume=Splash→正常进 InxRecent |
| loop() 超时自动睡 (713-722) | **排除** | `enterDeepSleep(true)` 前必打 `[SLP] Auto-sleep triggered after ...`，日志**没有**这一行；且该路径不进 SleepActivity/不渲染 /sleep.bmp |
| loop() 电源键长按 (726-732) | **选中** | 唯一满足：①无 SLP 日志 ②`enterDeepSleep()` 会经 goToSleep→SleepActivity，与日志 `[ACT] Entering Sleep`(4507ms)+`[SLP] Loading /sleep.bmp`(4921ms) 完全吻合 ③受 `millis()>=allowSleepAt` 门控，与 captain 观察的"allowSleepAt≈4508ms 触发"吻合 ④A4 非 PaperMono，排除 744 |
| loop() PaperMono 短路 (739-744) | **排除** | A4 非 FREEINK_DEVICE_PAPERMONO |

**时间线自洽性**：
- setup 在路由到 goToInxRecent 后即结束（InxRecent 的绘制在 loop() 内跑，见 2611/3165/3574 在 loop 多帧）；`allowSleepAt = setup_end + 2000`。
- 设 setup_end≈2450ms → allowSleepAt≈4450ms；日志 4507ms 入睡，`4507>=4450` 成立。
- 条件 `gpio.isPressed(BTN_POWER) && gpio.getPowerButtonHeldTime() > getPowerButtonDuration(默认400ms)`：即 **GPIO8(active-high) 在 ~4000ms 起被读为持续 HIGH（未按电源键却被判定"按住"）**，一旦 allowSleepAt 到点即触发入睡。
- `getPowerButtonDuration()`(CrossPointSettings.h:417-419) 默认 shortPwrBtn=IGNORE → **400ms** 阈值（非 SLEEP 的 10ms）。
- 3574ms 那笔 "Wait complete A4 refresh" 是 InxRecent 在更早 loop 迭代的绘制；电源键判定(726)在 loop() 的 `activityManager.loop()`(768) **之前**，故 4450-4507ms 一到即切入 Sleep。

**结论 (a)**：4508ms 的开机即睡是 **main.cpp loop() 的电源键"长按"(726-732) 路径**，由 **GPIO8 被误读为按住**触发；**不是** setup 的 AfterUSBPower/verifyPowerButtonWakeup，也**不是**超时自动睡。

---

## (b) 为什么 fe2b9eeb 正常、118aa1e0 开机即睡

### decisive 逻辑
- captain 已证明**闩锁恢复不能消除开机即睡** → 开机即睡**不是闩锁/电源保持问题**，是**触发问题**（GPIO8 误判按住→726 入睡）。
- 闩锁（t2 CRITICAL-1）只解释**入睡后掉电**（长按睡眠失效=硬关机、断电瞬间 EPD 残留），**不解释主动入睡**。

### 触发端为何 118aa1e0 会误判、fe2b9eeb 不会
- A4 电源键读取逻辑(getDigitalState 294-301、begin pinMode INPUT_PULLDOWN、update DigitalButtons 路径) 经 t2 逐行核对在 c9e9d2f6↔60fed53b 对 A4 **行为等价**。
- 即：**GPIO8 被动读 HIGH** 这件事本身不是 SDK 改动新引入的读取差异；而是 **GPIO8 在 118aa1e0 上被外部拉 HIGH 或在 boot 后被重配导致读 HIGH**。
- 关键嫌疑（需实机 GPIO 示波/烧日志验证，属工程确认而非代码静态可证）：
  1. A4 板级复用/上拉：candidate——A4 继承 X4-Pro 布局，GPIO8 在 X4 Pro 是前光冷色 PWM(FrontlightManager.h:11)。若 A4 前光或某初始化在不同时序把 GPIO8 从 INPUT_PULLDOWN 顶成高，则 118aa1e0 的 **SplashlessWake/`setupDisplayAndFonts(seamless)` 时序重组**（t1 h1、h3、h5 改变 boot 呈现与显示初始化顺序）可能触到这个复用冲突；fe2b9eeb 的 old 时序未触碰。
  2. `await allowSleepAt` 门控本身在 fe2b9eeb 与 118aa1e0 **相同**（旧版也有该行），故不是 allowSleepAt 本身新增；差异在"GPIO8 是否被读 HIGH"。

### 诚实声明（证据边界）
静态代码能 100% 锁定**触发路径 = 726**，但**无法从代码静态证明哪一行令 GPIO8 在 118aa1e0 读 HIGH**（可能是硬件/时序/复用）。要坐实需：实机在 loop 打印 GPIO8 数字电平或 pinMode；或验证 GPIO8 是否被驱动/复用。fe2b9eeb 对比侧也应确认其 loop() 同样读 GPIO8 且当时为 LOW。

---

## 最终判定：两个问题，各自根因与最小修复

**问题 1：开机即睡（4508ms 自动入睡 / 长按睡眠失效的硬断电）**
- 触发根因（本次定论）：**GPIO8 被误判"按住" → main.cpp loop() 726-732 电源键长按入睡**；与闩锁无关（captain 实测已证）。
- 电源保持根因：**t2 CRITICAL-1 闩锁删除**（入睡后掉电=硬关机=长按睡眠"不生效"/唤醒变冷启动）。
- **最小修复（触发侧，最该先做）**：限定 eego/A4，在主 loop 电源键判定处(726)对 A4 报告 GPIO8"按住"时**打印电平/校验**，或直接**只在真正确认 GPIO8 硬件按下（如 isPowerButtonPhysicallyPressed 连续稳定）才放行**。最小且安全的做法：给 A4 在 `millis() >= allowSleepAt` 之前确保 GPIO8 处于 INPUT_PULLDOWN 且不被复用驱动；若要立即止血，可对 A4 把电源键"长按入睡"门槛提高，或在 boot 后主动把 GPIO8 重配为 INPUT_PULLDOWN + 丢弃一次污染状态。
  - 首选实证方向：先加日志确认 GPIO8 在入睡时刻的数字电平，坐实"复用/外部拉高"后再定改 pinMode 还是改复用。**不要直接堆防御**。
- **最小修复（保持侧）**：仅对 battery-latched 板恢复 latch0 sleep-hold（F1，先前已给出）。**注意：此修复解决"入睡后稳定保持+可靠唤醒"，不解决开机即睡。**

**问题 2：唤醒卡残留画面**
- 根因：**main.cpp SplashlessWake 重构**（t1 h1-h5）：无条件 showBootScreen=false + 无帧不清屏 + 阅读 goToReader 分支丢弃 needsWakeRefresh → 滞留睡眠帧上 HALF_REFRESH，A4 UC8279C 灰阶鬼影。
- **最小修复**：SplashlessWake 无帧时 (a) goToReader 分支也 requestNextRefresh(HALF_REFRESH)（与 Home 对齐）；或 (b) 恢复无帧 goToBoot() 清屏 splash。建议 (a)。

---

## 建议的验证序列
1. **先证触发**：A4 烧 log 版本，loop 每次打印 `gpio.isPressed(BTN_POWER)` + PWR_HOLD_TIME；确认 4000-4508ms GPIO8 是否持续 HIGH。→ 决定 GPIO8 修复方向。
2. 修触发（问题1）后再烧 F1 闩锁，验证长按能驻留+电源键可靠唤醒。
3. 最后 F2 修唤醒残留；COM6+问题板各验。
   - 顺序理由：开机即睡不解决，后续唤醒/残留验证都无从做。
