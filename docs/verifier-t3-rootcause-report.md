# t3 综合根因报告（verifier 审阅定稿）— EEGO A4 开关机睡眠异常

比较范围：crossmux 主仓 `fe2b9eeb`（正常基线）→ `118aa1e0`（最新，复现），含 freeink-sdk 子模块（对应 pin `c9e9d2f6` → `60fed53b`）。
基于 t1（main-diff 主仓 main.cpp/hal）+ t2（sdk-diff freeink-sdk）差异清单，做交叉核对、误排校验与实机日志对齐后定稿。

---

## 0. 关键事实核对（verifier 独立复核，非转述）

| 事实 | 结论 | 证据 |
|---|---|---|
| 设备实际编译的 SDK | **crossmux/freeink-sdk @ 60fed53b**（子模块工作树，与 crossmux HEAD 指针一致） | `platformio.ini:139-148` 用 `symlink://freeink-sdk/...`；`crossmux/freeink-sdk` HEAD=60fed53b |
| ※ 易错点 | 工作区里另有一个 `D:\CrossSIGO\freeink-sdk` @ **f479d78**（eego-A4 fork，**仍含** latch hold）是**独立 checkout，crossmux 不编它**。初核易被误导 | 两个目录 HEAD 不同；BuildConfig 引用的是 `symlink://freeink-sdk`（相对 crossmux env → crossmux 内子模块），不是绝对路径 `D:\CrossSIGO\freeink-sdk` |
| SDK PowerManager 现状 | **60fed53b 已删除 A4 闩锁保持**：`powerDownRailsForSleep()` 用 `holdRailOff()` 只关轨道，无 `holdRailLevel(power.latch0,HIGH)` | `crossmux/freeink-sdk/.../PowerManager.cpp:63,73,83-90`（未见 latch 行） |
| A4 电源键 GPIO | **GPIO8，active-high**（t1 误写成 GPIO6；GPIO6 是显示字段引脚） | `BoardConfig.h:1508` `{...5,7,8,true}` |
| A4 闩锁 | `latch0=GPIO4`，boot 时 `holdPowerRails()` 只置 HIGH、**不 gpio_hold_en** | `BoardConfig.h:1527` `{4}`；main.cpp:374 |
| 实机 118aa1e0 | 开机后 Boot→InxRecent→FileBrowser→**Sleep 活动**约 4.5s 自动进入（无 `Auto-sleep` 日志、无用户输入） | `boot_sdkrestore.log:40-52`、`boot_repro2.log:34-43`、`boot_gate.log` 同模式 |
| 实机"suppress"固件 | 进入 InxRecent 后**不再自动睡眠**（第 5473ms 仅低功耗降频，不入睡） | `boot_suppress.log:48-51` |
| 实机 COM6 正常单元 | EpubReader→InxRecent→Settings 全程正常，不自动睡 | `boot_com6.log:36-51` |

---

## 1. 交叉核对：排除误标 / 查重

### t2（sdk-diff）——逐条校验
- **CRITICAL-1 闩锁删除：成立且适用于实际编译产物**（见 §0）。t2 对比 `c9e9d2f6→60fed53b` 正确；关键补充是确认 60fed53b 就是 crossmux 实际编译的子模块。**成立。**
- HIGH-1（InputManager::begin 电源脚 A4 门控）：对 A4 行为无影响（board==EegoA4 恒真，GPIO8 仍配 INPUT_PULLDOWN）。**保留但非根因。**
- HIGH-2（getDigitalState 重构）：A4 命中 active-high 分支，行为与旧版一致。**排除。**
- LOW-1（LM3630A 门控）、BoardConfig 结构、Uc8279cA4Driver 零改动：对 A4 无影响。**排除。**

### t1（main-diff）——逐条校验
- **h1-h5 SplashlessWake 重构：成立**，且为设备无关但 A4（X4 型灰阶非差分）最易见鬼影。逐行已在我代码复核中确认（main.cpp `showBootScreen=false` 208 行、`resume` 解析 521-524 行、SplashlessWake 帧回退 560-564 行、`needsWakeRefresh` 仅 Home 分支 606 行、非 quick-resume 删帧 226-229 行）。**成立。**
- **h6"开机即睡"归因：需修正**。t1 把"开机即睡"归到 `verifyPowerButtonWakeup/AfterUSBPower -> startDeepSleep`（未改路径）。但未改路径无法解释"fe2b9eeb 正常、118aa1e0 开机即睡"。真正让该路径**变成开机即睡死循环**的，是 t2 的闩锁删除（见 §3 机制）。故**开机即睡的主因是 t2 闩锁，不是 t1 的 h6**；h6 是被放大的既有行为。
- **h7（"A4 用裸 GPIO6 电源键"）**：电源键实为 GPIO8，属 t1 笔误，不影响结论。
- h8-h11：无关项，排除。

---

## 2. 三个症状——各自的机制归属

### 症状 A：开机即睡（~4.5s 自动入睡）
根因链（**主因：SDK 闩锁删除**）：
1. A4 是电池闩锁板（latch0=GPIO4，靠该 MOSFET 在深睡时维持供电并等待电源键 RTC 唤醒）。
2. 60fed53b 的 `powerDownRailsForSleep()` 删了 `gpio_hold_en(4)`，进入 deepSleep 时 `esp_sleep_config_gpio_isolate()` 把 GPIO4 断 pad / high-Z。
3. 闩锁 MOSFET 栅极失去驱动 → **深睡即电池断电（真正的冷关机）**。
4. 冷关机后靠 USB 复插/掉电再上电唤醒 → 唤醒原因是 `AfterUSBPower`（或 `Other`）→ main.cpp `case AfterUSBPower: startDeepSleep(gpio)` 立刻再次入睡 → **开机即睡：设备永远停不在开机态**，仅短暂闪正常画面（boot 日志里 Boot/InxRecent/FileBrowser 正常渲染后仍回 Sleep）。
5. "suppress"固件能停住，因为它绕过了触发入睡的路径（对应 t1 h5 的 Home 分支 / 或改动了 allowSleep gating），佐证该路径确是被触发的点。
除闩锁主因外,InputManager 电源键误判(h6)仅是把手;盘在闩锁坏后,任何一次"入睡"都会变硬断电循环。

### 症状 B：长按睡眠失效
- 闩锁删除下，长按电源→ `enterDeepSleep()` → 正常画睡眠屏 → `powerDownRailsForSleep()`（无 latch hold）→ 深睡 → **电池断电**。设备实际是硬关机，不是"保持供电等电源键唤醒"。
- 结果：长按后设备冷关机，再按电源键是掉电重启（冷 boot），唤醒路径与"保留的睡眠状态"完全丢失 → 表现为"长按睡眠不生效/睡眠功能异常"。睡眠屏画完即断电，若在 EPD 刷新未完成的断电瞬间，就在面板留下该帧残影。

### 症状 C：唤醒卡残留画面前光亮
根因链（**主因：main.cpp SplashlessWake 重构**，独立于闩锁）：
1. `enterDeepSleep` 无条件 `showBootScreen=false`（h1）→ 下次电源键唤醒走 `SplashlessWake`。
2. SplashlessWake 下无 sleep-frame 时（默认 DARK 睡眠本就不生成 quick-resume 帧，且 h4 又把旧帧删掉）不再 `goToBoot()` 清屏（h3 只置 `needsWakeRefresh`）。
3. `needsWakeRefresh` 只在 **Home 分支** 触发 HALF_REFRESH（h5）；**阅读恢复路径（goToReader）直接丢弃**，导致滞留面板的睡眠画面不被清掉。
4. A4 是 X4 型/UC8279C 灰阶非差分刷新，在滞留睡眠帧上做 HALF_REFRESH 最易鬼影/残留 → "唤醒卡残留画面"。设备被 USB 供电时 MCU 不因闩锁掉电，此路径完整可触发（"前光亮"说明设备带电、只是画面上帧残留）。

---

## 3. 根因清单（按可能性排序，附证据呼应）

| 排序 | 根因 | 归属 | 证据 | 影响症状 |
|---|---|---|---|---|
| **R1** | **SDK 闩锁删除**：`powerDownRailsForSleep()` 删除 A4 latch0(GPIO4) sleep-hold，深睡即电池断电 | freeink-sdk 60fed53b | §0 事实表；PowerManager.cpp:63,73,83-90；HalPowerManager.cpp:114,141 | A 开机即睡、B 长按睡眠失效（硬断电循环） |
| **R2** | **main.cpp SplashlessWake 重构**：无条件 `showBootScreen=false` + 无帧不清屏 + 阅读恢复路径不刷新 | 主仓 main.cpp h1-h5 | main.cpp 208/521-524/560-564/606/226-229；commit 4867cdff+497d65a0 | C 唤醒卡残留画面 |
| R3 | InputManager 电源键误判（h6/AfterUSB）→ 二次入睡 | 主仓（未改路径，被 R1 放大） | main.cpp case AfterUSBPower; verifyPowerButtonWakeup | 放大 A |
| R4 | 显示刷新/Uc8279cA4Driver | **排除**（两 pin 间 0 改动） | t2 排除项 | — |

**主根因判定**：R1（SDK 闩锁删除）是"开机即睡 + 长按睡眠失效"的**首要动力**（它是设备真正断电/无法驻留开机的物理开关层根因）；R2（SplashlessWake）是"唤醒残留画面"的**独立显示层根因**。两者同属 fe2b9eeb→118aa1e0 范围，分别为 SDK 子模块与主仓 main.cpp 的两处非关联回归，必须分别修复。

---

## 4. 最小修复建议（证据驱动、限定作用域、不堆叠防御补丁）

### 修复 F1（对应 R1）— 恢复 A4 睡眠期间 latch0 闩锁保持
- 位置：`crossmux/freeink-sdk/libs/hardware/PowerManager/src/PowerManager.cpp::powerDownRailsForSleep()`
- 改动：在 `holdRailOff(b.display.powerEnable, LOW)` 附近，按"板型带闩锁"（`FREEINK_DEVICE_EEGO_A4`，必要时含同样 battery-latched 的 Sticky/Xteink）恢复：
  ```cpp
  if (b.power.latch0 >= 0) {
    gpio_hold_dis((gpio_num_t)b.power.latch0);
    pinMode(b.power.latch0, OUTPUT);
    digitalWrite(b.power.latch0, HIGH);
    gpio_hold_en((gpio_num_t)b.power.latch0);
  }
  ```
- 作用域：**仅 battery-latched 板**（EEGO_A4 必须；Xteink X4/X4Pro / Sticky 若同样依赖开关闩锁也纳入）条件编译恢复；不触碰 60fed53b 对"纯 MCU 保持板"（S3 series 无闩锁）的语义简化，避免对其它板引入回归。
- 拒修事项：不做全局回退、不动 `holdRailOff` 命名与其余轨道关闭逻辑；只补闩锁 hold 一行族。

### 修复 F2（对应 R2）— 恢复"唤醒即清屏"，尤其阅读恢复路径
- 位置：`crossmux/src/main.cpp`（SplashlessWake 帧回退 + 路由）
- 目标：非 quick-resume 睡眠被电源键唤醒时，确保首帧把滞留睡眠画面清掉。
- 最小改动二选一（建议 A，最少）：
  - **A**：在 `goToReader` 恢复阅读分支（`main.cpp:614` 附近 **也执行** `renderer.requestNextRefresh(HALF_REFRESH)`，与 Home 分支 h5 对齐），使阅读恢复路径同样清睡眠帧。
  - **B**：恢复为 SplashlessWake 无帧时回退 `activityManager.goToBoot()`（清屏 splash），覆盖所有路由（Home 和 Reader），更彻底但对唤醒流畅性影响略大。
- 作用域：SplashlessWake 专属逻辑，`goToReader` 分支该 behavior 本就是新增回归点；改动不涉其它板正常 splash 启动。建议 A（最小、与已在 Home 分支的既有 `needsWakeRefresh` 模式一致），若需彻底则选 B。

### 建议验证
1. 在 A4 上烧 F1：验证"长按电源"后能驻留睡眠态（睡眠屏保持、电池不掉），再按电源键可靠唤醒（非冷 boot），开机即睡终止。
2. 再烧 F2：验证从阅读器进入睡眠→电源键唤醒后，阅读页/首页首帧干净（无睡眠画面残影）。
3. 在 COM6 正常板与用户出问题板各验一次（排除单板）。仅在 battery-latched 板回归检查是否破坏 Sticky/Xteink（F1 作用域内）。
