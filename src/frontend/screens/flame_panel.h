/* FlamePanel is the left half of the flaming screen: the two Rebirth Flames,
 * and the question one of them asks. It is laid out like the cubing screen,
 * since the two do the same thing to different lines.
 *
 * A Burning flame replaces the item's lines on every Confirm, as a Red Cube
 * does. A Black flame rolls into an After box under them and changes nothing
 * until the player keeps one, as a Black Cube does; rerolls continue against
 * After, so the same lines are never offered twice in a row.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_FLAME_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_FLAME_PANEL_H_

#include <cstdint>
#include <optional>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/reroll_prompt.h"
#include "src/item/equip_instance.h"
#include "src/item/flame.h"

namespace ms {

class FlamePanel {
 public:
  // The item and the purse that pays, read together like the cube shelf's.
  // Called every frame, so a reroll's new lines and smaller purse arrive here.
  void SetItem(const EquipInstance* item, int64_t meso);
  // Cursor on the first flame, question closed. Call when the screen opens.
  void Reset();
  // The shelf. `focused` highlights the title; it is off while the item card
  // beside it has the arrows.
  ftxui::Element Render(bool focused) const;
  // The question, for the caller to centre over both columns.
  ftxui::Element RenderConfirm() const;
  bool IsConfirming() const {
    return prompt_.open();
  }
  // Moves along the shelf, wrapping. Ignored while the question is open.
  void MoveCursor(int delta);
  RerollAction OnEvent(ftxui::Event event);
  FlameType selected_flame() const;
  // A Black flame's roll, waiting for Keep. Rerolls roll against it.
  const std::optional<FlameLines>& after() const {
    return after_;
  }
  void SetAfter(FlameLines after);
  // Hands back After and clears it, for the caller to apply on Keep After.
  FlameLines TakeAfter();

 private:
  const FlameLines& ItemLines() const;
  int64_t Cost() const;
  bool Affordable() const;
  std::string Prompt() const;
  ftxui::Element RenderChoice() const;

  const EquipInstance* item_ = nullptr;
  int64_t meso_ = 0;
  int selected_ = 0;
  std::optional<FlameLines> after_;
  RerollPrompt prompt_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_FLAME_PANEL_H_
