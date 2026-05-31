#include "src/tui.h"

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

using ftxui::Component;
using ftxui::Element;
using ftxui::Renderer;

std::string JobName(Job job) {
  switch (job) {
    case JOB_BEGINNER: return "Beginner";
    case JOB_WARRIOR:  return "Warrior";
    default:           return "Unknown";
  }
}

Element CharacterElement(const CharacterInstance& c) {
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

Element EquippedElement(const CharacterInstance& c) {
  std::vector<Element> rows;
  for (const std::pair<const EquipSlot, EquipInstance>& kv : c.equipped()) {
    rows.push_back(ftxui::text(kv.second.prototype().name()));
  }
  if (rows.empty()) { rows.push_back(ftxui::text("(empty)")); }
  return ftxui::window(ftxui::text(" Equipped "), ftxui::vbox(rows));
}

Element BagElement(const CharacterInstance& c) {
  std::vector<Element> rows;
  int i = 0;
  for (const EquipInstance& item : c.inventory()) {
    rows.push_back(ftxui::text(
        "[" + std::to_string(i++) + "] " + item.prototype().name()));
  }
  if (rows.empty()) { rows.push_back(ftxui::text("(empty)")); }
  return ftxui::window(ftxui::text(" Bag "), ftxui::vbox(rows));
}

}  // namespace

void RunTui(CharacterInstance& character,
            const std::vector<Scroll>& /*scrolls*/,
            std::mt19937& /*rng*/) {
  Component layout = Renderer([&character] {
    return ftxui::hbox({
      CharacterElement(character) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 22),
      ftxui::vbox({
        EquippedElement(character),
        BagElement(character),
      }) | ftxui::flex,
    });
  });

  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  screen.Loop(layout);
}

}  // namespace ms
