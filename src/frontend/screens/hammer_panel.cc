#include "src/frontend/screens/hammer_panel.h"

#include <cstdint>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/format.h"
#include "src/item/equip_instance.h"

namespace ms {

void HammerPanel::Reset(int64_t meso) {
  meso_ = meso;
  // On [Confirm]: a hammer always succeeds, so there is nothing to lose. It
  // buys a slot every time.
  confirm_.Open(/*cancel_selected=*/false);
}

bool HammerPanel::affordable() const {
  return meso_ >= kGoldenHammerCost;
}

ftxui::Element HammerPanel::Render() const {
  // Red on a price the purse can't cover: the reason is on the cell that causes
  // it, and the grey button below is what it blocks.
  ftxui::Element price =
      RedUnless(ftxui::text(FormatMeso(kGoldenHammerCost)), affordable());
  // No title: the question names the hammer, and a title would say it twice.
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
