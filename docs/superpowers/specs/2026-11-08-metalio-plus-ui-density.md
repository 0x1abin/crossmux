# Metalio Plus interface density

The user reports small fonts and bottom navigation icons. The native panel path
already maps the 1216x684 frame without scaling; GfxRenderer::begin reads runtime
geometry and exposes 684x1216 in portrait. This is separate from UI density.

Evidence: platformio.ini:865-872 previously lacked the high-density opt-in.
UiHighDpiProfile.h:3-28 provides an explicit approximately-300-PPI UI preset.
UITheme.cpp:26-37 selects actual 12/14/16 font resources; InxTheme.cpp:26 and
56-87 select native 56px navigation assets instead of the normal 38px ones.
UIThemeTokens.h:99-108 grows rows, gaps and hit areas with the preset.

Enable CROSSMUX_UI_PROFILE_HIGH_DPI only for metalio_eink4_plus_hardware. Reuse
the existing font resources, metrics, bitmaps and touch geometry. This is a
configuration correction, not a new design, font/image resampler or gray path.
No new dependencies, allocation policy, panel timings, waveform or SDK change.
The user's reading-family/size selection is independent of the interface preset.

Verify existing font dependency/chrome/layout checks and the native S31 build.
Measure actual RAM/Flash rather than assuming the larger glyph assets are free.
Archive before full CTest; preserve original recovery association and current
reading/settings data with update-and-keep. Physical acceptance checks interface
size, clipping, touch/tab alignment, book opening and continued low-flash turns.
Restore ThingsOS only at the user's request. Do not resume USB investigation.
