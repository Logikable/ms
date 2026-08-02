#include "src/frontend/screens/shop_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <random>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/character.h"
#include "src/frontend/widgets/colors.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

EquipPrototype MakeItem(const std::string& name, int level, int price) {
  EquipPrototype e;
  e.set_name(name);
  e.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  e.set_required_level(level);
  e.set_shop_price(price);
  return e;
}

class ShopPanelTest : public testing::Test {
 protected:
  std::string Render(const ShopPanel& panel) {
    ftxui::Element element = panel.Render();
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fit(element), ftxui::Dimension::Fit(element));
    ftxui::Render(screen, element);
    return screen.ToString();
  }

  // Whether any cell on the row holding `needle` is painted `color`. Reads the
  // screen's pixels rather than its escape codes: ftxui collapses colours into
  // whatever palette it believes the terminal has, and a test process has no
  // terminal, so the escape codes describe the fallback rather than the colour.
  bool RowIsColored(const ShopPanel& panel, const std::string& needle,
                    ftxui::Color color) {
    ftxui::Element element = panel.Render();
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fit(element), ftxui::Dimension::Fit(element));
    ftxui::Render(screen, element);
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string row;
      for (int x = 0; x < screen.dimx(); ++x) {
        // Unpainted cells hold an empty string, not a space; dropping them
        // would join text that is not actually adjacent.
        const std::string& ch = screen.PixelAt(x, y).character;
        row += ch.empty() ? " " : ch;
      }
      if (row.find(needle) == std::string::npos) {
        continue;
      }
      for (int x = 0; x < screen.dimx(); ++x) {
        if (screen.PixelAt(x, y).foreground_color == color) {
          return true;
        }
      }
      return false;
    }
    return false;
  }

  CharacterInstance MakeCharacter(int64_t meso) {
    Character proto;
    proto.set_level(1);
    CharacterInstance c(rng_, std::move(proto));
    c.AddMeso(meso);
    return c;
  }

  std::mt19937 rng_{0};
  std::map<std::string, EquipPrototype> equips_{
      {"long_sword", MakeItem("Long Sword", 10, 5000)},
      {"machete", MakeItem("Machete", 20, 10000)},
      {"gladius", MakeItem("Gladius", 30, 20000)},
      {"heirloom", MakeItem("Heirloom", 10, 0)},
  };
};

TEST_F(ShopPanelTest, ListsTheStockWithNamesAndCosts) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Long Sword"), std::string::npos);
  EXPECT_NE(rendered.find("5,000"), std::string::npos);
  EXPECT_NE(rendered.find("10,000"), std::string::npos);
  // Priced at zero, so not stocked -- the panel shows what ShopStock says.
  EXPECT_EQ(rendered.find("Heirloom"), std::string::npos);
}

TEST_F(ShopPanelTest, ShowsTheEquipsTabAndTheTitle) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Shop"), std::string::npos);
  EXPECT_NE(rendered.find("Equips"), std::string::npos);
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Cost"), std::string::npos);
}

// The player's balance sits with the prices, so the column has something to be
// read against.
TEST_F(ShopPanelTest, ShowsTheBalance) {
  CharacterInstance c = MakeCharacter(34567);
  ShopPanel panel(c, equips_);
  EXPECT_NE(Render(panel).find("34,567"), std::string::npos);
}

TEST_F(ShopPanelTest, OpensOnTheFirstItem) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  ASSERT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_item()->name(), "Long Sword");
  EXPECT_NE(Render(panel).find("> Long Sword"), std::string::npos);
}

TEST_F(ShopPanelTest, WalksTheListAndStopsAtTheEnds) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  panel.OnEvent(ftxui::Event::ArrowUp);  // already at the top
  EXPECT_EQ(panel.selected_item()->name(), "Long Sword");
  panel.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.selected_item()->name(), "Machete");
  panel.OnEvent(ftxui::Event::ArrowDown);
  panel.OnEvent(ftxui::Event::ArrowDown);  // already at the bottom
  EXPECT_EQ(panel.selected_item()->name(), "Gladius");
}

TEST_F(ShopPanelTest, ResetReturnsToTheTop) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  panel.OnEvent(ftxui::Event::ArrowDown);
  panel.Reset();
  EXPECT_EQ(panel.selected_item()->name(), "Long Sword");
}

// The list answers "what can I buy" without the player doing arithmetic on
// every row.
TEST_F(ShopPanelTest, RedsOutPricesBeyondTheBalance) {
  CharacterInstance c = MakeCharacter(7000);
  ShopPanel panel(c, equips_);
  EXPECT_FALSE(RowIsColored(panel, "Long Sword", kRed))
      << "5,000 is affordable on 7,000";
  EXPECT_TRUE(RowIsColored(panel, "Gladius", kRed)) << "20,000 is not";
}

TEST_F(ShopPanelTest, AnEmptyShopSaysSo) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> nothing;
  ShopPanel panel(c, nothing);
  EXPECT_EQ(panel.selected_item(), nullptr);
  EXPECT_NE(Render(panel).find("nothing for sale"), std::string::npos);
}

}  // namespace
}  // namespace ms
