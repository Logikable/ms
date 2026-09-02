/* CubePanel is the left half of the cubing screen: the cubes on the shelf,
 * and the question one of them asks.
 *
 * The list carries a Cost column and the title carries the purse, the two
 * numbers the choice is made between -- the scroll list is laid out the same
 * way for the same reason. A cube the purse cannot cover is dim with its price
 * in red, and its row is still walked onto: the state a player rerolls their
 * way into is one they should be able to read.
 *
 * The question is the one dialog in the game Confirm does not close. Every
 * Confirm answers kConfirmed and leaves the window standing, so the caller
 * rerolls and the lines under the question change where the player is looking.
 * Once the purse is short the window says so itself: the price goes red,
 * Confirm greys, and the cursor is moved off it.
 *
 * A reroll that carries the potential up a rank turns the window gold, and it
 * stays that way until the player's next key -- the one moment on the screen
 * worth stopping at, said where they are already looking.
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
  // The item the cubes would go into and the purse that buys them, together:
  // the price and what is there to pay it with are read in the same breath.
  // Called every frame, so a reroll's new lines and lighter purse arrive here.
  void SetItem(const EquipInstance* item, int64_t meso);
  // Cursor on the first cube, question shut. Call as the screen opens.
  void Reset();
  // The shelf. `focused` lights the title, for the frames the item card
  // beside it holds the arrows instead.
  ftxui::Element Render(bool focused) const;
  // The question, for the caller to centre over the whole screen -- it is
  // asked about both columns, not just this one.
  ftxui::Element RenderConfirm() const;
  bool IsConfirming() const {
    return confirm_.open();
  }
  // Whether the last reroll ranked the potential up, which is what the window
  // goes gold on. Set by the caller, which is the only one that sees the
  // potential either side of the cube; cleared by it on the next key.
  void SetRankUp(bool rank_up) {
    rank_up_ = rank_up;
  }
  // Walks the shelf, wrapping. Refused while the question is up.
  void MoveCursor(int delta);
  // Enter opens the question; inside it, Confirm answers kConfirmed WITHOUT
  // closing, and Cancel or Escape shuts it and answers kCancelled. Escape
  // outside the question is the caller's, so it is checked before this.
  ConfirmChoice OnEvent(ftxui::Event event);
  CubeType selected_cube() const;

 private:
  // What the cube under the cursor takes, and whether the purse covers it.
  int64_t Cost() const;
  bool Affordable() const;
  // The three lines the question is asked over, or three placeholders on an
  // item that has no potential yet.
  std::vector<ftxui::Element> LineRows() const;

  const EquipInstance* item_ = nullptr;
  int64_t meso_ = 0;
  int selected_ = 0;
  bool rank_up_ = false;  // see SetRankUp
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_CUBE_PANEL_H_
