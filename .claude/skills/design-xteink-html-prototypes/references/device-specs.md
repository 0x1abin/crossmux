# Xteink X3/X4 prototype reference

Use these values for HTML view prototypes. They model UI coordinates and input
semantics, not CAD-accurate chassis dimensions.

## Source priority

1. Firmware source controls resolution, logical orientation, safe margins, and input mapping.
2. Repository engineering docs summarize supported device behavior.
3. Xteink's official site supplies product metadata, colors, and exterior appearance only.

Do not substitute the official website's 285×467 X4 preview SVG dimensions for
the framebuffer or logical screen canvas.

## Display profiles

| Device | Panel/native | Portrait logical | Landscape logical | Official metadata |
|---|---:|---:|---:|---|
| X4 | 800×480 | 480×800 | 800×480 | 4.3 in, 5.98 mm, about 75 g |
| X3 | 792×528 | 528×792 | 792×528 | 3.7 in, 4.98 mm, about 58 g |

Both products are shown officially in black and white shells. Keep the screen
monochrome and do not imply touch support on X3/X4.

## Orientations and safe margins

The base physical viewable margins are top/right/bottom/left = `9/3/3/3`
logical pixels. Rotate the margins with the logical canvas:

| Orientation | X4 canvas | X3 canvas | Safe TRBL |
|---|---:|---:|---:|
| `portrait` | 480×800 | 528×792 | 9 / 3 / 3 / 3 |
| `landscape-cw` | 800×480 | 792×528 | 3 / 9 / 3 / 3 |
| `portrait-inverted` | 480×800 | 528×792 | 3 / 3 / 9 / 3 |
| `landscape-ccw` | 800×480 | 792×528 | 3 / 3 / 3 / 9 |

Use the safe margins as a minimum physical-bezel exclusion. Individual views
may reserve more space for their active theme's header, footer, or button hints.

## Physical controls

| Control | X4 portrait chassis | X3 portrait chassis | Firmware meaning |
|---|---|---|---|
| Front 1–4 | Four controls along the bottom | Four controls along the bottom | Default Back, Confirm, Left, Right; user-remappable |
| `BTN_UP` | Upper half of right-side rocker | Left edge button | Logical Up; default PageBack |
| `BTN_DOWN` | Lower half of right-side rocker | Right edge button | Logical Down; default PageForward |
| `BTN_POWER` | Independent short right-edge key | Top-edge key | Power; not remappable |

`PageBack` and `PageForward` use the two side buttons, but settings can swap or
disable them. `NavPrevious` accepts Up or Left; `NavNext` accepts Down or Right.
Orientation-following settings may flip front navigation, so prototypes must
label intended logical actions rather than promising a fixed raw button.

When rotating the chassis, rotate the physical controls with it:

- Portrait inverted: front controls move to the top.
- Landscape CW: front controls move to the left.
- Landscape CCW: front controls move to the right.
- X3's raw Up remains the physical left-edge control in portrait; raw Down
  remains the right-edge control. Their viewed position rotates with the chassis.

## Evidence

- Panel profiles and X3/X4 layout differences:
  [`docs/engineering/device-variants.md:89-139`](../../../../docs/engineering/device-variants.md)
- Orientation enum and base margins:
  [`lib/GfxRenderer/GfxRenderer.h:33-38,131-134`](../../../../lib/GfxRenderer/GfxRenderer.h)
- Logical dimensions and rotated safe margins:
  [`lib/GfxRenderer/GfxRenderer.cpp:1720-1745,2242-2267`](../../../../lib/GfxRenderer/GfxRenderer.cpp)
- Logical-to-physical button mapping:
  [`src/MappedInputManager.cpp:52-112`](../../../../src/MappedInputManager.cpp)
- Default front and side settings:
  [`src/CrossPointSettings.h:95-117,251-256`](../../../../src/CrossPointSettings.h)
- Official exterior and metadata:
  [Xteink 360° model viewer](https://www.xteink.cn/app/model-viewer-360),
  [official X4 browser preview](https://www.xteink.cn/app/experience)
