#include "src/character/dailies.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/character/character.h"
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
    // Something that isn't a symbol, which no claim may ever give.
    EquipPrototype sword;
    sword.set_name("Sword");
    sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    equips_["Sword"] = sword;
  }

  static CharacterInstance MakeCharacter() {
    static std::mt19937 rng(1);
    Character proto;
    proto.set_level(230);
    proto.set_job(JOB_HERO);
    proto.set_job_stage(4);
    return CharacterInstance(rng, std::move(proto));
  }

  const EquipPrototype& Proto(EquipSlot slot) {
    return equips_.at("Symbol " + std::to_string(slot));
  }
  void PutInBag(EquipSlot slot) {
    c_.PickUp(std::make_unique<EquipInstance>(Proto(slot)));
  }
  std::vector<EquipSlot> Claimable() {
    std::vector<EquipSlot> slots;
    for (const EquipPrototype* proto : ClaimableSymbols(c_, equips_)) {
      slots.push_back(proto->equip_slot());
    }
    return slots;
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

// A day's worth comes packed into one item per area, and is worth the same as
// twenty separate copies.
TEST_F(DailiesTest, AClaimPacksTwentyCopiesIntoOneItem) {
  Equip packed = PackedSymbol(kSymbolsPerDay);
  EXPECT_EQ(SymbolLevel(packed), 2);
  EXPECT_EQ(packed.symbol_exp(), 7);
  EXPECT_EQ(SymbolWorth(packed), kSymbolsPerDay);

  PutInBag(EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND);
  ASSERT_TRUE(ClaimDailies(c_, equips_, LocalTime(2026, 8, 20, 12)));
  EXPECT_EQ(c_.inventory().size(), 3) << "the one held, plus two claimed";
  EXPECT_EQ(SymbolWorth(c_.inventory().equip_instance(2)->equip_state()),
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

TEST_F(DailiesTest, NothingToClaimIsNotAClaim) {
  EXPECT_FALSE(ClaimDailies(c_, equips_, LocalTime(2026, 8, 20, 12)));
  EXPECT_EQ(c_.DailiesClaimedAt(), 0);
}

}  // namespace
}  // namespace ms
