#include "src/frontend/screens/hammer_panel.h"

#include <cstdint>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/equip_instance.h"

namespace ms {

void HammerPanel::Reset(int64_t meso) {
  meso_ = meso;
  // On [Confirm]: a hammer is not an attempt, and there is nothing to lose by
  // it -- what it buys is a slot, and it always lands.
  confirm_.Open(/*cancel_selected=*/false);
}

bool HammerPanel::affordable() const {
  return meso_ >= kGoldenHammerCost;
}

ftxui::Element HammerPanel::Render() const {
  // Red on a price the purse cannot cover: the reason sits on the cell that
  // carries it, and the greyed button below is the door it closes.
  ftxui::Element price = ftxui::text(FormatMeso(kGoldenHammerCost));
  if (!affordable()) {
    price = std::move(price) | ftxui::color(kRed);
  }
  // Titleless: the question names the hammer, and a chip over it would ask
  // twice.
  return DialogWindow("",
                      {
                          CenteredRow("Apply a Golden Hammer?"),
                          CenteredRow(std::move(price)),
                      },
                      ConfirmButtons(confirm_.focus(), affordable()));
}

ConfirmChoice HammerPanel::OnEvent(ftxui::Event event) {
  return confirm_.OnEvent(std::move(event), affordable());
}

}  // namespace ms
