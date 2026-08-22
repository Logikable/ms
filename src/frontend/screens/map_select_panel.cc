#include "src/frontend/screens/map_select_panel.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/game_state.h"
#include "src/map_level.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"

namespace ms {
namespace {

// Column widths of the map list. PadRight truncates rather than overflows, so
// a name past the column quietly loses its last letters. The chip bar above
// the rows is wider than they are, so this column is free up to that.
constexpr int kMapNameWidth = 34;
constexpr int kLevelWidth = 4;

// What a map row comes to, cursor included. The band bar is held to this, so
// the window is sized by the maps in it rather than by the tabs over them.
constexpr int kMapRowWidth = 2 + kMapNameWidth + kLevelWidth;

// Column widths of the mob table. Mob names top out at 19 ("Muddy Swamp
// Monster"); the column is wider than that because the map's name stands over
// it, and PadRight truncates rather than overflows.
constexpr int kMobNameWidth = 22;
constexpr int kCountWidth = 6;

// What every line of the mob table comes to. The map's name stands over the
// columns and is padded out to this: the two windows sit side by side and the
// pair is centered, so a name wider than the columns walks them both sideways
// as the cursor moves. Wide enough for the longest map name and one space.
constexpr int kMobTableWidth = 1 + kMobNameWidth + kLevelWidth + kCountWidth;

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
constexpr LevelBand kLevelBands[] = {{1, 10},   {11, 30},   {31, 60},
                                     {61, 100}, {101, 140}, {141, 170}};
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

// The map's level as the list shows it: rounded down, so a map reads as the
// tier it belongs to. Both the list order and the level column read this, so
// the list always runs low to high.
int WeightedLevel(const GameState& state, const MapData& map) {
  return static_cast<int>(MapLevel(state.mobs, map));
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
  zone_ = kZoneList;
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

int MapSelectPanel::CursorStop() const {
  return zone_ == kZoneTabs ? 0 : selected_ + 1;
}

void MapSelectPanel::MoveCursor(int delta) {
  int next = StepCursor(CursorStop(), delta,
                        1 + static_cast<int>(pages_[page_].size()));
  if (next == 0) {
    // Off the list rather than moved within it, so the row is left where it is
    // and the cursor comes back to it.
    zone_ = kZoneTabs;
    return;
  }
  zone_ = kZoneList;
  selected_ = next - 1;
}

void MapSelectPanel::ChangePage(int delta) {
  if (zone_ != kZoneTabs) {
    return;
  }
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

// The bands as a chip bar, the game's one tab style. The chips go white while
// the bar holds the cursor, which is how the player tells Left and Right are
// reaching it. There are more bands than fit, so the bar is held to the rows'
// width and scrolls under them rather than widening the window.
ftxui::Element MapSelectPanel::RenderBandBar() const {
  std::vector<TabSpec> bands;
  for (int band = 0; band < kBandCount; ++band) {
    bands.push_back({BandLabel(band)});
  }
  return TabBar(bands, page_, /*row_focused=*/zone_ == kZoneTabs, kMapRowWidth);
}

ftxui::Element MapSelectPanel::RenderMapList() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderBandBar());
  rows.push_back(ThemedSeparator());
  rows.push_back(ftxui::text("  " + PadRight("Name", kMapNameWidth) +
                             PadRight("Lv", kLevelWidth)));
  rows.push_back(ThemedSeparator());
  const std::vector<std::string>& page = pages_[page_];
  if (page.empty()) {
    rows.push_back(EmptyState("empty"));
  }
  for (int i = 0; i < static_cast<int>(page.size()); ++i) {
    const MapData& map = state_.maps.at(page[i]);
    std::string row = zone_ == kZoneList && i == selected_ ? "> " : "  ";
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
  return ThemedWindow(" Maps ", ftxui::vbox(rows));
}

ftxui::Element MapSelectPanel::RenderMobTable() const {
  std::vector<ftxui::Element> rows;
  // The map these mobs come from, standing where the band chips stand next
  // door. It keeps the two tables' headers on one line -- they sit side by
  // side, so a row one has and the other lacks shows as a step between them.
  std::string selected = selected_map();
  rows.push_back(
      ftxui::text(PadRight(
          " " + (selected.empty() ? "" : state_.maps.at(selected).name()),
          kMobTableWidth)) |
      ftxui::color(kTheme));
  rows.push_back(ThemedSeparator());
  rows.push_back(ftxui::text(" " + PadRight("Name", kMobNameWidth) +
                             PadRight("Lv", kLevelWidth) +
                             PadRight("Count", kCountWidth)));
  rows.push_back(ThemedSeparator());

  int shown = 0;
  if (!selected.empty()) {
    const MapData& map = state_.maps.at(selected);
    for (const Spawn& spawn : map.spawns()) {
      std::map<std::string, Mob>::const_iterator it =
          state_.mobs.find(spawn.mob());
      if (it == state_.mobs.end()) {
        continue;
      }
      const Mob& mob = it->second;
      std::string row = " " + PadRight(mob.name(), kMobNameWidth);
      row += PadRight(std::to_string(mob.level()), kLevelWidth);
      row += PadRight(std::to_string(SpawnCount(spawn)), kCountWidth);
      rows.push_back(ftxui::text(row));
      ++shown;
    }
  }
  if (shown == 0) {
    rows.push_back(EmptyState("empty"));
  }
  return ThemedWindow(" Mobs ", ftxui::vbox(rows));
}

ftxui::Element MapSelectPanel::Render() const {
  return ftxui::hbox({RenderMapList(), RenderMobTable()});
}

}  // namespace ms
