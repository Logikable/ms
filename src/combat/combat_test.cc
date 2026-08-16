#include "src/combat/combat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "src/character/exp_table.h"
#include "src/combat/fight.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// A weak mob: one sword hit kills it, worth 3 EXP, always drops a shell.
Mob SnailMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_level(1);
  mob.set_max_hp(10);
  mob.set_exp(3);
  MobDrop* drop = mob.add_drops();
  drop->set_item("green_snail_shell");
  drop->set_per_kill(1.0);
  return mob;
}

// The snail's drop item, as loaded into GameState.items.
ItemPrototype GreenSnailShell() {
  ItemPrototype item;
  item.set_name("Green Snail Shell");
  item.set_category(ITEM_CATEGORY_ETC);
  return item;
}

// A map of just snails with plenty of spawn slots.
MapData OneSnailMap() {
  MapData map;
  map.set_name("Snail Field");
  MapData::Spawn* snail = map.add_spawns();
  snail->set_mob("snail");
  snail->set_count(6);
  return map;
}

// The same snail, swinging hard enough to be felt through a starting
// character's DEF -- so a run that survives it survived something.
Mob BitingSnailMob() {
  Mob mob = SnailMob();
  mob.set_attack(20);
  return mob;
}

// A mob no starting character can kill or survive: far too much HP to chew
// through, and an attack far past what their bare DEF can cancel.
Mob OgreMob() {
  Mob mob;
  mob.set_name("Ogre");
  mob.set_level(1);
  mob.set_max_hp(1000000);
  mob.set_attack(200);
  mob.set_exp(3);
  return mob;
}

MapData OgreMap() {
  MapData map;
  map.set_name("Ogre Field");
  MapData::Spawn* ogre = map.add_spawns();
  ogre->set_mob("ogre");
  ogre->set_count(1);
  return map;
}

// Town: somewhere to be sent back to, with nothing on it to fight.
MapData HomeMap() {
  MapData map;
  map.set_name("Maple Island");
  return map;
}

// Equips a one-handed sword (100 weapon and magic attack) on the character.
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

// Levels the character to `level`. Combat is paced by level (see
// GameSpeedFactor), so a test that means to hold the pace still has to say
// which level it is holding it at.
void LevelTo(GameState& state, int level) {
  while (state.character.proto().level() < level) {
    state.character.LevelUp();
  }
}

// Farms for `seconds` of game time. A single call can advance at most one
// swing, so rewards only accrue over a loop -- as they do under the TUI's
// ticker.
void Farm(GameState& state, double seconds) {
  CombatSim sim;
  for (double elapsed = 0.0; elapsed < seconds; elapsed += 1.0) {
    AdvanceCombat(state, sim, 1.0);
  }
}

TEST(AdvanceCombatTest, GrantsExpWhileFarming) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 20000.0);  // many kills -> several level-ups
  EXPECT_GT(state.character.proto().level(), 2);
}

TEST(AdvanceCombatTest, AccruesDropsWhileFarming) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 20000.0);
  ASSERT_FALSE(state.character.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_EQ(state.character.stackables(ITEM_CATEGORY_ETC)[0].name(),
            "Green Snail Shell");
}

// A mob can hand over equipment, not just stackables. It lands in the equip
// tab as its own item, ready to be worn.
TEST(AdvanceCombatTest, DropsEquipmentIntoTheEquipTab) {
  Mob mob = SnailMob();
  mob.clear_drops();
  MobDrop* drop = mob.add_drops();
  drop->set_equip("frozen_top");
  drop->set_per_kill(1.0);

  EquipPrototype top;
  top.set_name("Frozen Top");
  top.set_equip_slot(EQUIP_SLOT_TOP);
  top.set_upgrade_slots(7);

  GameState state({{"frozen_top", top}}, {}, {}, {{"snail", mob}},
                  {{"field", OneSnailMap()}}, {}, GameMode::kPlay,
                  JOB_ADVANCEMENT_UNSPECIFIED, /*seed=*/5);
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 60.0);
  ASSERT_GT(state.character.inventory().size(), 1) << "nothing dropped";
  // Index 0 is the sword's replacement -- the swords are worn -- so look for
  // the piece by name rather than by position.
  bool found = false;
  for (int i = 0; i < state.character.inventory().size(); ++i) {
    if (state.character.inventory()[i].prototype().name() == "Frozen Top") {
      found = true;
      EXPECT_EQ(state.character.inventory()[i]
                    .equip_state()
                    .remaining_upgrade_slots(),
                7)
          << "it dropped in a state it can be scrolled from";
    }
  }
  EXPECT_TRUE(found);
}

// A full equip tab loses what drops into it. The alternative is a queue the
// player cannot see.
TEST(AdvanceCombatTest, AFullEquipTabLosesTheDrop) {
  Mob mob = SnailMob();
  mob.clear_drops();
  MobDrop* drop = mob.add_drops();
  drop->set_equip("frozen_top");
  drop->set_per_kill(1.0);

  EquipPrototype top;
  top.set_name("Frozen Top");
  top.set_equip_slot(EQUIP_SLOT_TOP);

  GameState state({{"frozen_top", top}}, {}, {}, {{"snail", mob}},
                  {{"field", OneSnailMap()}}, {}, GameMode::kPlay,
                  JOB_ADVANCEMENT_UNSPECIFIED, /*seed=*/5);
  state.current_map = "field";
  EquipSword(state);
  while (state.character.RoomFor(top) > 0) {
    state.character.PickUp(std::make_unique<EquipInstance>(top));
  }
  int filled = state.character.inventory().size();

  Farm(state, 60.0);
  EXPECT_EQ(state.character.inventory().size(), filled);
}

// A drop naming something no catalog holds is skipped, not guessed at.
TEST(AdvanceCombatTest, AnUnknownEquipDropsNothing) {
  Mob mob = SnailMob();
  mob.clear_drops();
  MobDrop* drop = mob.add_drops();
  drop->set_equip("no_such_item");
  drop->set_per_kill(1.0);

  GameState state({}, {}, {}, {{"snail", mob}}, {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 60.0);
  EXPECT_EQ(state.character.inventory().size(), 0);
}

TEST(AdvanceCombatTest, AccruesMesoWhileFarming) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 20000.0);
  EXPECT_GT(state.character.meso(), 0);
}

// The trial's ceiling seen from the fight: no amount of farming carries a
// character past it. The multiplier is only here to get there quickly.
TEST(AdvanceCombatTest, FarmingStopsAtTheLevelCap) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";
  // Big enough that one level at the top of the table falls inside the farm
  // below: the last one costs 21M, and a snail pays what a snail pays.
  state.exp_multiplier = 10000;
  EquipSword(state);
  // Set down one level short rather than farmed up the whole table: the
  // ceiling is what is being tested, and the climb to it only costs time --
  // time that grows every time the cap moves.
  LevelTo(state, kTrialLevelCap - 1);

  Farm(state, 20000.0);
  EXPECT_EQ(state.character.proto().level(), kTrialLevelCap);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

TEST(AdvanceCombatTest, SkipsFarmingWithoutWeapon) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  state.current_map = "field";

  int start_level = state.character.proto().level();
  Farm(state, 20000.0);
  EXPECT_EQ(state.character.proto().level(), start_level);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

TEST(AdvanceCombatTest, NoOpWithoutCurrentMap) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"field", OneSnailMap()}});
  EquipSword(state);  // current_map left empty

  int start_level = state.character.proto().level();
  Farm(state, 20000.0);
  EXPECT_EQ(state.character.proto().level(), start_level);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

// --- the level-banded pace ---

// Farms `seconds` at `level` and returns how many mobs died. The character is
// levelled by hand first so the band under test is the one in force.
//
// Corpses rather than EXP: EXP stops at kTrialLevelCap and most of the bands
// sit above it, so counting kills is the only way to see the whole table. The
// snail drops a shell every time, which makes the Etc stack the body count.
int64_t KillsFarmedAt(int level, double seconds) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", OneSnailMap()}});
  state.current_map = "field";
  LevelTo(state, level);
  EquipSword(state);
  Farm(state, seconds);
  const std::vector<StackableItem>& etc =
      state.character.stackables(ITEM_CATEGORY_ETC);
  return etc.empty() ? 0 : etc[0].count();
}

// The same fight kills less per second the higher the band, because the fight
// itself runs slower: these snails die in one hit at any of these levels, so
// nothing but the pace has changed between them.
TEST(AdvanceCombatTest, TheSameFightPaysLessAsTheGameSlowsDown) {
  int64_t at_9 = KillsFarmedAt(9, 600.0);
  int64_t at_10 = KillsFarmedAt(10, 600.0);
  int64_t at_140 = KillsFarmedAt(140, 600.0);
  ASSERT_GT(at_140, 0) << "the slowest band still has to kill something";
  EXPECT_GT(at_9, at_10) << "2x band vs 3x band";
  EXPECT_GT(at_10, at_140) << "3x band vs 10x band";
}

// The workbench's EXP bonus pays EXP and nothing else: same corpses, same
// drops, same meso.
//
// Both characters are pinned just under the cap first, or the bonus changes
// what it measures -- EXP levels the character, levelling slows the game, and
// the boosted one ends up killing FEWER mobs in the same wall time. Neither
// moves from there, so the multiplier is all that differs.
TEST(AdvanceCombatTest, TheExpMultiplierPaysExpAndNothingElse) {
  GameState plain({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", OneSnailMap()}});
  plain.current_map = "field";
  LevelTo(plain, kTrialLevelCap - 1);
  EquipSword(plain);
  Farm(plain, 600.0);

  GameState boosted({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                    {{"snail", SnailMob()}}, {{"field", OneSnailMap()}});
  boosted.current_map = "field";
  LevelTo(boosted, kTrialLevelCap - 1);
  boosted.exp_multiplier = 5;
  EquipSword(boosted);
  Farm(boosted, 600.0);

  ASSERT_EQ(boosted.character.proto().level(), kTrialLevelCap - 1)
      << "no band change";
  ASSERT_GT(plain.character.proto().exp(), 0);
  EXPECT_EQ(boosted.character.proto().exp(), 5 * plain.character.proto().exp());
  ASSERT_FALSE(plain.character.stackables(ITEM_CATEGORY_ETC).empty());
  ASSERT_FALSE(boosted.character.stackables(ITEM_CATEGORY_ETC).empty());
  EXPECT_EQ(boosted.character.stackables(ITEM_CATEGORY_ETC)[0].count(),
            plain.character.stackables(ITEM_CATEGORY_ETC)[0].count());
  EXPECT_EQ(boosted.character.meso(), plain.character.meso());
}

// Holy Symbol: the one skill paid out in EXP rather than in the fight. Meso
// and drops are untouched, the same bargain the debug multiplier makes.
TEST(AdvanceCombatTest, HolySymbolPaysExpAndNothingElse) {
  Skill symbol;
  symbol.set_name("Holy Symbol");
  symbol.set_kind(SKILL_KIND_PASSIVE);
  symbol.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  symbol.set_max_level(1);
  symbol.mutable_base()->set_exp_pct(1.0);

  GameState plain({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", OneSnailMap()}},
                  {{"holy_symbol", symbol}});
  plain.current_map = "field";
  LevelTo(plain, kTrialLevelCap - 1);
  EquipSword(plain);
  plain.character.AdvanceJob(JOB_SWORDMAN);
  Farm(plain, 600.0);

  GameState blessed({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                    {{"snail", SnailMob()}}, {{"field", OneSnailMap()}},
                    {{"holy_symbol", symbol}});
  blessed.current_map = "field";
  LevelTo(blessed, kTrialLevelCap - 1);
  EquipSword(blessed);
  // The skill is a Swordman's, so its book has to be open before its one
  // point can be spent.
  ASSERT_TRUE(blessed.character.CanAdvanceJob());
  blessed.character.AdvanceJob(JOB_SWORDMAN);
  ASSERT_TRUE(blessed.character.LearnSkill(symbol, 1));
  Farm(blessed, 600.0);

  ASSERT_GT(plain.character.proto().exp(), 0);
  EXPECT_EQ(blessed.character.proto().exp(), 2 * plain.character.proto().exp());
  EXPECT_EQ(blessed.character.meso(), plain.character.meso());
}

// Meso Mastery is the mirror of Holy Symbol: paid out in the purse, and the
// fight and the EXP behind it left exactly as they were.
TEST(AdvanceCombatTest, MesoMasteryPaysMesoAndNothingElse) {
  Skill mastery;
  mastery.set_name("Meso Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  mastery.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  mastery.set_max_level(1);
  mastery.mutable_base()->set_meso_pct(1.0);

  // A mob near the character's own level: meso falls off hard once they
  // out-level what they kill, and a purse of zero would prove nothing.
  Mob mob = SnailMob();
  mob.set_level(20);

  int64_t meso[2] = {0, 0};
  int64_t exp[2] = {0, 0};
  for (int pass = 0; pass < 2; ++pass) {
    // Both passes roll from the same stream: the drops are rolled now, so a
    // purse twice the size has to come from the bonus rather than from luck.
    GameState state({}, {}, {}, {{"snail", mob}}, {{"field", OneSnailMap()}},
                    {{"meso_mastery", mastery}}, GameMode::kPlay,
                    JOB_ADVANCEMENT_UNSPECIFIED, /*seed=*/7);
    state.current_map = "field";
    LevelTo(state, 25);
    EquipSword(state);
    ASSERT_TRUE(state.character.CanAdvanceJob());
    state.character.AdvanceJob(JOB_SWORDMAN);
    if (pass == 1) {
      ASSERT_TRUE(state.character.LearnSkill(mastery, 1));
    }
    Farm(state, 600.0);
    meso[pass] = state.character.meso();
    exp[pass] = state.character.proto().exp();
  }

  ASSERT_GT(meso[0], 0);
  EXPECT_EQ(meso[1], 2 * meso[0]);
  EXPECT_EQ(exp[1], exp[0]);
}

// Farms `state` until the ogre kills the character, or gives up after a
// generous stretch. Returns whether they died.
bool FarmUntilDeath(GameState& state) {
  CombatSim sim;
  for (int step = 0; step < 1000; ++step) {
    AdvanceCombat(state, sim, 1.0);
    if (state.current_map != "field") {
      return true;
    }
  }
  return false;
}

TEST(AdvanceCombatTest, DyingSendsThePlayerHome) {
  GameState state({}, {}, {}, {{"ogre", OgreMob()}},
                  {{"field", OgreMap()}, {kHomeMap, HomeMap()}});
  state.current_map = "field";
  EquipSword(state);

  ASSERT_TRUE(FarmUntilDeath(state));
  EXPECT_EQ(state.current_map, kHomeMap);
}

TEST(AdvanceCombatTest, DyingCostsNothingButTheTrip) {
  GameState state(
      {}, {}, {{"green_snail_shell", GreenSnailShell()}},
      {{"snail", SnailMob()}, {"ogre", OgreMob()}},
      {{"safe", OneSnailMap()}, {"field", OgreMap()}, {kHomeMap, HomeMap()}});
  state.current_map = "safe";
  EquipSword(state);
  Farm(state, 600.0);
  int64_t exp = state.character.proto().exp();
  int64_t meso = state.character.meso();
  ASSERT_GT(exp, 0);
  ASSERT_GT(meso, 0);

  state.current_map = "field";
  ASSERT_TRUE(FarmUntilDeath(state));
  EXPECT_EQ(state.character.proto().exp(), exp);
  EXPECT_EQ(state.character.meso(), meso);
}

TEST(AdvanceCombatTest, SurvivableMapsDoNotSendThePlayerHome) {
  // Ten minutes on a map whose mobs do land real damage, but that the
  // character clears -- and clearing it is the only thing that heals them, so
  // this is the whole no-regeneration design standing up over time.
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", BitingSnailMob()}},
                  {{"field", OneSnailMap()}, {kHomeMap, HomeMap()}});
  state.current_map = "field";
  EquipSword(state);

  CombatSim sim;
  bool took_a_hit = false;
  for (double elapsed = 0.0; elapsed < 600.0; elapsed += 1.0) {
    AdvanceCombat(state, sim, 1.0);
    took_a_hit = took_a_hit || sim.player_hp() < sim.player_max_hp();
  }
  EXPECT_TRUE(took_a_hit) << "the mobs have to be hurting them at all";
  EXPECT_EQ(state.current_map, "field");
}

}  // namespace
}  // namespace ms
