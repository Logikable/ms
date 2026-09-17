#include "src/frontend/screens/buff_info_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/character/consumables.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

class BuffInfoPanelTest : public PanelTest {
 protected:
  std::string RenderBuff(ConsumableType type, bool owned = false) {
    BuffInfoPanel panel;
    panel.SetBuff(type, owned);
    return RenderElement(panel.Render());
  }

  // The rows and columns the card asks for, read off its requirement: the
  // test screen is bigger than any card, so a rendered string cannot say.
  static ftxui::Requirement SizeOf(ConsumableType type, bool owned = false) {
    BuffInfoPanel panel;
    panel.SetBuff(type, owned);
    ftxui::Element card = panel.Render();
    card->ComputeRequirement();
    return card->requirement();
  }
};

TEST_F(BuffInfoPanelTest, ShowsTheNameTheEffectsAndBothPrices) {
  std::string rendered = RenderBuff(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION);
  EXPECT_NE(rendered.find("Wealth Acquisition Potion"), std::string::npos);
  EXPECT_NE(rendered.find("+20% Meso Obtained"), std::string::npos);
  EXPECT_NE(rendered.find("Farming only"), std::string::npos);
  EXPECT_NE(rendered.find("1,000 per second while farming"), std::string::npos);
  EXPECT_NE(rendered.find("100,000,000 to unlock permanently"),
            std::string::npos);
}

// The boss buff is charged by the entry rather than by the second.
TEST_F(BuffInfoPanelTest, ABossBuffIsPricedPerEntry) {
  std::string rendered = RenderBuff(CONSUMABLE_TYPE_EXTREME_GREEN_POTION);
  EXPECT_NE(rendered.find("1,000,000 per boss entry"), std::string::npos);
  EXPECT_NE(rendered.find("+1 Attack Speed"), std::string::npos);
}

// The totem's whole worth is the one line about the beat it plants.
TEST_F(BuffInfoPanelTest, TheTotemStatesTheBeatItPlants) {
  std::string rendered = RenderBuff(CONSUMABLE_TYPE_WILD_TOTEM);
  EXPECT_NE(rendered.find("Wild Totem"), std::string::npos);
  EXPECT_NE(rendered.find("Halves respawn time to 3.78s"), std::string::npos);
  EXPECT_NE(rendered.find("1,000,000,000 to unlock permanently"),
            std::string::npos);
}

// Bought outright, the price row says so instead of quoting a price again.
TEST_F(BuffInfoPanelTest, AnOwnedBuffHasNothingLeftToBuy) {
  std::string rendered =
      RenderBuff(CONSUMABLE_TYPE_WEALTH_ACQUISITION_POTION, /*owned=*/true);
  EXPECT_NE(rendered.find("Unlocked permanently"), std::string::npos);
  EXPECT_EQ(rendered.find("to unlock permanently"), std::string::npos);
}

// Every card is the same width, whoever owns the buff, and no taller than the
// buff needs: the two borders, the name and its rule, the second rule and the
// two price rows, and one row for each effect.
TEST_F(BuffInfoPanelTest, EveryCardIsOneWidthAndAsTallAsItsBuff) {
  for (const ConsumableInfo& info : AllConsumables()) {
    for (bool owned : {false, true}) {
      ftxui::Requirement card = SizeOf(info.type, owned);
      EXPECT_EQ(card.min_x, BuffInfoPanel::Columns()) << info.name;
      EXPECT_EQ(card.min_y, 7 + static_cast<int>(info.effects.size()))
          << info.name;
    }
  }
}

TEST_F(BuffInfoPanelTest, AnUnknownBuffRendersAPlaceholder) {
  EXPECT_NE(RenderBuff(CONSUMABLE_TYPE_UNSPECIFIED).find("no buff"),
            std::string::npos);
}

}  // namespace
}  // namespace ms
