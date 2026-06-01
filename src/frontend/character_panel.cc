#include "src/frontend/character_panel.h"

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

std::string JobName(Job job) {
  switch (job) {
    case JOB_BEGINNER: return "Beginner";
    case JOB_WARRIOR:  return "Warrior";
    default:           return "Unknown";
  }
}

}  // namespace

ftxui::Element CharacterElement(const CharacterInstance& c) {
  const Character& p = c.proto();
  const AllocatedStats& s = p.allocated_stats();
  return ftxui::window(ftxui::text(" Character "), ftxui::vbox({
    ftxui::text("Lv" + std::to_string(p.level()) + " " + JobName(p.job())),
    ftxui::separator(),
    ftxui::text("STR: " + std::to_string(s.str())),
    ftxui::text("DEX: " + std::to_string(s.dex())),
    ftxui::text("INT: " + std::to_string(s.int_())),
    ftxui::text("LUK: " + std::to_string(s.luk())),
  }));
}

}  // namespace ms
