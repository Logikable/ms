#include "src/frontend/map_select_panel.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/panel_util.h"
#include "src/game_state.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Column widths of the map list. Names run long ("Right Around Lith Harbor"),
// so leave room for one to sit beside the level rather than against it.
constexpr int kMapNameWidth = 28;
constexpr int kLevelWidth = 4;

// Column widths of the mob table.
constexpr int kMobNameWidth = 18;
constexpr int kCountWidth = 6;

// Mean mob level weighted by spawn count, rounded down: one number for how far
// along a map is meant for. Weighting by count puts the number where the
// player's time actually goes -- a couple of stragglers shouldn't pull a map's
// number up away from the crowd that fills it. Both the list order and the
// level column read this, so the list always runs low to high.
int WeightedLevel(const GameState& state, const MapData& map) {
  int levels = 0;
  int spawned = 0;
  for (const MapData::Spawn& spawn : map.spawns()) {
    std::map<std::string, Mob>::const_iterator it =
        state.mobs.find(spawn.mob());
    if (it == state.mobs.end()) {
      continue;  // Names no mob file defines are skipped.
    }
    levels += it->second.level() * spawn.count();
    spawned += spawn.count();
  }
  if (spawned == 0) {
    return 0;
  }
  return levels / spawned;  // Both positive, so this rounds down.
}

}  // namespace

MapSelectPanel::MapSelectPanel(const GameState& state) : state_(state) {
  // Sort by weighted level, then by name so equal maps hold a stable order.
  std::vector<std::pair<std::pair<int, std::string>, std::string>> sorted;
  for (const std::pair<const std::string, MapData>& entry : state_.maps) {
    int level = WeightedLevel(state_, entry.second);
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
                             PadRight("Lv", kLevelWidth)));
  rows.push_back(ThemedSeparator());
  if (order_.empty()) {
    rows.push_back(ftxui::text("(none)"));
  }
  for (int i = 0; i < static_cast<int>(order_.size()); ++i) {
    const MapData& map = state_.maps.at(order_[i]);
    std::string row = i == selected_ ? "> " : "  ";
    row += PadRight(map.name(), kMapNameWidth);
    row += PadRight(std::to_string(WeightedLevel(state_, map)), kLevelWidth);
    rows.push_back(ftxui::text(row));
  }
  return ThemedWindow(" Maps ", ftxui::vbox(rows));
}

ftxui::Element MapSelectPanel::RenderMobTable() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(" " + PadRight("Name", kMobNameWidth) +
                             PadRight("Lv", kLevelWidth) +
                             PadRight("Count", kCountWidth)));
  rows.push_back(ThemedSeparator());

  int shown = 0;
  if (!order_.empty()) {
    const MapData& map = state_.maps.at(order_[selected_]);
    for (const MapData::Spawn& spawn : map.spawns()) {
      std::map<std::string, Mob>::const_iterator it =
          state_.mobs.find(spawn.mob());
      if (it == state_.mobs.end()) {
        continue;
      }
      const Mob& mob = it->second;
      std::string row = " " + PadRight(mob.name(), kMobNameWidth);
      row += PadRight(std::to_string(mob.level()), kLevelWidth);
      row += PadRight(std::to_string(spawn.count()), kCountWidth);
      rows.push_back(ftxui::text(row));
      ++shown;
    }
  }
  if (shown == 0) {
    rows.push_back(ftxui::text(" (none)"));
  }
  return ThemedWindow(" Mobs ", ftxui::vbox(rows));
}

ftxui::Element MapSelectPanel::Render() const {
  return ftxui::hbox({RenderMapList(), RenderMobTable()});
}

}  // namespace ms
