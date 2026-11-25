#include "analysis/drop_use.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/combat/boss/boss_run.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

EquipPrototype Piece(const std::string& name, EquipSlot slot) {
  EquipPrototype piece;
  piece.set_name(name);
  piece.set_equip_slot(slot);
  piece.set_required_level(150);
  piece.add_equip_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  return piece;
}

// A boss dropping a belt and a coin that buys a hat.
std::unique_ptr<GameState> BeltAndCoin() {
  std::map<std::string, EquipPrototype> equips;
  equips["belt"] = Piece("Belt", EQUIP_SLOT_BELT);
  EquipPrototype hat = Piece("Hat", EQUIP_SLOT_HAT);
  TokenPrice* price = hat.add_token_prices();
  price->set_token_item("coin");
  price->set_count(2);
  equips["hat"] = hat;
  ItemPrototype coin;
  coin.set_name("Coin");
  auto state = std::make_unique<GameState>(
      std::move(equips), std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{{"coin", coin}},
      std::map<std::string, Mob>{{"snail", SnailMob()}},
      std::map<std::string, MapData>{{"field", SnailMap()}});
  state->current_map = "field";
  Character hero;
  hero.set_level(200);
  hero.set_job(JOB_HERO);
  state->character.RestoreFrom(hero, state->equips, state->items);
  Boss boss;
  boss.set_name("Boss");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->add_drops()->set_equip("belt");
  normal->add_drops()->set_item("coin");
  state->bosses["boss"] = boss;
  return state;
}

BossReward Paid(int belts, int coins, double drop) {
  BossReward reward;
  if (belts > 0) {
    reward.items.push_back({"Belt", belts, true, 0.2});
  }
  if (coins > 0) {
    reward.items.push_back({"Coin", coins, true, 1.0});
  }
  reward.item_drop_pct = drop;
  return reward;
}

const DropUseResult* Find(const std::vector<DropUseResult>& results,
                          const std::string& name) {
  for (const DropUseResult& result : results) {
    if (result.name == name) {
      return &result;
    }
  }
  return nullptr;
}

// The window closes at the last look anything the drop is for was worn: the
// belt counts the copy worn and the held copy a boom used, the coin what left
// the bag, and what dropped after neither was worn is left out.
TEST(DropUseTest, CountsWhatDroppedAndWhatWasUsedWhileItMattered) {
  std::unique_ptr<GameState> owner = BeltAndCoin();
  GameState& state = *owner;
  DropUse use(state);
  use.Clear("boss", "Normal", Paid(2, 4, 1.5), 1.0);
  state.character.AddItem(state.items.at("coin"), 2);  // two coins spent
  for (const char* key : {"belt", "hat"}) {
    state.character.PickUp(
        std::make_unique<EquipInstance>(state.equips.at(key)));
    ASSERT_TRUE(state.character.Equip(state.character.inventory().size() - 1,
                                      AutoswapSlotFor(Activity::kBossing)));
  }
  const std::map<std::string, int> spares = {{"Belt", 1}};
  use.Clear("boss", "Normal", Paid(1, 0, 2.5), 2.0);
  use.Look(state, spares, 2.0);
  state.character.Unequip(EQUIP_SLOT_BELT, AutoswapSlotFor(Activity::kBossing));
  state.character.Unequip(EQUIP_SLOT_HAT, AutoswapSlotFor(Activity::kBossing));
  use.Clear("boss", "Normal", Paid(5, 6, 4.0), 3.0);
  use.Look(state, spares, 3.0);

  std::vector<DropUseResult> results = use.Finish();
  const DropUseResult* belt = Find(results, "Belt");
  ASSERT_NE(belt, nullptr);
  EXPECT_TRUE(belt->equipment);
  EXPECT_TRUE(belt->worn);
  EXPECT_EQ(belt->dropped, 3);
  EXPECT_EQ(belt->first_day, 1.0);
  EXPECT_EQ(belt->used, 2);
  EXPECT_EQ(belt->start_day, 1.0);
  EXPECT_EQ(belt->end_day, 2.0);
  EXPECT_EQ(belt->low_drop, 1.5);
  EXPECT_EQ(belt->high_drop, 2.5);
  const DropUseResult* coin = Find(results, "Coin");
  ASSERT_NE(coin, nullptr);
  EXPECT_FALSE(coin->equipment);
  EXPECT_EQ(coin->dropped, 4);
  EXPECT_EQ(coin->first_day, 1.0);
  EXPECT_EQ(coin->used, 2);
}

// A drop the boss preset never wore is its first clear alone: nothing it paid
// was needed.
TEST(DropUseTest, ADropNeverWornIsItsFirstClear) {
  std::unique_ptr<GameState> owner = BeltAndCoin();
  DropUse use(*owner);
  use.Clear("boss", "Normal", Paid(1, 0, 1.0), 4.0);
  use.Clear("boss", "Normal", Paid(2, 0, 2.0), 5.0);
  std::vector<DropUseResult> results = use.Finish();
  const DropUseResult* belt = Find(results, "Belt");
  ASSERT_NE(belt, nullptr);
  EXPECT_FALSE(belt->worn);
  EXPECT_EQ(belt->start_day, 4.0);
  EXPECT_EQ(belt->end_day, 4.0);
  EXPECT_EQ(belt->dropped, 1);
  EXPECT_EQ(belt->first_day, 4.0);
  EXPECT_EQ(belt->high_drop, 1.0);
  EXPECT_EQ(belt->used, 0);
}

// The first copy can fall after the window opens: the clear that opened it may
// pay none.
TEST(DropUseTest, TheFirstCopyMayComeLater) {
  std::unique_ptr<GameState> owner = BeltAndCoin();
  GameState& state = *owner;
  DropUse use(state);
  use.Clear("boss", "Normal", Paid(0, 0, 1.0), 2.0);
  use.Clear("boss", "Normal", Paid(1, 0, 1.0), 5.0);
  state.character.PickUp(
      std::make_unique<EquipInstance>(state.equips.at("belt")));
  ASSERT_TRUE(state.character.Equip(state.character.inventory().size() - 1,
                                    AutoswapSlotFor(Activity::kBossing)));
  use.Look(state, {}, 5.0);
  const std::vector<DropUseResult> results = use.Finish();
  const DropUseResult* belt = Find(results, "Belt");
  ASSERT_NE(belt, nullptr);
  EXPECT_EQ(belt->start_day, 2.0);
  EXPECT_EQ(belt->first_day, 5.0);
  EXPECT_EQ(belt->longest_drought, 3.0);
}

// A piece a dropped scroll fits waits from the look that first saw it worn to
// the one that saw its last slot filled.
TEST(DropUseTest, TimesTheWaitForEveryUpgradeSlot) {
  std::unique_ptr<GameState> owner = BeltAndCoin();
  GameState& state = *owner;
  Scroll scroll;
  scroll.set_name("Coin Scroll");
  scroll.set_target(TargetForSlot(EQUIP_SLOT_BELT));
  scroll.add_applicable_job_categories(EQUIP_JOB_CATEGORY_UNIVERSAL);
  scroll.mutable_stats()->set_attack(1);
  scroll.set_paid_with("coin");
  state.scrolls["coin_scroll"] = scroll;
  DropUse use(state);
  const StatPreset boss = AutoswapSlotFor(Activity::kBossing);
  EquipPrototype belt = state.equips.at("belt");
  belt.set_upgrade_slots(2);
  state.character.PickUp(std::make_unique<EquipInstance>(belt));
  ASSERT_TRUE(
      state.character.Equip(state.character.inventory().size() - 1, boss));
  use.Look(state, {}, 2.0);
  int filled = 0;
  while (state.character.WornAt(boss, EQUIP_SLOT_BELT)
             ->equip_state()
             .remaining_upgrade_slots() > 0) {
    use.Look(state, {}, 3.0);
    state.character.ScrollEquipped(EQUIP_SLOT_BELT, scroll, boss);
    ASSERT_LT(++filled, 100);
  }
  ASSERT_GT(filled, 0) << "the belt needs an open slot to wait on";
  use.Look(state, {}, 5.0);
  use.Look(state, {}, 6.0);
  const std::vector<ScrollWait> waits = use.ScrollWaits();
  ASSERT_EQ(waits.size(), 1);
  EXPECT_EQ(waits[0].scroll, "Coin");
  EXPECT_EQ(waits[0].piece, "Belt");
  EXPECT_EQ(waits[0].start_day, 2.0);
  EXPECT_EQ(waits[0].full_day, 5.0);
}

// Over the whole run, whatever matters: what dropped, and what is still held
// unworn at the end.
TEST(DropUseTest, CountsWhatIsLeftAtTheEnd) {
  std::unique_ptr<GameState> state = BeltAndCoin();
  DropUse use(*state);
  use.Clear("boss", "Normal", Paid(2, 5, 1.0), 1.0);
  state->character.AddItem(state->items.at("coin"), 3);  // two spent
  state->character.PickUp(
      std::make_unique<EquipInstance>(state->equips.at("belt")));
  state->character.PickUp(
      std::make_unique<EquipInstance>(state->equips.at("belt")));
  ASSERT_TRUE(state->character.Equip(0));  // one worn, one spare
  std::map<std::string, DropLeftover> by_name;
  for (const DropLeftover& leftover : use.Leftovers(*state)) {
    by_name[leftover.name] = leftover;
  }
  ASSERT_EQ(by_name.count("Belt"), 1);
  EXPECT_TRUE(by_name["Belt"].equipment);
  EXPECT_EQ(by_name["Belt"].dropped, 2);
  EXPECT_EQ(by_name["Belt"].left, 1);
  ASSERT_EQ(by_name.count("Coin"), 1);
  EXPECT_EQ(by_name["Coin"].dropped, 5);
  EXPECT_EQ(by_name["Coin"].left, 3);
}

// Only the difficulty that drops it opens a drop's window.
TEST(DropUseTest, AnotherDifficultyOpensNothing) {
  std::unique_ptr<GameState> owner = BeltAndCoin();
  DropUse use(*owner);
  use.Clear("boss", "Hard", Paid(0, 0, 1.0), 4.0);
  EXPECT_TRUE(use.Finish().empty());
}

// A piece worn only in the drop preset is not in use: drop gear is never
// upgraded, so it would hold the window open for good.
TEST(DropUseTest, OnlyTheBossPresetCounts) {
  std::unique_ptr<GameState> owner = BeltAndCoin();
  GameState& state = *owner;
  DropUse use(state);
  use.Clear("boss", "Normal", Paid(1, 0, 1.0), 1.0);
  state.character.PickUp(
      std::make_unique<EquipInstance>(state.equips.at("belt")));
  ASSERT_TRUE(state.character.Equip(state.character.inventory().size() - 1,
                                    kDropPreset));
  use.Look(state, {}, 2.0);
  std::vector<DropUseResult> results = use.Finish();
  const DropUseResult* belt = Find(results, "Belt");
  ASSERT_NE(belt, nullptr);
  EXPECT_FALSE(belt->worn);
  EXPECT_EQ(belt->end_day, 1.0);
  EXPECT_EQ(belt->used, 0);
}

}  // namespace
}  // namespace ms
