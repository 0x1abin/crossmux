# 根因报告：118aa1e0 开机约 4.5s 自动睡眠（EEGO A4）

> 状态：根因已确认，修复已实现，待真机验证。
> 涉及提交：crossmux fe2b9eeb → 118aa1e0；SDK 子模块 c9e9d2f6 → 60fed53b。

## 现象

刷入 118aa1e0 后：开机 → 显示语言/主页 → 约 4504–4508ms 自动进入睡眠
（`Entering activity: Sleep`），日志中**没有** `SLP Auto-sleep triggered`
（即不是空闲超时路径，而是 main.cpp 的电源键长按守卫路径触发）。

## 证据链（全部为实测/编译期证据，无推断）

1. **编译期**：`.pio/build/eego_a4/compile_commands.json` 显示
   `InputManager.cpp` 的编译命令包含 `-DFREEINK_DEVICE_EEGO_A4=1`
   —— 排除"宏未传到 symlink 库编译"的假设。
2. **运行时**（临时 PWDIAG 日志，已移除）：`raw8=0`（GPIO8 物理电平
   始终为低，引脚正常），但 `isPressed(BTN_POWER)=1` 且
   `held`（`getPowerButtonHeldTime()`）从 ~184ms 持续增长到 ~1789ms，
   直到 `allowSleepAt=4424ms` 后 main.cpp 的电源长按守卫触发。
3. **代码比对**：`git diff c9e9d2f6 60fed53b -- libs/hardware/InputManager/src/InputManager.cpp`
   显示电源键读取被重构：

旧版（fe2b9eeb 配套，设备正常）：
```cpp
const bool powerPressed =
    powerPin >= 0 && digitalRead(powerPin) == (powerActiveHigh ? HIGH : LOW);
if (powerPressed && 非 Hold 风格) state |= (1 << BTN_POWER);
```

新版（118aa1e0 配套，损坏）：
```cpp
#if FREEINK_DEVICE_EEGO_A4
if (board==EegoA4 && power>=0 &&
    digitalRead(power)==(powerActiveHigh?HIGH:LOW) && 非 Hold 风格) {
  state |= (1 << BTN_POWER);
} else
#endif
if (isDigitalPressed(power) /* == digitalRead(pin)==LOW */ && 非 Hold 风格) {
  state |= (1 << BTN_POWER);
}
```

## 根因

新版给 A4 分支加了 **else 兜底**：`isDigitalPressed(pin)` 恒为低电平有效
（`digitalRead(pin)==LOW`）。A4 电源键 GPIO8 为高电平有效 + 内部下拉，
空闲电平是 LOW——恰好满足兜底分支的"按下"条件：

- A4 分支要求 `digitalRead(8)==HIGH`，空闲时**不成立**；
- 于是每次采样都落入兜底分支，把空闲 LOW 判为"按下"；
- 结果 `BTN_POWER` 自开机起**恒置位、永无释放**，
  `powerButtonPressStart` 定格在首次采样时刻，`held` 单调增长；
- 过了 `allowSleepAt`（setup 结束 + 2s）后，main.cpp 的
  `isPressed(BTN_POWER) && getPowerButtonHeldTime() > duration` 守卫
  必然成立 → 触发睡眠（~4508ms，与实测一致）。

这也解释了连带症状：电源键状态机永远处于"按下"，短按/长按/释放事件
全部错乱（长按睡眠逻辑失效、唤醒后再次自动睡等）。

fe2b9eeb 正常的原因：旧版只有一条极性感知读取，没有低电平兜底。

## 修复（eego 限定，已批准）

保留作者的 `#if FREEINK_DEVICE_EEGO_A4` 结构，但把 A4 分支改为**自包含**
的极性感知读取——A4 不再落入低电平兜底；非 A4 板（如 Murphy M4 的
低电平电源键）保持 60fed53b 原逻辑不变：

```cpp
#if FREEINK_DEVICE_EEGO_A4
  if (BoardConfig::ACTIVE.board == BoardConfig::Board::EegoA4) {
    // A4 power key: self-contained polarity-aware read. The generic
    // LOW-active fallback below must never see A4's idle pull-down level —
    // it would report the un-pressed key as held forever (root cause of the
    // 4508ms boot-time sleep on 118aa1e0). Non-A4 boards keep upstream logic.
    if (BoardConfig::ACTIVE.input.power >= 0 &&
        digitalRead(BoardConfig::ACTIVE.input.power) ==
            (BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW) &&
        BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmBackHold &&
        BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmPowerHold) {
      state |= (1 << BTN_POWER);
    }
  } else
#endif
  if (isDigitalPressed(BoardConfig::ACTIVE.input.power) && ...) {
    state |= (1 << BTN_POWER);
  }
```

配套改动（本会话此前已确认）：
- **F1 已被上游实现，本 PR 不再包含**：上游 SDK e39bbd0（crossmux 7489ae5d
  `fix(eego-a4): restore deep-sleep wake` 配套）已在 `powerDownRailsForSleep()`
  中加入等效逻辑（`holdRailLevel(b.power.latch0, HIGH)`，A4 电池锁存 GPIO4
  睡眠期间保持）——修复"长按睡眠 → 硬断电 → 唤醒冷启动"。真机验证时本会话
  固件所用的 F1 与该上游实现功能等价。
- **F2**（`main.cpp`）：SplashlessWake 后从阅读器唤醒时补一次
  `HALF_REFRESH` —— 修复"从图片退出后屏幕残留不刷新"。上游 7489ae5d
  未涉及此区域，F2 保留在本 PR。
- PWDIAG 临时日志：已移除。
- 本 PR 实际内容：SDK 侧仅 InputManager 电源键兜底修复；crossmux 侧仅
  main.cpp F2 + 子模块指针 + 本报告。

## 验证计划

1. 构建 eego_a4 → IDF esptool 烧录 COM14（DIO/80MHz/16MB）。
2. 串口确认：开机不再于 ~4508ms 睡眠；空闲 `isPressed(BTN_POWER)=0`。
3. 短按电源键 → 睡眠；长按 → 睡眠/强制重启（硬件行为）。
4. 唤醒后无残留帧（F2），睡眠期间电池锁存保持（F1，唤醒非冷启动）。
