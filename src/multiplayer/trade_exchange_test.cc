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
  proto.set_max_stack(100);
  return proto;
}

ItemPrototype Trace() {
  ItemPrototype proto;
  proto.set_name(kSpellTraceName);
  proto.set_kind(ITEM_KIND_SPELL_TRACE);
  return proto;
}

class TradeExchangeTest : public ::testing::Test {
 protected:
  TradeExchangeTest() {
    items_[Scroll().name()] = Scroll();
    items_[kSpellTraceName] = Trace();
    equips_[Sword().name()] = Sword();
  }

  CharacterInstance MakeCharacter() {
    Character proto;
    proto.set_level(1);
    proto.set_job(JOB_BEGINNER);
    return CharacterInstance(rng_, std::move(proto));
  }

  // Fills the equip tab, or the Etc tab's slots, completely.
  void FillEquipTab(CharacterInstance& character) {
    while (character.inventory().room() > 0) {
      character.PickUp(std::make_unique<EquipInstance>(Sword()));
    }
  }
  void FillEtcTab(CharacterInstance& character) {
    for (int i = 0; i < kTabCapacity; ++i) {
      ItemPrototype filler = Scroll();
      filler.set_name("Filler " + std::to_string(i));
      character.AddItem(filler, 1);
    }
  }

  std::mt19937 rng_{0};
  std::map<std::string, ItemPrototype> items_;
  std::map<std::string, EquipPrototype> equips_;
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

  // Two out for two in fits exactly; one out for two in doesn't.
  TradeOffer given;
  given.add_equips()->set_equip_name("Sword");
  EXPECT_FALSE(HasRoomForTrade(character, items_, given, received));
  given.add_equips()->set_equip_name("Sword");
  EXPECT_TRUE(HasRoomForTrade(character, items_, given, received));
}

TEST_F(TradeExchangeTest, AFullEtcTabStillTopsUpAnOpenStack) {
  CharacterInstance character = MakeCharacter();
  character.AddItem(Scroll(), 40);
  FillEtcTab(character);
  ASSERT_EQ(character.RoomFor(Scroll()), 60) << "the open stack and no slot";

  TradeOffer received;
  TradeStack* stack = received.add_stacks();
  stack->set_name("Chaos Scroll");
  stack->set_count(60);
  EXPECT_TRUE(HasRoomForTrade(character, items_, TradeOffer(), received));

  stack->set_count(61);
  EXPECT_FALSE(HasRoomForTrade(character, items_, TradeOffer(), received));

  // Emptying a stack on the way out frees its slot for the extra item.
  TradeOffer given;
  TradeStack* spent = given.add_stacks();
  spent->set_name("Filler 0");
  spent->set_count(1);
  EXPECT_TRUE(HasRoomForTrade(character, items_, given, received));
}

// A trace is a balance, not a row, so a bag with no free slot still has room
// for any number of them.
TEST_F(TradeExchangeTest, TracesNeedNoSlot) {
  CharacterInstance character = MakeCharacter();
  FillEtcTab(character);
  TradeOffer received;
  received.set_spell_traces(1000000);
  EXPECT_TRUE(HasRoomForTrade(character, items_, TradeOffer(), received));
}

TEST_F(TradeExchangeTest, AnItemThisBuildCannotNameIsRefused) {
  CharacterInstance character = MakeCharacter();
  TradeOffer received;
  received.add_stacks()->set_name("Something Else");
  received.mutable_stacks(0)->set_count(1);
  EXPECT_FALSE(HasRoomForTrade(character, items_, TradeOffer(), received));
}

TEST_F(TradeExchangeTest, TheExchangeTakesAndGives) {
  CharacterInstance character = MakeCharacter();
  character.AddMeso(10000);
  character.AddItem(Scroll(), 20);
  character.AddItem(Trace(), 50);
  Equip starred;
  starred.set_equip_name("Sword");
  starred.set_stars(4);
  character.PickUp(std::make_unique<EquipInstance>(Sword(), starred));
  character.PickUp(std::make_unique<EquipInstance>(Sword()));

  TradeOffer given;
  given.set_meso(2500);
  given.set_spell_traces(30);
  given.add_stacks()->set_name("Chaos Scroll");
  given.mutable_stacks(0)->set_count(5);
  *given.add_equips() = starred;

  TradeOffer received;
  received.set_meso(400);
  received.set_spell_traces(7);
  Equip theirs;
  theirs.set_equip_name("Sword");
  theirs.set_stars(9);
  *received.add_equips() = theirs;

  ApplyTrade(character, equips_, items_, {0}, given, received);

  EXPECT_EQ(character.meso(), 10000 - 2500 + 400);
  EXPECT_EQ(character.CountItem(kSpellTraceName), 50 - 30 + 7);
  EXPECT_EQ(character.CountItem("Chaos Scroll"), 15);
  // The starred item left and theirs arrived whole; the plain one stayed.
  ASSERT_EQ(character.inventory().size(), 2);
  EXPECT_EQ(character.inventory()[0].stars(), 0);
  EXPECT_EQ(character.inventory()[1].stars(), 9);
}

TEST_F(TradeExchangeTest, RowsComeOutBackToFront) {
  CharacterInstance character = MakeCharacter();
  for (int stars = 0; stars < 4; ++stars) {
    Equip state;
    state.set_equip_name("Sword");
    state.set_stars(stars);
    character.PickUp(std::make_unique<EquipInstance>(Sword(), state));
  }

  TradeOffer given;
  *given.add_equips() = character.inventory()[0].SavedState();
  *given.add_equips() = character.inventory()[2].SavedState();
  ApplyTrade(character, equips_, items_, {0, 2}, given, TradeOffer());

  // The named rows are the ones that left, whatever order they were given in.
  ASSERT_EQ(character.inventory().size(), 2);
  EXPECT_EQ(character.inventory()[0].stars(), 1);
  EXPECT_EQ(character.inventory()[1].stars(), 3);
}

TEST_F(TradeExchangeTest, ATraceCrossesAsTheItemItIs) {
  CharacterInstance character = MakeCharacter();
  Equip trace;
  trace.set_equip_name("Sword");
  trace.set_trace(true);
  trace.set_stars(12);

  TradeOffer received;
  *received.add_equips() = trace;
  ApplyTrade(character, equips_, items_, {}, TradeOffer(), received);

  ASSERT_EQ(character.inventory().size(), 1);
  EXPECT_TRUE(character.inventory()[0].is_trace());
  EXPECT_EQ(character.inventory()[0].stars(), 12);
}

}  // namespace
}  // namespace ms
