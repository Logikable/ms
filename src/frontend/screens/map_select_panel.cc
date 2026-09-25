#include "src/frontend/screens/map_select_panel.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/exp_table.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/keys.h"
#include "src/game_state.h"
#include "src/map_force.h"
#include "src/map_level.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/spawn.h"

namespace ms {
namespace {

// Column widths of the map list. PadRight truncates instead of overflowing, so
// a name longer than the column loses its last letters. The chip bar above the
// rows is wider than they are, so this column can grow up to that.
constexpr int kMapNameWidth = 36;
constexpr int kLevelWidth = 4;
// The force a map requires, Arcane Force or Sacred Power. Blank on every map
// that requires neither; an empty cell says nothing is needed, while a "-"
// would suggest the map refuses something.
constexpr int kForceWidth = 4;

// A map row's full width, cursor included. The band bar is held to this, so the
// window is sized by the maps rather than by the tabs above them.
constexpr int kMapRowWidth = 2 + kMapNameWidth + kLevelWidth + kForceWidth;

// Column widths of the mob table: the longest mob name, 25 ("Enhanced Diamond
// Guardian"), plus a space. PadRight truncates instead of overflowing.
constexpr int kMobNameWidth = 26;
constexpr int kCountWidth = 6;

// The width of every line of the mob table. The map's name sits above the
// columns and is padded to this: the two windows are side by side and centred
// as a pair, so a name wider than the columns would shift both sideways as the
// cursor moved. Wide enough for the longest map name plus a space.
constexpr int kMobTableWidth = 1 + kMobNameWidth + kLevelWidth + kCountWidth;

// Which force a map requires, which decides which group of tabs it goes in:
// Arcane River and Grandis each have their own, separate from the level bands.
enum class Region { kOverworld, kArcaneRiver, kGrandis };

Region RegionOf(const MapData& map) {
  if (map.sacred_power() > 0) {
    return Region::kGrandis;
  }
  return map.arcane_force() > 0 ? Region::kArcaneRiver : Region::kOverworld;
}

// The tabs of the list: the overworld by level, low to high, each band covering
// more levels than the one below; then Arcane River in two halves; then
// Grandis.
//
// Keep the bands holding similar numbers of maps rather than similar level
// ranges, and re-split as content is added. The list pads every band to the
// tallest, so a small band wastes the difference on blank rows and a large one
// pushes the screen past the terminal. //src/data_test:screen_fit_test catches
// this.
struct LevelBand {
  Region region;
  int min;
  int max;
  // The chip's text, or null for a band named by its levels.
  const char* label = nullptr;
};
constexpr LevelBand kLevelBands[] = {
    {Region::kOverworld, 1, 10},
    {Region::kOverworld, 11, 30},
    {Region::kOverworld, 31, 60},
    {Region::kOverworld, 61, 100},
    {Region::kOverworld, 101, 140},
    {Region::kOverworld, 141, 170},
    {Region::kOverworld, 171, 200},
    {Region::kOverworld, 201, 220},
    {Region::kArcaneRiver, 1, 230, "Arcane River P1"},
    {Region::kArcaneRiver, 231, kMaxLevel, "Arcane River P2"},
    {Region::kGrandis, 1, kMaxLevel, "Grandis"},
};
constexpr int kBandCount = static_cast<int>(std::size(kLevelBands));

// The band a map of `region` at `level` belongs to. A level past its region's
// last band goes into that band rather than nowhere, since content should never
// fall out of the list for lack of a band. Give it its own band when that
// happens.
int BandFor(Region region, int level) {
  int last = 0;
  for (int band = 0; band < kBandCount; ++band) {
    if (kLevelBands[band].region != region) {
      continue;
    }
    if (level <= kLevelBands[band].max) {
      return band;
    }
    last = band;
  }
  return last;
}

std::string BandLabel(int band) {
  if (kLevelBands[band].label != nullptr) {
    return kLevelBands[band].label;
  }
  return std::to_string(kLevelBands[band].min) + "-" +
         std::to_string(kLevelBands[band].max);
}

// The map's level as the list shows it: rounded down, so a map shows the tier
// it belongs to. The list order and the level column both use this, so the list
// always runs low to high.
int WeightedLevel(const GameState& state, const MapData& map) {
  return static_cast<int>(MapLevel(state.mobs, map));
}

// The force cell of a map row: what the map requires, in red where the
// character doesn't have enough. Red on the cell rather than dimming the row,
// because a map short of force can still be farmed: it is a penalty, not a
// locked door.
ftxui::Element ForceCell(const GameState& state, const MapData& map) {
  MapForce force = MapForceFor(map, state.character);
  if (force.required == 0) {
    return ftxui::text(std::string(kForceWidth, ' '));
  }
  return RedUnless(
      ftxui::text(PadRight(std::to_string(force.required), kForceWidth)),
      force.owned >= force.required);
}

}  // namespace

MapSelectPanel::MapSelectPanel(const GameState& state)
    : state_(state), menu_({"Move", "Inspect", "Close"}) {
  // Sort by weighted level, then by name so equal maps keep a stable order,
  // then put each map into its band, which keeps every band sorted too.
  std::vector<std::pair<std::pair<int, std::string>, std::string>> sorted;
  for (const std::pair<const std::string, MapData>& entry : state_.maps) {
    int level = WeightedLevel(state_, entry.second);
    sorted.push_back({{level, entry.second.name()}, entry.first});
  }
  std::sort(sorted.begin(), sorted.end());
  pages_.resize(kBandCount);
  for (const std::pair<std::pair<int, std::string>, std::string>& entry :
       sorted) {
    pages_[BandFor(RegionOf(state_.maps.at(entry.second)), entry.first.first)]
        .push_back(entry.second);
  }
}

void MapSelectPanel::Reset() {
  zone_ = kZoneList;
  menu_open_ = false;
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
    // Off the list rather than moved within it, so the row stays where it is
    // and the cursor returns to it.
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
  // No row on the new band matches the old one, so start at the top.
  selected_ = 0;
}

std::string MapSelectPanel::selected_map() const {
  if (pages_[page_].empty()) {
    return "";
  }
  return pages_[page_][selected_];
}

void MapSelectPanel::OpenMenu() {
  if (zone_ != kZoneList || selected_map().empty()) {
    // Nothing to open a menu on: the cursor is on the band bar, which isn't a
    // map.
    return;
  }
  menu_.Reset();
  menu_open_ = true;
}

bool MapSelectPanel::menu_open() const {
  return menu_open_;
}

Screen MapSelectPanel::OnMenuEvent(ftxui::Event event) {
  if (IsBack(event)) {
    menu_open_ = false;
    return kMapSelect;
  }
  if (event == ftxui::Event::ArrowUp) {
    menu_.Up();
    return kMapMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    menu_.Down();
    return kMapMenu;
  }
  if (IsForward(event)) {
    menu_open_ = false;
    if (menu_.selected() == kMapMenuMove) {
      return kMain;
    }
    if (menu_.selected() == kMapMenuInspect) {
      return kMobInspect;
    }
    return kMapSelect;
  }
  // Consume everything else, since the menu is modal over the list.
  return kMapMenu;
}

int MapSelectPanel::MenuRow() const {
  // +5 rows: the window's top border, the band bar, its separator, the column
  // header and its separator. One row back from there, so the highlighted entry
  // sits beside the map rather than below it.
  constexpr int kFirstMapRow = 5;
  return kFirstMapRow + selected_ - 1;
}

// The bands as a chip bar, in the game's single tab style. The chips turn white
// while the bar has the cursor, which shows the player that Left and Right
// apply to it. There are more bands than fit, so the bar is held to the rows'
// width and scrolls instead of widening the window.
ftxui::Element MapSelectPanel::RenderBandBar() const {
  std::vector<TabSpec> bands;
  for (int band = 0; band < kBandCount; ++band) {
    bands.push_back({BandLabel(band)});
  }
  return TabBar(bands, page_, /*row_focused=*/zone_ == kZoneTabs, kMapRowWidth);
}

// The force column's header. Outside Arcane River and Grandis no map requires
// any force, and a column of blanks under an "AF" header would only leave the
// player wondering what it is for.
std::string MapSelectPanel::PageForceHeader() const {
  for (const std::string& key : pages_[page_]) {
    MapForce force = MapForceFor(state_.maps.at(key), state_.character);
    if (force.required > 0) {
      return force.abbreviation;
    }
  }
  return "";
}

ftxui::Element MapSelectPanel::RenderMapList() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderBandBar());
  rows.push_back(ThemedSeparator());
  const std::vector<std::string>& page = pages_[page_];
  // The cells are blank on a band where no map requires force, so the header is
  // removed with them. The column keeps its width either way, which keeps the
  // window still as the player pages.
  std::string force_header = PadRight(PageForceHeader(), kForceWidth);
  rows.push_back(ftxui::text("  " + PadRight("Name", kMapNameWidth) +
                             PadRight("Lv", kLevelWidth) + force_header));
  rows.push_back(ThemedSeparator());
  if (page.empty()) {
    rows.push_back(EmptyState("empty"));
  }
  for (int i = 0; i < static_cast<int>(page.size()); ++i) {
    const MapData& map = state_.maps.at(page[i]);
    bool on_cursor = zone_ == kZoneList && i == selected_;
    std::string row = on_cursor ? "> " : "  ";
    row += PadRight(map.name(), kMapNameWidth);
    row += PadRight(std::to_string(WeightedLevel(state_, map)), kLevelWidth);
    rows.push_back(HighlightRow(
        ftxui::hbox({ftxui::text(row), ForceCell(state_, map)}), on_cursor));
  }
  // Every band is padded to the height of the largest. The panel is centred, so
  // a band with fewer maps would otherwise move the window's borders up and
  // down as the player pages.
  int tallest = 1;  // An empty band still uses one row for "(none)".
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
  // The name of the map these mobs come from, on the same row as the band chips
  // next door. It keeps the two tables' headers on one line: they sit side by
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
  ftxui::Element screen = ftxui::hbox({RenderMapList(), RenderMobTable()});
  if (!menu_open_) {
    return screen;
  }
  // Placed relative to the panel rather than the terminal, because the screen
  // is centred and has no fixed position. kMenuCol clears the border and the
  // name column, so the menu covers the level rather than which map it is
  // about.
  constexpr int kMenuCol = 2 + kMapNameWidth;
  return ftxui::dbox({
      std::move(screen),
      Floating(menu_.Render(MenuRow(), kMenuCol)),
  });
}

}  // namespace ms
