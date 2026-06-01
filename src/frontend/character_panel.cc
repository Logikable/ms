#include "src/frontend/character_panel.h"

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {

CharacterPanel::CharacterPanel(const CharacterInstance& character)
    : character_(character) {
}

ftxui::Element CharacterPanel::Render() const {
  const Character& p = character_.proto();
  const AllocatedStats& s = p.allocated_stats();
  return ftxui::window(ftxui::text(" Character "),
                       ftxui::vbox({
                           ftxui::text("Lv" + std::to_string(p.level()) + " " +
                                       JobName(p.job())),
                           ftxui::separator(),
                           ftxui::text("STR: " + std::to_string(s.str())),
                           ftxui::text("DEX: " + std::to_string(s.dex())),
                           ftxui::text("INT: " + std::to_string(s.int_())),
                           ftxui::text("LUK: " + std::to_string(s.luk())),
                       }));
}

std::string CharacterPanel::JobName(Job job) {
  switch (job) {
    case JOB_BEGINNER:
      return "Beginner";
    case JOB_WARRIOR:
      return "Warrior";
    default:
      return "Unknown";
  }
}

}  // namespace ms
