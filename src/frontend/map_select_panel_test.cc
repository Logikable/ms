#include "src/frontend/map_select_panel.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/game_state.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

Mob SnailMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_level(1);
  mob.set_max_hp(15);
  mob.set_exp(3);
  MobDrop* drop = mob.add_drops();
  drop->set_item("green_snail_shell");
  drop->set_per_kill(0.4);
  return mob;
}

Mob MushroomMob() {
  Mob mob;
  mob.set_name("Horny Mushroom");
  mob.set_level(8);
  mob.set_max_hp(100);
  mob.set_exp(20);
  return mob;
}

ItemPrototype GreenSnailShell() {
  ItemPrototype item;
  item.set_name("Green Snail Shell");
  return item;
}

MapData MakeMap(const std::string& name, int spawn_count) {
  MapData map;
  map.set_name(name);
  map.set_spawn_count(spawn_count);
  return map;
}

// Three maps whose display order is not their alphabetical order: Green and
// Mixed both start at level 1 (Green first by name), and Horny --
// alphabetically in the middle -- sorts last because its weakest mob is
// level 8.
GameState ThreeMaps() {
  MapData green = MakeMap("Green Field", 4);
  green.add_mobs("snail");
  MapData mixed = MakeMap("Mixed Field", 2);
  mixed.add_mobs("snail");
  mixed.add_mobs("mushroom");
  MapData horny = MakeMap("Horny Field", 6);
  horny.add_mobs("mushroom");
  return GameState(
      {}, {}, {{"green_snail_shell", GreenSnailShell()}},
      {{"snail", SnailMob()}, {"mushroom", MushroomMob()}},
      {{"green_field", green}, {"mixed_field", mixed}, {"horny_field", horny}});
}

std::string Render(const MapSelectPanel& panel) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(12));
  ftxui::Render(screen, element);
  return screen.ToString();
}

// The first rendered line containing `needle`, with runs of spaces collapsed so
// assertions can name the columns without pinning their widths. The map list
// and the mob table share a line, since they sit side by side.
std::string LineWith(const std::string& rendered, const std::string& needle) {
  std::istringstream lines(rendered);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.find(needle) == std::string::npos) {
      continue;
    }
    std::string squeezed;
    bool in_spaces = false;
    for (char c : line) {
      if (c == ' ') {
        in_spaces = true;
        continue;
      }
      if (in_spaces && !squeezed.empty()) {
        squeezed += ' ';
      }
      in_spaces = false;
      squeezed += c;
    }
    return squeezed;
  }
  return "";
}

TEST(MapSelectPanelTest, ListsMapsByLowestMobLevelThenName) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);

  size_t green = rendered.find("Green Field");
  size_t mixed = rendered.find("Mixed Field");
  size_t horny = rendered.find("Horny Field");
  ASSERT_NE(horny, std::string::npos);
  EXPECT_LT(green, mixed);
  EXPECT_LT(mixed, horny);
}

TEST(MapSelectPanelTest, ShowsAverageLevelAndSpawnCount) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);

  EXPECT_NE(LineWith(rendered, "Green Field").find("Green Field 1 4"),
            std::string::npos);
  // Mixed Field holds a level 1 and a level 8 mob: 4.5, rounded to 5.
  EXPECT_NE(LineWith(rendered, "Mixed Field").find("Mixed Field 5 2"),
            std::string::npos);
}

TEST(MapSelectPanelTest, ResetPutsTheCursorOnTheMapBeingFarmed) {
  GameState state = ThreeMaps();
  state.current_map = "horny_field";
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_EQ(panel.selected_map(), "horny_field");
  std::string rendered = Render(panel);
  EXPECT_NE(LineWith(rendered, "Horny Field").find("> Horny Field"),
            std::string::npos);
  EXPECT_EQ(LineWith(rendered, "Green Field").find(">"), std::string::npos);
}

TEST(MapSelectPanelTest, MobTableFollowsTheCursor) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);

  // Green Field, the first row, holds only snails.
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Snail"), std::string::npos);
  EXPECT_EQ(rendered.find("Horny Mushroom"), std::string::npos);

  panel.MoveCursor(1);  // Mixed Field, which holds both
  rendered = Render(panel);
  EXPECT_NE(rendered.find("Snail"), std::string::npos);
  EXPECT_NE(rendered.find("Horny Mushroom"), std::string::npos);
}

TEST(MapSelectPanelTest, MobTableShowsLevelHpExpAndDrops) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);

  EXPECT_NE(LineWith(rendered, "Snail").find("Snail 1 15 3 Green Snail Shell"),
            std::string::npos);
}

TEST(MapSelectPanelTest, CursorStopsAtBothEndsOfTheList) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);

  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected_map(), "green_field");

  panel.MoveCursor(5);
  EXPECT_EQ(panel.selected_map(), "horny_field");
}

TEST(MapSelectPanelTest, HandlesAWorldWithNoMaps) {
  GameState state({}, {}, {}, {}, {});
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_EQ(panel.selected_map(), "");
  EXPECT_NE(Render(panel).find("(none)"), std::string::npos);
}

}  // namespace
}  // namespace ms
