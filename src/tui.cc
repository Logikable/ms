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

}  // namespace

void RunTui(CharacterInstance& character,
            const std::vector<Scroll>& /*scrolls*/,
            std::mt19937& /*rng*/) {
  int bag_selected = 0;
  std::vector<std::string> bag_entries;

  ftxui::MenuOption bag_opt;
  bag_opt.on_enter = [&]() {
    character.Equip(bag_selected);
    bag_selected = std::min(bag_selected,
        std::max(0, (int)character.inventory().size() - 1));
  };

  Component bag_menu = ftxui::Menu(&bag_entries, &bag_selected, bag_opt);

  Component bag_panel = Renderer(bag_menu, [&]() -> Element {
    bag_entries.clear();
    for (const EquipInstance& item : character.inventory()) {
      bag_entries.push_back(
          "[" + std::to_string(bag_entries.size()) + "] " +
          item.prototype().name());
    }
    if (bag_entries.empty()) {
      return ftxui::window(ftxui::text(" Bag "), ftxui::text("(empty)"));
    }
    return ftxui::window(ftxui::text(" Bag "), bag_menu->Render());
  });

  Component layout = Renderer(bag_panel, [&]() -> Element {
    return ftxui::vbox({
      ftxui::hbox({
        CharacterElement(character) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 22),
        ftxui::vbox({
          EquippedElement(character),
          bag_panel->Render(),
        }) | ftxui::flex,
      }),
      ftxui::filler(),
    });
  });

  ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
  screen.Loop(layout);
}

}  // namespace ms
