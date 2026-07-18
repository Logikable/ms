#include "src/frontend/sp_alloc_panel.h"

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"

namespace ms {

SpAllocPanel::SpAllocPanel(CharacterInstance& character)
    : character_(character) {
}

void SpAllocPanel::Reset() {
}

ftxui::Element SpAllocPanel::Render() const {
  int stage = character_.proto().job_stage();
  int sp = character_.sp(stage);
  return ThemedWindow(
      " SP Allocation ",
      ftxui::vbox({
          ftxui::text(" SP: " + std::to_string(sp) + " "),
          ThemedSeparator(),
          // The per-advancement tabs and skill lists land once skills exist.
          ftxui::text(" No skills to allocate yet. "),
      }));
}

Screen SpAllocPanel::OnEvent(ftxui::Event event) {
  if (IsBack(event)) {
    return kMain;
  }
  return kSpAlloc;
}

}  // namespace ms
