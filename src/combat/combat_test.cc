#include "src/combat/combat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "src/character/consumables.h"
#include "src/character/exp_table.h"
#include "src/character/honor.h"
#include "src/character/skill_placement.h"
#include "src/character/v_matrix.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/combat/loot.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

// The real snail, hitting hard enough to get through a starting character's
// DEF, so surviving it means something.
Mob BitingSnailMob() {
  Mob mob = SnailMob();
  mob.set_attack(20);
  return mob;
}

// Equips a one-handed sword (100 weapon and magic attack) on the character.
void EquipSword(GameState& state, int item_drop_rate = 0) {
  EquipPrototype sword;
  sword.set_name("Sword");
  sword.mutable_base_stats()->set_item_drop_rate(item_drop_rate);
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(ATTACK_SPEED_AVERAGE);
  // Set both, so the attack lands whatever job the starting character is. The
  // encounter math must not depend on the job.
  sword.mutable_base_stats()->set_attack(100);
  sword.mutable_base_stats()->set_magic_attack(100);
  state.character.PickUp(std::make_unique<EquipInstance>(sword));
  state.character.Equip(0);
}

// Levels the character to `level`. Combat pace depends on level (see
// GameSpeedFactor), so a test that relies on the pace must set the level.
void LevelTo(GameState& state, int level) {
  while (state.character.proto().level() < level) {
    state.character.LevelUp();
  }
}

// Farms for `seconds` of game time. One call advances at most one attack, so
// rewards build up over a loop, as they do under the TUI's ticker.
void Farm(GameState& state, double seconds) {
  CombatSim sim;
  for (double elapsed = 0.0; elapsed < seconds; elapsed += 1.0) {
    AdvanceCombat(state, sim, 1.0);
  }
}

// --- AwardCombatRewards ---

// Offline progress pays hours of kills in one call. The tally is what the
// caller shows the player.
TEST(AwardCombatRewardsTest, PaysABatchOfKillsAndTalliesThem) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);
  CombatParams params = ComputeCombatParams(state);

  RewardTally tally = AwardCombatRewards(state, params, {1000});

  EXPECT_EQ(tally.exp, 3000);
  EXPECT_GT(tally.meso, 0);
  EXPECT_EQ(state.character.meso(), tally.meso);
  ASSERT_EQ(tally.items.size(), 1u);
  EXPECT_EQ(tally.items[0].name, "Green Snail Shell");
  // Counted in items, not stacks: a thousand kills with a certain drop give a
  // thousand shells, however many stacks hold them.
  EXPECT_EQ(tally.items[0].count, 1000);
  EXPECT_EQ(tally.items[0].discarded, 0);
}

// Meso bonuses apply in order: additive bonuses are summed first, then the
// multiplier applies to the total. A 20% bonus under a 1.2x multiplier is
// 1.44x.
TEST(AwardCombatRewardsTest, TheMesoMultiplierLandsOnTheSummedShare) {
  int64_t meso[3] = {0, 0, 0};
  for (int pass = 0; pass < 3; ++pass) {
    GameState state({}, {}, {}, {{"snail", SnailMob()}},
                    {{"field", SnailMap()}}, {}, GameMode::kPlay, TestOptions{},
                    /*seed=*/7);
    state.current_map = "field";
    EquipSword(state);
    CombatParams params = ComputeCombatParams(state);
    if (pass > 0) {
      params.meso_pct = 0.20;
    }
    if (pass > 1) {
      params.meso_final_mult = 1.2;
    }
    meso[pass] = AwardCombatRewards(state, params, {10000}).meso;
  }

  ASSERT_GT(meso[0], 0);
  EXPECT_EQ(meso[1], static_cast<int64_t>(meso[0] * 1.20));
  EXPECT_EQ(meso[2], static_cast<int64_t>(meso[0] * 1.44));
}

// The Wealth Acquisition Potion is used up per second of farming. It pays a
// bonus past the cap, under a multiplier.
TEST(AdvanceCombatTest, TheWealthPotionDrinksBySecondAndPaysAMultiple) {
  Mob mob = SnailMob();
  mob.set_level(kConsumableUnlockLevel);
  int64_t earned[2] = {0, 0};
  int64_t drunk[2] = {0, 0};
  for (int pass = 0; pass < 2; ++pass) {
    GameState state({}, {}, {}, {{"snail", mob}}, {{"field", SnailMap()}}, {},
                    GameMode::kPlay, TestOptions{}, /*seed=*/7);
    state.current_map = "field";
    LevelTo(state, kConsumableUnlockLevel);
    EquipSword(state);
    state.character.AddMeso(1'000'000);
    if (pass == 1) {
      ASSERT_TRUE(state.character.ToggleConsumable(
          CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));
    }
    CombatSim sim;
    for (int second = 0; second < 100; ++second) {
      RewardTally tally = AdvanceCombat(state, sim, 1.0);
      earned[pass] += tally.meso;
      drunk[pass] += tally.consumable_cost;
    }
  }

  // A hundred seconds at a thousand each, and the switched-off buff used
  // nothing.
  EXPECT_EQ(drunk[0], 0);
  EXPECT_EQ(drunk[1], 100'000);
  // More than the 1.44x from the bonus and multiplier alone, because drop rate
  // also applies, and a meso drop has to happen before it can be multiplied.
  ASSERT_GT(earned[0], 0);
  EXPECT_GT(earned[1], static_cast<int64_t>(earned[0] * 1.44));
}

// A character in town doesn't use up the potion. It drains in the same call as
// the fight, and that call does nothing without a map.
TEST(AdvanceCombatTest, TheWealthPotionDrinksNothingOffAMap) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  LevelTo(state, kConsumableUnlockLevel);
  EquipSword(state);
  state.character.AddMeso(1'000'000);
  ASSERT_TRUE(state.character.ToggleConsumable(
      CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION));

  Farm(state, 100.0);  // no current map
  EXPECT_EQ(state.character.meso(), 1'000'000);
}

// Honor comes from kills alone: no bonus raises it. The mob gives no EXP, so no
// level-up adds honor here either.
TEST(AwardCombatRewardsTest, KillsPayHonorIntoTheTallyAndThePool) {
  Mob mob = SnailMob();
  mob.set_exp(0);
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", mob}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);
  CombatParams params = ComputeCombatParams(state);

  RewardTally tally = AwardCombatRewards(state, params, {10000});

  EXPECT_NEAR(tally.honor, 10000 * kMobHonorPerKill, 800);
  EXPECT_EQ(tally.honor % kMobHonorPerDrop, 0);
  EXPECT_EQ(state.character.honor(), tally.honor);
}

// V Points drop only on maps that require a force. The same kills on a normal
// map give none, and neither map needs the 5th advancement.
TEST(AwardCombatRewardsTest, OnlyArcaneRiverPaysVPoints) {
  Mob mob = SnailMob();
  mob.set_exp(0);
  MapData river = SnailMap();
  river.set_arcane_force(600);
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", mob}}, {{"field", SnailMap()}, {"river", river}});
  EquipSword(state);
  ASSERT_FALSE(state.character.v_matrix_unlocked());

  state.current_map = "field";
  EXPECT_EQ(
      AwardCombatRewards(state, ComputeCombatParams(state), {100000}).v_points,
      0);
  EXPECT_EQ(state.character.v_points(), 0);

  state.current_map = "river";
  RewardTally tally =
      AwardCombatRewards(state, ComputeCombatParams(state), {100000});
  EXPECT_NEAR(tally.v_points, 100000 * kVPointDropChance, 40);
  EXPECT_EQ(state.character.v_points(), tally.v_points);
}

// A boss is paid from its fight's own reward table, so the boss mob itself
// gives no EXP, meso or honor, whatever its mob proto says. Its drops still
// fall.
TEST(AwardCombatRewardsTest, ABossBodyPaysNoExpMesoOrHonor) {
  Mob boss = SnailMob();
  boss.set_boss(true);
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", boss}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);
  CombatParams params = ComputeCombatParams(state);

  RewardTally tally = AwardCombatRewards(state, params, {1000});

  EXPECT_EQ(tally.exp, 0);
  EXPECT_EQ(state.character.proto().exp(), 0);
  EXPECT_EQ(tally.meso, 0);
  EXPECT_EQ(state.character.meso(), 0);
  EXPECT_EQ(tally.honor, 0);
  EXPECT_EQ(state.character.honor(), 0);
  ASSERT_EQ(tally.items.size(), 1u);
  EXPECT_EQ(tally.items[0].count, 1000);
}

// A full bag discards the rest, and the tally reports how many.
TEST(AwardCombatRewardsTest, WhatTheBagCannotHoldIsCountedAsDiscarded) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);
  CombatParams params = ComputeCombatParams(state);

  // Far more than 128 slots at the item's max stack can hold.
  int64_t kills = 100000000;
  RewardTally tally = AwardCombatRewards(state, params, {kills});

  ASSERT_EQ(tally.items.size(), 1u);
  EXPECT_GT(tally.items[0].discarded, 0);
  EXPECT_EQ(tally.items[0].count + tally.items[0].discarded, kills);
}

// Two mob types dropping the same item show as one line, not two.
TEST(AwardCombatRewardsTest, OneLinePerItemAcrossMobTypes) {
  Mob slime = SnailMob();
  slime.set_name("Slime");
  MapData map = SnailMap();
  Spawn* second = map.add_spawns();
  second->set_mob("slime");
  second->set_count(6);

  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}, {"slime", slime}}, {{"field", map}});
  state.current_map = "field";
  EquipSword(state);
  CombatParams params = ComputeCombatParams(state);
  ASSERT_EQ(params.types.size(), 2u);

  RewardTally tally = AwardCombatRewards(state, params, {10, 10});

  ASSERT_EQ(tally.items.size(), 1u);
  EXPECT_EQ(tally.items[0].count, 20);
}

TEST(AdvanceCombatTest, GrantsExpWhileFarming) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 20000.0);  // many kills -> several level-ups
  EXPECT_GT(state.character.proto().level(), 2);
}

TEST(AdvanceCombatTest, AccruesDropsWhileFarming) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 20000.0);
  ASSERT_FALSE(state.character.stackables().empty());
  EXPECT_EQ(state.character.stackables()[0].name(), "Green Snail Shell");
}

// A mob can drop equipment, not just stackables. It lands in the equip tab as
// its own item, ready to wear.
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
                  {{"field", SnailMap()}}, {}, GameMode::kPlay, TestOptions{},
                  /*seed=*/5);
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 60.0);
  ASSERT_GT(state.character.inventory().size(), 1) << "nothing dropped";
  // Index 0 isn't the drop, because the sword is worn, so look for the piece by
  // name instead of by position.
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

// A full equip tab loses the drop. The alternative would be a queue the player
// can't see.
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
                  {{"field", SnailMap()}}, {}, GameMode::kPlay, TestOptions{},
                  /*seed=*/5);
  state.current_map = "field";
  EquipSword(state);
  while (state.character.RoomFor(top) > 0) {
    state.character.PickUp(std::make_unique<EquipInstance>(top));
  }
  int filled = state.character.inventory().size();

  Farm(state, 60.0);
  EXPECT_EQ(state.character.inventory().size(), filled);
}

// A drop naming an item missing from every catalog is skipped, not guessed at.
TEST(AdvanceCombatTest, AnUnknownEquipDropsNothing) {
  Mob mob = SnailMob();
  mob.clear_drops();
  MobDrop* drop = mob.add_drops();
  drop->set_equip("no_such_item");
  drop->set_per_kill(1.0);

  GameState state({}, {}, {}, {{"snail", mob}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 60.0);
  EXPECT_EQ(state.character.inventory().size(), 0);
}

TEST(AdvanceCombatTest, AccruesMesoWhileFarming) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);

  Farm(state, 20000.0);
  EXPECT_GT(state.character.meso(), 0);
}

// No amount of farming takes a character past the trial's level cap. The EXP
// multiplier just gets there quickly.
TEST(AdvanceCombatTest, FarmingStopsAtTheLevelCap) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  // Large enough that a whole level at the top of the table fits in the farming
  // below. The last level costs 243B, and a snail pays very little.
  state.exp_multiplier = 100000000;
  EquipSword(state);
  // Start one level short instead of farming up the whole table. The test is
  // about the cap, and the climb only costs time, which grows every time the
  // cap moves.
  LevelTo(state, kTrialLevelCap - 1);

  Farm(state, 20000.0);
  EXPECT_EQ(state.character.proto().level(), kTrialLevelCap);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

TEST(AdvanceCombatTest, SkipsFarmingWithoutWeapon) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";

  int start_level = state.character.proto().level();
  Farm(state, 20000.0);
  EXPECT_EQ(state.character.proto().level(), start_level);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

TEST(AdvanceCombatTest, NoOpWithoutCurrentMap) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  EquipSword(state);  // current_map left empty

  int start_level = state.character.proto().level();
  Farm(state, 20000.0);
  EXPECT_EQ(state.character.proto().level(), start_level);
  EXPECT_EQ(state.character.proto().exp(), 0);
}

// --- the level-banded pace ---

// How many items the character holds. The snail always drops a shell, so this
// is the kill count.
int64_t EtcHeld(const GameState& state) {
  int64_t held = 0;
  for (const StackableItem& stack : state.character.stackables()) {
    held += stack.count();
  }
  return held;
}

// Farms `seconds` at `level` and returns how many mobs died. Kills are
// counted instead of EXP because EXP stops at kTrialLevelCap and most bands
// sit above it.
int64_t KillsFarmedAt(int level, double seconds) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  LevelTo(state, level);
  EquipSword(state);
  Farm(state, seconds);
  return EtcHeld(state);
}

// Meso per kill for a level-`level` character farming level-20 snails, long
// enough for the roll to average out.
double MesoPerKillAt(int level) {
  Mob mob = SnailMob();
  mob.set_level(20);
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", mob}}, {{"field", SnailMap()}}, {},
                  GameMode::kPlay, TestOptions{}, /*seed=*/3);
  state.current_map = "field";
  LevelTo(state, level);
  EquipSword(state);
  Farm(state, 6000.0);
  int64_t kills = EtcHeld(state);
  return kills == 0 ? 0.0 : static_cast<double>(state.character.meso()) / kills;
}

// A mob pays the same whatever the level of the character killing it. GMS cuts
// the reward once the level gap passes ten either way, to protect a shared
// economy we don't have. Under GMS's rule, the +40 gap here would pay nothing.
TEST(AdvanceCombatTest, TheLevelGapDoesNotChangeWhatAMobPays) {
  Mob mob;
  mob.set_level(20);
  double expected = ExpectedMesoPerKill(mob, 0.0);
  EXPECT_NEAR(MesoPerKillAt(20) / expected, 1.0, 0.05);
  EXPECT_NEAR(MesoPerKillAt(60) / expected, 1.0, 0.05);  // 40 levels over
  EXPECT_NEAR(MesoPerKillAt(5) / expected, 1.0, 0.05);   // 15 levels under
}

// The same fight kills fewer mobs per second at higher level bands, because the
// game runs slower. These snails die in one hit at every level tested, so only
// the pace differs.
TEST(AdvanceCombatTest, TheSameFightPaysLessAsTheGameSlowsDown) {
  int64_t at_9 = KillsFarmedAt(9, 600.0);
  int64_t at_10 = KillsFarmedAt(10, 600.0);
  int64_t at_140 = KillsFarmedAt(140, 600.0);
  ASSERT_GT(at_140, 0) << "the slowest band still has to kill something";
  EXPECT_GT(at_9, at_10) << "2x band vs 3x band";
  EXPECT_GT(at_10, at_140) << "3x band vs 10x band";
}

// The EXP multiplier changes EXP and nothing else: same kills, drops and meso.
//
// Both characters start just under the cap and stay there. Otherwise the extra
// EXP would level the boosted character, slowing their game, and they would
// kill fewer mobs in the same time.
TEST(AdvanceCombatTest, TheExpMultiplierPaysExpAndNothingElse) {
  // Use one seed for both runs. Meso is rolled, so two random streams would
  // give different amounts however little the bonus affects them.
  GameState plain({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", SnailMap()}}, {},
                  GameMode::kPlay, TestOptions{}, /*seed=*/9);
  plain.current_map = "field";
  LevelTo(plain, kTrialLevelCap - 1);
  EquipSword(plain);
  Farm(plain, 600.0);

  GameState boosted({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                    {{"snail", SnailMob()}}, {{"field", SnailMap()}}, {},
                    GameMode::kPlay, TestOptions{}, /*seed=*/9);
  boosted.current_map = "field";
  LevelTo(boosted, kTrialLevelCap - 1);
  boosted.exp_multiplier = 5;
  EquipSword(boosted);
  Farm(boosted, 600.0);

  ASSERT_EQ(boosted.character.proto().level(), kTrialLevelCap - 1)
      << "no band change";
  ASSERT_GT(plain.character.proto().exp(), 0);
  EXPECT_EQ(boosted.character.proto().exp(), 5 * plain.character.proto().exp());
  ASSERT_FALSE(plain.character.stackables().empty());
  ASSERT_FALSE(boosted.character.stackables().empty());
  EXPECT_EQ(boosted.character.stackables()[0].count(),
            plain.character.stackables()[0].count());
  EXPECT_EQ(boosted.character.meso(), plain.character.meso());
}

// Holy Symbol pays out in EXP, not in the fight. Meso and drops are unchanged,
// the same as the debug multiplier.
TEST(AdvanceCombatTest, HolySymbolPaysExpAndNothingElse) {
  Skill symbol;
  symbol.set_name("Holy Symbol");
  symbol.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(symbol, JOB_ADVANCEMENT_SWORDMAN);
  symbol.set_max_level(1);
  symbol.mutable_base()->set_exp_pct(1.0);

  // One seed for both runs, as above.
  GameState plain({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", SnailMap()}},
                  {{"holy_symbol", symbol}}, GameMode::kPlay, TestOptions{},
                  /*seed=*/9);
  plain.current_map = "field";
  LevelTo(plain, kTrialLevelCap - 1);
  EquipSword(plain);
  plain.character.AdvanceJob(JOB_SWORDMAN);
  Farm(plain, 600.0);

  GameState blessed({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                    {{"snail", SnailMob()}}, {{"field", SnailMap()}},
                    {{"holy_symbol", symbol}}, GameMode::kPlay, TestOptions{},
                    /*seed=*/9);
  blessed.current_map = "field";
  LevelTo(blessed, kTrialLevelCap - 1);
  EquipSword(blessed);
  // The skill belongs to Swordman, so the character must advance before
  // spending its one point.
  ASSERT_TRUE(blessed.character.CanAdvanceJob());
  blessed.character.AdvanceJob(JOB_SWORDMAN);
  ASSERT_TRUE(blessed.character.LearnSkill(symbol, 1));
  Farm(blessed, 600.0);

  ASSERT_GT(plain.character.proto().exp(), 0);
  EXPECT_EQ(blessed.character.proto().exp(), 2 * plain.character.proto().exp());
  EXPECT_EQ(blessed.character.meso(), plain.character.meso());
}

// Meso Mastery is the opposite of Holy Symbol: it adds meso and leaves the
// fight and EXP exactly as they were.
TEST(AdvanceCombatTest, MesoMasteryPaysMesoAndNothingElse) {
  Skill mastery;
  mastery.set_name("Meso Mastery");
  mastery.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(mastery, JOB_ADVANCEMENT_SWORDMAN);
  mastery.set_max_level(1);
  mastery.mutable_base()->set_meso_pct(1.0);

  Mob mob = SnailMob();
  mob.set_level(20);

  int64_t meso[2] = {0, 0};
  int64_t exp[2] = {0, 0};
  for (int pass = 0; pass < 2; ++pass) {
    // Both passes roll from the same stream, since drops are rolled. A doubled
    // meso total must come from the bonus, not luck.
    GameState state({}, {}, {}, {{"snail", mob}}, {{"field", SnailMap()}},
                    {{"meso_mastery", mastery}}, GameMode::kPlay, TestOptions{},
                    /*seed=*/7);
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

// Farms `state` until the ogre kills the character, or gives up after a long
// time. Returns whether they died.
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
      {{"safe", SnailMap()}, {"field", OgreMap()}, {kHomeMap, HomeMap()}});
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
  // Ten minutes on a map whose mobs deal real damage but which the character
  // clears. Clearing is the only thing that heals them, so this checks that the
  // no-regeneration design holds up over time.
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", BitingSnailMob()}},
                  {{"field", SnailMap()}, {kHomeMap, HomeMap()}});
  state.current_map = "field";
  EquipSword(state);

  CombatSim sim;
  bool took_a_hit = false;
  for (double elapsed = 0.0; elapsed < 600.0; elapsed += 1.0) {
    AdvanceCombat(state, sim, 1.0);
    took_a_hit = took_a_hit || sim.view().player_hp < sim.view().player_max_hp;
  }
  EXPECT_TRUE(took_a_hit) << "the mobs have to be hurting them at all";
  EXPECT_EQ(state.current_map, "field");
}

// Drop rate raises both parts of a kill's reward: the mob's listed items and
// its meso. A snail always drops its shell, so a rate above one shows the whole
// number part is paid outright.
TEST(AdvanceCombatTest, DropRatePaysMoreItemsAndMoreMeso) {
  Mob snail = SnailMob();
  snail.set_level(20);  // level 1 pays a flat meso; a band pays by the level
  std::map<std::string, ItemPrototype> items = {
      {"green_snail_shell", GreenSnailShell()}};
  std::map<std::string, Mob> mobs = {{"snail", snail}};
  std::map<std::string, MapData> maps = {{"field", SnailMap()}};

  GameState plain({}, {}, items, mobs, maps, {}, GameMode::kPlay, TestOptions{},
                  /*seed=*/17);
  plain.current_map = "field";
  EquipSword(plain);
  Farm(plain, 20000.0);

  GameState lucky({}, {}, items, mobs, maps, {}, GameMode::kPlay, TestOptions{},
                  /*seed=*/17);
  lucky.current_map = "field";
  EquipSword(lucky, 50);
  Farm(lucky, 20000.0);

  int64_t kills = EtcHeld(plain);
  ASSERT_GT(kills, 100) << "too few kills to measure a rate against";
  // Half again as many shells for the same kills, and the same increase in
  // kills that pay meso.
  EXPECT_NEAR(static_cast<double>(EtcHeld(lucky)) / kills, 1.5, 0.05);
  EXPECT_NEAR(static_cast<double>(lucky.character.meso()) /
                  static_cast<double>(plain.character.meso()),
              1.5, 0.05);
}

}  // namespace
}  // namespace ms
