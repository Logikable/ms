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
 * A choosing cube (Black, White) rolls into an After box under the item's own
 * lines and changes nothing until the player keeps one. Rerolls continue from
 * After, rank included, so Confirm can be pressed again and again; the Keep
 * buttons sit above Confirm and Cancel, a square the arrows walk.
 *
 * A reroll that raises the potential's rank turns the window gold for
 * kRankUpSeconds and until the player's next key, whichever is later: the one
 * moment on this screen worth stopping at, shown where they are already
 * looking.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_CUBE_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_CUBE_PANEL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/item/equip_instance.h"
#include "src/item/potential.h"

namespace ms {

// How long a rank-up keeps the window gold, at the least.
inline constexpr double kRankUpSeconds = 2.0;

// One cube on the shelf, and whether its row is gold: the end of its trail.
struct ShelfEntry {
  CubeType type;
  bool lead = false;
};

// What a key did in the question.
enum class CubeAction {
  kNone,
  // Confirm: buy another roll.
  kReroll,
  // A choosing cube's Keep buttons. Keep After is the caller's to apply, with
  // TakeAfter.
  kKeepBefore,
  kKeepAfter,
  // Cancel or Escape. A pending After is dropped, keeping the item as it was.
  kClosed,
};

class CubePanel {
 public:
  // The item the cubes would go into, together with the purse that pays for
  // them, since the price and the money to pay it are read together. Called
  // every frame, so a reroll's new lines and smaller purse arrive here.
  void SetItem(const EquipInstance* item, int64_t meso);
  // Which cubes the shelf offers, in kCubes order: only unlocked ones, since a
  // locked feature isn't shown at all. Called every frame, like SetItem.
  void SetShelf(std::vector<ShelfEntry> shelf);
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
  // Turns the window gold after a reroll that raised the rank of the potential
  // it rolled from. Raised by the caller, the only one that sees both.
  void RaiseRankUp();
  // The gold's clock, and the player's key that must also come after it. Every
  // key but the ticker's redraw counts.
  void AdvanceRankUp(double elapsed_seconds);
  void TouchRankUp();
  bool rank_up() const;
  // Moves along the shelf, wrapping. Ignored while the question is open.
  void MoveCursor(int delta);
  // Enter opens the question; inside it, see CubeAction. Confirm leaves it
  // open. Escape outside the question is the caller's, so it is checked
  // before this.
  CubeAction OnEvent(ftxui::Event event);
  CubeType selected_cube() const;
  // A choosing cube's roll, waiting for Keep. Rerolls start from it.
  const std::optional<Potential>& after() const {
    return after_;
  }
  void SetAfter(Potential after);
  // Hands back After and clears it, for the caller to apply on Keep After.
  Potential TakeAfter();

 private:
  // Which Keep button the cursor is on, if it is on that row.
  enum class KeepFocus { kNone, kBefore, kAfter };

  const Potential& SelectedPotential() const;
  bool Choosing() const;
  // The price of the cube under the cursor, and whether the purse covers it.
  int64_t Cost() const;
  bool Affordable() const;
  // The prompt over the lines: a reroll, or a grant on an item with none.
  std::string Prompt() const;
  // The Keep row's keys, while the cursor is on it.
  CubeAction OnKeepEvent(const ftxui::Event& event);
  // Back to the Confirm row, on Confirm unless the purse is short.
  void FocusConfirmRow(bool cancel);
  ftxui::Element KeepButtons() const;
  ftxui::Element RenderChoice(ftxui::Color accent) const;

  const EquipInstance* item_ = nullptr;
  int64_t meso_ = 0;
  int selected_ = 0;
  std::vector<ShelfEntry> shelf_ = {{CubeType::kRed}};
  std::optional<Potential> after_;
  KeepFocus keep_focus_ = KeepFocus::kNone;
  // The rank-up gold; see RaiseRankUp.
  bool rank_up_ = false;
  double rank_up_seconds_ = 0.0;
  bool rank_up_touched_ = false;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_CUBE_PANEL_H_
