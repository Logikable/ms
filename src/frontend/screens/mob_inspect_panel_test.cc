#include "src/frontend/screens/mob_inspect_panel.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/game_state.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

ItemPrototype Item(const std::string& name) {
  ItemPrototype item;
  item.set_name(name);
  return item;
}

void AddDrop(Mob* mob, const std::string& item, double per_kill) {
  MobDrop* drop = mob->add_drops();
  drop->set_item(item);
  drop->set_per_kill(per_kill);
}

// A wordy blurb and two drops, one of them the rarest rate the game ships.
Mob SnailMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_level(10);
  mob.set_max_hp(125);
  mob.set_exp(17);
  mob.set_attack(41);
  mob.set_description(
      "A small, weak creature native to Maple Island and Victoria Island. "
      "They used to be so common that there was a saying.");
  AddDrop(&mob, "shell", 0.4);
  AddDrop(&mob, "hat", 0.00025);
  return mob;
}

// Nothing written about it, and nothing to drop.
Mob GolemMob() {
  Mob mob;
  mob.set_name("Stone Golem");
  mob.set_level(45);
  mob.set_max_hp(3000);
  mob.set_exp(200);
  mob.set_attack(500);
  return mob;
}

void AddSpawn(MapData* map, const std::string& mob, int count) {
  Spawn* spawn = map->add_spawns();
  spawn->set_mob(mob);
  spawn->set_count(count);
}

GameState OneMap() {
  MapData field;
  field.set_name("Green Field");
  AddSpawn(&field, "snail", 9);
  AddSpawn(&field, "golem", 4);
  // A spawn the catalog does not know, which the panel drops rather than
  // numbering its cursor around a row it cannot draw.
  AddSpawn(&field, "ghost", 1);
  return GameState({}, {},
                   {{"shell", Item("Green Snail Shell")},
                    {"hat", Item("A Very Long Hat Name Indeed")}},
                   {{"snail", SnailMob()}, {"golem", GolemMob()}},
                   {{"green_field", field}});
}

std::string Render(const MobInspectPanel& panel) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                               ftxui::Dimension::Fixed(30));
  ftxui::Render(screen, element);
  return screen.ToString();
}

int Height(const MobInspectPanel& panel) {
  ftxui::Element element = panel.Render();
  return ftxui::Dimension::Fit(element).dimy;
}

TEST(MobInspectPanelTest, ListsTheMapsMobsAndOpensOnTheFirst) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  EXPECT_EQ(panel.selected_mob(), "snail");
  std::string out = Render(panel);
  EXPECT_NE(out.find("Green Field"), std::string::npos);
  EXPECT_NE(out.find("> Snail"), std::string::npos);
  EXPECT_NE(out.find("  Stone Golem"), std::string::npos);
}

TEST(MobInspectPanelTest, CursorWrapsPastTheKnownMobsOnly) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_mob(), "golem");
  // Past the golem, not onto the spawn no mob file defines.
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_mob(), "snail");
  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected_mob(), "golem");
}

TEST(MobInspectPanelTest, ShowsTheStatsAndTheDropsWithTheirChances) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  std::string out = Render(panel);
  EXPECT_NE(out.find("Level"), std::string::npos);
  EXPECT_NE(out.find("125"), std::string::npos);  // HP
  EXPECT_NE(out.find("17"), std::string::npos);   // EXP
  EXPECT_NE(out.find("41"), std::string::npos);   // Attack
  EXPECT_NE(out.find("120"), std::string::npos);  // meso: 6 * 10 * 2.0
  EXPECT_NE(out.find("60%"), std::string::npos);  // the meso's own chance
  EXPECT_NE(out.find("Green Snail Shell"), std::string::npos);
  EXPECT_NE(out.find("40%"), std::string::npos);
  EXPECT_NE(out.find("0.025%"), std::string::npos);
}

// Under the rule the panel is two columns, so the first stat and the first
// drop stand on one line.
TEST(MobInspectPanelTest, StatsAndDropsShareTheirRows) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  std::istringstream rendered(Render(panel));
  std::string line;
  bool shared = false;
  while (std::getline(rendered, line)) {
    if (line.find("Level") != std::string::npos &&
        line.find("60%") != std::string::npos) {
      shared = true;
    }
  }
  EXPECT_TRUE(shared);
}

// The blurb is what the flavour block is for, and a mob with none still
// spends the rows so the stats under it do not walk up the panel.
TEST(MobInspectPanelTest, FlavourBlockIsTheSameHeightEitherWay) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  std::string described = Render(panel);
  EXPECT_NE(described.find("A small, weak creature"), std::string::npos);
  panel.MoveCursor(1);
  std::string blank = Render(panel);
  EXPECT_NE(blank.find("(no record)"), std::string::npos);
  // The Level row lands on the same screen row for both.
  std::istringstream first(described);
  std::istringstream second(blank);
  std::string line;
  int described_row = 0;
  for (int row = 0; std::getline(first, line); ++row) {
    if (line.find("Level") != std::string::npos) {
      described_row = row;
      break;
    }
  }
  int blank_row = 0;
  for (int row = 0; std::getline(second, line); ++row) {
    if (line.find("Level") != std::string::npos) {
      blank_row = row;
      break;
    }
  }
  EXPECT_GT(described_row, 0);
  EXPECT_EQ(described_row, blank_row);
}

// The screen keeps one height whichever mob is up, so the fullest panel the
// game can draw has to fit inside it.
TEST(MobInspectPanelTest, HoldsOneHeightAcrossTheList) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  int first = Height(panel);
  panel.MoveCursor(1);
  EXPECT_EQ(Height(panel), first);
}

TEST(MobInspectPanelTest, AMapNobodyKnowsDrawsAnEmptyPanel) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("nowhere");
  EXPECT_EQ(panel.selected_mob(), "");
  EXPECT_NE(Render(panel).find("(empty)"), std::string::npos);
}

// An Arcane River map, which takes a toll for letting the character hurt what
// lives there.
GameState ArcaneMap() {
  Mob erda;
  erda.set_name("Raging Erda");
  erda.set_level(201);
  MapData rage;
  rage.set_name("Weathered Land of Rage");
  AddSpawn(&rage, "erda", 33);
  rage.set_arcane_force(100);
  return GameState({}, {}, {}, {{"erda", erda}}, {{"rage", rage}});
}

// A character short of the requirement is told what it costs them: 30 of 100
// is 30% met, which the table pays at 60% dealt and 1.8x taken.
TEST(MobInspectPanelTest, TheArcaneForceTollIsSpeltOut) {
  GameState state = ArcaneMap();
  MobInspectPanel panel(state);
  panel.SetMap("rage");
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Arcane Force"), std::string::npos) << rendered;
  // The carried figure is coloured red where it falls short, so it and the
  // requirement are separated by escape codes rather than sitting in one run.
  EXPECT_NE(rendered.find(" / 100"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Damage 10%"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Taken 2.8x"), std::string::npos) << rendered;
}

// Every other map asks for nothing and takes nothing, so the rows are not
// there at all rather than reading 1x against a requirement of zero.
TEST(MobInspectPanelTest, NoArcaneRowsOutsideArcaneRiver) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  EXPECT_EQ(Render(panel).find("Arcane Force"), std::string::npos);
}

}  // namespace
}  // namespace ms
