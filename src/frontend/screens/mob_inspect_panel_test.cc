#include "src/frontend/screens/mob_inspect_panel.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/testing/screen_text.h"
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

// A long description and two drops, one of them the rarest rate in the game.
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

// No description and no drops.
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
  // A spawn the catalog doesn't know, which the panel leaves out rather than
  // numbering its cursor around a row it can't draw.
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

// The panel's rows with styling removed, so a test can read what is against a
// border. screen.ToString() keeps colour escapes and would put one between the
// last character and the rule beside it.
std::vector<std::string> RenderRows(const MobInspectPanel& panel) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                               ftxui::Dimension::Fixed(30));
  ftxui::Render(screen, element);
  return ScreenRows(screen);
}

int Height(const MobInspectPanel& panel) {
  ftxui::Element element = panel.Render();
  return ftxui::Dimension::Fit(element).dimy;
}

int Width(const MobInspectPanel& panel) {
  ftxui::Element element = panel.Render();
  return ftxui::Dimension::Fit(element).dimx;
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
  // Honor is listed once, among the drops, with its chance. The amount is the
  // same from every monster, so the stat column doesn't show it, since it would
  // say nothing about this monster.
  EXPECT_EQ(out.find("Honor"), out.rfind("Honor"));
  EXPECT_NE(out.find("Honor"), std::string::npos);
  EXPECT_NE(out.find("5%"), std::string::npos);
  EXPECT_NE(out.find("Green Snail Shell"), std::string::npos);
  EXPECT_NE(out.find("40%"), std::string::npos);
  EXPECT_NE(out.find("0.025%"), std::string::npos);
}

// Below the rule the panel has two columns, so the first stat and the first
// drop are on the same line.
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

// The description block always takes the same rows, even for a mob with none,
// so the stats below don't move up the panel.
TEST(MobInspectPanelTest, FlavourBlockIsTheSameHeightEitherWay) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  std::string described = Render(panel);
  EXPECT_NE(described.find("A small, weak creature"), std::string::npos);
  panel.MoveCursor(1);
  std::string blank = Render(panel);
  EXPECT_NE(blank.find("(no record)"), std::string::npos);
  // The Level row is on the same screen row for both.
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

// The screen keeps one height whichever mob is shown, so the fullest panel the
// game can draw has to fit inside it.
TEST(MobInspectPanelTest, HoldsOneHeightAcrossTheList) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  int first = Height(panel);
  panel.MoveCursor(1);
  EXPECT_EQ(Height(panel), first);
}

// Arcane River HP runs to eleven digits, which written out in full would push
// the stats column past its width and the drops beside it along with it.
TEST(MobInspectPanelTest, WritesAHugeHpCompactlyAndKeepsItsWidth) {
  GameState state = OneMap();
  int narrow;
  {
    MobInspectPanel panel(state);
    panel.SetMap("green_field");
    narrow = Width(panel);
  }
  state.mobs["snail"].set_max_hp(99999999999LL);
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  std::string out = Render(panel);
  EXPECT_NE(out.find("100B"), std::string::npos) << out;
  EXPECT_EQ(Width(panel), narrow);
}

TEST(MobInspectPanelTest, AMapNobodyKnowsDrawsAnEmptyPanel) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("nowhere");
  EXPECT_EQ(panel.selected_mob(), "");
  EXPECT_NE(Render(panel).find("(empty)"), std::string::npos);
}

// An Arcane River map, which penalises the character for fighting what lives
// there.
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

// A character short of the requirement is told what it costs them: 30 of 100 is
// 30% met, which the table turns into 60% damage dealt and 1.8x damage taken.
TEST(MobInspectPanelTest, TheArcaneForceTollIsSpeltOut) {
  GameState state = ArcaneMap();
  MobInspectPanel panel(state);
  panel.SetMap("rage");
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Arcane Force"), std::string::npos) << rendered;
  // The character's value is red where it falls short, so escape codes separate
  // it from the requirement.
  EXPECT_NE(rendered.find(" / 100"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Damage 10%"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Taken 2.8x"), std::string::npos) << rendered;
}

// Every other map requires nothing and applies no penalty, so the rows are
// absent rather than reading 1x against a requirement of zero.
TEST(MobInspectPanelTest, NoArcaneRowsOutsideArcaneRiver) {
  GameState state = OneMap();
  MobInspectPanel panel(state);
  panel.SetMap("green_field");
  EXPECT_EQ(Render(panel).find("Arcane Force"), std::string::npos);
}

// V Points are the one currency that depends on the map, so the drop table
// lists them where they drop and leaves the row off elsewhere.
TEST(MobInspectPanelTest, VPointsDropOnlyWhereAForceIsAsked) {
  GameState arcane = ArcaneMap();
  MobInspectPanel river(arcane);
  river.SetMap("rage");
  EXPECT_NE(Render(river).find("V Points"), std::string::npos);

  GameState plain = OneMap();
  MobInspectPanel field(plain);
  field.SetMap("green_field");
  EXPECT_EQ(Render(field).find("V Points"), std::string::npos);
}

// Grandis shows its own force in the penalty rows, on its own table, and drops
// V Points like Arcane River. This character has no Sacred Power: 30 short is
// 70% dealt.
TEST(MobInspectPanelTest, GrandisSpellsOutSacredPower) {
  Mob spirit;
  spirit.set_name("Fire Spirit");
  spirit.set_level(261);
  MapData ramparts;
  ramparts.set_name("Cernium Eastern City Ramparts 1");
  AddSpawn(&ramparts, "spirit", 39);
  ramparts.set_sacred_power(30);
  GameState state({}, {}, {}, {{"spirit", spirit}}, {{"ramparts", ramparts}});
  MobInspectPanel panel(state);
  panel.SetMap("ramparts");
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Sacred Power"), std::string::npos) << rendered;
  EXPECT_EQ(rendered.find("Arcane Force"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find(" / 30"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Damage 70%"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Taken 1.5x"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("V Points"), std::string::npos) << rendered;
}

// A card that measures its own width has to request its right margin.
// RowsTouchingTheRightBorder only sees the panel's outer edge, so the mob
// list's own edge is checked by reading the rows back. The penalty rows span
// the whole list instead of sitting in its columns, so they are the ones with
// no column padding to spare.
TEST(MobInspectPanelTest, EveryRowKeepsAColumnClearOfTheRightBorder) {
  GameState plain = OneMap();
  MobInspectPanel panel(plain);
  panel.SetMap("green_field");
  std::vector<std::string> touching =
      RowsTouchingTheRightBorder(panel.Render());
  EXPECT_TRUE(touching.empty()) << (touching.empty() ? "" : touching[0]);

  GameState arcane = ArcaneMap();
  MobInspectPanel toll(arcane);
  toll.SetMap("rage");
  std::vector<std::string> rows = RenderRows(toll);
  EXPECT_NE(rows[2].find("Taken 2.8x │"), std::string::npos) << rows[2];
}

}  // namespace
}  // namespace ms
