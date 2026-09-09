---
name: design-xteink-html-prototypes
description: Create or revise accurate, self-contained HTML view prototypes for CrossPoint Reader on Xteink X3/X4. Use for HTML prototypes, UI mockups, view drafts, device frames, screen simulators, 视图原型, HTML 稿件, 设备框架, or 屏幕模拟 where device dimensions, orientation, safe margins, and physical-versus-logical button behavior must stay consistent with the firmware.
---

# Design Xteink HTML Prototypes

Build view prototypes inside a stable X3/X4 device frame without rediscovering
screen geometry or button semantics.

## Workflow

1. Read [references/device-specs.md](references/device-specs.md) completely.
2. Copy [assets/device-prototype.html](assets/device-prototype.html) to the requested output location.
3. Keep the device controls, profile data, scaling logic, and assertions intact.
4. Replace only the contents of `#prototype-screen` with the requested view.
5. Lay out screen content in native logical pixels. Use `100%`,
   `--screen-width`, `--screen-height`, and the `--safe-*` variables instead of
   introducing another set of device dimensions.
6. Describe actions with logical names such as Back, Confirm, PageBack, and
   PageForward. Treat the four front labels as defaults because users can remap
   them.
7. Open the result in a browser and check X3 and X4 in all requested
   orientations. Inspect the console assertions and verify that no external
   resource is requested.

Default both devices to portrait when the request does not specify an
orientation. Keep the other orientations available in the device controls.

## Preserve the Contract

- Keep `data-device="x3|x4"`,
  `data-orientation="portrait|landscape-cw|portrait-inverted|landscape-ccw"`,
  and `data-shell="black|white"` on `.prototype-lab`.
- Keep `#prototype-screen` as the native-pixel content slot.
- Keep the safe-area overlay driven by `--safe-top`, `--safe-right`,
  `--safe-bottom`, and `--safe-left`.
- Keep the physical button markers outside the screen. They show hardware
  position; in-screen hints show logical actions.
- Keep the template self-contained. Do not add a framework, webfont, vendor
  SVG/GLB, or network dependency.

## Resolve Conflicts

Prefer current firmware source for resolution, orientation, safe-area, and
button mapping. Prefer repository engineering docs for summarized behavior.
Use the manufacturer site only for industrial appearance and product metadata.
If firmware behavior changes, update the reference and template together.
