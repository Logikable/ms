#include "src/combat/boss/liberation.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "src/character/character.h"
#include "src/combat/boss/boss_run.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

// A Hero in the workbench's gear, at 260 scrolled and at 22 stars unless
// `bare`: at 200 with plain gear, weak enough that no hit reaches the cap.
std::unique_ptr<GameState> Hero(bool bare = false) {
  TestOptions test;
  test.job = JOB_ADVANCEMENT_HERO_V;
  test.level = bare ? 200 : 260;
  test.equips.scrolled = !bare;
  test.equips.stars = bare ? 0 : 22;
  test.equips.weapon_stars = bare ? 0 : 22;
  test.skills = TestSkills::kMax;
  return std::make_unique<GameState>(
      LoadTestData<EquipPrototype>("equip"), LoadTestData<Scroll>("scrolls"),
      LoadTestData<ItemPrototype>("items"), LoadTestData<Mob>("mobs"),
      LoadTestData<MapData>("maps"), LoadTestData<Skill>("skills"),
      GameMode::kTest, test, /*seed=*/1u, LoadTestData<EquipSet>("sets"),
      LoadTestData<Boss>("bosses"));
}

const BossDifficulty& Difficulty(const GameState& state, int mission) {
  const Boss& boss = state.bosses.at(kLiberationMissions[mission].boss);
  for (const BossDifficulty& difficulty : boss.difficulties()) {
    if (difficulty.name() == kLiberationMissions[mission].difficulty) {
      return difficulty;
    }
  }
  return boss.difficulties(0);
}

double FirstHit(const CombatParams& params) {
  return params.attacks.empty() || params.attacks[0].damage_per_hit.empty()
             ? 0.0
             : params.attacks[0].damage_per_hit[0];
}

TEST(LiberationTest, EveryMissionIsABuiltFight) {
  std::unique_ptr<GameState> state = Hero();
  for (int i = 0; i < kLiberationMissionCount; ++i) {
    std::unique_ptr<BossRun> run =
        LiberationMissionRun(*state, i, BossOptions());
    ASSERT_NE(run, nullptr) << kLiberationMissions[i].boss;
    EXPECT_TRUE(run->mission());
    EXPECT_FALSE(Difficulty(*state, i).coming_soon());
  }
}

TEST(LiberationTest, TanadianRuinJoinsAfterLotus) {
  EXPECT_DOUBLE_EQ(LiberationRules(0).damage_factor, 0.25);
  EXPECT_TRUE(LiberationRules(0).sealed);
  EXPECT_DOUBLE_EQ(LiberationRules(2).damage_factor, 0.80);
  EXPECT_FALSE(LiberationRules(2).sealed);
  EXPECT_DOUBLE_EQ(LiberationRules(3).damage_factor, 1.10);
  EXPECT_DOUBLE_EQ(LiberationRules(6).hp_factor, 0.75);
}

TEST(LiberationTest, TheWeaponIsOfTheTypeWorn) {
  std::unique_ptr<GameState> state = Hero();
  const EquipPrototype* worn = EquippedWeapon(*state, Activity::kBossing);
  ASSERT_NE(worn, nullptr);
  const EquipPrototype* genesis = GenesisWeaponFor(*state);
  ASSERT_NE(genesis, nullptr);
  EXPECT_EQ(genesis->equip_type(), worn->equip_type());
  EXPECT_EQ(genesis->name().rfind("Genesis ", 0), 0u);
}

// The cut multiplies the damage, and the HP cut every monster's HP.
TEST(LiberationTest, TheRulesScaleDamageAndHp) {
  std::unique_ptr<GameState> state = Hero();
  const BossDifficulty& hilla = Difficulty(*state, 6);
  CombatParams plain = ComputeBossParams(*state, "verus_hilla", hilla, 0);
  MissionRules rules;
  rules.damage_factor = 0.5;
  rules.hp_factor = 0.75;
  CombatParams cut = ComputeBossParams(*state, "verus_hilla", hilla, 0, rules);
  ASSERT_GT(FirstHit(plain), 0.0);
  EXPECT_NEAR(FirstHit(cut) / FirstHit(plain), 0.5, 0.01);
  ASSERT_FALSE(plain.types.empty());
  EXPECT_NEAR(static_cast<double>(cut.types[0].mob->max_hp()) /
                  static_cast<double>(plain.types[0].mob->max_hp()),
              0.75, 1e-6);
  EXPECT_EQ(state->mobs.at(hilla.phases(0).spawns(0).mob()).max_hp(),
            plain.types[0].mob->max_hp())
      << "the catalog's mob is untouched";
}

// A sealed fight is fought in the bare weapon alone, and the character's own
// gear comes back after it.
TEST(LiberationTest, ASealedFightStripsTheGearAndPutsItBack) {
  std::unique_ptr<GameState> state = Hero();
  const BossDifficulty& magnus = Difficulty(*state, 1);
  const int attack_before = state->character.equip_stats().attack();
  const size_t worn_before = state->character.equipped().size();
  CombatParams dressed = ComputeBossParams(*state, "magnus", magnus, 0);
  CombatParams sealed;
  {
    const EquipPrototype* genesis = GenesisWeaponFor(*state);
    ASSERT_NE(genesis, nullptr);
    ScopedGear gear(state->character, SealedGear(*state, *genesis));
    for (const auto& [slot, item] : state->character.equipped()) {
      EXPECT_TRUE(slot == EQUIP_SLOT_PRIMARY_WEAPON ||
                  slot == EQUIP_SLOT_SECONDARY ||
                  item->prototype().has_arcane_symbol() ||
                  item->prototype().has_sacred_symbol() ||
                  slot == EQUIP_SLOT_PROJECTILE)
          << EquipSlot_Name(slot);
      EXPECT_EQ(item->equip_state().stars(), 0)
          << EquipSlot_Name(slot) << " keeps no stars";
      EXPECT_EQ(item->prototype().weapon_skills_size(), 0)
          << "the sealed weapon has no skills yet";
    }
    sealed = ComputeBossParams(*state, "magnus", magnus, 0);
  }
  EXPECT_LT(FirstHit(sealed), FirstHit(dressed) / 2);
  EXPECT_EQ(state->character.equip_stats().attack(), attack_before);
  EXPECT_EQ(state->character.equipped().size(), worn_before);
}

// The run seals itself: Magnus's mission takes longer than the same cut
// in the character's own gear.
TEST(LiberationTest, ASealedMissionFightsInTheBareWeapon) {
  std::unique_ptr<GameState> state = Hero(/*bare=*/true);
  std::unique_ptr<BossRun> sealed =
      LiberationMissionRun(*state, 1, BossOptions());
  ASSERT_NE(sealed, nullptr);
  const Boss& magnus = state->bosses.at("magnus");
  int hard = 0;
  while (magnus.difficulties(hard).name() != "Hard") {
    ++hard;
  }
  BossRun dressed("magnus", magnus, hard);
  MissionRules unsealed = LiberationRules(1);
  unsealed.sealed = false;
  dressed.MakeMission(unsealed, 1, kLiberationMissionCount);
  for (int step = 0; step < 20000 && !(sealed->won() && dressed.won());
       ++step) {
    sealed->Advance(*state, 0.1);
    dressed.Advance(*state, 0.1);
  }
  ASSERT_TRUE(sealed->won());
  ASSERT_TRUE(dressed.won());
  EXPECT_GT(sealed->clear_seconds(), 1.2 * dressed.clear_seconds());
}

// A mission pays nothing and records nothing; the run pays at its end.
TEST(LiberationTest, AMissionPaysNothing) {
  std::unique_ptr<GameState> state = Hero();
  const int64_t meso = state->character.meso();
  std::unique_ptr<BossRun> run = LiberationMissionRun(*state, 3, BossOptions());
  ASSERT_NE(run, nullptr);
  run->ClaimClear(*state, 60.0);
  EXPECT_TRUE(run->won());
  EXPECT_EQ(state->character.meso(), meso);
  EXPECT_TRUE(run->reward().items.empty());
  EXPECT_EQ(run->title(), "Hard Damien (4/7)");
}

TEST(LiberationTest, WinningPaysTheWeaponOnce) {
  std::unique_ptr<GameState> state = Hero();
  const EquipPrototype* genesis = GenesisWeaponFor(*state);
  ASSERT_NE(genesis, nullptr);
  EXPECT_FALSE(Liberated(state->character));
  const int bag = state->character.inventory().size();
  ASSERT_TRUE(GrantLiberation(*state, *genesis));
  EXPECT_TRUE(Liberated(state->character));
  ASSERT_EQ(state->character.inventory().size(), bag + 1);
  const Equip& paid = state->character.inventory()[bag].equip_state();
  EXPECT_EQ(paid.stars(), 22);
  EXPECT_EQ(paid.remaining_upgrade_slots(), 0);
  EXPECT_FALSE(paid.has_main_potential());
}

}  // namespace
}  // namespace ms
