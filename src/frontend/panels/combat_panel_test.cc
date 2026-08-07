#include "src/frontend/panels/combat_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
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
  MapData::Spawn* snail = map.add_spawns();
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
                                               ftxui::Dimension::Fixed(7));
  ftxui::Render(screen, element);
  return screen;
}

std::string RenderPanel(const GameState& state, const CombatSim& sim,
                        int panel_focus = kEquipPanel) {
  return RenderScreen(state, sim, panel_focus).ToString();
}

TEST(CombatPanelTest, RendersExactlyKTotalWidthColumns) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;

  // The top border's closing corner lands on the last column, and nothing is
  // drawn past it.
  ftxui::Screen screen = RenderScreen(state, sim);
  EXPECT_EQ(screen.PixelAt(CombatPanel::kTotalWidth - 1, 0).character, "╮");
  EXPECT_NE(screen.PixelAt(CombatPanel::kTotalWidth, 0).character, "─");
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

  ASSERT_GT(sim.player_max_hp(), 0);
  std::string full = "HP " + std::to_string(sim.player_max_hp()) + " / " +
                     std::to_string(sim.player_max_hp());
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
  CombatParams params;
  params.active = true;
  params.map = "field";
  params.swing_seconds = 10.0;
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
  CombatParams params;
  params.active = true;
  params.map = "field";
  params.swing_seconds = 1.0;
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
  sim.Advance(params, params.swing_seconds);  // one swing kills the lone snail
  ASSERT_TRUE(sim.respawning());

  EXPECT_NE(RenderPanel(state, sim).find("Respawning"), std::string::npos);
}

}  // namespace
}  // namespace ms
