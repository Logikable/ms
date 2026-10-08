/* FamiliarCubePanel is the left half of a familiar's cubing screen: a shelf of
 * one card, the Red Familiar Card, and the question it asks. It is the item
 * cubing screen's shelf and question (see CubePanel) cut to what a familiar
 * has: one cube, one pair of lines, and no rank to climb, since a familiar's
 * rank is its level.
 *
 * Confirm rerolls and leaves the window open, so the player watches the lines
 * change. Once the purse is short the price turns red, Confirm greys, and the
 * cursor moves off it.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_FAMILIAR_CUBE_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_FAMILIAR_CUBE_PANEL_H_

#include <cstdint>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/reroll_prompt.h"
#include "src/protos/familiar.pb.h"

namespace ms {

// The card's name, which is the cube's on the shelf and the dialog's title.
inline constexpr char kFamiliarCubeName[] = "Red Familiar Card";

class FamiliarCubePanel {
 public:
  // The familiar the card would go into and the purse that pays for it.
  // Called every frame, so a reroll's lines and smaller purse arrive here.
  void SetFamiliar(const Familiar* familiar, int64_t meso);
  // Question closed. Call when the screen opens.
  void Reset();
  // The shelf. `focused` highlights the title; it is off while the card beside
  // it has the arrows.
  ftxui::Element Render(bool focused) const;
  // The question, for the caller to centre over the whole screen.
  ftxui::Element RenderConfirm() const;
  bool IsConfirming() const {
    return prompt_.open();
  }
  // Enter opens the question; inside it, Confirm is kReroll and Cancel
  // kClosed. Escape outside the question is the caller's.
  RerollAction OnEvent(ftxui::Event event);

 private:
  bool Affordable() const;

  const Familiar* familiar_ = nullptr;
  int64_t meso_ = 0;
  RerollPrompt prompt_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_FAMILIAR_CUBE_PANEL_H_
