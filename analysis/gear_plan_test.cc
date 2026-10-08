#include "analysis/gear_plan.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "analysis/sim_gear.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/character/symbol.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/soul.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

EquipPrototype Sword() {
  EquipPrototype weapon;
  weapon.set_name("Sword");
  weapon.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  weapon.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  weapon.set_required_level(30);
  weapon.set_attack_speed(ATTACK_SPEED_AVERAGE);
  weapon.mutable_base_stats()->set_attack(100);
  return weapon;
}

Skill SlashBlast() {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(slash, JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  slash.set_max_enemies(6);
  slash.mutable_base()->set_skill_pct(1.83);
  return slash;
}

ItemPrototype Shard() {
  ItemPrototype shard;
  shard.set_name("Lucid's Soul Shard");
  shard.set_short_name("Lucid");
  shard.set_kind(ITEM_KIND_SOUL_SHARD);
  shard.set_soul_tier(SOUL_TIER_SS);
  return shard;
}

class GearPlanTest : public ::testing::Test {
 protected:
  void Grow(int level, const EquipPrototype& weapon = Sword()) {
    state_ = std::make_unique<GameState>(
        std::map<std::string, EquipPrototype>{},
        std::map<std::string, Scroll>{}, std::map<std::string, ItemPrototype>{},
        std::map<std::string, Mob>{{"snail", SnailMob()}},
        std::map<std::string, MapData>{{"field", SnailMap()}},
        std::map<std::string, Skill>{{"slash_blast", SlashBlast()}});
    state_->current_map = "field";
    CharacterInstance& character = state_->character;
    while (character.proto().level() < level) {
      if (character.CanAdvanceJob() && character.proto().job_stage() < 1) {
        character.AdvanceJob(JOB_SWORDMAN);
      }
      character.LevelUp();
    }
    while (character.AllocateStat(STAT_FIELD_STR, 1)) {
    }
    character.LearnSkill(SlashBlast(), 1);
    character.PickUp(std::make_unique<EquipInstance>(weapon));
    character.Equip(0);
    character.AddItem(Shard(), 30 * kShardsPerSoul);
  }

  // A level 200 character with a level 1 symbol worn, against a daily boss
  // asking 100 Arcane Force. Returns the symbol.
  EquipPrototype GrowAgainstTheWall() {
    EquipPrototype weapon = Sword();
    weapon.set_required_level(200);
    Grow(200, weapon);
    Mob wall = SnailMob();
    wall.set_name("Wall");
    wall.set_level(200);
    wall.set_max_hp(1'000'000'000'000);
    wall.set_boss(true);
    state_->mobs["wall"] = wall;
    Boss boss;
    boss.set_name("Wall");
    BossDifficulty* normal = boss.add_difficulties();
    normal->set_name("Normal");
    normal->set_reset(RESET_PERIOD_DAILY);
    normal->set_unlock_level(200);
    normal->set_time_limit_seconds(1800);
    normal->set_arcane_force(100);
    BossPhase* phase = normal->add_phases();
    Spawn* spawn = phase->add_spawns();
    spawn->set_mob("wall");
    spawn->add_spots()->set_x(2);
    ArenaSpot* stand = phase->add_player_spots();
    stand->set_x(2);
    stand->set_y(1);
    state_->bosses["wall"] = boss;

    const EquipPrototype symbol = VanishingJourneySymbol();
    Equip banked;
    banked.set_symbol_exp(1000);
    CharacterInstance& character = state_->character;
    character.PickUp(std::make_unique<EquipInstance>(symbol, banked));
    EXPECT_TRUE(character.Equip(character.inventory().size() - 1));
    EXPECT_EQ(character.base_arcane_force(), 30);
    return symbol;
  }

  const Soul& WornSoul() const {
    return state_->character.WornAt(kBossGear, EQUIP_SLOT_PRIMARY_WEAPON)
        ->equip_state()
        .soul();
  }

  int64_t Shards() const {
    return state_->character.currencies().Count(Shard().name());
  }

  std::unique_ptr<GameState> state_;
};

// Thirty souls in hand all but surely land the best line, and the shopper
// stops there; a second pass with nothing changed spends nothing.
TEST_F(GearPlanTest, ShardsAreRolledUntilTheBestLineLands) {
  Grow(UnlockLevel(Feature::kSoul));
  GearShopper shopper{GearPlan()};
  shopper.Spend(*state_);
  const int souls = shopper.life().souls;
  ASSERT_GE(souls, 1);
  EXPECT_EQ(Shards(), (30 - souls) * kShardsPerSoul);
  const SoulLine kept = WornSoul().line();

  CharacterInstance& character = state_->character;
  Soul trial = WornSoul();
  SoulLine best = SOUL_LINE_UNSPECIFIED;
  double best_power = 0.0;
  for (int line = SOUL_LINE_ATTACK; line <= SOUL_LINE_BOSS_DAMAGE; ++line) {
    trial.set_line(static_cast<SoulLine>(line));
    character.TakeSoul(EQUIP_SLOT_PRIMARY_WEAPON, trial, kBossGear);
    const double power = shopper.Power(*state_);
    if (power > best_power) {
      best = trial.line();
      best_power = power;
    }
  }
  trial.set_line(kept);
  character.TakeSoul(EQUIP_SLOT_PRIMARY_WEAPON, trial, kBossGear);
  EXPECT_EQ(kept, best) << SoulLine_Name(kept);

  shopper.Spend(*state_);
  EXPECT_EQ(shopper.life().souls, souls);
}

TEST_F(GearPlanTest, NoSoulIsRolledBeforeTheEntryUnlocks) {
  Grow(UnlockLevel(Feature::kSoul) - 1);
  GearShopper shopper{GearPlan()};
  shopper.Spend(*state_);
  EXPECT_EQ(shopper.life().souls, 0);
  EXPECT_EQ(WornSoul().line(), SOUL_LINE_UNSPECIFIED);
  EXPECT_EQ(Shards(), 30 * kShardsPerSoul);
}

// A fight asking 100 Arcane Force of a level 1 symbol (30) is fought at 60%
// damage, and two levels reach 50%, 70%. The weapon's stars pay more than a
// symbol level's stat alone, so only the bracket can put the purse there first.
TEST_F(GearPlanTest, SymbolLevelsAreBoughtToTheFightsForceBracket) {
  const EquipPrototype symbol = GrowAgainstTheWall();
  CharacterInstance& character = state_->character;
  const int64_t run =
      SymbolLevelUpCost(symbol, 1) + SymbolLevelUpCost(symbol, 2);
  character.AddMeso(run - character.meso());

  GearShopper shopper{GearPlan()};
  shopper.Spend(*state_);
  EXPECT_EQ(character.base_arcane_force(), 50);
  EXPECT_EQ(shopper.life().symbols, run);
}

// The shelf's rate is what a Familiar Cube is priced at, read after the
// income is set and before the next pass.
TEST_F(GearPlanTest, SettingTheIncomeKeepsTheShelfsRate) {
  GrowAgainstTheWall();
  state_->character.AddMeso(1'000'000'000);
  GearShopper shopper{GearPlan()};
  shopper.Spend(*state_);
  const double rate = shopper.power_per_meso();
  ASSERT_GT(rate, 0.0);
  shopper.SetIncome(CubeIncome());
  EXPECT_EQ(shopper.power_per_meso(), rate);
}

}  // namespace
}  // namespace ms
