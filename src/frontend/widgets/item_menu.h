/* ItemMenu is the context menu that opens when the player presses Enter on an
 * item in the equipped or bag panel. It lists actions for that item (e.g.
 * Unequip, Inspect, Scroll) in a bordered box placed near the selected row with
 * dbox layering. The caller drives it: Up() and Down() on arrow keys,
 * selected() to read the chosen action, and Reset() each time the menu opens.
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
  // Returns an element for dbox layering, padded on the top and left so the box
  // appears at (row, col).
  ftxui::Element Render(int row, int col) const;
  // Move between entries, skipping disabled ones. The list wraps: Up from the
  // first entry goes to the last, and Down from the last goes to the first.
  void Up();
  void Down();
  void Reset();
  // Dims the entry at `index` and skips it in Up/Down. Call after Reset().
  //
  // Use it for an action the item could take but not in its current state, such
  // as a trace that can't be worn. The row stays visible because a missing row
  // would be more surprising. An upgrade the item can never take uses Hide.
  void Disable(int index);
  // Hides the entry at `index`. Call after Reset().
  //
  // Use it for an action the player hasn't unlocked, where a grey row would
  // advertise something they can't use. Leave at least one entry visible.
  void Hide(int index);
  // Draws the entry at `index` in gold. It can still be selected as normal.
  // Call after Reset() and after any Disable(); a disabled entry is never gold.
  // Use it for a newly unlocked action at the end of the trail the level-up
  // card starts.
  void Highlight(int index);
  // Renames the entry at `index`. Call after Reset() and before Width(), so the
  // box is wide enough for the new label. Use it for an action named after the
  // state it leads to, such as a buff's Enable and Disable.
  void SetLabel(int index, std::string label);
  // The width of the box, borders included, for a caller keeping the menu
  // inside its panel. Call after Hide(), since hidden entries don't count.
  int Width() const;
  int selected() const;

 private:
  // Moves `delta` places, skipping disabled entries. Stays put when there is no
  // enabled entry to move to.
  void Step(int delta);

  std::vector<std::string> options_;
  std::vector<bool> disabled_;
  std::vector<bool> hidden_;
  std::vector<bool> highlighted_;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_ITEM_MENU_H_
