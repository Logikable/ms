#include "src/frontend/screens/flame_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/widgets/colors.h"
#include "src/item/equip_instance.h"
#include "src/item/flame.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

const int64_t kBurningCost = FlameOf(FlameType::kBurning).cost;
const int64_t kBlackCost = FlameOf(FlameType::kBlack).cost;

class FlamePanelTest : public PanelTest {
 protected:
  // A sword with one line, enough to test the window.
  EquipInstance Flamed() {
    Equip state;
    FlameLine* line = state.add_flame();
    line->set_stat(FLAME_STAT_STR_DEX);
    line->set_tier(6);
    return EquipInstance(sword_, state);
  }

  FlameLines OneLine(FlameStat stat, int tier) {
    FlameLines lines;
    FlameLine* line = lines.Add();
    line->set_stat(stat);
    line->set_tier(tier);
    return lines;
  }
};

TEST_F(FlamePanelTest, TheShelfListsBothFlamesAndTheirPrices) {
  EquipInstance item = Flamed();
  FlamePanel panel;
  panel.Reset();
  panel.SetItem(&item, kBurningCost);
  std::string rendered = RenderElement(panel.Render(true));
  EXPECT_NE(rendered.find("Flame Selection"), std::string::npos);
  EXPECT_LT(rendered.find("Burning Rebirth Flame"),
            rendered.find("Black Rebirth Flame"));
  EXPECT_NE(rendered.find("5,000,000"), std::string::npos);
  EXPECT_NE(rendered.find("15,000,000"), std::string::npos);
  EXPECT_EQ(panel.selected_flame(), FlameType::kBurning);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_flame(), FlameType::kBlack);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_flame(), FlameType::kBurning) << "it wraps";
}

// Burning replaces: one window with the item's own lines, and Confirm rerolls
// without closing it.
TEST_F(FlamePanelTest, BurningAsksOverTheItemsOwnLines) {
  EquipInstance blank(sword_);
  FlamePanel panel;
  panel.Reset();
  panel.SetItem(&blank, kBurningCost);
  panel.OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(panel.IsConfirming());
  std::string rendered = RenderElement(panel.RenderConfirm());
  EXPECT_NE(rendered.find("Grant a flame?"), std::string::npos);
  EXPECT_EQ(rendered.find("After"), std::string::npos);

  EquipInstance item = Flamed();
  panel.SetItem(&item, kBurningCost);
  rendered = RenderElement(panel.RenderConfirm());
  EXPECT_NE(rendered.find("Reroll these lines?"), std::string::npos);
  // A level 10 item's pairs are 1 a tier. The value column is as wide as the
  // pool's widest value, so only the order is fixed.
  const size_t name = rendered.find("STR & DEX");
  ASSERT_NE(name, std::string::npos) << rendered;
  EXPECT_LT(name, rendered.find("+6"));
  EXPECT_LT(rendered.find("+6"), rendered.find("T6"));
  EXPECT_EQ(LabelColor(panel.RenderConfirm(), "+6"), kTeal);

  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kReroll);
  EXPECT_TRUE(panel.IsConfirming());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), RerollAction::kClosed);
  EXPECT_FALSE(panel.IsConfirming());
}

// Black chooses: After waits under Before, the Keep row opens only once there
// is something to keep, and Keep After hands it back.
TEST_F(FlamePanelTest, BlackOffersAfterAndKeepsEitherSide) {
  EquipInstance item = Flamed();
  FlamePanel panel;
  panel.Reset();
  panel.MoveCursor(1);
  panel.SetItem(&item, 2 * kBlackCost);
  panel.OnEvent(ftxui::Event::Return);
  std::string rendered = RenderElement(panel.RenderConfirm());
  EXPECT_LT(rendered.find("Before"), rendered.find("After"));

  panel.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kReroll)
      << "with nothing to keep, Up stays on Confirm";

  panel.SetAfter(OneLine(FLAME_STAT_BOSS_DAMAGE, 7));
  panel.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kKeepBefore);
  EXPECT_FALSE(panel.after().has_value()) << "keeping Before drops After";

  panel.SetAfter(OneLine(FLAME_STAT_BOSS_DAMAGE, 7));
  panel.OnEvent(ftxui::Event::ArrowUp);
  panel.OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kKeepAfter);
  FlameLines kept = panel.TakeAfter();
  ASSERT_EQ(kept.size(), 1);
  EXPECT_EQ(kept[0].stat(), FLAME_STAT_BOSS_DAMAGE);
  EXPECT_FALSE(panel.after().has_value());
}

TEST_F(FlamePanelTest, AShortPurseGreysConfirmAndRedsThePrice) {
  EquipInstance item = Flamed();
  FlamePanel panel;
  panel.Reset();
  panel.SetItem(&item, kBurningCost - 1);
  panel.OnEvent(ftxui::Event::Return);
  ASSERT_TRUE(panel.IsConfirming());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), RerollAction::kClosed)
      << "the cursor opens on Cancel";
  EXPECT_EQ(LabelColor(panel.Render(true), "5,000,000"), kRed);
}

}  // namespace
}  // namespace ms
