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
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/types.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

Mob SnailMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_level(1);
  mob.set_max_hp(10);
  return mob;
}

// One snail and one spawn slot, so a single hit clears the map.
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
  // Both attack stats, so the attack lands whatever the starting character's
  // job is. The fight shouldn't depend on it.
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  state.character.Equip(0);
}

// Lays the panel out as the Tui does, beside a filler, so it keeps its own
// width instead of stretching to the screen.
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

  // The top border's right corner is on the last column, and nothing is drawn
  // past it.
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

// A map name a column wider than the row is cut to fit instead of widening the
// panel, and scrolls while the panel has focus.
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
  // Still exactly as wide as before, whatever the name.
  EXPECT_EQ(screen.PixelAt(kLeftColumnMin - 1, 0).character, "╮");
}

TEST(CombatPanelTest, ReportsNotFightingWithoutAWeapon) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  CombatSim sim;
  sim.Advance(ComputeCombatParams(state), 1.0);  // no weapon, so inactive

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
  // A mob that lives long enough to attack, and hits hard enough to get through
  // the starting character's DEF.
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
  sim.Advance(ComputeCombatParams(state), 0.1);  // no skill: basic attack

  EXPECT_NE(RenderPanel(state, sim).find("Attack"), std::string::npos);
}

TEST(CombatPanelTest, MergesEngagedMobsIntoOneBar) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  // Drive the sim directly with a reach of 2 over two snails, so both are
  // engaged and merge into one "x2" bar.
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
  // One attack kills the lone snail.
  sim.Advance(params, params.attacks.front().swing_seconds);
  ASSERT_TRUE(sim.respawning());

  EXPECT_NE(RenderPanel(state, sim).find("Respawning"), std::string::npos);
}

TEST(CombatPanelTest, ShowsTheRespawnBeatUnderTheMobs) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  // A mob with too much HP to die, so the bar under it is the respawn bar and
  // not the "Respawning..." line a cleared map would show.
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
  // Two steps, because Advance limits one step to a single attack.
  sim.Advance(params, 1.0);
  sim.Advance(params, 1.0);

  ASSERT_FALSE(sim.respawning());
  EXPECT_NE(RenderPanel(state, sim).find("Respawn"), std::string::npos);
  EXPECT_NEAR(sim.view().respawn_fraction, 0.25, 0.001);
}

// A boss doesn't respawn, so the panel has no respawn bar.
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
// rather than a copy of its own arithmetic.
int DrawnRows(const GameState& state, const CombatSim& sim) {
  int focus = kEquipPanel;
  CombatPanel panel(state, sim, focus);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60),
                                               ftxui::Dimension::Fixed(20));
  // Both fillers, as in the main layout: the hbox keeps the panel's own width
  // and the vbox its own height. Without them the window stretches to the
  // screen and every row counts as drawn.
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

  // Two mob types, so a height counting one bar per fight instead of one per
  // type would fail here.
  Mob slime = SnailMob();
  slime.set_name("Slime");
  slime.set_level(2);
  MapData two_types = SnailField();
  Spawn* second = two_types.add_spawns();
  second->set_mob("slime");
  second->set_count(1);
  // An attack that reaches both of them. Otherwise only the first mob is
  // engaged and there is one bar either way.
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
  // SP comes with levels, and only after level 10.
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

  // Clearing the map puts one row where the mob bars were.
  sim.Advance(params, 100.0);
  ASSERT_TRUE(sim.respawning());
  EXPECT_EQ(panel.Height(), DrawnRows(state, sim));
}

TEST(CombatPanelTest, TheRowsKeepOffTheRightBorder) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", SnailField()}});
  state.current_map = "field";
  EquipSword(state);
  CombatSim sim;
  int focus = kEquipPanel;
  CombatPanel panel(state, sim, focus);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
}
// The charge bar's buff dots are an option, off by default.
TEST(CombatPanelTest, BuffDotsFollowTheOption) {
  Skill rage;
  rage.set_name("Rage");
  rage.set_kind(SKILL_KIND_ACTIVE);
  PlaceIn(rage, JOB_ADVANCEMENT_SWORDMAN);
  rage.set_max_level(1);
  rage.set_cooldown_seconds(60.0);
  rage.mutable_buff()->set_duration_seconds(60.0);
  rage.mutable_buff()->mutable_base()->set_damage_pct(0.1);
  Mob tough = SnailMob();
  tough.set_max_hp(1000000000);
  GameState state({}, {}, {}, {{"snail", tough}}, {{"field", SnailField()}},
                  {{"rage", rage}});
  state.current_map = "field";
  EquipSword(state);
  state.character.AdvanceJob(JOB_SWORDMAN);
  for (int i = 0; i < 10; ++i) {
    state.character.LevelUp();
  }
  ASSERT_TRUE(state.character.LearnSkill(rage, 1));
  CombatSim sim;
  sim.Advance(ComputeCombatParams(state), 1.0);
  ASSERT_EQ(sim.view().buff_count, 1);

  EXPECT_EQ(RenderPanel(state, sim).find("·"), std::string::npos);
  state.account.SetBuffIndicators(true);
  EXPECT_NE(RenderPanel(state, sim).find("·"), std::string::npos);
}

}  // namespace
}  // namespace ms
