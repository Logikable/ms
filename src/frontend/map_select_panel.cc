#include "src/frontend/map_select_panel.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"
#include "src/game_state.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Column widths of the map list.
constexpr int kMapNameWidth = 24;
constexpr int kLevelWidth = 4;
constexpr int kSpawnWidth = 6;

// Column widths of the mob table.
constexpr int kMobNameWidth = 18;
constexpr int kHpWidth = 6;
constexpr int kExpWidth = 5;

// The map's mobs, in map order. Names no mob file defines are skipped.
std::vector<const Mob*> MapMobs(const GameState& state, const MapData& map) {
  std::vector<const Mob*> mobs;
  for (const std::string& name : map.mobs()) {
    std::map<std::string, Mob>::const_iterator it = state.mobs.find(name);
    if (it != state.mobs.end()) {
      mobs.push_back(&it->second);
    }
  }
  return mobs;
}

// Mean mob level, rounded: one number for how far along a map is meant for.
int AverageLevel(const std::vector<const Mob*>& mobs) {
  if (mobs.empty()) {
    return 0;
  }
  int total = 0;
  for (const Mob* mob : mobs) {
    total += mob->level();
  }
  double mean = static_cast<double>(total) / mobs.size();
  return static_cast<int>(std::lround(mean));
}

// Level of the map's weakest mob. Sorting on this rather than the average puts
// a map wherever a player first stands a chance in it.
int LowestLevel(const std::vector<const Mob*>& mobs) {
  int lowest = 0;
  for (const Mob* mob : mobs) {
    if (lowest == 0 || mob->level() < lowest) {
      lowest = mob->level();
    }
  }
  return lowest;
}

// The mob's drops, by display name. Items no data file defines are skipped.
std::string DropNames(const GameState& state, const Mob& mob) {
  std::string names;
  for (const MobDrop& drop : mob.drops()) {
    std::map<std::string, ItemPrototype>::const_iterator it =
        state.items.find(drop.item());
    if (it == state.items.end()) {
      continue;
    }
    if (!names.empty()) {
      names += ", ";
    }
    names += it->second.name();
  }
  return names.empty() ? "-" : names;
}

}  // namespace

MapSelectPanel::MapSelectPanel(const GameState& state) : state_(state) {
  // Sort by the weakest mob, then by name so equal maps hold a stable order.
  std::vector<std::pair<std::pair<int, std::string>, std::string>> sorted;
  for (const std::pair<const std::string, MapData>& entry : state_.maps) {
    int level = LowestLevel(MapMobs(state_, entry.second));
    sorted.push_back({{level, entry.second.name()}, entry.first});
  }
  std::sort(sorted.begin(), sorted.end());
  for (const std::pair<std::pair<int, std::string>, std::string>& entry :
       sorted) {
    order_.push_back(entry.second);
  }
}

void MapSelectPanel::Reset() {
  selected_ = 0;
  for (int i = 0; i < static_cast<int>(order_.size()); ++i) {
    if (order_[i] == state_.current_map) {
      selected_ = i;
      return;
    }
  }
}

void MapSelectPanel::MoveCursor(int delta) {
  if (order_.empty()) {
    return;
  }
  int last = static_cast<int>(order_.size()) - 1;
  selected_ = std::clamp(selected_ + delta, 0, last);
}

std::string MapSelectPanel::selected_map() const {
  if (order_.empty()) {
    return "";
  }
  return order_[selected_];
}

ftxui::Element MapSelectPanel::RenderMapList() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text("  " + PadRight("Name", kMapNameWidth) +
                             PadRight("Lv", kLevelWidth) +
                             PadRight("Mobs", kSpawnWidth)));
  rows.push_back(ThemedSeparator());
  if (order_.empty()) {
    rows.push_back(ftxui::text("(none)"));
  }
  for (int i = 0; i < static_cast<int>(order_.size()); ++i) {
    const MapData& map = state_.maps.at(order_[i]);
    std::string row = i == selected_ ? "> " : "  ";
    row += PadRight(map.name(), kMapNameWidth);
    row += PadRight(std::to_string(AverageLevel(MapMobs(state_, map))),
                    kLevelWidth);
    row += PadRight(std::to_string(map.spawn_count()), kSpawnWidth);
    rows.push_back(ftxui::text(row));
  }
  return ThemedWindow(" Maps ", ftxui::vbox(rows));
}

ftxui::Element MapSelectPanel::RenderMobTable() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(
      " " + PadRight("Name", kMobNameWidth) + PadRight("Lv", kLevelWidth) +
      PadRight("HP", kHpWidth) + PadRight("EXP", kExpWidth) + "Drops"));
  rows.push_back(ThemedSeparator());

  std::vector<const Mob*> mobs;
  if (!order_.empty()) {
    mobs = MapMobs(state_, state_.maps.at(order_[selected_]));
  }
  if (mobs.empty()) {
    rows.push_back(ftxui::text(" (none)"));
  }
  for (const Mob* mob : mobs) {
    std::string row = " " + PadRight(mob->name(), kMobNameWidth);
    row += PadRight(std::to_string(mob->level()), kLevelWidth);
    row += PadRight(std::to_string(mob->max_hp()), kHpWidth);
    row += PadRight(std::to_string(mob->exp()), kExpWidth);
    row += DropNames(state_, *mob);
    rows.push_back(ftxui::text(row));
  }
  return ThemedWindow(" Mobs ", ftxui::vbox(rows));
}

ftxui::Element MapSelectPanel::Render() const {
  return ftxui::hbox({RenderMapList(), RenderMobTable()});
}

}  // namespace ms
