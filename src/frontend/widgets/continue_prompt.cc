#include "src/frontend/widgets/continue_prompt.h"

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {

void ContinuePrompt::Open() {
  open_ = true;
}

void ContinuePrompt::Close() {
  open_ = false;
}

bool ContinuePrompt::OnEvent(ftxui::Event event) {
  if (!open_) {
    return false;
  }
  // Either key dismisses it: there is one button, so backing out and pressing
  // it are the same thing.
  if (IsForward(event) || IsBack(event)) {
    Close();
    return true;
  }
  return false;
}

ftxui::Element ContinuePrompt::Render(const std::string& label) const {
  return ContinueButton(label);
}

ftxui::Element ContinuePrompt::RenderWindow() const {
  return ThemedWindow("", Render() | ftxui::hcenter);
}

}  // namespace ms
