#include "src/multiplayer/trade_exchange.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>

#include "src/character/character.h"
#include "src/item/equip_instance.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

EquipPrototype Sword() {
  EquipPrototype proto;
  proto.set_name("Sword");
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  return proto;
}

ItemPrototype Scroll() {
  ItemPrototype proto;
  proto.set_name("Chaos Scroll");
  proto.set_category(ITEM_CATEGORY_ETC);
  proto.set_max_stack(100);
  return proto;
}

ItemPrototype Trace() {
  ItemPrototype proto;
  proto.set_name(kSpellTraceName);
  proto.set_category(ITEM_CATEGORY_ETC);
  proto.set_max_stack(100);
  return proto;
}

class TradeExchangeTest : public ::testing::Test {
 protected:
  TradeExchangeTest() {
    items_[Scroll().name()] = Scroll();
    items_[kSpellTraceName] = Trace();
  }

  CharacterInstance MakeCharacter() {
    Character proto;
    proto.set_level(1);
    proto.set_job(JOB_BEGINNER);
    return CharacterInstance(rng_, std::move(proto));
  }

  // Fills the equip tab, or the Etc tab's slots, to the brim.
  void FillEquipTab(CharacterInstance& character) {
    while (character.inventory().room() > 0) {
      character.PickUp(std::make_unique<EquipInstance>(Sword()));
    }
  }
  void FillEtcTab(CharacterInstance& character) {
    for (int i = 0; i < kTabCapacity; ++i) {
      ItemPrototype filler = Scroll();
      filler.set_name("Filler " + std::to_string(i));
      character.AddStackable(filler, 1);
    }
  }

  std::mt19937 rng_{0};
  std::map<std::string, ItemPrototype> items_;
};

TEST_F(TradeExchangeTest, AnEmptyBagTakesAnything) {
  CharacterInstance character = MakeCharacter();
  TradeOffer received;
  received.add_equips()->set_equip_name("Sword");
  received.add_stacks()->set_name("Chaos Scroll");
  received.mutable_stacks(0)->set_count(50);
  received.set_spell_traces(900);
  EXPECT_TRUE(HasRoomForTrade(character, items_, TradeOffer(), received));
}

TEST_F(TradeExchangeTest, WhatYouGiveMakesTheRoom) {
  CharacterInstance character = MakeCharacter();
  FillEquipTab(character);

  TradeOffer received;
  received.add_equips()->set_equip_name("Sword");
  received.add_equips()->set_equip_name("Sword");
  EXPECT_FALSE(HasRoomForTrade(character, items_, TradeOffer(), received));

  // Two out for two in fits exactly; one out for two does not.
  TradeOffer given;
  given.add_equips()->set_equip_name("Sword");
  EXPECT_FALSE(HasRoomForTrade(character, items_, given, received));
  given.add_equips()->set_equip_name("Sword");
  EXPECT_TRUE(HasRoomForTrade(character, items_, given, received));
}

TEST_F(TradeExchangeTest, AFullEtcTabStillTopsUpAnOpenStack) {
  CharacterInstance character = MakeCharacter();
  character.AddStackable(Scroll(), 40);
  FillEtcTab(character);
  ASSERT_EQ(character.RoomFor(Scroll()), 60) << "the open stack and no slot";

  TradeOffer received;
  TradeStack* stack = received.add_stacks();
  stack->set_name("Chaos Scroll");
  stack->set_count(60);
  EXPECT_TRUE(HasRoomForTrade(character, items_, TradeOffer(), received));

  stack->set_count(61);
  EXPECT_FALSE(HasRoomForTrade(character, items_, TradeOffer(), received));

  // Emptying a stack on the way out frees its slot for the overflow.
  TradeOffer given;
  TradeStack* spent = given.add_stacks();
  spent->set_name("Filler 0");
  spent->set_count(1);
  EXPECT_TRUE(HasRoomForTrade(character, items_, given, received));
}

TEST_F(TradeExchangeTest, TracesTakeSlotsLikeAnythingElse) {
  CharacterInstance character = MakeCharacter();
  FillEtcTab(character);

  TradeOffer received;
  received.set_spell_traces(1);
  EXPECT_FALSE(HasRoomForTrade(character, items_, TradeOffer(), received));

  // Traces given away come off the same tab, so they free the same slot.
  CharacterInstance richer = MakeCharacter();
  richer.AddStackable(Trace(), 1);
  FillEtcTab(richer);
  TradeOffer given;
  given.set_spell_traces(1);
  EXPECT_TRUE(HasRoomForTrade(richer, items_, given, received));
}

TEST_F(TradeExchangeTest, AnItemThisBuildCannotNameIsRefused) {
  CharacterInstance character = MakeCharacter();
  TradeOffer received;
  received.add_stacks()->set_name("Something Else");
  received.mutable_stacks(0)->set_count(1);
  EXPECT_FALSE(HasRoomForTrade(character, items_, TradeOffer(), received));
}

}  // namespace
}  // namespace ms
