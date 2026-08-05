/* ItemMenu is the context menu that appears when the player presses Enter on
 * an item in the equipped or bag panel. It shows actions relevant to that
 * item (e.g. Unequip, Inspect, Scroll) as a bordered list anchored near the
 * selected row via dbox layering. The caller drives navigation: call Up() and
 * Down() on arrow-key events, selected() to read the chosen action, and
 * Reset() each time the menu is opened.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_ITEM_MENU_H_
#define MS_SRC_FRONTEND_WIDGETS_ITEM_MENU_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

class ItemMenu {
 public:
  explicit ItemMenu(std::vector<std::string> options);
  // Returns a positioned element for dbox layering. The element includes
  // top/left padding so the menu box appears at (row, col).
  ftxui::Element Render(int row, int col) const;
  // Walk the entries, skipping disabled ones. The list is a ring: Up off the
  // first entry lands on the last, and Down off the last lands on the first.
  void Up();
  void Down();
  void Reset();
  // Marks the entry at `index` as disabled: rendered dim and skipped during
  // Up/Down navigation. Must be called after Reset().
  //
  // For an action this item cannot take right now -- a trace that cannot be
  // worn, a weapon with no slots left. The entry stays visible because its
  // absence would be the surprise.
  void Disable(int index);
  // Marks the entry at `index` as hidden: not rendered at all, and skipped
  // like a disabled one. Must be called after Reset().
  //
  // For an action the player has not unlocked yet, where a greyed row would
  // advertise something they cannot ask about. Callers must leave at least
  // one entry standing; Close is never hidden.
  void Hide(int index);
  int selected() const;

 private:
  // One step of `delta` places, past any disabled entries in the way. Stays put
  // when there is nowhere enabled to go.
  void Step(int delta);

  std::vector<std::string> options_;
  std::vector<bool> disabled_;
  std::vector<bool> hidden_;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_ITEM_MENU_H_
