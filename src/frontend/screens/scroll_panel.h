/* ScrollPanel lists available scrolls when the player selects "Scroll" from
 * an item's context menu. It overlays the main layout while kScrollSelect is
 * active. TuiController forwards all events here; OnEvent returns false only
 * for navigation so Tui can update scroll position. TakeConfirmed() returns
 * true once when the player confirms the selection.
 *
 * Every scroll costs spell traces, so the list carries a Cost column and the
 * title carries how many the player owns -- the two numbers the choice is
 * made between. The name column gives up the width for it and slides its
 * longer names under the column instead, as the bag's rows do.
 *
 * A Cost is only meaningful once the target item is known: the price comes
 * from the item's level band, so the same scroll is dearer on better gear.
 * Both SetFilter calls take that level for it.
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
  // `required_level` is the target item's, which is what prices every row, and
  // `target` is what kind of equipment it is, which is what its pins are
  // filed under.
  void SetFilter(std::vector<const Scroll*> filtered, int required_level,
                 ScrollTarget target);
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
  // Traces the selected scroll costs on the item being scrolled. The price is
  // the item's, not the scroll's, so the caller cannot work it out alone.
  int CostOfSelected() const;
  // Whether the player is holding enough traces for the selected scroll. The
  // panel spends nothing itself; this is what the caller checks before it does.
  bool CanAffordSelected() const;
  // The save key for the selected scroll's pin, and whether it is pinned now.
  // The panel writes nothing to the character; the caller toggles the pin and
  // calls Resort.
  std::string PinKeyOfSelected() const;
  bool SelectedIsPinned() const;
  // Re-sorts the list after a pin changed, keeping the cursor on the scroll it
  // was on rather than on the row number.
  void Resort();
  int selected() const {
    return selected_;
  }

 private:
  void ResetComponent();
  // One row's text: name, success, stats. The cost is not in it -- it is drawn
  // as its own cell so it can turn red. `elapsed` is how long this row has been
  // selected, which is what slides a too-long name under its column.
  static std::string FormatEntry(const Scroll& scroll,
                                 std::chrono::steady_clock::duration elapsed);
  // The Cost cell of the row at `index`, red when the player cannot pay it.
  ftxui::Element CostCellFor(int index) const;
  // The Pin cell of the row at `index`: the pin itself, or the space it would
  // take, so the column holds its width whatever is in it.
  std::string PinCellFor(int index) const;
  // The save key a scroll's pin is filed under, on the item now being
  // scrolled. Two scrolls that differ only by tier share one key, so a pin
  // holds as the character outgrows a tier.
  std::string PinKey(const Scroll& scroll) const;
  // Pinned first, then the order the list has always used. Sorts ordered_.
  void SortRows();
  // Spell traces the character owns.
  int TracesOwned() const;
  // The pop-up that asks before a scroll is spent: what it is going on, what
  // it does, and what it costs. Drawn over the list, not below it.
  ftxui::Element RenderConfirm() const;

  const CharacterInstance& character_;
  const std::map<std::string, Scroll>& scrolls_;
  // Display name of the item being scrolled, for the confirmation window.
  std::string target_name_;
  // Its required level, which is what every price on this screen is read from.
  int target_level_ = 0;
  // What kind of equipment it is. Pins are filed per kind, so the weapons a
  // player pins do not follow them onto their armour.
  ScrollTarget target_target_ = SCROLL_TARGET_UNSPECIFIED;
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
