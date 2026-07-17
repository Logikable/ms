#include "src/frontend/map_select_panel.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/game_state.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

Mob SnailMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_level(1);
  return mob;
}

Mob MushroomMob() {
  Mob mob;
  mob.set_name("Horny Mushroom");
  mob.set_level(8);
  return mob;
}

void AddSpawn(MapData* map, const std::string& mob, int count) {
  MapData::Spawn* spawn = map->add_spawns();
  spawn->set_mob(mob);
  spawn->set_count(count);
}

// Three maps whose display order is not their alphabetical order: Green and
// Mixed both weigh in at level 1 (Green first by name), and Horny --
// alphabetically in the middle -- sorts last at level 8. Mixed is mostly
// snails, so the count is what holds it down next to Green.
GameState ThreeMaps() {
  MapData green;
  green.set_name("Green Field");
  AddSpawn(&green, "snail", 4);
  MapData mixed;
  mixed.set_name("Mixed Field");
  AddSpawn(&mixed, "snail", 9);
  AddSpawn(&mixed, "mushroom", 1);
  MapData horny;
  horny.set_name("Horny Field");
  AddSpawn(&horny, "mushroom", 6);
  return GameState(
      {}, {}, {}, {{"snail", SnailMob()}, {"mushroom", MushroomMob()}},
      {{"green_field", green}, {"mixed_field", mixed}, {"horny_field", horny}});
}

Mob GolemMob() {
  Mob mob;
  mob.set_name("Stone Golem");
  mob.set_level(15);
  return mob;
}

// Green (level 1) and Horny (level 8) sit on the 1-10 band; Temple (level 15)
// sits alone on the 11-30 one.
GameState TwoBands() {
  MapData green;
  green.set_name("Green Field");
  AddSpawn(&green, "snail", 4);
  MapData horny;
  horny.set_name("Horny Field");
  AddSpawn(&horny, "mushroom", 6);
  MapData temple;
  temple.set_name("Temple");
  AddSpawn(&temple, "golem", 3);
  return GameState(
      {}, {}, {},
      {{"snail", SnailMob()},
       {"mushroom", MushroomMob()},
       {"golem", GolemMob()}},
      {{"green_field", green}, {"horny_field", horny}, {"temple", temple}});
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

TEST(MapSelectPanelTest, ListsMapsByWeightedLevelThenName) {
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

TEST(MapSelectPanelTest, ShowsWeightedLevelRoundedDown) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);

  EXPECT_NE(LineWith(rendered, "Green Field").find("Green Field 1"),
            std::string::npos);
  // Mixed Field spawns nine level 1 mobs and one level 8: 1.7, down to 1.
  // Ignoring the counts would say 4.5, and rounding to nearest would say 2.
  EXPECT_NE(LineWith(rendered, "Mixed Field").find("Mixed Field 1"),
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

TEST(MapSelectPanelTest, MobTableShowsLevelAndCount) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);

  // Green Field, the first row, spawns four snails.
  EXPECT_NE(LineWith(rendered, "Snail").find("Snail 1 4"), std::string::npos);
}

TEST(MapSelectPanelTest, CursorStopsAtBothEndsOfTheList) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);

  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected_map(), "green_field");

  panel.MoveCursor(5);
  EXPECT_EQ(panel.selected_map(), "horny_field");
}

TEST(MapSelectPanelTest, OpensOnTheLowestBandWithNothingBeingFarmed) {
  GameState state = TwoBands();
  MapSelectPanel panel(state);
  panel.Reset();
  std::string rendered = Render(panel);

  EXPECT_NE(rendered.find("Lv 1-10"), std::string::npos);
  EXPECT_NE(rendered.find("Green Field"), std::string::npos);
  EXPECT_EQ(rendered.find("Temple"), std::string::npos);
}

TEST(MapSelectPanelTest, ChangingPageShowsTheNextBandFromItsTop) {
  GameState state = TwoBands();
  MapSelectPanel panel(state);
  panel.Reset();
  panel.MoveCursor(1);  // Horny Field, so the cursor has somewhere to fall from

  panel.ChangePage(1);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Lv 11-30"), std::string::npos);
  EXPECT_NE(rendered.find("Temple"), std::string::npos);
  EXPECT_EQ(rendered.find("Green Field"), std::string::npos);
  EXPECT_EQ(panel.selected_map(), "temple");
}

TEST(MapSelectPanelTest, ResetOpensOnTheBandHoldingTheMapBeingFarmed) {
  GameState state = TwoBands();
  state.current_map = "temple";
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_EQ(panel.selected_map(), "temple");
  EXPECT_NE(Render(panel).find("Lv 11-30"), std::string::npos);
}

TEST(MapSelectPanelTest, PagingStopsAtBothEndsOfTheBands) {
  GameState state = TwoBands();
  MapSelectPanel panel(state);
  panel.Reset();

  panel.ChangePage(-1);
  EXPECT_EQ(panel.selected_map(), "green_field");

  panel.ChangePage(5);
  EXPECT_EQ(panel.selected_map(), "temple");
}

TEST(MapSelectPanelTest, MapsPastTheLastBandShowOnIt) {
  // Nothing holds level 35 yet; it must not fall out of the list for that.
  Mob drake;
  drake.set_name("Drake");
  drake.set_level(35);
  MapData cave;
  cave.set_name("Deep Cave");
  AddSpawn(&cave, "drake", 5);
  GameState state({}, {}, {}, {{"drake", drake}}, {{"deep_cave", cave}});
  MapSelectPanel panel(state);
  panel.Reset();
  panel.ChangePage(1);

  EXPECT_EQ(panel.selected_map(), "deep_cave");
  EXPECT_NE(Render(panel).find("Deep Cave"), std::string::npos);
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
