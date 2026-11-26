/* HexaStatActivatePanel is the Activate HEXA Stat dialog: the three stats of
 * the node about to open, a main stat and two additional ones, each chosen
 * from a dropdown of the six, then [Activate] and [Cancel].
 *
 * The dropdown greys out what GMS's rules forbid given the other two picks and
 * the nodes already open (HexaStatPickAllowed), so every pick it allows can be
 * activated. [Activate] stays grey until all three are picked. The panel
 * spends nothing: the caller opens the node on kActivate.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_HEXA_HEXA_STAT_ACTIVATE_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_HEXA_HEXA_STAT_ACTIVATE_PANEL_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/matrix/hexa_stat.h"
#include "src/frontend/widgets/lists/item_menu.h"
#include "src/protos/character.pb.h"

namespace ms {

class HexaStatActivatePanel {
 public:
  enum class Result { kPending, kActivate, kCancel };

  HexaStatActivatePanel();

  // Starts a fresh pick for the node after `stats`, named for a job whose
  // primary stat is `main_stat`. Every row starts unpicked, the cursor on the
  // first.
  void Open(const HexaStats& stats, StatField main_stat);

  // Every key goes to the dropdown while it is open, and Escape closes only
  // it. kActivate only once all three are picked.
  Result OnEvent(const ftxui::Event& event);

  ftxui::Element Render() const;
  bool dropdown_open() const {
    return dropdown_open_;
  }
  // The dropdown at screen coordinates beside the row it fills, for a
  // Floating overlay. One frame behind the dialog, like the panels' menus.
  ftxui::Element RenderDropdown() const;

  // The picks so far, HEXA_STAT_UNSPECIFIED where none is made yet.
  const HexaStatNode& draft() const {
    return draft_;
  }
  bool Complete() const;

 private:
  // The rows the cursor stops on: the three stats, then the buttons.
  static constexpr int kButtonsRow = 3;

  HexaStatSlot SlotAt(int row) const;
  HexaStat PickAt(int row) const;
  void SetPick(int row, HexaStat stat);
  void OpenDropdown();
  Result OnDropdownEvent(const ftxui::Event& event);
  ftxui::Element RenderRow(int row) const;

  HexaStats stats_;
  StatField main_stat_ = STAT_FIELD_UNSPECIFIED;
  HexaStatNode draft_;
  int row_ = 0;
  bool cancel_focused_ = false;
  bool dropdown_open_ = false;
  ItemMenu dropdown_;
  mutable ftxui::Box row_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_HEXA_HEXA_STAT_ACTIVATE_PANEL_H_
