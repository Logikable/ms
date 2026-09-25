/* ScrollPanel lists the available scrolls when the player chooses "Scroll" from
 * an item's context menu. It is drawn over the main layout while kScrollSelect
 * is active. TuiController forwards all events here, and OnEvent returns the
 * ConfirmChoice every dialog returns: kConfirmed once the player agrees to a
 * scroll they can pay for.
 *
 * Every scroll costs spell traces, so the list has a Cost column and the title
 * shows how many the player owns, the two numbers the choice depends on. The
 * name column gives up width for it and scrolls longer names instead, like the
 * bag's rows.
 *
 * A Cost only means something once the target item is known, since the price
 * comes from the item's level band and the same scroll costs more on better
 * gear. Both SetFilter calls take that level.
 *
 * Enter on a row opens a menu (Scroll, Pin or Unpin, Close) instead of going
 * straight to the confirm window. The panel changes nothing in the game: it
 * raises TakeScrollChosen and TakePinToggled for the caller, which owns the
 * item and the character.
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
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

class ScrollPanel {
 public:
  ScrollPanel(const CharacterInstance& character,
              const std::map<std::string, Scroll>& scrolls);
  // The catalog is held by reference, so a temporary one would dangle.
  ScrollPanel(const CharacterInstance& character,
              std::map<std::string, Scroll>&& scrolls) = delete;
  // Replaces the scroll list and resets the selection. Call before entering
  // kScrollSelect. `required_level` is the target item's, which sets every
  // row's price, and `target` is the kind of equipment its pins are filed
  // under.
  void SetFilter(std::vector<const Scroll*> filtered, int required_level,
                 ScrollTarget target);
  // Filters to scrolls that apply to proto by tier and job category, then calls
  // SetFilter. Returns false (and leaves the filter unchanged) if no scrolls
  // match. Remembers the item's name for the confirm window.
  bool SetFilterForPrototype(const EquipPrototype& proto);
  // `focused` highlights the title. The list shares the arrows with the item
  // card beside it, and the one with the arrows shows it.
  ftxui::Element Render(bool focused);
  ftxui::Element RenderResult(const ScrollResult& r) const;
  // Handles the list, the menu and the confirm window. While the confirm window
  // is open, returns its answer; otherwise returns kPending.
  ConfirmChoice OnEvent(ftxui::Event event);
  // Whether the confirm window is open.
  bool IsConfirming() const {
    return confirm_.open();
  }
  bool IsMenuOpen() const {
    return menu_open_;
  }
  // True once when the player chose Scroll from the menu. The caller checks
  // that the item has a slot for it and then calls OpenConfirm.
  bool TakeScrollChosen();
  // True once when they chose Pin or Unpin. The caller writes the pin to the
  // character and calls Resort.
  bool TakePinToggled();
  // Opens the confirm window on the selected scroll.
  void OpenConfirm();
  // The scroll at the current selection.
  const Scroll& selected_scroll() const;
  // The traces the selected scroll costs on the item being scrolled. The price
  // depends on the item, not the scroll, so the caller can't compute it alone.
  int CostOfSelected() const;
  // Whether the player has enough traces for the selected scroll. The panel
  // spends nothing itself; the caller checks this before it does.
  bool CanAffordSelected() const;
  // The save key for the selected scroll's pin, and whether it is pinned now.
  // The panel writes nothing to the character; the caller toggles the pin and
  // calls Resort.
  std::string PinKeyOfSelected() const;
  bool SelectedIsPinned() const;
  // Re-sorts the list after a pin changed, keeping the cursor on the same
  // scroll rather than the same row number.
  void Resort();
  int selected() const {
    return selected_;
  }

 private:
  void ResetComponent();
  // One row's text: name, success rate and stats. The cost is drawn as its own
  // cell so it can turn red. `elapsed` is how long this row has been selected,
  // which scrolls a name too long for its column.
  static std::string FormatEntry(const Scroll& scroll,
                                 std::chrono::steady_clock::duration elapsed);
  // The Cost cell of the row at `index`, red when the player can't pay.
  ftxui::Element CostCellFor(int index) const;
  // The Pin cell of the row at `index`: the pin, or blank space of the same
  // width, so the column keeps its width.
  std::string PinCellFor(int index) const;
  // The save key for a scroll's pin on the item being scrolled. Two scrolls
  // that differ only by tier share a key, so a pin holds as the character
  // outgrows a tier.
  std::string PinKey(const Scroll& scroll) const;
  // Pinned first, then the usual order. Sorts ordered_.
  void SortRows();
  // The spell traces the character owns.
  int TracesOwned() const;
  void OpenMenu();
  // The menu's own key handling while it is open. It is modal and consumes what
  // it doesn't use, so nothing reaches the list behind it.
  bool OnMenuEvent(ftxui::Event event);
  // The dialog that asks before a scroll is used: which item it goes on, what
  // it does, and what it costs. Drawn over the list, not below it.
  ftxui::Element RenderConfirm() const;

  const CharacterInstance& character_;
  const std::map<std::string, Scroll>& scrolls_;
  // The display name of the item being scrolled, for the confirm window.
  std::string target_name_;
  // Its required level, which every price on this screen depends on.
  int target_level_ = 0;
  // The item's kind of equipment. Pins are filed per kind, so weapon pins don't
  // carry over to armour.
  ScrollTarget target_target_ = SCROLL_TARGET_UNSPECIFIED;
  std::vector<const Scroll*> ordered_;
  int selected_ = 0;
  std::vector<std::string> entries_;
  ftxui::Component component_;
  ConfirmPrompt confirm_;
  // The menu Enter opens on a row. Rebuilt each time, because the middle entry
  // is Pin or Unpin depending on the row.
  ItemMenu menu_{{"Scroll", "Pin", "Close"}};
  bool menu_open_ = false;
  // The last value passed to Render, for the window built inside the
  // component's own renderer.
  bool focused_ = true;
  bool scroll_chosen_ = false;
  bool pin_toggled_ = false;
  // Owned here because only the panel knows when the selection moved.
  SelectionClock clock_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SCROLL_PANEL_H_
