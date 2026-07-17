#include "src/frontend/map_select_panel.h"

#include <algorithm>
#include <iterator>
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

// The level bands the list pages through, low to high. One band holds more of
// the ladder than the one below it, since a level buys less the further along
// the player is.
//
// Aim to keep the bands holding similar numbers of MAPS, not similar spans of
// levels, and resplit them as content lands. The list pads every band out to
// the tallest one (see RenderMapList), so a band much smaller than its
// neighbor spends the difference on blank rows.
struct LevelBand {
  int min;
  int max;
};
constexpr LevelBand kLevelBands[] = {{1, 10}, {11, 30}};
constexpr int kBandCount = static_cast<int>(std::size(kLevelBands));

// Band `level` belongs to. A level past the last band's top lands there rather
// than nowhere -- content should never fall out of the list for want of a band.
// Give it its own band when that happens.
int BandFor(int level) {
  for (int band = 0; band < kBandCount; ++band) {
    if (level <= kLevelBands[band].max) {
      return band;
    }
  }
  return kBandCount - 1;
}

std::string BandLabel(int band) {
  return std::to_string(kLevelBands[band].min) + "-" +
         std::to_string(kLevelBands[band].max);
}

// Title of the map list: the band on show, with an arrow only where there's a
// band to reach. An arrow that isn't there still holds its space, so the band
// stays put as the player pages rather than sliding around under them. The
// band rides in the title because the Mobs window sits alongside in an hbox --
// a row here would push this list out of step with that one.
std::string BandTitle(int band) {
  std::string title = " Maps  ";
  title += band > 0 ? "< " : "  ";
  title += "Lv " + BandLabel(band);
  title += band < kBandCount - 1 ? " >" : "  ";
  return title + " ";
}

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
  // Sort by weighted level, then by name so equal maps hold a stable order,
  // then deal each map onto its band -- which keeps every band sorted too.
  std::vector<std::pair<std::pair<int, std::string>, std::string>> sorted;
  for (const std::pair<const std::string, MapData>& entry : state_.maps) {
    int level = WeightedLevel(state_, entry.second);
    sorted.push_back({{level, entry.second.name()}, entry.first});
  }
  std::sort(sorted.begin(), sorted.end());
  pages_.resize(kBandCount);
  for (const std::pair<std::pair<int, std::string>, std::string>& entry :
       sorted) {
    pages_[BandFor(entry.first.first)].push_back(entry.second);
  }
}

void MapSelectPanel::Reset() {
  for (int band = 0; band < kBandCount; ++band) {
    for (int i = 0; i < static_cast<int>(pages_[band].size()); ++i) {
      if (pages_[band][i] == state_.current_map) {
        page_ = band;
        selected_ = i;
        return;
      }
    }
  }
  // Nothing being farmed: open on the lowest band, where a new character is.
  page_ = 0;
  selected_ = 0;
}

void MapSelectPanel::MoveCursor(int delta) {
  if (pages_[page_].empty()) {
    return;
  }
  int last = static_cast<int>(pages_[page_].size()) - 1;
  selected_ = std::clamp(selected_ + delta, 0, last);
}

void MapSelectPanel::ChangePage(int delta) {
  int page = std::clamp(page_ + delta, 0, kBandCount - 1);
  if (page == page_) {
    return;
  }
  page_ = page;
  // No row on the new band answers to the old one, so start at the top.
  selected_ = 0;
}

std::string MapSelectPanel::selected_map() const {
  if (pages_[page_].empty()) {
    return "";
  }
  return pages_[page_][selected_];
}

ftxui::Element MapSelectPanel::RenderMapList() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text("  " + PadRight("Name", kMapNameWidth) +
                             PadRight("Lv", kLevelWidth)));
  rows.push_back(ThemedSeparator());
  const std::vector<std::string>& page = pages_[page_];
  if (page.empty()) {
    rows.push_back(ftxui::text("(none)"));
  }
  for (int i = 0; i < static_cast<int>(page.size()); ++i) {
    const MapData& map = state_.maps.at(page[i]);
    std::string row = i == selected_ ? "> " : "  ";
    row += PadRight(map.name(), kMapNameWidth);
    row += PadRight(std::to_string(WeightedLevel(state_, map)), kLevelWidth);
    rows.push_back(ftxui::text(row));
  }
  // Every band fills out to the height of the biggest one. The panel is
  // centered, so a band holding fewer maps than its neighbor would otherwise
  // walk the window's borders up and down as the player pages.
  int tallest = 1;  // An empty band still spends its one row on "(none)".
  for (const std::vector<std::string>& band : pages_) {
    tallest = std::max(tallest, static_cast<int>(band.size()));
  }
  int filled = page.empty() ? 1 : static_cast<int>(page.size());
  for (int i = filled; i < tallest; ++i) {
    rows.push_back(ftxui::text(""));
  }
  return ThemedWindow(BandTitle(page_), ftxui::vbox(rows));
}

ftxui::Element MapSelectPanel::RenderMobTable() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(ftxui::text(" " + PadRight("Name", kMobNameWidth) +
                             PadRight("Lv", kLevelWidth) +
                             PadRight("Count", kCountWidth)));
  rows.push_back(ThemedSeparator());

  int shown = 0;
  std::string selected = selected_map();
  if (!selected.empty()) {
    const MapData& map = state_.maps.at(selected);
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
