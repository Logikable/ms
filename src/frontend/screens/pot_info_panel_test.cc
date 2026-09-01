#include "src/frontend/screens/pot_info_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/character/consumables.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

class PotInfoPanelTest : public PanelTest {
 protected:
  std::string RenderPot(ConsumableType type, bool owned = false) {
    PotInfoPanel panel;
    panel.SetPot(type, owned);
    return RenderElement(panel.Render());
  }

  // The rows and columns the card asks for, read off its requirement: the
  // test screen is bigger than any card, so a rendered string cannot say.
  static ftxui::Requirement SizeOf(ConsumableType type, bool owned = false) {
    PotInfoPanel panel;
    panel.SetPot(type, owned);
    ftxui::Element card = panel.Render();
    card->ComputeRequirement();
    return card->requirement();
  }
};

TEST_F(PotInfoPanelTest, ShowsTheNameTheEffectsAndBothPrices) {
  std::string rendered = RenderPot(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  EXPECT_NE(rendered.find("Wealth Acquisition Potion"), std::string::npos);
  EXPECT_NE(rendered.find("+20% Meso Obtained"), std::string::npos);
  EXPECT_NE(rendered.find("Farming only"), std::string::npos);
  EXPECT_NE(rendered.find("1,000 per second while farming"), std::string::npos);
  EXPECT_NE(rendered.find("100,000,000 to unlock permanently"),
            std::string::npos);
}

// The boss pot is charged by the entry rather than by the second.
TEST_F(PotInfoPanelTest, ABossPotIsPricedPerEntry) {
  std::string rendered = RenderPot(CONSUMABLE_TYPE_EXTREME_GREEN_POTION);
  EXPECT_NE(rendered.find("1,000,000 per boss entry"), std::string::npos);
  EXPECT_NE(rendered.find("+1 Attack Speed"), std::string::npos);
}

// Bought outright, the price row says so instead of quoting a price again.
TEST_F(PotInfoPanelTest, AnOwnedPotHasNothingLeftToBuy) {
  std::string rendered =
      RenderPot(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION, /*owned=*/true);
  EXPECT_NE(rendered.find("Unlocked permanently"), std::string::npos);
  EXPECT_EQ(rendered.find("to unlock permanently"), std::string::npos);
}

// Every card is the same shape, whatever the pot and whoever owns it.
TEST_F(PotInfoPanelTest, EveryCardIsTheSameSize) {
  ftxui::Requirement first = SizeOf(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  for (const ConsumableInfo& info : AllConsumables()) {
    for (bool owned : {false, true}) {
      ftxui::Requirement other = SizeOf(info.type, owned);
      EXPECT_EQ(other.min_x, first.min_x) << info.name;
      EXPECT_EQ(other.min_y, first.min_y) << info.name;
    }
  }
  EXPECT_EQ(first.min_x, PotInfoPanel::Columns());
}

TEST_F(PotInfoPanelTest, AnUnknownPotRendersAPlaceholder) {
  EXPECT_NE(RenderPot(CONSUMABLE_TYPE_UNSPECIFIED).find("no pot"),
            std::string::npos);
}

}  // namespace
}  // namespace ms
