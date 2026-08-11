# EEGO A4 安全显示区域规范

本文定义模板、示例和后续产品 UI 共同遵守的显示边界。这里的“安全”是指扣除
实体屏装配误差和四角遮挡后，前景内容一定能够被用户完整看到的区域。

## 1. 唯一几何定义

EEGO A4 使用 `552×768` 竖屏逻辑坐标，左上角为 `(0, 0)`。实机反复目测标定的
物理外轮廓、圆角安全区与普通 UI 内容区如下：

| 项目 | 数值 |
|---|---:|
| 逻辑 framebuffer | 552×768 px |
| 物理外轮廓 | 紧贴四边，R60 |
| 圆角安全区避让 | 12 px |
| 圆角安全区 | `(12, 12), 528×744, R48` |
| 普通 UI 统一避让 | 28 px |
| 零圆角 UI 内容区 | `(28, 28), 496×712, R0` |
| UI 内容区右下角（含） | `(523, 739)` |

内圆角不是另一次经验猜测，而是同心圆角向内偏移后的结果：

```text
inner radius = outer radius - inset = 60 - 12 = 48 px
```

因此第一层安全区是 `x=12, y=12, w=528, h=744, r=48` 的圆角矩形，不是简单的
`12..539 × 12..755` 矩形。位于这个矩形四个角、但落在 R48 圆弧之外的像素仍然
不安全。

普通页面不再根据纵坐标单独计算可用宽度，而是在 R48 内使用统一的零圆角矩形。
R48 圆角的最大内接对称矩形至少还要向内：

```text
ceil(48 × (1 - 1/√2)) = 15 px
最小总边距 = 12 + 15 = 27 px
模板布局边距 = 28 px（对齐 4 px 网格并保留栅格余量）
```

最终普通 UI 内容区为 `x=28, y=28, w=496, h=712, r=0`。其四角到对应 R48
圆心的距离约为 `45.25 px`，小于 48 px，因此完整位于圆角安全区内。

权威常量和逐像素判定函数位于
[`EegoA4Hardware.h`](../lib/EegoA4Support/include/EegoA4Hardware.h)：

```cpp
eego::DISPLAY_OUTER_RADIUS;  // 60
eego::SAFE_CONTENT_INSET;    // 12
eego::SAFE_CONTENT_RADIUS;   // 48
eego::SAFE_CONTENT_WIDTH;    // 528
eego::SAFE_CONTENT_HEIGHT;   // 744
eego::isInsideSafeContent(x, y);

eego::UI_CONTENT_INSET;      // 28
eego::UI_CONTENT_WIDTH;      // 496
eego::UI_CONTENT_HEIGHT;     // 712
eego::isInsideUiContentRect(x, y);
```

应用不得复制一套 magic number。若未来用新批次实体机重新标定，应一起修改这些
常量、诊断页、文档和回归测试。

## 2. 必须留在安全区内的内容

以下前景 UI 必须完整落在 28 px、496×712 的零圆角 UI 内容区内：

- 标题、副标题、正文、数字、状态和错误信息；
- 图标、列表内容、焦点框、按钮及按钮文字；
- 用户必须看见才能完成操作的提示、返回/确认入口；
- 产品功能依赖的图表刻度、阅读正文和分页信息。

裁剪能阻止越界像素出现，但裁掉半个字或半个按钮仍是布局错误。页面设计应先让
完整内容进入安全区，再把裁剪当作最后一道防线。

页面基线推荐再留 4 px 视觉内边距：大标题从 `x=32, y=32` 开始，正文和卡片
通常使用 `x=32` 或矩形边界 `x=28`。这样标题左侧不会因避让圆弧而被迫移动到
`x=60`，同时顶部也不会紧贴屏幕。文本右边界仍必须按实际字号和字形宽度计算。

## 3. 允许越界的明确例外

只有不承载必读正文、且确实需要覆盖物理面板的内容可以越过安全区：

- 纯色、图片、纹理等全屏背景；
- 屏幕边框、坏点、均匀性、波形和灰阶测试；
- 为验证触控边缘可达性而绘制的网格或触点标记；
- 明确设计为出血的装饰，但其缺失不能影响理解和操作。

例外不向子元素传递。即使背景全屏，叠加的标题、正文和按钮仍必须回到安全区。

## 4. 模板代码的正确用法

`PortraitCanvas` 默认使用 `DrawingRegion::UiContentRect`，普通页面还应在每次
开始绘制时显式声明，使页面契约一眼可见：

```cpp
canvas.useUiContentRect();
canvas.clear();  // 背景清屏可覆盖全 framebuffer
canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
canvas.print("SAFE TITLE");
```

`clear()` 只负责全屏背景，不会改变当前裁剪模式。必须做面板级测试时，例外范围
应尽量短，并立即恢复：

```cpp
canvas.useFullPanel();
canvas.drawRect(0, 0, canvas.width(), canvas.height(), 0);  // 边缘测试

canvas.useUiContentRect();  // 立即恢复
canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
canvas.print("VISIBLE LABEL");
```

所有 Adafruit GFX 图元和 `CpFontRenderer` 字形最终都经过
`PortraitCanvas::drawPixel()`，因此共用同一个 496×712 零圆角裁剪规则；该
矩形已经由编译期断言证明位于 R48 内。直接修改
`EInkDisplay::getFrameBuffer()` 会绕过保护，只能用于经过说明的底层显示测试。

## 5. 模板中的例外清单

当前代码只有这些有意的全屏绘制：

| 页面/代码 | 原因 |
|---|---|
| `renderSafeAreaTest()` | 必须画到像素 0 才能校准物理外轮廓 |
| `renderDisplayPattern()` | 验证边缘、行列和刷新波形 |
| `renderSolidDisplay()` / `testGrayscale()` | 验证全屏均匀性、坏点和灰阶 |
| `renderTouchTest()` | 验证触控完整物理范围 |
| Quickstart 的触点标记 | 显示边缘触控实际命中位置 |

这些页面的解释文字和操作提示仍在安全裁剪内。新增 `useFullPanel()` 时，应在代码
旁说明理由，并在同一小段绘制完成后调用 `useUiContentRect()`。

## 6. 验证

编译前运行：

```sh
python3 scripts/validate_project.py --quick
```

脚本会检查共享几何、画布裁剪、诊断页以及五个示例是否仍使用安全区契约。实体机
上输入 `screen safe`，应看到：

- 12 px 黑带紧贴四边；
- 外轮廓 R60；
- 黑带内的白色内容区为 R48；
- R48 内有一条 `x=28, y=28, 496×712` 的零圆角 UI 矩形细线；
- 四角、四边对称，标签完整可见。

串口 `screenshot` 可以像素级验证 framebuffer；物理圆角遮挡、装配误差和残影仍
必须由肉眼检查。
