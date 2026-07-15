#include "src/frontend/combat_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/equip_instance.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
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
  map.add_mobs("snail");
  map.set_spawn_count(1);
  return map;
}

void EquipSword(GameState& state) {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  sword.mutable_base_stats()->set_attack(100);
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
                                               ftxui::Dimension::Fixed(6));
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

TEST(CombatPanelTest, LabelsTheHpBarWithTheTarget) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;
  sim.Advance(ComputeCombatParams(state), 0.1);  // engaged, no hit yet

  EXPECT_NE(RenderPanel(state, sim).find("Snail"), std::string::npos);
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
