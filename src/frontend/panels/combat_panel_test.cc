#include "src/frontend/panels/combat_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/skill_placement.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/frontend/panel_widths.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

Mob SnailMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_level(1);
  mob.set_max_hp(10);
  return mob;
}

// One snail, one spawn slot, so a single hit clears the whole roster.
MapData SnailField() {
  MapData map;
  map.set_name("Snail Field");
  Spawn* snail = map.add_spawns();
  snail->set_mob("snail");
  snail->set_count(1);
  return map;
}

void EquipSword(GameState& state) {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  // Both halves, so the swing lands whatever job the starting character
  // happens to be -- kStartingJob is a testing knob, not something the
  // encounter math should depend on.
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  state.character.Equip(0);
}

// Lays the panel out the way the Tui does -- beside a filler, so it keeps its
// own width instead of stretching to the screen.
ftxui::Screen RenderScreen(const GameState& state, const CombatSim& sim,
                           int panel_focus = kEquipPanel) {
  CombatPanel panel(state, sim, panel_focus);
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(10));
  ftxui::Render(screen, element);
  return screen;
}

std::string RenderPanel(const GameState& state, const CombatSim& sim,
                        int panel_focus = kEquipPanel) {
  return RenderScreen(state, sim, panel_focus).ToString();
}

TEST(CombatPanelTest, RendersItsColumnsWidth) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;

  // The top border's closing corner lands on the last column, and nothing is
  // drawn past it.
  ftxui::Screen screen = RenderScreen(state, sim);
  EXPECT_EQ(screen.PixelAt(kLeftColumnMin - 1, 0).character, "╮");
  EXPECT_NE(screen.PixelAt(kLeftColumnMin, 0).character, "─");
}

TEST(CombatPanelTest, NamesTheMapBeingFarmed) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;

  EXPECT_NE(RenderPanel(state, sim).find("Snail Field"), std::string::npos);
}

TEST(CombatPanelTest, ShowsTheMapCursorOnlyWhenFocused) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;

  std::string focused = RenderPanel(state, sim, kCombatPanel);
  EXPECT_NE(focused.find("> Snail Field"), std::string::npos);

  std::string unfocused = RenderPanel(state, sim, kEquipPanel);
  EXPECT_EQ(unfocused.find("> Snail Field"), std::string::npos);
  EXPECT_NE(unfocused.find("  Snail Field"), std::string::npos);
}

// The longest map in the game is a column wider than the row. It is cut to
// the row rather than pushing the panel out, and slides under it while the
// panel holds focus.
TEST(CombatPanelTest, ALongMapNameIsCutToItsRow) {
  const std::string kLongest = "Battlefield of Fire and Darkness";
  MapData map = SnailField();
  map.set_name(kLongest);
  GameState state({}, {}, {}, {{"snail", SnailMob()}}, {{"field", map}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;

  ftxui::Screen screen = RenderScreen(state, sim, kCombatPanel);
  std::string rendered = screen.ToString();
  EXPECT_EQ(rendered.find(kLongest), std::string::npos)
      << "the whole name fits, so this test proves nothing";
  EXPECT_NE(rendered.find(kLongest.substr(0, 31)), std::string::npos);
  // Still exactly as wide as it was, name or no name.
  EXPECT_EQ(screen.PixelAt(kLeftColumnMin - 1, 0).character, "╮");
}

TEST(CombatPanelTest, ReportsNotFightingWithoutAWeapon) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  CombatSim sim;
  sim.Advance(ComputeCombatParams(state), 1.0);  // no weapon -> inactive

  EXPECT_NE(RenderPanel(state, sim).find("Not fighting"), std::string::npos);
}

TEST(CombatPanelTest, LabelsTheHpBarWithTheTargetLevelAndName) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;
  sim.Advance(ComputeCombatParams(state), 0.1);  // engaged, no hit yet

  EXPECT_NE(RenderPanel(state, sim).find("Lv.1 Snail"), std::string::npos);
}

TEST(CombatPanelTest, ShowsThePlayersOwnHpAgainstTheirPool) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;
  sim.Advance(ComputeCombatParams(state), 0.1);

  ASSERT_GT(sim.view().player_max_hp, 0);
  std::string full = "HP " + std::to_string(sim.view().player_max_hp) + " / " +
                     std::to_string(sim.view().player_max_hp);
  EXPECT_NE(RenderPanel(state, sim).find(full), std::string::npos);
}

TEST(CombatPanelTest, ThePlayersHpBarFallsAsTheyAreHit) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  // A mob that survives long enough to land hits, swinging hard enough to be
  // felt through the starting character's DEF.
  Mob ogre = SnailMob();
  ogre.set_name("Ogre");
  ogre.set_max_hp(1000000);
  CombatType type;
  type.mob = &ogre;
  type.simultaneous = 1;
  type.damage_to_player = 10.0;
  AttackOption attack;
  attack.damage_per_hit = {4.0};
  attack.swing_seconds = 10.0;
  CombatParams params;
  params.active = true;
  params.encounter = "field";
  params.respawn_seconds = 1000.0;
  params.hit_seconds = 1.0;
  params.max_player_hp = 50;
  params.types = {type};
  params.attacks = {attack};
  CombatSim sim;
  sim.Advance(params, 1.0);

  EXPECT_NE(RenderPanel(state, sim).find("HP 40 / 50"), std::string::npos);
}

TEST(CombatPanelTest, LabelsTheAttackBarWithTheAttackName) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;
  sim.Advance(ComputeCombatParams(state), 0.1);  // no skill -> the bare poke

  EXPECT_NE(RenderPanel(state, sim).find("Attack"), std::string::npos);
}

TEST(CombatPanelTest, MergesEngagedMobsIntoOneBar) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  // Drive the sim directly with a 2-wide reach over two snails so both land in
  // the engaged window and merge into one "x2" bar.
  Mob snail = SnailMob();
  CombatType type;
  type.mob = &snail;
  type.simultaneous = 2;
  AttackOption attack;
  attack.max_enemies = 2;
  attack.damage_per_hit = {4.0};
  attack.swing_seconds = 1.0;
  CombatParams params;
  params.active = true;
  params.encounter = "field";
  params.respawn_seconds = 100.0;
  params.types = {type};
  params.attacks = {attack};
  CombatSim sim;
  sim.Advance(params, 0.1);  // both engaged, no hit yet

  EXPECT_NE(RenderPanel(state, sim).find("Snail x2"), std::string::npos);
}

TEST(CombatPanelTest, ShowsRespawningOnceTheRosterIsClear) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;
  CombatParams params = ComputeCombatParams(state);
  // One swing kills the lone snail.
  sim.Advance(params, params.attacks.front().swing_seconds);
  ASSERT_TRUE(sim.respawning());

  EXPECT_NE(RenderPanel(state, sim).find("Respawning"), std::string::npos);
}

TEST(CombatPanelTest, ShowsTheRespawnBeatUnderTheMobs) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  // A mob too fat to die, so the bar under it is the beat's and not the
  // "Respawning..." line the cleared map would put there.
  Mob ogre = SnailMob();
  ogre.set_name("Ogre");
  ogre.set_max_hp(1000000);
  CombatType type;
  type.mob = &ogre;
  type.simultaneous = 1;
  AttackOption attack;
  attack.damage_per_hit = {4.0};
  attack.swing_seconds = 1.0;
  CombatParams params;
  params.active = true;
  params.encounter = "field";
  params.respawn_seconds = 8.0;
  params.max_player_hp = 100;
  params.types = {type};
  params.attacks = {attack};
  CombatSim sim;
  // Two steps, because Advance clamps one to a single swing.
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);

  ASSERT_FALSE(sim.respawning());
  EXPECT_NE(RenderPanel(state, sim).find("Respawn"), std::string::npos);
  EXPECT_NEAR(sim.view().respawn_fraction, 0.25, 0.001);
}

// A boss has no beat, so the panel has no bar for one.
TEST(CombatPanelTest, HidesTheRespawnBarWhenNothingRespawns) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  Mob snail = SnailMob();
  snail.set_max_hp(1000000);
  CombatType type;
  type.mob = &snail;
  type.simultaneous = 1;
  AttackOption attack;
  attack.damage_per_hit = {4.0};
  attack.swing_seconds = 1.0;
  CombatParams params;
  params.active = true;
  params.encounter = "field";
  params.respawn_seconds = 0.0;
  params.types = {type};
  params.attacks = {attack};
  CombatSim sim;
  sim.Advance(params, 0.1);

  EXPECT_FALSE(sim.view().respawns);
  EXPECT_EQ(RenderPanel(state, sim).find("Respawn"), std::string::npos);
}

// The rows the panel actually draws, so Height() is checked against the panel
// rather than against a copy of its own arithmetic.
int DrawnRows(const GameState& state, const CombatSim& sim) {
  int focus = kEquipPanel;
  CombatPanel panel(state, sim, focus);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(20));
  // Both fillers, as the main layout has them: the hbox keeps the panel's own
  // width and the vbox its own height. Without them the window stretches to
  // the screen and every row reads as drawn.
  ftxui::Render(screen,
                ftxui::vbox({ftxui::hbox({panel.Render(), ftxui::filler()}),
                             ftxui::filler()}));
  int rows = 0;
  for (int y = 0; y < screen.dimy(); ++y) {
    if (screen.PixelAt(0, y).character != " " &&
        !screen.PixelAt(0, y).character.empty()) {
      rows = y + 1;
    }
  }
  return rows;
}

TEST(CombatPanelTest, HeightMatchesWhatItDraws) {
  int focus = kEquipPanel;
  GameState idle({}, {}, {}, {}, {});
  CombatSim no_fight;
  CombatPanel idle_panel(idle, no_fight, focus);
  EXPECT_EQ(idle_panel.Height(), DrawnRows(idle, no_fight));

  // Two mob types, so a height that counted one bar per fight rather than one
  // per type would still be wrong here.
  Mob slime = SnailMob();
  slime.set_name("Slime");
  slime.set_level(2);
  MapData two_types = SnailField();
  Spawn* second = two_types.add_spawns();
  second->set_mob("slime");
  second->set_count(1);
  // A swing that reaches both of them, or only the mob at the head of the
  // queue is engaged and there is one bar either way.
  Skill sweep;
  sweep.set_name("Sweep");
  sweep.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(sweep, JOB_ADVANCEMENT_SWORDMAN);
  sweep.set_max_level(1);
  sweep.set_max_enemies(6);
  sweep.mutable_base()->set_skill_pct(1.0);
  GameState state({}, {}, {}, {{"snail", SnailMob()}, {"slime", slime}},
                  {{"field", two_types}}, {{"sweep", sweep}});
  state.current_map = "field";
  EquipSword(state);
  state.character.AdvanceJob(JOB_SWORDMAN);
  // SP arrives with the levels, and only past level 10.
  for (int i = 0; i < 12; ++i) {
    state.character.LevelUp();
  }
  ASSERT_TRUE(state.character.LearnSkill(sweep, 1));
  CombatSim sim;
  CombatParams params = ComputeCombatParams(state);
  sim.Advance(params, 0.0);
  ASSERT_EQ(sim.view().engaged_groups.size(), 2u);
  CombatPanel panel(state, sim, focus);
  EXPECT_EQ(panel.Height(), DrawnRows(state, sim));

  // Clearing the roster puts one row where the mob bars were.
  sim.Advance(params, 100.0);
  ASSERT_TRUE(sim.respawning());
  EXPECT_EQ(panel.Height(), DrawnRows(state, sim));
}

}  // namespace
}  // namespace ms
