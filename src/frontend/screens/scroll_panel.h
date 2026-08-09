/* ScrollPanel lists available scrolls when the player selects "Scroll" from
 * an item's context menu. It overlays the main layout while kScrollSelect is
 * active. TuiController forwards all events here; OnEvent returns false only
 * for navigation so Tui can update scroll position. TakeConfirmed() returns
 * true once when the player confirms the selection.
 *
 * Every scroll costs spell traces, so the list carries a Cost column and the
 * title carries what the player is holding -- the two numbers the choice is
 * made between. The name column gives up the width for it and slides its
 * longer names under the column instead, as the bag's rows do.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SCROLL_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SCROLL_PANEL_H_

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "src/character/character.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

class ScrollPanel {
 public:
  ScrollPanel(const CharacterInstance& character,
              const std::map<std::string, Scroll>& scrolls);
  // Replaces the displayed scroll list and resets selection to 0. Call before
  // entering kScrollSelect to show only scrolls applicable to the target item.
  void SetFilter(std::vector<const Scroll*> filtered);
  // Filters to scrolls applicable to proto by tier and job category, then
  // calls SetFilter. Returns false (and does not update the filter) if no
  // scrolls match. Remembers the item's name for the confirmation window.
  bool SetFilterForPrototype(const EquipPrototype& proto);
  ftxui::Element Render();
  ftxui::Element RenderResult(const ScrollResult& r) const;
  // Handles navigation (Up/Down) and confirm-bar interaction. Returns false
  // only for navigation events so the caller can update the scroll position.
  bool OnEvent(ftxui::Event event);
  // Returns true once when the player has confirmed a scroll selection, then
  // resets the flag.
  bool TakeConfirmed();
  bool IsConfirming() const {
    return confirm_.open();
  }
  // Returns the scroll at the current selection.
  const Scroll& selected_scroll() const;
  // Whether the player is holding enough traces for the selected scroll. The
  // panel spends nothing itself; this is what the caller checks before it does.
  bool CanAffordSelected() const;
  int selected() const {
    return selected_;
  }

 private:
  void ResetComponent();
  // One row: name, success, stats, cost. `elapsed` is how long this row has
  // been selected, which is what slides a too-long name under its column.
  static std::string FormatEntry(const Scroll& scroll,
                                 std::chrono::steady_clock::duration elapsed);
  // Spell traces the character is carrying.
  int TracesHeld() const;
  // The window that asks before a scroll is spent: what it is going on, what
  // it does, what it costs, and what the player is left holding.
  ftxui::Element RenderConfirm() const;

  const CharacterInstance& character_;
  const std::map<std::string, Scroll>& scrolls_;
  // Display name of the item being scrolled, for the confirmation window.
  std::string target_name_;
  std::vector<const Scroll*> ordered_;
  int selected_ = 0;
  std::vector<std::string> entries_;
  ftxui::Component component_;
  ConfirmPrompt confirm_;
  bool confirmed_ = false;
  // Owned here because only the panel knows when the selection moved.
  SelectionClock clock_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SCROLL_PANEL_H_
