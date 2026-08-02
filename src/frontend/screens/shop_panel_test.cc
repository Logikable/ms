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

EquipPrototype MakeItem(const std::string& name, int level, int price,
                        EquipJobCategory job = EQUIP_JOB_CATEGORY_UNIVERSAL,
                        EquipSlot slot = EQUIP_SLOT_PRIMARY_WEAPON) {
  EquipPrototype e;
  e.set_name(name);
  e.set_equip_slot(slot);
  e.set_required_level(level);
  e.set_shop_price(price);
  e.add_equip_job_categories(job);
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

  // The index of the first rendered row holding `needle`, or -1.
  int RowIndexWith(const ShopPanel& panel, const std::string& needle) {
    std::string rendered = Render(panel);
    int row = 0;
    size_t start = 0;
    while (start <= rendered.size()) {
      size_t end = rendered.find('\n', start);
      if (end == std::string::npos) {
        end = rendered.size();
      }
      if (rendered.substr(start, end - start).find(needle) !=
          std::string::npos) {
        return row;
      }
      start = end + 1;
      row++;
    }
    return -1;
  }

  int RenderHeight(const ShopPanel& panel) {
    ftxui::Element element = panel.Render();
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fit(element), ftxui::Dimension::Fit(element));
    return screen.dimy();
  }

  // The colour of the cell holding `cell`, on the row holding `row_needle`.
  //
  // Reads the screen's pixels rather than its escape codes: ftxui collapses
  // colours into whatever palette it believes the terminal has, and a test
  // process has no terminal, so the escape codes describe the fallback rather
  // than the colour.
  //
  // Per cell rather than per row, because a row now carries three things that
  // redden for three different reasons -- asking whether anything on the row
  // is red could not tell an unaffordable price from a level too high.
  ftxui::Color CellColor(const ShopPanel& panel, const std::string& row_needle,
                         const std::string& cell) {
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
        if (ch.empty()) {
          row += " ";
        } else {
          row += ch;
        }
      }
      if (row.find(row_needle) == std::string::npos) {
        continue;
      }
      size_t at = row.find(cell);
      EXPECT_NE(at, std::string::npos)
          << "'" << cell << "' is not on the '" << row_needle << "' row";
      if (at == std::string::npos) {
        return ftxui::Color::Default;
      }
      // `at` counts glyphs, and every glyph before a cell on these rows is one
      // column wide, so it is also the column.
      return screen.PixelAt(static_cast<int>(at), y).foreground_color;
    }
    ADD_FAILURE() << "no row holding '" << row_needle << "'";
    return ftxui::Color::Default;
  }

  CharacterInstance MakeCharacter(int64_t meso, int level = 1,
                                  Job job = JOB_SWORDMAN) {
    Character proto;
    proto.set_level(level);
    proto.set_job(job);
    CharacterInstance c(rng_, std::move(proto));
    c.AddMeso(meso);
    return c;
  }

  std::mt19937 rng_{0};
  std::map<std::string, EquipPrototype> equips_{
      {"long_sword", MakeItem("Long Sword", 10, 5000)},
      {"machete", MakeItem("Machete", 20, 10000, EQUIP_JOB_CATEGORY_WARRIOR)},
      {"gladius", MakeItem("Gladius", 30, 20000, EQUIP_JOB_CATEGORY_WARRIOR)},
      {"subi", MakeItem("Subi Throwing-Stars", 10, 1000,
                        EQUIP_JOB_CATEGORY_THIEF, EQUIP_SLOT_STARS)},
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
  panel.OnEvent(ftxui::Event::ArrowDown);
  panel.OnEvent(ftxui::Event::ArrowDown);  // already at the bottom
  EXPECT_EQ(panel.selected_item()->name(), "Subi Throwing-Stars");
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
  CharacterInstance c = MakeCharacter(7000, /*level=*/99);
  ShopPanel panel(c, equips_);
  EXPECT_NE(CellColor(panel, "Long Sword", "5,000"), kRed)
      << "5,000 is affordable on 7,000";
  EXPECT_EQ(CellColor(panel, "Gladius", "20,000"), kRed) << "20,000 is not";
}

// The three columns the bag's equip tab shows, in the same order and the same
// widths, so an item reads the same way in both lists.
TEST_F(ShopPanelTest, ShowsSlotLevelAndJob) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/99);
  ShopPanel panel(c, equips_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_NE(rendered.find("Level"), std::string::npos);
  EXPECT_NE(rendered.find("Job"), std::string::npos);
  EXPECT_NE(rendered.find("Weapon"), std::string::npos);
  EXPECT_NE(rendered.find("Stars"), std::string::npos);
  EXPECT_NE(rendered.find("Lv20"), std::string::npos);
  EXPECT_NE(rendered.find("Warrior"), std::string::npos);
  EXPECT_NE(rendered.find("All"), std::string::npos);
}

// Red says which requirement is in the way, so the two colour independently:
// a level too high does not also accuse the class, and vice versa.
TEST_F(ShopPanelTest, RedsOutALevelTheCharacterHasNotReached) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/20, JOB_SWORDMAN);
  ShopPanel panel(c, equips_);
  EXPECT_NE(CellColor(panel, "Machete", "Lv20"), kRed) << "level 20 reaches it";
  EXPECT_EQ(CellColor(panel, "Gladius", "Lv30"), kRed) << "level 30 does not";
  EXPECT_NE(CellColor(panel, "Gladius", "Warrior"), kRed)
      << "a swordman is the right class for it; only the level is wrong";
}

TEST_F(ShopPanelTest, RedsOutAJobTheCharacterCannotBe) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/99, JOB_MAGICIAN);
  ShopPanel panel(c, equips_);
  EXPECT_EQ(CellColor(panel, "Machete", "Warrior"), kRed)
      << "a magician cannot hold a warrior weapon";
  EXPECT_NE(CellColor(panel, "Machete", "Lv20"), kRed)
      << "level 99 clears it; only the class is wrong";
  EXPECT_NE(CellColor(panel, "Long Sword", "All"), kRed)
      << "anyone can hold a universal item";
}

TEST_F(ShopPanelTest, LeavesAnEquippableItemUncolored) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/99, JOB_SWORDMAN);
  ShopPanel panel(c, equips_);
  EXPECT_NE(CellColor(panel, "Machete", "Lv20"), kRed);
  EXPECT_NE(CellColor(panel, "Machete", "Warrior"), kRed);
  EXPECT_NE(CellColor(panel, "Machete", "10,000"), kRed);
}

TEST_F(ShopPanelTest, OpensAMenuOverTheSelectedItem) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  EXPECT_FALSE(panel.menu_open());
  panel.OpenMenu();
  EXPECT_TRUE(panel.menu_open());
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Inspect"), std::string::npos);
  EXPECT_NE(rendered.find("Buy"), std::string::npos);
  EXPECT_NE(rendered.find("Close"), std::string::npos);
}

TEST_F(ShopPanelTest, DoesNotDrawTheMenuUntilItIsOpened) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("Inspect"), std::string::npos);
  EXPECT_EQ(rendered.find("Close"), std::string::npos);
}

TEST_F(ShopPanelTest, MenuEntriesGoToTheirScreens) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);

  panel.OpenMenu();
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return), kShopInspect)
      << "the menu opens on Inspect";

  panel.OpenMenu();
  panel.OnMenuEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return), kShopBuy);

  panel.OpenMenu();
  panel.OnMenuEvent(ftxui::Event::ArrowDown);
  panel.OnMenuEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return), kShop);
}

// Whatever the entry, the menu comes down on the way out -- otherwise it would
// still be standing over the list behind the screen it opened.
TEST_F(ShopPanelTest, ChoosingAnEntryClosesTheMenu) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  panel.OpenMenu();
  panel.OnMenuEvent(ftxui::Event::Return);
  EXPECT_FALSE(panel.menu_open());
  EXPECT_EQ(Render(panel).find("Inspect"), std::string::npos);
}

TEST_F(ShopPanelTest, EscapeClosesTheMenuAndStaysInTheShop) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  panel.OpenMenu();
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Escape), kShop);
  EXPECT_FALSE(panel.menu_open());
}

TEST_F(ShopPanelTest, TheMenuStaysUpWhileWalkingIt) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  panel.OpenMenu();
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::ArrowDown), kShopMenu);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::ArrowUp), kShopMenu);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Character('x')), kShopMenu)
      << "the menu is modal over the list";
  EXPECT_TRUE(panel.menu_open());
}

// The menu opens on Inspect every time, rather than wherever it was left.
TEST_F(ShopPanelTest, TheMenuReopensOnItsFirstEntry) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  panel.OpenMenu();
  panel.OnMenuEvent(ftxui::Event::ArrowDown);
  panel.OnMenuEvent(ftxui::Event::Escape);
  panel.OpenMenu();
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return), kShopInspect);
}

// The menu asks for every row above it as well as its own, so anchoring it low
// in the list would make the overlay taller than the window and stretch the
// panel. Opening it must never change how tall the shop is.
TEST_F(ShopPanelTest, TheMenuDoesNotGrowThePanel) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  int closed = RenderHeight(panel);
  for (int i = 0; i < 4; ++i) {
    panel.OpenMenu();
    EXPECT_EQ(RenderHeight(panel), closed)
        << "the menu grew the panel with the cursor on item " << i;
    panel.OnMenuEvent(ftxui::Event::Escape);
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
}

// Fitting inside the window is not enough: the menu has to stop above the
// window's own bottom border rather than draw over it.
TEST_F(ShopPanelTest, TheMenuLeavesTheBottomBorderAlone) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  for (int i = 0; i < 3; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  ASSERT_EQ(panel.selected_item()->name(), "Subi Throwing-Stars");
  panel.OpenMenu();
  int height = RenderHeight(panel);
  // One row below the last entry is the menu's own bottom border, and that has
  // to land above the panel's, which is the last row.
  int last_entry = RowIndexWith(panel, "Close");
  ASSERT_GE(last_entry, 0);
  EXPECT_LT(last_entry + 1, height - 1)
      << "the menu is drawing over the window's bottom border";
}

// Held back only as far as it has to be: away from the foot of the list the
// menu still starts on the row of the item it belongs to.
TEST_F(ShopPanelTest, TheMenuStartsOnItsOwnItemWhenThereIsRoom) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many;
  for (int i = 0; i < 12; ++i) {
    std::string name = "Item " + std::string(1, 'A' + i);
    many["k" + std::to_string(i)] = MakeItem(name, 10 + i, 100);
  }
  ShopPanel panel(c, many);
  panel.OpenMenu();
  // The menu's top border lands on the selected item's row, so its first entry
  // sits on the row below -- beside the next item down.
  int item_row = RowIndexWith(panel, "> Item A");
  int entry_row = RowIndexWith(panel, "Inspect");
  ASSERT_GE(item_row, 0);
  ASSERT_GE(entry_row, 0);
  EXPECT_EQ(entry_row, item_row + 1)
      << "the menu should hang off the item it belongs to";
}

TEST_F(ShopPanelTest, ResetTakesDownAnOpenMenu) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_);
  panel.OpenMenu();
  panel.Reset();
  EXPECT_FALSE(panel.menu_open());
}

// Nothing to act on, so nothing to open a menu of.
TEST_F(ShopPanelTest, AnEmptyShopOpensNoMenu) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> nothing;
  ShopPanel panel(c, nothing);
  panel.OpenMenu();
  EXPECT_FALSE(panel.menu_open());
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
