#include "src/frontend/screens/soul_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "src/frontend/testing/panel_test_base.h"
#include "src/item/currency.h"
#include "src/item/equip_instance.h"
#include "src/item/soul.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

class SoulPanelTest : public PanelTest {
 protected:
  static ItemPrototype Shard(const std::string& boss, SoulTier tier,
                             int level) {
    ItemPrototype shard;
    shard.set_name(boss + "'s Soul Shard");
    shard.set_short_name(boss);
    shard.set_kind(ITEM_KIND_SOUL_SHARD);
    shard.set_soul_tier(tier);
    shard.set_currency_level(level);
    return shard;
  }

  EquipInstance Souled() {
    Equip state;
    Soul* soul = state.mutable_soul();
    soul->set_boss("Magnus");
    soul->set_tier(SOUL_TIER_SS);
    soul->set_line(SOUL_LINE_BOSS_DAMAGE);
    return EquipInstance(sword_, state);
  }
};

// Best tier first, only bosses with a soul's worth, and Quantity counts souls.
TEST_F(SoulPanelTest, TheListShowsTheSoulsThePurseMakes) {
  CurrencyPurse purse;
  purse.Add(Shard("Zakum", SOUL_TIER_C, 110), 25);
  purse.Add(Shard("Lucid", SOUL_TIER_SS, 230), 10);
  purse.Add(Shard("Hilla", SOUL_TIER_A, 120), 9);
  purse.Add(Shard("Will", SOUL_TIER_SS, 235), 99999);
  EquipInstance item(sword_);
  SoulPanel panel;
  panel.Reset();
  panel.SetItem(&item, purse);
  const std::string rendered = RenderElement(panel.Render(true));
  EXPECT_NE(rendered.find("Soul Selection"), std::string::npos);
  EXPECT_LT(rendered.find("Soul"), rendered.find("Tier"));
  EXPECT_LT(rendered.find("Tier"), rendered.find("Quantity"));
  EXPECT_LT(rendered.find("Will"), rendered.find("Lucid"));
  EXPECT_LT(rendered.find("Lucid"), rendered.find("Zakum"));
  EXPECT_EQ(rendered.find("Hilla"), std::string::npos) << "9 shards make none";
  EXPECT_NE(rendered.find("999"), std::string::npos) << "the cap";
  EXPECT_EQ(rendered.find("9,999"), std::string::npos);
  EXPECT_NE(rendered.find("SS"), std::string::npos);
  EXPECT_EQ(panel.selected_shard()->short_name(), "Will");
  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected_shard()->short_name(), "Zakum") << "it wraps";
}

TEST_F(SoulPanelTest, AnEmptyListSaysHowToFillIt) {
  CurrencyPurse purse;
  purse.Add(Shard("Zakum", SOUL_TIER_C, 110), 9);
  EquipInstance item(sword_);
  SoulPanel panel;
  panel.Reset();
  panel.SetItem(&item, purse);
  const std::string rendered = RenderElement(panel.Render(true));
  EXPECT_NE(rendered.find("Collect 10 of any Soul Shard."), std::string::npos);
  EXPECT_EQ(rendered.find("Quantity"), std::string::npos);
  EXPECT_EQ(panel.selected_shard(), nullptr);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kNone);
  EXPECT_FALSE(panel.IsConfirming());
}

// A bare weapon is asked only the question; a souled one also hears what it
// would lose. Confirm keeps the window open until the shards run out.
TEST_F(SoulPanelTest, TheQuestionNamesTheSoulItWouldReplace) {
  CurrencyPurse purse;
  purse.Add(Shard("Zakum", SOUL_TIER_C, 110), 20);
  EquipInstance bare(sword_);
  SoulPanel panel;
  panel.Reset();
  panel.SetItem(&bare, purse);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kNone);
  ASSERT_TRUE(panel.IsConfirming());
  std::string rendered = RenderElement(panel.RenderConfirm());
  EXPECT_NE(rendered.find("Apply Zakum's Soul?"), std::string::npos);
  EXPECT_EQ(rendered.find("override"), std::string::npos);

  EquipInstance souled = Souled();
  panel.SetItem(&souled, purse);
  rendered = RenderElement(panel.RenderConfirm());
  EXPECT_NE(rendered.find("This will override"), std::string::npos);
  EXPECT_NE(rendered.find("your current Soul:"), std::string::npos);
  EXPECT_NE(rendered.find("Soul: Magnus"), std::string::npos);
  EXPECT_NE(rendered.find("Boss Damage +7%, ATT +20"), std::string::npos);
  EXPECT_LT(rendered.find("Apply"), rendered.find("override"));
  EXPECT_LT(rendered.find("Boss Damage"), rendered.find("Confirm"));

  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kReroll);
  EXPECT_TRUE(panel.IsConfirming()) << "Confirm leaves it open";
  // The caller spent the shards: the row is gone, but the question stays on
  // Zakum with Confirm greyed.
  ASSERT_TRUE(purse.Spend("Zakum's Soul Shard", 15));
  panel.SetItem(&souled, purse);
  EXPECT_EQ(panel.selected_shard()->short_name(), "Zakum");
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kClosed)
      << "the cursor moved to Cancel";
  EXPECT_FALSE(panel.IsConfirming());
  EXPECT_EQ(panel.selected_shard(), nullptr);
}

}  // namespace
}  // namespace ms
