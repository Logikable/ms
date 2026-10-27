#include "analysis/gear_plan.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>

#include "analysis/sim_gear.h"
#include "src/character/progression.h"
#include "src/character/skill_placement.h"
#include "src/character/symbol.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/potential.h"
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
    // Seeded, since the state seeds itself from random_device otherwise and a
    // test reading one run of rolls would pass only most of the time.
    state_->rng.seed(1);
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

  // A level 200 sword with 7 slots, which takes no stars, and a 100% trace
  // for it, which is returned.
  Scroll GrowScrollableSword() {
    EquipPrototype weapon = Sword();
    weapon.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
    weapon.set_required_level(200);
    weapon.set_upgrade_slots(7);
    weapon.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
    Grow(200, weapon);
    // Trying on a scroll rebuilds the character from the catalog.
    state_->equips["sword"] = weapon;
    ItemPrototype trace;
    trace.set_name("Spell Trace");
    trace.set_shop_price(100);
    state_->items["spell_trace"] = trace;
    Scroll traced;
    traced.set_name("Trace");
    traced.set_tier(TierForLevel(200));
    traced.set_target(SCROLL_TARGET_WEAPON);
    traced.set_scroll_type(SCROLL_TYPE_ATT);
    traced.set_success_rate(100);
    traced.set_trace_cost(10);
    traced.mutable_stats()->set_attack(3);
    traced.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
    state_->scrolls["trace"] = traced;
    state_->character.AddMeso(1'000'000'000);
    return traced;
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

// A wear-it-now cube left half-run can leave the piece worse, so the purse
// must hold its whole run; a choosing cube needs only the next cube.
TEST_F(GearPlanTest, AReplacingRunMustBeCoveredWhole) {
  // Stars this cheap would price every cube off the shelf.
  EquipPrototype weapon = Sword();
  weapon.add_unsupported_upgrades(UPGRADE_STAR_FORCE);
  weapon.add_unsupported_upgrades(UPGRADE_SCROLL);
  Grow(kBlackCubeUnlockLevel, weapon);
  CharacterInstance& character = state_->character;
  Potential bare;
  bare.set_rank(POTENTIAL_RANK_EPIC);
  ASSERT_TRUE(character.TakePotential(EQUIP_SLOT_PRIMARY_WEAPON,
                                      PotentialTrack::kMain, bare, kBossGear));
  character.AddMeso(1'000'000'000'000);
  GearShopper shopper{GearPlan()};
  int replacing = 0;
  int choosing = 0;
  for (const GearShopper::Candidate& offer : shopper.Shelf(*state_)) {
    if (!offer.cube) {
      continue;
    }
    if (CubeOf(offer.cube_type).choose) {
      EXPECT_EQ(offer.outlay, CubeOf(offer.cube_type).cost);
      ++choosing;
    } else {
      EXPECT_EQ(offer.outlay, 0) << "the run's cost is what must be held";
      ++replacing;
    }
  }
  EXPECT_GT(replacing, 0) << "no replacing cube on the shelf to test";
  EXPECT_GT(choosing, 0) << "no choosing cube on the shelf to test";
}

// A weapon a dropped scroll fits is traced as usual, unless the plan holds its
// slots for that scroll; then the shelf offers it neither trace nor star.
TEST_F(GearPlanTest, AHeldSlotWaitsForTheDroppedScroll) {
  const Scroll traced = GrowScrollableSword();
  Scroll magical = traced;
  magical.set_name("Magical");
  magical.set_paid_with("Magical");
  magical.mutable_stats()->set_attack(10);
  state_->scrolls["magical"] = magical;
  auto offers_for_weapon = [this](const GearPlan& plan) {
    GearShopper shopper{plan};
    shopper.Shelf(
        *state_);  // the first shelf sets the rate slots are judged at
    int offers = 0;
    for (const GearShopper::Candidate& offer : shopper.Shelf(*state_)) {
      offers += offer.scroll != nullptr ? 1 : 0;
    }
    return offers;
  };

  EXPECT_GT(offers_for_weapon(GearPlan()), 0);
  GearPlan held;
  held.holds = [](const std::string& paid_with) {
    return paid_with == "Magical";
  };
  EXPECT_EQ(offers_for_weapon(held), 0);
}

// With Clean Slates giving back each failure, a 15% slot always lands, and
// with nothing else to spend on the shopper grinds every slot at 15%. Without
// slates, or on a piece a later tier replaces, it keeps the 100% trace.
TEST_F(GearPlanTest, CleanSlatesGrindEverySlotAtFifteen) {
  const Scroll traced = GrowScrollableSword();
  Scroll fifteen = traced;
  fifteen.set_name("15% Trace");
  fifteen.set_success_rate(15);
  fifteen.set_trace_cost(20);
  fifteen.mutable_stats()->set_attack(9);
  state_->scrolls["fifteen"] = fifteen;
  Scroll slate;
  slate.set_name("Clean Slate");
  slate.set_scroll_category(SCROLL_CATEGORY_CLEAN_SLATE);
  slate.set_tier(traced.tier());
  slate.set_success_rate(100);
  slate.set_trace_cost(50);
  state_->scrolls["slate"] = slate;
  const Character before = state_->character.ToProto();
  auto scrolled_attack = [this, &before](const GearPlan& plan) {
    state_->character.RestoreFrom(before, state_->equips, state_->items);
    GearShopper shopper{plan};
    shopper.Spend(*state_);
    const Equip& sword =
        state_->character.WornAt(kBossGear, EQUIP_SLOT_PRIMARY_WEAPON)
            ->equip_state();
    EXPECT_EQ(sword.remaining_upgrade_slots(), 0);
    return sword.scroll_stats().attack();
  };
  const int slots = 7 + 2;  // the Golden Hammer's two built in

  EXPECT_EQ(scrolled_attack(GearPlan()), 9 * slots);
  GearPlan plain;
  plain.clean_slates = false;
  EXPECT_EQ(scrolled_attack(plain), 3 * slots);

  // A sword at a later tier would replace this one, and the grind with it.
  EquipPrototype later = state_->equips.at("sword");
  later.set_name("Later Sword");
  later.set_required_level(250);
  state_->equips["later_sword"] = later;
  EXPECT_EQ(scrolled_attack(GearPlan()), 3 * slots);
}

// A replacing run the purse can no longer cover again takes the first roll
// ahead of where it began, rather than rolling until it is left on the last.
TEST_F(GearPlanTest, AReplacingRunShortOfMesoSettlesForAnyGain) {
  Grow(70);
  CharacterInstance& character = state_->character;
  Potential bare;
  bare.set_rank(POTENTIAL_RANK_EPIC);
  ASSERT_TRUE(character.TakePotential(EQUIP_SLOT_PRIMARY_WEAPON,
                                      PotentialTrack::kMain, bare, kBossGear));
  const Cube& red = CubeOf(CubeType::kRed);
  character.AddMeso(200 * red.cost - character.meso());
  GearShopper::Candidate offer;
  offer.slot = EQUIP_SLOT_PRIMARY_WEAPON;
  offer.cube = true;
  offer.cube_type = CubeType::kRed;
  offer.cube_program.reserve.fill(1e18);  // a goal no roll reaches
  offer.cube_program.cost = 1000 * red.cost;
  GearShopper shopper{GearPlan()};
  const double before = shopper.Power(*state_);
  GearSpend spend;
  ASSERT_TRUE(shopper.Buy(*state_, offer, spend));
  EXPECT_GT(character.meso(), 0) << "the run rolled until the purse ran dry";
  // At least level: the run settles on lines its own valuation scores above
  // the start, and a %stat line on a stat this warrior barely has (DEX) rounds
  // to nothing in the fight.
  EXPECT_GE(shopper.Power(*state_), before);
}

// A piece bought with tokens has no shop copy to recover a boom with. With the
// tokens for a spare in hand, the run past 15 is priced and taken; without
// them, it stops where booms begin.
TEST_F(GearPlanTest, TokensForASpareOpenTheRunPastFifteen) {
  EquipPrototype weapon = Sword();
  weapon.set_required_level(160);
  weapon.add_unsupported_upgrades(UPGRADE_SCROLL);
  weapon.add_unsupported_upgrades(UPGRADE_CUBE);
  TokenPrice* price = weapon.add_token_prices();
  price->set_token_item("coin");
  price->set_count(5);
  Grow(200, weapon);
  CharacterInstance& character = state_->character;
  ItemPrototype coin;
  coin.set_name("Coin");
  coin.set_kind(ITEM_KIND_TOKEN);
  state_->items["coin"] = coin;
  Equip fifteen;
  fifteen.set_stars(15);
  character.PickUp(std::make_unique<EquipInstance>(weapon, fifteen));
  ASSERT_TRUE(character.Equip(character.inventory().size() - 1));
  // The copy Grow wore is now in the bag, and would be the spare.
  for (int i = character.inventory().size() - 1; i >= 0; --i) {
    if (character.inventory().equip_instance(i) != nullptr) {
      character.SellEquip(i);
    }
  }
  character.AddMeso(1'000'000'000'000);

  auto furthest = [this]() {
    GearShopper shopper{GearPlan()};
    int to = 0;
    for (const GearShopper::Candidate& offer : shopper.Shelf(*state_)) {
      if (offer.star && offer.slot == EQUIP_SLOT_PRIMARY_WEAPON) {
        to = std::max(to, offer.star_to);
      }
    }
    return to;
  };
  EXPECT_EQ(furthest(), 0) << "every star past 15 can boom";
  character.AddItem(coin, 5);
  EXPECT_GT(furthest(), 15);
}

}  // namespace
}  // namespace ms
