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

// The shipped snail, swinging hard enough to be felt through a starting
// character's DEF -- so a run that survives it survived something.
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

// --- AwardCombatRewards ---

// Paying a batch of kills in one call is what offline progress does with
// hours of them. The tally is what a caller shows the player.
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
  // Counted in units, not stacks: a thousand kills of a certain drop is a
  // thousand shells however many stacks they were put into.
  EXPECT_EQ(tally.items[0].count, 1000);
  EXPECT_EQ(tally.items[0].discarded, 0);
}

// The two shares meet the purse in order: everything additive is summed and
// the multiplier lands on the total. A 20% share under a 1.2x is 1.44x.
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

// The Wealth Acquisition Potion is charged by the second of farming, and what
// it pays back is a share past the cap under a multiplier.
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

  // A hundred seconds at a thousand each, and the pot switched off drank
  // nothing at all.
  EXPECT_EQ(drunk[0], 0);
  EXPECT_EQ(drunk[1], 100'000);
  // More than the 1.44x the share and the multiplier come to on their own:
  // the drop rate rides with them, and a meso drop has to happen before it
  // can be multiplied.
  ASSERT_GT(earned[0], 0);
  EXPECT_GT(earned[1], static_cast<int64_t>(earned[0] * 1.44));
}

// A character standing in town is not drinking it: the drain rides the same
// call the fight does, and that call does nothing without a map.
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

// A kill's honor is its own: no bonus lifts it, and nothing but the kills is
// counted here -- the mob is worth no EXP, so no level pays honor over it.
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

// V Points are the 5th job's alone: a 4th job's kills pay none however many
// they are, and the same kills pay once the advancement is taken.
TEST(AwardCombatRewardsTest, OnlyAFifthJobIsPaidVPoints) {
  Mob mob = SnailMob();
  mob.set_exp(0);
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", mob}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);
  CombatParams params = ComputeCombatParams(state);

  EXPECT_EQ(AwardCombatRewards(state, params, {100000}).v_points, 0);
  EXPECT_EQ(state.character.v_points(), 0);

  for (int stage = state.character.proto().job_stage(); stage < kFifthJobStage;
       ++stage) {
    state.character.AdvanceJob(state.character.proto().job());
  }
  ASSERT_TRUE(state.character.v_matrix_unlocked());

  RewardTally tally = AwardCombatRewards(state, params, {100000});
  EXPECT_NEAR(tally.v_points, 100000 * kVPointDropChance, 40);
  EXPECT_EQ(state.character.v_points(), tally.v_points);
}

// A boss is paid for out of its fight's own table, so the body itself is
// worth neither EXP, meso nor honor however much its mob proto still carries.
// Its drops are its own and still fall.
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

// A full bag throws the rest away, and the tally says how many.
TEST(AwardCombatRewardsTest, WhatTheBagCannotHoldIsCountedAsDiscarded) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  EquipSword(state);
  CombatParams params = ComputeCombatParams(state);

  // Far more than 128 slots of the item's max stack can hold.
  int64_t kills = 100000000;
  RewardTally tally = AwardCombatRewards(state, params, {kills});

  ASSERT_EQ(tally.items.size(), 1u);
  EXPECT_GT(tally.items[0].discarded, 0);
  EXPECT_EQ(tally.items[0].count + tally.items[0].discarded, kills);
}

// Two mob types dropping the same item read as one line, not two.
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
                  {{"field", SnailMap()}}, {}, GameMode::kPlay, TestOptions{},
                  /*seed=*/5);
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

// A drop naming something no catalog holds is skipped, not guessed at.
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

// The trial's ceiling seen from the fight: no amount of farming carries a
// character past it. The multiplier is only here to get there quickly.
TEST(AdvanceCombatTest, FarmingStopsAtTheLevelCap) {
  GameState state({}, {}, {}, {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  // Big enough that one level at the top of the table falls inside the farm
  // below: the last one costs 46.7B, and a snail pays what a snail pays.
  state.exp_multiplier = 20000000;
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

// Farms `seconds` at `level` and returns how many mobs died. The character is
// levelled by hand first so the band under test is the one in force.
//
// Corpses rather than EXP: EXP stops at kTrialLevelCap and most of the bands
// sit above it, so counting kills is the only way to see the whole table. The
// snail drops a shell every time, which makes the Etc stack the body count.
int64_t EtcHeld(const GameState& state) {
  int64_t held = 0;
  for (const StackableItem& stack :
       state.character.stackables(ITEM_CATEGORY_ETC)) {
    held += stack.count();
  }
  return held;
}

int64_t KillsFarmedAt(int level, double seconds) {
  GameState state({}, {}, {{"green_snail_shell", GreenSnailShell()}},
                  {{"snail", SnailMob()}}, {{"field", SnailMap()}});
  state.current_map = "field";
  LevelTo(state, level);
  EquipSword(state);
  Farm(state, seconds);
  return EtcHeld(state);
}

// Meso a level-`level` character earned per mob killed, farming level-20
// snails for long enough that the roll averages out. The shell drops every
// kill, so the Etc the character holds is the body count.
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

// A mob pays what it is worth, whatever level the character killing it is.
// GMS reduces the reward once the gap passes ten levels either way, which
// guards a shared economy we do not have; we pay the mob's own worth instead.
// Under GMS's rule the +40 gap here paid nothing at all.
TEST(AdvanceCombatTest, TheLevelGapDoesNotChangeWhatAMobPays) {
  Mob mob;
  mob.set_level(20);
  double expected = ExpectedMesoPerKill(mob, 0.0);
  EXPECT_NEAR(MesoPerKillAt(20) / expected, 1.0, 0.05);
  EXPECT_NEAR(MesoPerKillAt(60) / expected, 1.0, 0.05);  // 40 levels over
  EXPECT_NEAR(MesoPerKillAt(5) / expected, 1.0, 0.05);   // 15 levels under
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
  // One seed across both runs: the purse is rolled, so two streams disagree on
  // it however little the bonus touches them.
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
  PlaceIn(symbol, JOB_ADVANCEMENT_SWORDMAN);
  symbol.set_max_level(1);
  symbol.mutable_base()->set_exp_pct(1.0);

  // One seed across both runs, as above.
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
  PlaceIn(mastery, JOB_ADVANCEMENT_SWORDMAN);
  mastery.set_max_level(1);
  mastery.mutable_base()->set_meso_pct(1.0);

  Mob mob = SnailMob();
  mob.set_level(20);

  int64_t meso[2] = {0, 0};
  int64_t exp[2] = {0, 0};
  for (int pass = 0; pass < 2; ++pass) {
    // Both passes roll from the same stream: the drops are rolled now, so a
    // purse twice the size has to come from the bonus rather than from luck.
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
  // Ten minutes on a map whose mobs do land real damage, but that the
  // character clears -- and clearing it is the only thing that heals them, so
  // this is the whole no-regeneration design standing up over time.
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

// Drop rate reaches both halves of what a kill pays: the items the mob lists
// and the meso it carries. A snail always drops its shell, so a rate past one
// is the case that proves the whole part is paid outright.
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
  // Half again as many shells for the same body count, and the same share more
  // of the kills paying meso.
  EXPECT_NEAR(static_cast<double>(EtcHeld(lucky)) / kills, 1.5, 0.05);
  EXPECT_NEAR(static_cast<double>(lucky.character.meso()) /
                  static_cast<double>(plain.character.meso()),
              1.5, 0.05);
}

}  // namespace
}  // namespace ms
