// Host-only command trace for INX's shared SDK components. No device allocations.
#include <FreeInkApp.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace freeink::ui;

class TraceTarget : public DrawTarget {
 public:
  int16_t lineH = 24;
  bool scaledFonts = false;
  static constexpr FontId FONT_BODY = 1;
  Size measureText(FontId font, const char* s, TextStyle) const override {
    const int16_t height = lineHeight(font);
    return {static_cast<int16_t>(std::strlen(s) * (height / 2)), height};
  }
  int16_t lineHeight(FontId font) const override { return lineH + (scaledFonts && (font == 1 || font == 2) ? 4 : 0); }
#ifdef FREEINK_UI_THEME_LAYOUT_POLICY
  void setTextCentering(TextCentering) {}
#endif
  void fill(Rect r, Paint p, uint8_t radius, uint8_t corners) override {
    if (p.kind != PaintKind::None)
      std::printf("fill %d %d %d %d %d %d %d %d\n", r.x, r.y, r.width, r.height, int(p.kind), int(p.color), radius,
                  corners);
  }
  void stroke(Rect r, Paint p, uint8_t w, uint8_t radius, uint8_t corners) override {
    if (p.kind != PaintKind::None)
      std::printf("stroke %d %d %d %d %d %d %d %d %d\n", r.x, r.y, r.width, r.height, int(p.kind), int(p.color), w,
                  radius, corners);
  }
  void line(Point a, Point b, uint8_t w, Paint p) override {
    std::printf("line %d %d %d %d %d %d %d\n", a.x, a.y, b.x, b.y, w, int(p.kind), int(p.color));
  }
  void triangle(Point a, Point b, Point c, Paint p) override {
    std::printf("triangle %d %d %d %d %d %d %d\n", a.x, a.y, b.x, b.y, c.x, c.y, int(p.color));
  }
  void text(Rect r, const char* s, TextStyle t) override {
    const int width = measureText(t.font, s, t).width;
    int x = r.x;
    if (t.align == TextAlign::Center) x = std::max<int>(r.x, r.x + (r.width - width) / 2);
    if (t.align == TextAlign::Right) x = std::max<int>(r.x, r.right() - width);
    const int y = r.y + std::max<int>(0, (r.height - lineHeight(t.font)) / 2);
    std::printf("text %d %d %d %d %d %d %s\n", x, y, t.font, t.bold, int(t.color), int(t.rotation), s);
  }
  void bitmap(Rect r, BitmapRef b, BitmapMode mode, Paint p, Rotation rotation) override {
    std::printf("bitmap %d %d %d %d %d %d %d %d %d\n", r.x, r.y, r.width, r.height, b.width, b.height, int(mode),
                int(p.color), int(rotation));
    if (b.data && b.format == BitmapFormat::BW1) {
      uint32_t hash = 2166136261u;
      for (size_t i = 0; i < static_cast<size_t>((b.width + 7) / 8) * b.height; ++i)
        hash = (hash ^ b.data[i]) * 16777619u;
      std::printf("pixels %u\n", hash);
    }
  }
};

#ifdef UPSTREAM_THEME_PARITY
#include "UIScale.h"
#include "UIThemeTokens.h"
#endif

void traceScenes() {
  constexpr int sceneCount =
#ifdef UPSTREAM_THEME_PARITY
      156;
#else
      155;
#endif
  for (bool landscape : {false, true})
    for (bool touch : {false, true})
      for (int16_t lineH : {20, 24, 32}) {
        for (int scene = 0; scene < sceneCount; ++scene) {
          std::printf("SCENE %d %d %d %d\n", landscape, touch, lineH, scene);
          TraceTarget target;
          target.lineH = lineH;
#ifdef UPSTREAM_THEME_PARITY
          BoardConfig::touch = touch;
          const auto fonts = uiScaleSpec();
          target.scaledFonts = fonts.bodyFontId != fonts.smallFontId;
          std::printf("fonts %d %d %d\n", fonts.smallFontId, fonts.bodyFontId, fonts.titleFontId);
#endif
          DeviceContext device;
          device.width = landscape ? 800 : 480;
          device.height = landscape ? 480 : 800;
          device.hasTouch = touch;
#ifdef UPSTREAM_THEME_PARITY
          device.safeArea = {11, 7, 13, 9};
#endif
          InteractionBuffer<64> hits;
          InputSnapshot input;
          Frame<64> frame(target, device, input, hits);
          ThemeTokens theme = themeTokensForLineHeight(lineH);
#ifdef UPSTREAM_THEME_PARITY
          theme = uiThemeTokens(target);
#else
          theme.listRowGap = 0;
          theme.listSidePadding = 20;
          theme.listRowRadius = 0;
          theme.listInset = 0;
          theme.listSeparator = SeparatorStyle::Dotted;
          theme.listScrollWidth = 6;
          theme.listValueMaxWidth = 200;
          theme.listSelectionCoversScrollReservation = true;
#ifdef FREEINK_UI_THEME_LAYOUT_POLICY
          theme.listLayoutPolicy = ListLayoutPolicy::ThemeRow;
#endif
#endif
          Screen<64> screen(frame, theme);
          if (scene <= 3 || scene >= 151) {
            ListItem items[9]{};
            for (int i = 0; i < 9; ++i) {
              items[i].label = "Book title";
              items[i].actionValue = i;
            }
            items[0].label = scene == 151 ? "Home" : (scene == 0 ? "Next book" : "Category");
            ListProps props;
            props.items = items;
            props.count = scene == 151 ? 1 : (scene == 0 ? 3 : 9);
            props.action = 1;
            props.selectedIndex = scene == 151 ? 0 : 1;
#ifndef UPSTREAM_THEME_PARITY
            if (!touch) props.rowHeight = 66;
#endif
            if (scene == 2) {
              props.topIndex = 3;
              props.selectedIndex = 4;
            }
            if (scene == 3) {
              props.labelText.maxLines = 3;
              items[0].label = "A long book title that wraps over multiple lines and must remain clickable";
            }
            if (scene == 152) {
              items[0].subtitle = "Network description";
              props.subtitleText.maxLines = 2;
            }
            if (scene >= 153) {
              if (scene == 153) props.count = 5;
              items[0].isHeader = true;
              items[1].value = "Enabled";
              items[2].enabled = false;
            }
            if (scene == 155) items[1].state = StateDisabled;
            screen.list(props);
          } else if (scene == 4) {
            OptionDialogProps props;
            props.title = "Choose action";
            props.titleText.font = 1;
            props.titleText.bold = true;
            props.titleText.align = TextAlign::Center;
            props.message = "Confirmation message";
            DialogOption options[2]{};
            options[0].label = "Confirm";
            options[0].action = 1;
            options[1].label = "Cancel";
            options[1].action = 2;
            props.options = options;
            props.optionCount = 2;
            props.verticalOptions = true;
            const int16_t width = 360;
            const int16_t height = optionDialogHeight(target, props, width);
            optionDialog(frame, centeredRect(device.screen(), Size{width, height}), props);
          } else if (scene == 5 || scene >= 7) {
            const int variant = scene >= 7 ? scene - 7 : 0;
            const auto layout = static_cast<KeyboardLayoutId>(variant / 16);
            KeyboardProps props;
            props.layout = &builtinKeyboardLayout(layout, variant & 1, variant & 2, scene == 5 || (variant & 4),
                                                  scene == 5 || (variant & 8)
#if defined(FREEINK_UI_THEME_LAYOUT_POLICY) && !defined(UPSTREAM_THEME_PARITY)
                                                      ,
                                                  KeyboardGeometry::Classic
#endif
            );
            props.labelText.font = 1;
            props.altText.font = 0;
            props.padding = {0, 0, 0, 0};
            props.gap = 3;
            props.selectedIndex = scene == 151 ? 0 : 1;
            props.keyAction = 5;
#if defined(FREEINK_UI_THEME_LAYOUT_POLICY) && !defined(UPSTREAM_THEME_PARITY)
            props.geometry = KeyboardGeometry::Classic;
            props.rowGap = props.gap;
            props.keyRadius = 0;
#endif
            keyboard(frame, Rect{0, 100, device.width, 300}, props);
          } else {
            ButtonProps buttonProps;
            buttonProps.label = "Confirm";
            buttonProps.action = 3;
            button(frame, Rect{10, 20, 150, 64}, buttonProps);
            HeaderProps headerProps;
            headerProps.title = "Library";
#ifdef UPSTREAM_THEME_PARITY
            screen.header(headerProps);
#else
            header(frame, Rect{0, 100, device.width, 66}, headerProps);
#endif
            BookCardProps card;
            card.title = "Book";
            card.author = "Author";
            card.progress = 25;
            card.progressMax = 100;
            bookCard(frame, Rect{0, 200, device.width, 200}, card);
          }
          for (size_t i = 0; i < hits.count(); ++i) {
            const auto& h = hits.data()[i];
            std::printf("hit %d %d %d %d %d %d\n", h.rect.x, h.rect.y, h.rect.width, h.rect.height, h.action, h.value);
          }
        }
      }
}

int main() {
#ifdef UPSTREAM_THEME_PARITY
  for (const auto& metrics : parityMetrics) {
    UITheme::getInstance().metrics = &metrics;
    traceScenes();
  }
#else
  traceScenes();
#endif
}
