#include "analysis/gear_plan.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "analysis/sim_gear.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/soul.h"
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
  void Grow(int level) {
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
    character.PickUp(std::make_unique<EquipInstance>(Sword()));
    character.Equip(0);
    character.AddItem(Shard(), 30 * kShardsPerSoul);
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

}  // namespace
}  // namespace ms
