#include "src/character/dailies.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/character/symbol.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

int64_t LocalTime(int year, int month, int day, int hour) {
  std::tm local{};
  local.tm_year = year - 1900;
  local.tm_mon = month - 1;
  local.tm_mday = day;
  local.tm_hour = hour;
  local.tm_isdst = -1;
  return static_cast<int64_t>(std::mktime(&local));
}

class DailiesTest : public testing::Test {
 protected:
  DailiesTest() : rng_(1), c_(MakeCharacter()) {
    for (EquipSlot slot :
         {EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND,
          EQUIP_SLOT_SYMBOL_LACHELEIN, EQUIP_SLOT_SYMBOL_ARCANA,
          EQUIP_SLOT_SYMBOL_MORASS, EQUIP_SLOT_SYMBOL_ESFERA}) {
      EquipPrototype proto;
      proto.set_name("Symbol " + std::to_string(slot));
      proto.set_equip_slot(slot);
      proto.mutable_arcane_symbol()->set_meso_cost_base(8);
      equips_[proto.name()] = proto;
    }
    for (EquipSlot slot :
         {EQUIP_SLOT_SYMBOL_CERNIUM, EQUIP_SLOT_SYMBOL_ARCUS,
          EQUIP_SLOT_SYMBOL_ODIUM, EQUIP_SLOT_SYMBOL_TALLAHART}) {
      EquipPrototype proto;
      proto.set_name("Symbol " + std::to_string(slot));
      proto.set_equip_slot(slot);
      proto.mutable_sacred_symbol()->set_meso_cost_base(13.2);
      proto.mutable_sacred_symbol()->set_daily_copies(
          slot == EQUIP_SLOT_SYMBOL_CERNIUM ? 30 : 15);
      equips_[proto.name()] = proto;
    }
    // Something that isn't a symbol, which no claim may ever give.
    EquipPrototype sword;
    sword.set_name("Sword");
    sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    equips_["Sword"] = sword;
  }

  // At the 5th advancement, which Arcane Symbol dailies need.
  static CharacterInstance MakeCharacter(int job_stage = 5) {
    static std::mt19937 rng(1);
    Character proto;
    proto.set_level(260);
    proto.set_job(JOB_HERO);
    proto.set_job_stage(job_stage);
    return CharacterInstance(rng, std::move(proto));
  }

  const EquipPrototype& Proto(EquipSlot slot) {
    return equips_.at("Symbol " + std::to_string(slot));
  }
  void PutInBag(EquipSlot slot) {
    PutInBag(c_, slot);
  }
  void PutInBag(CharacterInstance& c, EquipSlot slot) {
    c.PickUp(std::make_unique<EquipInstance>(Proto(slot)));
  }
  std::vector<EquipSlot> Claimable() {
    return Claimable(c_);
  }
  std::vector<EquipSlot> Claimable(
      const CharacterInstance& c,
      const std::map<std::string, double>& shares = {}) {
    std::vector<EquipSlot> slots;
    for (const DailyReward& reward : ClaimableSymbols(c, equips_, shares)) {
      slots.push_back(reward.proto->equip_slot());
    }
    return slots;
  }
  std::string Name(EquipSlot slot) {
    return Proto(slot).name();
  }

  std::mt19937 rng_;
  CharacterInstance c_;
  std::map<std::string, EquipPrototype> equips_;
};

// Owning one symbol unlocks every area below it, since the river is unlocked in
// order. Owning none unlocks nothing.
TEST_F(DailiesTest, OneSymbolOpensEveryAreaBelowIt) {
  EXPECT_TRUE(Claimable().empty());

  PutInBag(EQUIP_SLOT_SYMBOL_LACHELEIN);
  EXPECT_EQ(Claimable(),
            (std::vector<EquipSlot>{EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY,
                                    EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND,
                                    EQUIP_SLOT_SYMBOL_LACHELEIN}));

  // Worn counts the same as held, and the furthest one decides.
  c_.PickUp(std::make_unique<EquipInstance>(Proto(EQUIP_SLOT_SYMBOL_ARCANA)));
  ASSERT_TRUE(c_.Equip(c_.inventory().size() - 1));
  EXPECT_EQ(Claimable().size(), 4u);
}

// A symbol whose copies already reach the max level isn't claimed again: the
// day's copies would have nowhere to go. The areas around it still are.
TEST_F(DailiesTest, ASymbolHeldToItsMaxIsNotClaimed) {
  Equip maxed;
  maxed.set_symbol_level(kMaxArcaneSymbolLevel);
  c_.PickUp(std::make_unique<EquipInstance>(
      Proto(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY), maxed));
  ASSERT_TRUE(c_.Equip(c_.inventory().size() - 1));
  PutInBag(EQUIP_SLOT_SYMBOL_LACHELEIN);
  EXPECT_EQ(Claimable(),
            (std::vector<EquipSlot>{EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND,
                                    EQUIP_SLOT_SYMBOL_LACHELEIN}));

  // Spares that would finish it count as well as levels already bought.
  Equip nearly;
  nearly.set_symbol_level(19);
  nearly.set_symbol_exp(371);
  c_.PickUp(std::make_unique<EquipInstance>(
      Proto(EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND), nearly));
  ASSERT_TRUE(c_.Equip(c_.inventory().size() - 1));
  EXPECT_EQ(Claimable().size(), 2u);
  PutInBag(EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND);
  EXPECT_EQ(Claimable(), (std::vector<EquipSlot>{EQUIP_SLOT_SYMBOL_LACHELEIN}));
}

// A day's worth comes packed into one item per area, and is worth the same as
// 75 separate copies.
TEST_F(DailiesTest, AClaimPacksADaysCopiesIntoOneItem) {
  EquipPrototype chu_chu = Proto(EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND);
  Equip packed = PackedSymbol(chu_chu, kSymbolsPerDay);
  EXPECT_EQ(SymbolLevel(packed), 5);
  EXPECT_EQ(packed.symbol_exp(), 0);
  EXPECT_EQ(SymbolWorth(chu_chu, packed), kSymbolsPerDay);

  PutInBag(EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND);
  ASSERT_TRUE(ClaimDailies(c_, equips_, LocalTime(2026, 8, 20, 12)));
  EXPECT_EQ(c_.inventory().size(), 3) << "the one held, plus two claimed";
  const EquipInstance& claimed = *c_.inventory().equip_instance(2);
  EXPECT_EQ(SymbolWorth(claimed.prototype(), claimed.equip_state()),
            kSymbolsPerDay);
}

TEST_F(DailiesTest, TheClaimIsOncePerReset) {
  PutInBag(EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY);
  int64_t noon = LocalTime(2026, 8, 20, 12);
  ASSERT_TRUE(ClaimDailies(c_, equips_, noon));
  EXPECT_FALSE(DailiesAvailable(c_.DailiesClaimedAt(), noon));
  EXPECT_FALSE(ClaimDailies(c_, equips_, LocalTime(2026, 8, 21, 3)))
      << "before 4am is still the same day";
  EXPECT_EQ(c_.inventory().size(), 2);

  EXPECT_TRUE(
      DailiesAvailable(c_.DailiesClaimedAt(), LocalTime(2026, 8, 21, 4)));
  EXPECT_TRUE(ClaimDailies(c_, equips_, LocalTime(2026, 8, 21, 4)));
  EXPECT_EQ(c_.inventory().size(), 3) << "a day's claim is its own item";
}

// Claiming half would cost the player the rest until tomorrow, so a bag without
// room for all of it claims nothing.
TEST_F(DailiesTest, ABagWithoutRoomForTheLotClaimsNothing) {
  PutInBag(EQUIP_SLOT_SYMBOL_LACHELEIN);
  while (c_.inventory().room() > 2) {
    c_.PickUp(std::make_unique<EquipInstance>(equips_.at("Sword")));
  }
  int64_t noon = LocalTime(2026, 8, 20, 12);
  EXPECT_FALSE(ClaimDailies(c_, equips_, noon));
  EXPECT_EQ(c_.inventory().room(), 2);
  EXPECT_TRUE(DailiesAvailable(c_.DailiesClaimedAt(), noon))
      << "nothing was taken, so the day is still there";
}

TEST_F(DailiesTest, ArcaneSymbolsWaitForTheFifthJob) {
  CharacterInstance fourth = MakeCharacter(4);
  PutInBag(fourth, EQUIP_SLOT_SYMBOL_LACHELEIN);
  EXPECT_TRUE(Claimable(fourth).empty());
}

// Sacred Symbols wait for the 6th advancement and are paid by the measured
// shares alone: no symbol need be owned, and owning one opens nothing.
TEST_F(DailiesTest, SacredSymbolsArePaidByTheirSharesFromTheSixthJob) {
  std::map<std::string, double> shares = {
      {Name(EQUIP_SLOT_SYMBOL_CERNIUM), 1.0},
      {Name(EQUIP_SLOT_SYMBOL_TALLAHART), 0.5}};
  EXPECT_TRUE(Claimable(c_, shares).empty()) << "only the 5th advancement";

  CharacterInstance sixth = MakeCharacter(6);
  EXPECT_EQ(Claimable(sixth, shares),
            (std::vector<EquipSlot>{EQUIP_SLOT_SYMBOL_CERNIUM,
                                    EQUIP_SLOT_SYMBOL_TALLAHART}));

  PutInBag(sixth, EQUIP_SLOT_SYMBOL_ODIUM);
  PutInBag(sixth, EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND);
  EXPECT_EQ(
      Claimable(sixth, shares),
      (std::vector<EquipSlot>{
          EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY, EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND,
          EQUIP_SLOT_SYMBOL_CERNIUM, EQUIP_SLOT_SYMBOL_TALLAHART}))
      << "Arcane first; owning Odium doesn't open Odium or Arcus";
}

// A share pays that part of the day's count, rounded down; one that rounds to
// nothing leaves the area out.
TEST_F(DailiesTest, ASacredClaimPaysItsShareOfEachCount) {
  CharacterInstance sixth = MakeCharacter(6);
  std::map<std::string, double> shares = {
      {Name(EQUIP_SLOT_SYMBOL_CERNIUM), 0.8},
      {Name(EQUIP_SLOT_SYMBOL_ARCUS), 0.5},
      {Name(EQUIP_SLOT_SYMBOL_ODIUM), 0.01}};
  std::vector<DailyReward> rewards = ClaimableSymbols(sixth, equips_, shares);
  ASSERT_EQ(rewards.size(), 2u);
  EXPECT_EQ(rewards[0].copies, 24);
  EXPECT_EQ(rewards[1].copies, 7) << "7.5 rounds down";
  EXPECT_EQ(SacredCopies(15, 0.7999999999999999), 12)
      << "a share a hair under a whole copy is that copy";

  ASSERT_TRUE(ClaimDailies(sixth, equips_, LocalTime(2026, 8, 20, 12), shares));
  EXPECT_EQ(sixth.SpareSymbolWorths(EQUIP_SLOT_SYMBOL_CERNIUM),
            (std::vector<int>{24}));
  EXPECT_EQ(sixth.SpareSymbolWorths(EQUIP_SLOT_SYMBOL_ARCUS),
            (std::vector<int>{7}));
}

TEST_F(DailiesTest, NothingToClaimIsNotAClaim) {
  EXPECT_FALSE(ClaimDailies(c_, equips_, LocalTime(2026, 8, 20, 12)));
  EXPECT_EQ(c_.DailiesClaimedAt(), 0);
}

}  // namespace
}  // namespace ms
