#pragma once
#include <FreeInkUICore.h>
#include <GfxRenderer.h>
#include <components/lists/list.h>

#include "MappedInputManager.h"
struct ActivityResult {};
struct UiScreen {
  struct {
    int spaceMd = 8;
  } theme_;
  void setContentMargin(freeink::ui::Insets) {}
  void spacer(int16_t) {}
  const auto& theme() const { return theme_; }
  void list(const freeink::ui::ListProps&) {}
  void centeredText(const char*) {}
};
struct Activity {
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual bool keepsBluetoothAlive() const { return false; }
};
class UiListActivity : public Activity {
 public:
  // Exercise virtual activity entrypoints without exposing subclass internals.
  void testBuild(UiScreen& screen) { buildScreen(screen); }
  void testActivate(int row) { activateIndex(row); }
  void testDraw() { drawChrome(); }

 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  struct {
    void clearTapFlash() {}
  } app;
  struct {
    int selected = 0;
  } nav;
  static constexpr int ACTION_ROW = 1;
  UiListActivity(const char*, GfxRenderer& r, MappedInputManager& i, bool = false) : renderer(r), mappedInput(i) {}
  virtual int listCount() const = 0;
  virtual void buildScreen(UiScreen&) = 0;
  virtual void activateIndex(int) = 0;
  virtual void onRowLongPress(int) {}
  virtual bool handleCustomInput() { return false; }
  virtual bool handleButtons() { return false; }
  virtual void onBackButton() {}
  virtual const char* headerTitle() const = 0;
  virtual void drawChrome() {}
  void requestUpdate() {}
  void moveSelectionTo(int row) { nav.selected = row; }
  void finish() {}
  void syncListViewport(UiScreen&, freeink::ui::ListProps&) {}
  template <class T, class F>
  void startActivityForResultWith(F) {}
};
