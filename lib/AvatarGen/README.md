# AvatarGen

Random face generator for the CrossPoint Reader e-ink display.

## Origin

Ported and adapted from the open-source web project **ugly-avatar** by Xuan Tang:

> https://github.com/txstc55/ugly-avatar

The original is a Vue 3 + SVG application that renders random "ugly" faces in
the browser. This C++ port reproduces the same parametric curve algorithms
(egg-shape face contour, cubic Bézier eyelids, multi-style hair, etc.) and
rasterizes them through `GfxRenderer` to a 1bpp 800×480 framebuffer suitable
for an ESP32-C3 e-ink panel.

## License

The original ugly-avatar is licensed under **CC BY-NC 4.0**
(Creative Commons Attribution-NonCommercial 4.0 International).
This port retains attribution to Xuan Tang. Anyone redistributing or modifying
this module should observe the original license terms — in particular, the
non-commercial restriction.

## Notable adaptations from the original

- SVG `feTurbulence` "fuzzy" filter dropped — no equivalent on a 1bpp panel
- SVG `clipPath` for pupils replaced with a `pointInPolygon` test in C++
- SVG `linearGradient` rainbow hair → solid black strokes
- Hair styles: 2 of the original 4 methods ported (#0 and #2)
- Face contour points: 400 → 200 (visible smoothness ≈ unchanged on e-ink)
- Hair line cap: 24 strands max (vs ~250 in the original)
- All point buffers statically embedded in `AvatarData` to avoid heap fragmentation
- xorshift32 PRNG instead of `Math.random()`

## Layout

| File | Role |
|---|---|
| `AvatarGen.{h,cpp}` | Public types: `PointF`, `Polyline`, `AvatarData`, `Rng` (xorshift32) |
| `Bezier.{h,cpp}` | `cubicBezier` + N-degree de Casteljau |
| `FaceShape.{h,cpp}` | Egg-shape + rectangular contour blending |
| `EyeShape.{h,cpp}` | Cubic-bezier eyelids with control-point convergence |
| `MouthShape.{h,cpp}` | 3 mouth variants |
| `HairLines.{h,cpp}` | 2 hair generation methods |
| `Nose.{h,cpp}` | Dot-cluster or curve nose |
| `AvatarGenerator.{h,cpp}` | Top-level `generate(seed) → AvatarData` |
| `Render.{h,cpp}` | Renders `AvatarData` to a `GfxRenderer` (the only file with hardware dependency) |
