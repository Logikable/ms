/* CubePanel is the left half of the cubing screen: the cubes on the shelf, and
 * the question one of them asks.
 *
 * The list has a Cost column and the title shows the purse, the two numbers the
 * choice depends on; the scroll list is laid out the same way for the same
 * reason. A cube the purse can't cover is dimmed with its price in red, and its
 * row can still be selected, since a player can reroll into that state and
 * should be able to read it.
 *
 * The question is the one dialog in the game that Confirm doesn't close. Every
 * Confirm returns kConfirmed and leaves the window open, so the caller rerolls
 * and the lines under the question change where the player is looking. Once the
 * purse is short the window says so: the price turns red, Confirm greys, and
 * the cursor moves off it.
 *
 * A reroll that raises the potential's rank turns the window gold until the
 * player's next key: the one moment on this screen worth stopping at, shown
 * where they are already looking.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_CUBE_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_CUBE_PANEL_H_

#include <cstdint>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/item/equip_instance.h"
#include "src/item/potential.h"

namespace ms {

class CubePanel {
 public:
  // The item the cubes would go into, together with the purse that pays for
  // them, since the price and the money to pay it are read together. Called
  // every frame, so a reroll's new lines and smaller purse arrive here.
  void SetItem(const EquipInstance* item, int64_t meso);
  // Cursor on the first cube, question closed. Call when the screen opens.
  void Reset();
  // The shelf. `focused` highlights the title; it is off while the item card
  // beside it has the arrows.
  ftxui::Element Render(bool focused) const;
  // The question, for the caller to centre over the whole screen, since it is
  // about both columns, not just this one.
  ftxui::Element RenderConfirm() const;
  bool IsConfirming() const {
    return confirm_.open();
  }
  // Whether the last reroll raised the potential's rank, which turns the window
  // gold. Set by the caller, the only one that sees the potential before and
  // after the cube, and cleared by it on the next key.
  void SetRankUp(bool rank_up) {
    rank_up_ = rank_up;
  }
  // Moves along the shelf, wrapping. Ignored while the question is open.
  void MoveCursor(int delta);
  // Enter opens the question. Inside it, Confirm returns kConfirmed without
  // closing, and Cancel or Escape closes it and returns kCancelled. Escape
  // outside the question is the caller's, so it is checked before this.
  ConfirmChoice OnEvent(ftxui::Event event);
  CubeType selected_cube() const;

 private:
  // The price of the cube under the cursor, and whether the purse covers it.
  int64_t Cost() const;
  bool Affordable() const;
  // The three lines the question is asked about, or three placeholders for an
  // item with no potential yet.
  std::vector<ftxui::Element> LineRows() const;

  const EquipInstance* item_ = nullptr;
  int64_t meso_ = 0;
  int selected_ = 0;
  bool rank_up_ = false;  // see SetRankUp
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_CUBE_PANEL_H_
