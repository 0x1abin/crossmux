#pragma once

// UI preset calibrated for roughly 300-PPI displays; targets opt in explicitly.
// Panel dimensions, bezel insets and hardware capabilities remain board-owned.
namespace UiHighDpiProfile {
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
inline constexpr bool enabled = true;
#else
inline constexpr bool enabled = false;
#endif
// Distinct bitmap revisions invalidate sections laid out with the legacy faces.
inline constexpr int reader12FontId = 0x4738000C;
inline constexpr int contentPadding = 32;
inline constexpr int controlGap = 12;
inline constexpr int navigationHeight = 96;
inline constexpr int statusHeight = 48;
inline constexpr int headerHeight = 112;
inline constexpr int rowHeight = 104;
inline constexpr int subtitleRowHeight = 128;
inline constexpr int buttonHeight = 96;
inline constexpr int navigationIconSize = 56;
inline constexpr int controlIconSize = 48;
inline constexpr int batteryWidth = 32;
inline constexpr int batteryHeight = 20;
inline constexpr int readerStatusHeight = 48;
inline constexpr int readerStatusIconSize = 24;
inline constexpr int readerStatusHorizontalMargin = 14;
inline constexpr int readerContentStatusGap = 6;
// NotoSans 12 with centered CJK 12 fallback leaves 5px below the Chinese ink.
inline constexpr int readerStatusBottomPadding = 1;
}  // namespace UiHighDpiProfile
