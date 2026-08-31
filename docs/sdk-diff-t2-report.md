# Task t2 — freeink-sdk 差异对 EEGO A4 的影响分析

SDK 子模块对比：`c9e9d2f6` (基线, 旧版) → `60fed53b` (当前, 对应 crossmux fe2b9eeb↔118aa1e0)。
目标: 找出对 EEGO A4 (FREEINK_DEVICE_EEGO_A4) 有实际影响的改动, 聚焦"开机即睡 + 长按睡眠失效 + 唤醒卡残留画面"。

范围外声明:
- `Uc8279cA4Driver.cpp/.h`(A4 显示驱动) 在两个 pin 之间 **完全未改动**(git diff 0 行)。A4 的显示/唤醒刷新路径与旧的 fe2b9eeb 一致。
- `Uc8279X4Driver` 大改(+356/-) 但与 A4 **无关**: FreeInkDisplay::selectDriver 对 A4 走 `uc8279cA4Driver()` (FREEINK_DRIVER_UC8279C_A4), 不经过 Uc8279X4Driver。波形/刷新差异不影响 A4。
- `FreeInkDisplay.h/.cpp` 大改全部为 Murphy M4 / PaperColor(SPECTRA) 专用; A4 的 X4/PanelSel 路径未动。

A4 profile (c9e9d2f6 与 60fed53b 一致, 无变化):
```
EEGO_A4 = { board=EegoA4, inputStyle=DigitalButtons, UC8279C, 768x552 }
  buttons {back,confirm,left,right,up,down,power,pwrActiveHigh} = {-, -, -, -, 5, 7, 8, true}
  touch = Gslx680 (sda2/scl1, reset=3)
  power latch = {4}  → latch0=GPIO4, latch1=UNASSIGNED
  frontlight = LM3630A (i2c 2/1, enable=12)
```

---

## 对 A4 影响排序的完整差异清单

### [CRITICAL-1] PowerManager::powerDownRailsForSleep() 删除 A4 电池闩锁保持
- 位置: libs/hardware/PowerManager/src/PowerManager.cpp (merge 6cda207 "Merge upstream main" 引入)
- 新旧对比:
```cpp
// OLD (c9e9d2f6):
  holdRailLevel(b.display.rst, resetSleepLevel);
  // Keep battery-latch rails asserted while the switchable peripherals sleep.
  holdRailLevel(b.power.latch0, HIGH);   // <-- A4 latch0=GPIO4 被置位并 hold
  holdRailLevel(b.power.latch1, HIGH);
  holdRailLevel(b.display.powerEnable, LOW);

// NEW (60fed53b):
  holdRailOff(b.display.rst, resetSleepLevel);   // 函数重命名为 holdRailOff
  holdRailOff(b.display.powerEnable, LOW);        // latch 两行被删除
```
- 函数语义从"把引脚驱动到指定电平并 latch"改为只负责"关(off)轨道"; helper 由 `holdRailLevel(pin, level)` 更名 `holdRailOff(pin, offLevel)`。
- A4 影响判定: **高危**。A4 是电池闩锁板(latch0=GPIO4,main.cpp:374 boot 时 `holdPowerRails()` 把 GPIO4 置 HIGH, 但**不** gpio_hold_en)。旧代码在 sleep 前会:
  `gpio_hold_dis(4) → pinMode(4,OUTPUT) → digitalWrite(4,HIGH) → gpio_hold_en(4)`,
  随后 `deepSleep()` 调 `gpio_deep_sleep_hold_en()`, 使 GPIO4 输出在深睡期间被锁定保持 → 主电源 MOSFET 保持导通。
  新代码 **从未** 对 GPIO4 调用 gpio_hold_en。进入 deep sleep 时 `esp_sleep_config_gpio_isolate()` 断开 pad, 无 GPIO4 hold → 闩锁 MOSFET 栅极失去驱动 → **电池电源释放, 设备睡眠即断电(冷关机)**。
- 与症状的关联:
  - "开机即睡/长按睡眠失效/唤醒卡残留": 设备无法真正进入"深度睡眠但保持供电并等电源键唤醒"的闩锁保持状态。
  - "唤醒卡残留画面": 若靠 USB 供电时主 MCU 仍被深睡眠逻辑停在关态/或断电瞬间 EPD 帧未清空, 残留上一帧画面; 与 latc0 释放造成的不稳定电源域有关。
  - 这是 upstream 为不带闩锁的板子(纯靠 MCU 保持)做的语义简化, 但 crossmux 分支的 battery-latched 板(A4/Sticky/X4) 依赖 sleep 中的 latch hold, 被一并删除。**必须为 A4 恢复 GPIO4 的 sleep 期间 hold。**

### [HIGH-1] InputManager::begin() 电源脚 pinMode 增加 A4 门控
- 位置: libs/hardware/InputManager/src/InputManager.cpp (merge 6cda207)
- 新旧对比:
```cpp
// OLD:
  if (BoardConfig::ACTIVE.input.power >= 0) {
    pinMode(power, powerActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  }
// NEW:
  #if FREEINK_DEVICE_EEGO_A4
  if (BoardConfig::ACTIVE.board == BoardConfig::Board::EegoA4 && BoardConfig::ACTIVE.input.power >= 0) {
    pinMode(power, powerActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  }
  #endif
```
- A4 影响判定: 对 A4 **本身无影响**(board==EegoA4 恒真, 电源脚 GPIO8 仍被配置为 INPUT_PULLDOWN)。但该改动把"有电源脚的板子在 begin 阶段配置电源脚"的功能收缩到仅 EegoA4; 其它带电源键的板(Xteink X4、Sticky、Murphy M4) 在 InputManager::begin 不再配置电源脚, 依赖 PowerManager 的 armPowerButtonWakeup 覆盖。**对 A4 无影响, 但暴露了上游把 A4 电源脚逻辑做成特例的方向**。

### [HIGH-2] InputManager::getDigitalState() 电源键判定重构 (A4 分支保留原意)
- 位置: InputManager.cpp (merge 6cda207)
- 新旧对比:
```cpp
// OLD:
  const int8_t powerPin = input.power;
  const bool powerPressed = powerPin >= 0 &&
      digitalRead(powerPin) == (powerActiveHigh ? HIGH : LOW);   // 按 active 电平
  if (powerPressed && !BackHold && !PowerHold) state |= (1<<BTN_POWER);
// NEW:
  #if FREEINK_DEVICE_EEGO_A4
  if (board==EegoA4 && input.power>=0 &&
      digitalRead(input.power)==(powerActiveHigh?HIGH:LOW) &&   // A4 走 active 电平
      !BackHold && !PowerHold) state |= BTN_POWER;
  else
  #endif
  if (isDigitalPressed(input.power) &&                          // 非 A4: 恒 active-LOW
      !BackHold && !PowerHold) state |= BTN_POWER;
```
- A4 影响判定: **无影响**。A4 (powerActiveHigh=true, GPIO8) 命中第一个分支: `digitalRead(8)==HIGH` 判定按下, 与旧版完全一致。非 A4 分支改为 active-LOW 语义, 不影响 A4。
- 同时新增公开接口 `bool isPowerButtonPhysicallyPressed()`(纯新增, 供调用方读取原始电平), 不改变 A4 行为。`updateDigitalTwoButton` 的 PWR_BTN pressStart 逻辑未变。

### [LOW-1] FrontlightManager LM3630A 路径加 A4 门控 (功能保留)
- 位置: libs/hardware/FrontlightManager/src/FrontlightManager.cpp (merge 6cda207)
- 新旧对比:
```cpp
// OLD: begin(): if (i2c.controller==Lm3630a) { ...Wire.begin... }
//      apply(): if (i2c.controller==Lm3630a) { applyLm3630a(); return; }
// NEW: begin(): if (board==EegoA4 && i2c.controller==Lm3630a) { ... }
//      apply(): if (board==EegoA4 && i2c.controller==Lm3630a) { applyLm3630a(); return; }
```
整段 LM3630A 代码包在 `#if FREEINK_DEVICE_EEGO_A4`。
- A4 影响判定: **无影响**。A4 是唯一设置 Lm3630a 的板子, 条件 `board==EegoA4 && controller==Lm3630a` 恒真 → 行为与旧版一致。仅 gamma 映射常量表等宽重排, 无逻辑变化。(前灯与睡眠症状无关。)

### [INFO] BoardConfig.h 结构调整 — A4 profile 字段本身未变
- BoardConfig.h +93/-; 新增 `chargeEnable`(Sticky/Murphy)、`batteryChargeStatusActiveHigh`(X4Pro/LilyGo)、移除 `DisplayController::SSD1683` 枚举、`holdPowerRails()` 增加 chargeEnable 处理。
- EEGO_A4 profile 各字段(latch {4}、power=8/activeHigh、touch Gslx680) 完全未变。
- A4 影响判定: 无直接影响, 但 `holdPowerRails()` 的 latch 处理保持"不 hold_en", 与 [CRITICAL-1] 互为印证(闩锁的 sleep-hold 只存在于被删除的 PowerManager 两行)。

### [INFO] 其余大改均与 A4 无关
- InputManager: MurphyM4 FT6336U 后台轮询任务、multi-touch rotation、chsc6x 坐标映射、LilyGo/T5 相关 → 全部 `#if FREEINK_DEVICE_MURPHY_M4/_MURPHY/_WAVESHARE` 门控, 不编译进 A4。
- BatteryMonitor/Rtc/SDCard/UsbMassStorage/XteinkDetect: 无 A4 睡眠/电源键相关改动。
- FreeInkUI/MurphyM4Batch: A4 不涉及。

---

## 关键可疑点 (供最小修复)

1. **[必须] PowerManager::powerDownRailsForSleep() 恢复对 A4 (及所有 battery-latched 板) 的 latch0 保持**:
   在 `holdRailOff(b.display.powerEnable, LOW)` 附近按板型(至少 `latch0>=0` 且非总线冲突)重新加入
   `gpio_hold_dis → pinMode OUTPUT → digitalWrite(HIGH) → gpio_hold_en` 到 GPIO4,
   使闩锁在 deep sleep 期间保持闭合。这是唯一能解释"睡眠即断电/开机即睡/无法稳定睡眠"的 SDK 级根因改动。
   → 应仅对 `FREEINK_DEVICE_EEGO_A4`(及需要闩锁的 Sticky/Xteink)条件恢复, 避免回退 upstream 对纯 MCU 保持板(S3 series) 的语义简化。**拒绝为一个无关板加防御补丁。**

2. **确认 A4 的 `holdPowerRails()`(boot) 在唤醒冷启动后正确执行**: main.cpp:374 已调用。若设备因 latch 释放进入冷启动, boot 会重新 holdPowerRails → 设备立刻重新上电 → 若 app 因残留状态又立即进入 sleep, 就表现为"开机即睡"死循环。这与 [CRITICAL-1] 相扣, 需验证 boot 后是否立即触发自动睡眠路径。

3. **验证唤醒残留画面**: A4 显示驱动未变, EPD 帧应为正常。残留画面更可能是"断电瞬间(due to latch 释放)未完成刷新/未走 deepSleep command"造成的静态残留, 而非驱动 bug。

## 结论
freeink-sdk 从 fe2b9eeb↔118aa1e0 对 A4 的**唯一实质性破坏改动**是 `6cda207` merge 在
`PowerManager::powerDownRailsForSleep()` 中删除的电池闩锁(latch0=GPIO4)保持两行。
其余 A4 相关改动(power 脚门控、getDigitalState 重构、LM3630A 门控)经逐一验证对 A4 行为**无影响**。
最小修复集中在恢复 A4 sleep 期间的 latch0 hold, 其余为分析结论、不动代码。
