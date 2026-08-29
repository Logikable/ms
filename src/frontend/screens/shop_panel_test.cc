#include "src/frontend/screens/shop_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/character.h"
#include "src/frontend/widgets/colors.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

EquipPrototype MakeItem(const std::string& name, int level, int price,
                        EquipJobCategory job = EQUIP_JOB_CATEGORY_UNIVERSAL,
                        EquipType type = EQUIP_TYPE_ONE_HANDED_SWORD,
                        EquipSlot slot = EQUIP_SLOT_PRIMARY_WEAPON) {
  EquipPrototype e;
  e.set_name(name);
  e.set_equip_slot(slot);
  e.set_equip_type(type);
  e.set_required_level(level);
  e.set_shop_price(price);
  e.add_equip_job_categories(job);
  return e;
}

// An item the shop does not stock: it names no price at all, which is not the
// same as naming zero -- the shop hands one item over for nothing.
EquipPrototype MakeUnpricedItem(
    const std::string& name, int level,
    EquipJobCategory job = EQUIP_JOB_CATEGORY_UNIVERSAL,
    EquipType type = EQUIP_TYPE_ONE_HANDED_SWORD,
    EquipSlot slot = EQUIP_SLOT_PRIMARY_WEAPON);

// The same item, on the token shelf instead: it names a token rather than a
// price, which is what puts it there.
EquipPrototype MakeTokenItem(
    const std::string& name, int level, const std::string& token, int count,
    EquipJobCategory job = EQUIP_JOB_CATEGORY_UNIVERSAL,
    EquipType type = EQUIP_TYPE_ONE_HANDED_SWORD,
    EquipSlot slot = EQUIP_SLOT_PRIMARY_WEAPON) {
  EquipPrototype e = MakeUnpricedItem(name, level, job, type, slot);
  e.set_token_item(token);
  e.set_token_price(count);
  return e;
}

EquipPrototype MakeUnpricedItem(const std::string& name, int level,
                                EquipJobCategory job, EquipType type,
                                EquipSlot slot) {
  EquipPrototype e = MakeItem(name, level, /*price=*/0, job, type, slot);
  e.clear_shop_price();
  return e;
}

ItemPrototype MakeToken(const std::string& name, CurrencyColor color) {
  ItemPrototype p;
  p.set_name(name);
  p.set_category(ITEM_CATEGORY_ETC);
  p.set_currency_mark("●");
  p.set_currency_color(color);
  return p;
}

ItemPrototype MakeStackable(const std::string& name, int price, int stack) {
  ItemPrototype p;
  p.set_name(name);
  p.set_category(ITEM_CATEGORY_ETC);
  p.set_shop_price(price);
  p.set_max_stack(stack);
  return p;
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

  int RenderWidth(const ShopPanel& panel) {
    ftxui::Element element = panel.Render();
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fit(element), ftxui::Dimension::Fit(element));
    return screen.dimx();
  }

  // The panel drawn the way the game shows it -- centred on a terminal of the
  // given size -- with every row of that terminal returned, not just the ones
  // the panel covers.
  //
  // `Render` fits the screen to the panel, which makes it the harness for the
  // panel's own size and the wrong one for the menu: anything drawn outside
  // the window is clipped away before it can be read back.
  std::vector<std::string> ScreenRows(const ShopPanel& panel, int width = 100,
                                      int height = 40) {
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, ftxui::center(panel.Render()));
    std::vector<std::string> rows;
    for (int y = 0; y < height; ++y) {
      std::string row;
      for (int x = 0; x < width; ++x) {
        // Unpainted cells hold an empty string rather than a space.
        const std::string& cell = screen.PixelAt(x, y).character;
        if (cell.empty()) {
          row += " ";
        } else {
          row += cell;
        }
      }
      rows.push_back(std::move(row));
    }
    return rows;
  }

  // The index of the first row holding `needle`, or -1.
  static int IndexWith(const std::vector<std::string>& rows,
                       const std::string& needle) {
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      if (rows[i].find(needle) != std::string::npos) {
        return i;
      }
    }
    return -1;
  }

  // The index of the LAST row holding `needle`, for finding a window's bottom
  // border once a menu has drawn a border of its own.
  static int LastIndexWith(const std::vector<std::string>& rows,
                           const std::string& needle) {
    for (int i = static_cast<int>(rows.size()) - 1; i >= 0; --i) {
      if (rows[i].find(needle) != std::string::npos) {
        return i;
      }
    }
    return -1;
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
      // A row is searched as bytes and read as columns, which are not the same
      // thing: a border is one column and three bytes, so the byte a match
      // starts at is nowhere near the column it is drawn in.
      std::vector<int> column_of_byte;
      for (int x = 0; x < screen.dimx(); ++x) {
        // Unpainted cells hold an empty string, not a space; dropping them
        // would join text that is not actually adjacent.
        std::string ch = screen.PixelAt(x, y).character;
        if (ch.empty()) {
          ch = " ";
        }
        row += ch;
        column_of_byte.insert(column_of_byte.end(), ch.size(), x);
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
      return screen.PixelAt(column_of_byte[at], y).foreground_color;
    }
    ADD_FAILURE() << "no row holding '" << row_needle << "'";
    return ftxui::Color::Default;
  }

  // The column the last drawn character of the row holding `row_needle` sits
  // in. What asks whether two cells are right-aligned with each other: a coin
  // and a token mark are different numbers of bytes and different numbers of
  // columns, so nothing in the row's text answers it.
  int RightEdgeOf(const ShopPanel& panel, const std::string& row_needle) {
    ftxui::Element element = panel.Render();
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fit(element), ftxui::Dimension::Fit(element));
    ftxui::Render(screen, element);
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string row;
      for (int x = 0; x < screen.dimx(); ++x) {
        std::string ch = screen.PixelAt(x, y).character;
        row += ch.empty() ? " " : ch;
      }
      if (row.find(row_needle) == std::string::npos) {
        continue;
      }
      // Past the border and the scroll bar's column, both of which are drawn
      // whatever the cost cell says.
      for (int x = screen.dimx() - 1; x >= 0; --x) {
        const std::string& ch = screen.PixelAt(x, y).character;
        if (!ch.empty() && ch != " " && ch != "│" && ch != "┃" && ch != "╹") {
          return x;
        }
      }
    }
    ADD_FAILURE() << "no row holding '" << row_needle << "'";
    return -1;
  }

  CharacterInstance MakeCharacter(int64_t meso, int level = 1,
                                  Job job = JOB_SWORDMAN, int stage = 1) {
    Character proto;
    proto.set_level(level);
    proto.set_job(job);
    // Which advancement the character holds, not just which line: an off-hand
    // asks for the branch, and a 2nd job is a stage as well as a job.
    proto.set_job_stage(stage);
    CharacterInstance c(rng_, std::move(proto));
    c.AddMeso(meso);
    return c;
  }

  // Walks the bar to `tab` from wherever the panel opened. Up is what puts the
  // cursor on the bar; Left and Right only reach it from there.
  // Up twice: the pay row sits between the list and the tab bar.
  static void OpenShelf(ShopPanel& panel, ShopTab tab) {
    panel.OnEvent(ftxui::Event::ArrowUp);
    panel.OnEvent(ftxui::Event::ArrowUp);
    for (int i = 0; i < tab; ++i) {
      panel.OnEvent(ftxui::Event::ArrowRight);
    }
  }

  // Puts the cursor on the Token half of the pay row of the open tab.
  static void OpenTokenShelf(ShopPanel& panel, ShopTab tab) {
    OpenShelf(panel, tab);
    panel.OnEvent(ftxui::Event::ArrowDown);  // tab bar -> pay bar
    panel.OnEvent(ftxui::Event::ArrowRight);
  }

  // `count` universal weapons, one per level so they list in name order.
  static std::map<std::string, EquipPrototype> ManyItems(int count) {
    std::map<std::string, EquipPrototype> many;
    for (int i = 0; i < count; ++i) {
      std::string suffix = i < 10 ? "0" + std::to_string(i) : std::to_string(i);
      many["k" + suffix] = MakeItem("Item " + suffix, 10 + i, 100);
    }
    return many;
  }

  std::mt19937 rng_{0};
  std::map<std::string, EquipPrototype> equips_{
      {"long_sword", MakeItem("Long Sword", 10, 5000)},
      {"machete", MakeItem("Machete", 20, 10000, EQUIP_JOB_CATEGORY_WARRIOR)},
      {"gladius", MakeItem("Gladius", 30, 20000, EQUIP_JOB_CATEGORY_WARRIOR)},
      {"scimitar", MakeItem("Scimitar", 30, 20000, EQUIP_JOB_CATEGORY_WARRIOR,
                            EQUIP_TYPE_TWO_HANDED_SWORD)},
      // A thief's, so a warrior never sees it.
      {"subi",
       MakeItem("Subi Throwing-Stars", 10, 1000, EQUIP_JOB_CATEGORY_THIEF,
                EQUIP_TYPE_THROWING_STAR, EQUIP_SLOT_PROJECTILE)},
      // The off-hands of two warrior branches, so the shelf can be checked for
      // both what it holds and what it keeps back.
      {"medallion",
       MakeItem("Powers Medallion", 30, 10000, EQUIP_JOB_CATEGORY_WARRIOR,
                EQUIP_TYPE_MEDALLION, EQUIP_SLOT_SECONDARY)},
      {"rosary", MakeItem("Holy Rosary", 30, 10000, EQUIP_JOB_CATEGORY_WARRIOR,
                          EQUIP_TYPE_ROSARY, EQUIP_SLOT_SECONDARY)},
      {"heirloom", MakeUnpricedItem("Heirloom", 10)},
      // The token shelves: one weapon and one off-hand nothing but a token
      // buys.
      {"frozen_sword", MakeTokenItem("Frozen Sword", 120, "weapon_token",
                                     /*count=*/1, EQUIP_JOB_CATEGORY_WARRIOR)},
      // Nine tokens, and a level with no nine in it: the price is then the only
      // 9 on its row, which is what lets a test read the colour of it alone.
      {"frozen_axe", MakeTokenItem("Frozen Axe", 130, "weapon_token",
                                   /*count=*/9, EQUIP_JOB_CATEGORY_WARRIOR)},
      {"frozen_medal",
       MakeTokenItem("Frozen Medal", 120, "secondary_token", /*count=*/1,
                     EQUIP_JOB_CATEGORY_WARRIOR, EQUIP_TYPE_MEDALLION,
                     EQUIP_SLOT_SECONDARY)},
  };

  std::map<std::string, ItemPrototype> items_{
      {"spell_trace", MakeStackable("Spell Trace", 5000, 30000)},
      // Unpriced, so the Etc shelf never shows it.
      {"shell", MakeStackable("Snail Shell", 0, 200)},
      {"weapon_token", MakeToken("Weapon Token", CURRENCY_COLOR_THEME)},
      {"secondary_token", MakeToken("Secondary Token", CURRENCY_COLOR_ORANGE)},
  };
};

TEST_F(ShopPanelTest, ListsTheStockWithNamesAndCosts) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Long Sword"), std::string::npos);
  EXPECT_NE(rendered.find("5,000"), std::string::npos);
  EXPECT_NE(rendered.find("10,000"), std::string::npos);
  // Priced at zero, so not stocked -- the panel shows what ShopStock says.
  EXPECT_EQ(rendered.find("Heirloom"), std::string::npos);
}

TEST_F(ShopPanelTest, ShowsTheWeaponTabAndTheTitle) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Shop"), std::string::npos);
  EXPECT_NE(rendered.find("Weapon"), std::string::npos);
  EXPECT_NE(rendered.find("Meso"), std::string::npos) << "the second row";
  EXPECT_NE(rendered.find("Token"), std::string::npos);
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Cost"), std::string::npos);
}

// The player's balance sits with the prices, so the column has something to be
// read against.
TEST_F(ShopPanelTest, ShowsTheBalance) {
  CharacterInstance c = MakeCharacter(34567);
  ShopPanel panel(c, equips_, items_);
  EXPECT_NE(Render(panel).find("34,567"), std::string::npos);
}

TEST_F(ShopPanelTest, OpensOnTheFirstItem) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  ASSERT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_item()->name(), "Long Sword");
  EXPECT_NE(Render(panel).find("> Long Sword"), std::string::npos);
}

TEST_F(ShopPanelTest, WalksTheList) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  panel.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.selected_item()->name(), "Machete");
  panel.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel.selected_item()->name(), "Long Sword");
}

// --- the two tab bars are stops in the same ring ---

// The caret and the white chip are never both on screen, so where the caret is
// says which of the three has the keys.
TEST_F(ShopPanelTest, ArrowUpFromTheFirstItemLandsOnTheBars) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  ASSERT_NE(Render(panel).find("> Long Sword"), std::string::npos);
  panel.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(Render(panel).find("> Long Sword"), std::string::npos);
}

TEST_F(ShopPanelTest, ArrowUpFromTheTopBarLandsOnTheLastItem) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  panel.OnEvent(ftxui::Event::ArrowUp);  // first item -> pay bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // pay bar -> tab bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // tab bar -> the last item
  EXPECT_EQ(panel.selected_item()->name(), "Scimitar");
  EXPECT_NE(Render(panel).find("> Scimitar"), std::string::npos);
}

TEST_F(ShopPanelTest, DownFromTheLastItemReturnsToTheBar) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  panel.OnEvent(ftxui::Event::ArrowUp);  // straight to the last item
  panel.OnEvent(ftxui::Event::ArrowUp);
  panel.OnEvent(ftxui::Event::ArrowUp);
  ASSERT_NE(Render(panel).find("> Scimitar"), std::string::npos);
  panel.OnEvent(ftxui::Event::ArrowDown);  // off the bottom -> the tab bar
  EXPECT_EQ(Render(panel).find("> Scimitar"), std::string::npos);
}

// Enter on a bar is not Enter on an item. Opening the menu there would put a
// context menu over a row the cursor is not on.
TEST_F(ShopPanelTest, NoContextMenuOpensFromEitherBar) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  panel.OnEvent(ftxui::Event::ArrowUp);  // first item -> pay bar
  panel.OpenMenu();
  EXPECT_FALSE(panel.menu_open());
  panel.OnEvent(ftxui::Event::ArrowUp);  // pay bar -> tab bar
  panel.OpenMenu();
  EXPECT_FALSE(panel.menu_open());
}

TEST_F(ShopPanelTest, ResetPutsTheCursorBackInTheList) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  panel.OnEvent(ftxui::Event::ArrowUp);  // first item -> tab bar
  panel.Reset();
  EXPECT_NE(Render(panel).find("> Long Sword"), std::string::npos);
}

TEST_F(ShopPanelTest, ResetReturnsToTheTop) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  panel.OnEvent(ftxui::Event::ArrowDown);
  panel.Reset();
  EXPECT_EQ(panel.selected_item()->name(), "Long Sword");
}

// The list answers "what can I buy" without the player doing arithmetic on
// every row.
TEST_F(ShopPanelTest, RedsOutPricesBeyondTheBalance) {
  CharacterInstance c = MakeCharacter(7000, /*level=*/99);
  ShopPanel panel(c, equips_, items_);
  EXPECT_NE(CellColor(panel, "Long Sword", "5,000"), kRed)
      << "5,000 is affordable on 7,000";
  EXPECT_EQ(CellColor(panel, "Gladius", "20,000"), kRed) << "20,000 is not";
}

// What every weapon in the shop shares is the slot it goes in, so the column
// names the kind of weapon instead. Class is not a column either: the list
// holds nothing this character is the wrong class for.
TEST_F(ShopPanelTest, ShowsTypeAndLevel) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/99);
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Type"), std::string::npos);
  EXPECT_NE(rendered.find("One-Handed Sword"), std::string::npos);
  EXPECT_NE(rendered.find("Two-Handed Sword"), std::string::npos);
  EXPECT_NE(rendered.find("Lv20"), std::string::npos);
  EXPECT_EQ(rendered.find("Equip Slot"), std::string::npos);
  EXPECT_EQ(rendered.find("Job"), std::string::npos);
}

TEST_F(ShopPanelTest, RedsOutALevelTheCharacterHasNotReached) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/20, JOB_SWORDMAN);
  ShopPanel panel(c, equips_, items_);
  EXPECT_NE(CellColor(panel, "Machete", "Lv20"), kRed) << "level 20 reaches it";
  EXPECT_EQ(CellColor(panel, "Gladius", "Lv30"), kRed) << "level 30 does not";
  EXPECT_NE(CellColor(panel, "Gladius", "20,000"), kRed)
      << "the price is affordable; only the level is wrong";
}

TEST_F(ShopPanelTest, LeavesAnEquippableItemUncolored) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/99, JOB_SWORDMAN);
  ShopPanel panel(c, equips_, items_);
  EXPECT_NE(CellColor(panel, "Machete", "Lv20"), kRed);
  EXPECT_NE(CellColor(panel, "Machete", "10,000"), kRed);
}

// --- the stock is what this character could hold ---

// A level too high is still listed, in red: it is something to save toward.
// A class they can never be is not, which is what makes a Job column pointless.
TEST_F(ShopPanelTest, OmitsWeaponsOfAnotherClass) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/1, JOB_SWORDMAN);
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("Subi"), std::string::npos)
      << "a warrior is shown a thief's stars";
  EXPECT_NE(rendered.find("Machete"), std::string::npos);
  EXPECT_NE(rendered.find("Gladius"), std::string::npos)
      << "level 30 is out of reach at level 1, which is not a reason to hide "
         "it";
}

TEST_F(ShopPanelTest, StocksTheOtherClassForTheOtherClass) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/99, JOB_ROGUE);
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Subi"), std::string::npos);
  EXPECT_EQ(rendered.find("Machete"), std::string::npos);
}

// Universal items are everyone's, so nobody is left with an empty shop.
TEST_F(ShopPanelTest, StocksUniversalItemsForAnyClass) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/99, JOB_MAGICIAN);
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Long Sword"), std::string::npos);
  EXPECT_EQ(rendered.find("Machete"), std::string::npos);
}

// The stock is a function of the class, so advancing has to change it. Reset is
// where that happens -- it runs every time the screen opens.
TEST_F(ShopPanelTest, ResetRestocksAfterAJobAdvancement) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/99, JOB_BEGINNER);
  ShopPanel panel(c, equips_, items_);
  ASSERT_EQ(Render(panel).find("Machete"), std::string::npos)
      << "a beginner is no class for a warrior weapon";
  c.AdvanceJob(JOB_SWORDMAN);
  panel.Reset();
  EXPECT_NE(Render(panel).find("Machete"), std::string::npos);
}

TEST_F(ShopPanelTest, OpensAMenuOverTheSelectedItem) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
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
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  EXPECT_EQ(rendered.find("Inspect"), std::string::npos);
  EXPECT_EQ(rendered.find("Close"), std::string::npos);
}

TEST_F(ShopPanelTest, MenuEntriesGoToTheirScreens) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);

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
  ShopPanel panel(c, equips_, items_);
  panel.OpenMenu();
  panel.OnMenuEvent(ftxui::Event::Return);
  EXPECT_FALSE(panel.menu_open());
  EXPECT_EQ(Render(panel).find("Inspect"), std::string::npos);
}

TEST_F(ShopPanelTest, EscapeClosesTheMenuAndStaysInTheShop) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  panel.OpenMenu();
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Escape), kShop);
  EXPECT_FALSE(panel.menu_open());
}

TEST_F(ShopPanelTest, TheMenuStaysUpWhileWalkingIt) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
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
  ShopPanel panel(c, equips_, items_);
  panel.OpenMenu();
  panel.OnMenuEvent(ftxui::Event::ArrowDown);
  panel.OnMenuEvent(ftxui::Event::Escape);
  panel.OpenMenu();
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return), kShopInspect);
}

// The menu is drawn floating, so no matter how far down the list it opens it
// asks the panel for no room and cannot stretch it. Walks every item,
// including the last, where the overlay reaches furthest past the window.
TEST_F(ShopPanelTest, TheMenuDoesNotGrowThePanel) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  int closed = RenderHeight(panel);
  for (int i = 0; i < 4; ++i) {
    panel.OpenMenu();
    EXPECT_EQ(RenderHeight(panel), closed)
        << "the menu grew the panel with the cursor on item " << i;
    panel.OnMenuEvent(ftxui::Event::Escape);
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
}

// The point of floating it: on the last item the menu hangs out below the shop
// rather than being held back inside it, so it still opens on the row of the
// item it belongs to.
TEST_F(ShopPanelTest, TheMenuDrawsPastTheBottomBorder) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  panel.OnEvent(ftxui::Event::ArrowUp);  // first item -> pay bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // pay bar -> tab bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // tab bar -> the last item
  // Measured with the menu closed: the window is one height whatever it holds,
  // so where its bottom border sits does not depend on the menu at all.
  int border = IndexWith(ScreenRows(panel), "\u2570");
  ASSERT_GE(border, 0);
  panel.OpenMenu();
  int last_entry = IndexWith(ScreenRows(panel), "Close");
  ASSERT_GE(last_entry, 0);
  EXPECT_GT(last_entry, border) << "the menu is being held inside the window "
                                   "instead of hanging out of it";
}

// The whole point of the anchor: wherever the cursor is, the menu opens on
// that row. Checked at the foot of a list long enough that there is no room
// below -- the case that used to hold it back.
TEST_F(ShopPanelTest, TheMenuOpensBesideTheLastItem) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(12);
  ShopPanel panel(c, many, items_);
  for (int i = 0; i < 11; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  ASSERT_EQ(panel.selected_item()->name(), "Item 11");
  panel.OpenMenu();
  std::vector<std::string> rows = ScreenRows(panel);
  // The menu's top border lands on the selected item's row, so its first entry
  // sits on the row below.
  int item_row = IndexWith(rows, "> Item 11");
  int entry_row = IndexWith(rows, "Inspect");
  ASSERT_GE(item_row, 0);
  ASSERT_GE(entry_row, 0);
  EXPECT_EQ(entry_row, item_row + 1);
}

// And the same on an item with plenty of room below it, so the anchor is not
// merely right at the one end of the list.
TEST_F(ShopPanelTest, TheMenuOpensBesideTheFirstItem) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(12);
  ShopPanel panel(c, many, items_);
  panel.OpenMenu();
  std::vector<std::string> rows = ScreenRows(panel);
  int item_row = IndexWith(rows, "> Item 00");
  int entry_row = IndexWith(rows, "Inspect");
  ASSERT_GE(item_row, 0);
  ASSERT_GE(entry_row, 0);
  EXPECT_EQ(entry_row, item_row + 1);
}

// Hanging out of the window is fine; hanging off the terminal is not, since
// none of it would be drawn. On a screen with only one row to spare the menu
// gives up its anchor and comes back up to sit on the last one.
TEST_F(ShopPanelTest, TheMenuSlidesBackOntoAShortScreen) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  for (int i = 0; i < 3; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  panel.OpenMenu();
  constexpr int kHeight = 13;
  std::vector<std::string> rows = ScreenRows(panel, /*width=*/100, kHeight);
  // The lowest bottom-left corner on the screen is the menu's own, and it has
  // to be on the screen at all.
  EXPECT_EQ(LastIndexWith(rows, "\u2570"), kHeight - 1);
  EXPECT_NE(IndexWith(rows, "Close"), -1);
}

// Shorter still, and the menu and the rows above it no longer fit together at
// all. It keeps itself whole and lets the empty space that positions it run
// off the top, rather than staying anchored and losing its own last entry.
TEST_F(ShopPanelTest, TheMenuStaysWholeOnAShortTerminal) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  for (int i = 0; i < 3; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  panel.OpenMenu();
  constexpr int kHeight = 12;
  std::vector<std::string> rows = ScreenRows(panel, /*width=*/100, kHeight);
  EXPECT_EQ(LastIndexWith(rows, "\u2570"), kHeight - 1);
  EXPECT_NE(IndexWith(rows, "Inspect"), -1);
  EXPECT_NE(IndexWith(rows, "Close"), -1);
}

// --- a list longer than the window ---

// Fifteen rows on screen at once, so a warrior's list -- the longest any class
// has -- is nearly all of it, and the window still clears a modest terminal.
constexpr int kVisibleRows = 15;

TEST_F(ShopPanelTest, ShowsOnlyAWindowOfALongList) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Item 00"), std::string::npos);
  EXPECT_NE(rendered.find("Item 14"), std::string::npos) << "the 15th row";
  EXPECT_EQ(rendered.find("Item 15"), std::string::npos) << "the 16th";
}

// The window moves by as little as it takes, so walking down one row does not
// throw the list about under the cursor.
TEST_F(ShopPanelTest, ScrollsOneRowAtATimeOffTheBottom) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  for (int i = 0; i < kVisibleRows - 1; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  ASSERT_NE(Render(panel).find("> Item 14"), std::string::npos)
      << "the last row of the window is reached without scrolling";
  ASSERT_NE(Render(panel).find("Item 00"), std::string::npos);

  panel.OnEvent(ftxui::Event::ArrowDown);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("> Item 15"), std::string::npos);
  EXPECT_EQ(rendered.find("Item 00"), std::string::npos) << "scrolled by one";
  EXPECT_NE(rendered.find("Item 01"), std::string::npos);
}

TEST_F(ShopPanelTest, ScrollsBackUpOffTheTop) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  for (int i = 0; i < 20; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  ASSERT_EQ(Render(panel).find("Item 00"), std::string::npos);
  for (int i = 0; i < 20; ++i) {
    panel.OnEvent(ftxui::Event::ArrowUp);
  }
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("> Item 00"), std::string::npos);
  EXPECT_NE(rendered.find("Item 14"), std::string::npos);
}

// Wrapping round the ring is the one move that goes a long way at once.
TEST_F(ShopPanelTest, WrappingToTheLastItemScrollsToTheFoot) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  panel.OnEvent(ftxui::Event::ArrowUp);  // first item -> pay bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // pay bar -> tab bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // tab bar -> the last item
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("> Item 29"), std::string::npos);
  EXPECT_EQ(rendered.find("Item 14"), std::string::npos);
}

TEST_F(ShopPanelTest, ResetScrollsBackToTheTop) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  for (int i = 0; i < 20; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  panel.Reset();
  EXPECT_NE(Render(panel).find("> Item 00"), std::string::npos);
}

// One width, on the same grounds and for a longer list of reasons: the tab
// the player is on, the price of the dearest thing on it, and how much meso
// they are carrying all used to set it. A centred window that changes width
// slides sideways under whoever is reading it.
TEST_F(ShopPanelTest, ThePanelIsOneWidthWhateverItHolds) {
  CharacterInstance poor = MakeCharacter(500, /*level=*/60, JOB_FIGHTER,
                                         /*stage=*/2);
  CharacterInstance rich = MakeCharacter(1000000000, /*level=*/60, JOB_FIGHTER,
                                         /*stage=*/2);
  ShopPanel panel(poor, equips_, items_);
  int width = RenderWidth(panel);
  for (int tab = 0; tab < kNumShopTabs; ++tab) {
    ShopPanel stepped(poor, equips_, items_);
    OpenShelf(stepped, static_cast<ShopTab>(tab));
    EXPECT_EQ(RenderWidth(stepped), width) << "on tab " << tab;
  }
  ShopPanel wealthy(rich, equips_, items_);
  EXPECT_EQ(RenderWidth(wealthy), width) << "a big meso counter widened it";

  // A price with three digits more than anything the catalog stocks today.
  std::map<std::string, EquipPrototype> dear = equips_;
  dear["dear"] = MakeItem("Fafnir Mistilteinn", 100, 9999999);
  ShopPanel expensive(rich, dear, items_);
  EXPECT_EQ(RenderWidth(expensive), width) << "a dear item widened it";
  EXPECT_NE(Render(expensive).find("9,999,999"), std::string::npos)
      << "and its price is still shown in full";
  // Both prices end in the same column. Each row holds exactly one coin, so
  // the bytes it costs are the same on both and the offsets compare.
  std::vector<std::string> rows = ScreenRows(expensive);
  int dear_row = -1;
  int cheap_row = -1;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].find("9,999,999") != std::string::npos) {
      dear_row = i;
    } else if (rows[i].find("5,000") != std::string::npos) {
      cheap_row = i;
    }
  }
  ASSERT_GE(dear_row, 0);
  ASSERT_GE(cheap_row, 0);
  EXPECT_EQ(rows[dear_row].find_last_of("0123456789"),
            rows[cheap_row].find_last_of("0123456789"))
      << "the prices are not aligned on the same column";
}

// The panel is one height whatever the tab holds -- it is drawn centred, so a
// shelf that shrank the window would slide the title and the column header up
// the screen every time the player stepped along the bar.
TEST_F(ShopPanelTest, ThePanelIsOneHeightHoweverManyRows) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> nothing;
  std::map<std::string, EquipPrototype> few = ManyItems(2);
  std::map<std::string, EquipPrototype> full = ManyItems(kVisibleRows);
  std::map<std::string, EquipPrototype> over = ManyItems(60);
  ShopPanel empty(c, nothing, items_);
  ShopPanel small(c, few, items_);
  ShopPanel exact(c, full, items_);
  ShopPanel big(c, over, items_);
  EXPECT_EQ(RenderHeight(small), RenderHeight(exact));
  EXPECT_EQ(RenderHeight(empty), RenderHeight(exact));
  EXPECT_EQ(RenderHeight(big), RenderHeight(exact));
}

TEST_F(ShopPanelTest, DrawsAScrollBarOnlyWhenThereIsMoreToSee) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> fits = ManyItems(kVisibleRows);
  std::map<std::string, EquipPrototype> over = ManyItems(30);
  ShopPanel small(c, fits, items_);
  ShopPanel big(c, over, items_);
  EXPECT_EQ(Render(small).find("\u2503"), std::string::npos)
      << "nothing is off screen, so there is nothing to indicate";
  EXPECT_NE(Render(big).find("\u2503"), std::string::npos);
}

// The bar says where in the list the window is, so it has to move with it.
TEST_F(ShopPanelTest, TheScrollBarFollowsTheWindow) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(60);
  ShopPanel panel(c, many, items_);
  int at_top = RowIndexWith(panel, "\u2503");
  ASSERT_GE(at_top, 0);
  panel.OnEvent(ftxui::Event::ArrowUp);  // first item -> pay bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // pay bar -> tab bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // tab bar -> the last item
  EXPECT_GT(RowIndexWith(panel, "\u2503"), at_top);
}

// The menu anchors on the cursor's row of the WINDOW, not its place in the
// stock -- the reason the panel keeps the scroll offset itself.
TEST_F(ShopPanelTest, TheMenuOpensBesideAScrolledRow) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  for (int i = 0; i < 25; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  ASSERT_EQ(panel.selected_item()->name(), "Item 25");
  panel.OpenMenu();
  std::vector<std::string> rows = ScreenRows(panel);
  int item_row = IndexWith(rows, "> Item 25");
  int entry_row = IndexWith(rows, "Inspect");
  ASSERT_GE(item_row, 0);
  ASSERT_GE(entry_row, 0);
  EXPECT_EQ(entry_row, item_row + 1);
}

TEST_F(ShopPanelTest, ResetTakesDownAnOpenMenu) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  panel.OpenMenu();
  panel.Reset();
  EXPECT_FALSE(panel.menu_open());
}

// Nothing to act on, so nothing to open a menu of.
TEST_F(ShopPanelTest, AnEmptyShopOpensNoMenu) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> nothing;
  ShopPanel panel(c, nothing, items_);
  panel.OpenMenu();
  EXPECT_FALSE(panel.menu_open());
}

TEST_F(ShopPanelTest, AnEmptyShopSaysSo) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> nothing;
  ShopPanel panel(c, nothing, items_);
  EXPECT_EQ(panel.selected_item(), nullptr);
  EXPECT_NE(Render(panel).find("(empty)"), std::string::npos);
}

// --- the Equips tab ---

// The bar reads left to right in the order a player meets the shelves: the
// weapon first, then the rest of what is worn, and the consumables last.
TEST_F(ShopPanelTest, TheBarReadsWeaponEquipsEtc) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  size_t weapon = rendered.find("Weapon");
  size_t equips = rendered.find("Equips");
  size_t etc = rendered.find("Etc");
  ASSERT_NE(equips, std::string::npos);
  EXPECT_LT(weapon, equips);
  EXPECT_LT(equips, etc);
}

// The meso counter is drawn over the same row as the chips. A third chip took
// the bar out far enough that a centred counter landed on top of "Etc" and cut
// it to "Et" -- so the counter now sits in what the chips leave.
TEST_F(ShopPanelTest, TheMesoCounterDoesNotCoverATab) {
  CharacterInstance c = MakeCharacter(1000000000);
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Etc"), std::string::npos);
  EXPECT_NE(rendered.find("1,000,000,000"), std::string::npos);
}

TEST_F(ShopPanelTest, TheEquipShelfHoldsTheBranchsOwnOffHand) {
  CharacterInstance c = MakeCharacter(100000, 30, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopEquipsTab);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Powers Medallion"), std::string::npos);
  EXPECT_EQ(rendered.find("Holy Rosary"), std::string::npos)
      << "a Fighter is offered a Page's rosary";
  // A shelf of its own, not more rows of the weapons.
  EXPECT_EQ(rendered.find("Long Sword"), std::string::npos);
  ASSERT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_item()->name(), "Powers Medallion");
}

// An off-hand belongs to one branch of one job, and a 1st job is not in a
// branch yet -- so there is nothing on the shelf to buy or to want. The
// accessories that fit anybody are not in this test's catalog.
TEST_F(ShopPanelTest, TheEquipShelfHasNoOffHandBeforeTheSecondJob) {
  CharacterInstance c = MakeCharacter(100000, 30, JOB_SWORDMAN);
  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopEquipsTab);
  EXPECT_NE(Render(panel).find("(empty)"), std::string::npos);
  EXPECT_EQ(panel.selected_item(), nullptr);
}

// The shelf carries everything worn that is not swung, so a ring stands beside
// the off-hands. Nothing about a ring turns on which kind of ring it is, so it
// has no equip type -- and the type column then reads the slot, which is what
// the bag shows for the same item.
TEST_F(ShopPanelTest, TheEquipShelfCarriesAccessoriesAndNamesTheirSlot) {
  std::map<std::string, EquipPrototype> equips{
      {"ring", MakeItem("Signet Ring", 30, 7000, EQUIP_JOB_CATEGORY_UNIVERSAL,
                        EQUIP_TYPE_UNSPECIFIED, EQUIP_SLOT_RING)},
  };
  CharacterInstance c = MakeCharacter(100000, 30, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips, items_);
  OpenShelf(panel, kShopEquipsTab);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Signet Ring"), std::string::npos);
  EXPECT_NE(rendered.find("Ring"), std::string::npos);
  ASSERT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_item()->name(), "Signet Ring");
}

// A price of zero is a price: the shelf lists the item and the cost column says
// what it costs, rather than the row being left off as an unstocked one is.
TEST_F(ShopPanelTest, AFreeItemIsOnTheShelfAtZero) {
  std::map<std::string, EquipPrototype> equips{
      {"medal",
       MakeItem("Master Adventurer", 100, 0, EQUIP_JOB_CATEGORY_UNIVERSAL,
                EQUIP_TYPE_UNSPECIFIED, EQUIP_SLOT_MEDAL)},
  };
  CharacterInstance c = MakeCharacter(100000, 30, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips, items_);
  OpenShelf(panel, kShopEquipsTab);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Master Adventurer"), std::string::npos);
  EXPECT_NE(rendered.find("🪙 0"), std::string::npos);
}

// --- the Etc tab ---

// Left and Right only reach the bar from the bar. Up off the first row is what
// puts the cursor there, exactly as in the bag.
TEST_F(ShopPanelTest, RightOnTheBarOpensTheEtcShelf) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  EXPECT_EQ(Render(panel).find("Spell Trace"), std::string::npos);
  OpenShelf(panel, kShopEtcTab);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Spell Trace"), std::string::npos);
  EXPECT_NE(rendered.find("5,000"), std::string::npos);
  // Unpriced, so the shelf never stocks it.
  EXPECT_EQ(rendered.find("Snail Shell"), std::string::npos);
  // The weapons are gone with the tab, not merely scrolled past.
  EXPECT_EQ(rendered.find("Long Sword"), std::string::npos);
}

// A tab is a different list, so the two selections must not be read as one:
// asking for an equip on the Etc shelf has to answer nothing.
TEST_F(ShopPanelTest, OnlyOneOfTheTwoSelectionsEverAnswers) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  EXPECT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_stackable(), nullptr);
  OpenShelf(panel, kShopEtcTab);
  EXPECT_EQ(panel.selected_item(), nullptr);
  ASSERT_NE(panel.selected_stackable(), nullptr);
  EXPECT_EQ(panel.selected_stackable()->name(), "Spell Trace");
}

TEST_F(ShopPanelTest, TheEndsOfTheBarAreWalls) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  panel.OnEvent(ftxui::Event::ArrowUp);
  panel.OnEvent(ftxui::Event::ArrowUp);
  panel.OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_NE(panel.selected_item(), nullptr) << "stepped off the left end";
  for (int i = 0; i < kNumShopTabs; ++i) {
    panel.OnEvent(ftxui::Event::ArrowRight);
  }
  // One step more than there are tabs, so a bar that wrapped would be back on
  // Weapons. Buy-Back is the last shelf, and the only one with a Qty column.
  EXPECT_NE(Render(panel).find("Qty"), std::string::npos)
      << "stepped off the right end";
}

// --- the pay row ---

// The same shelf, read for the other price. What meso buys and what a token
// buys are different lists, and neither holds anything off the other.
TEST_F(ShopPanelTest, TheTokenTabHoldsWhatATokenBuys) {
  CharacterInstance c = MakeCharacter(100000, 120, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips_, items_);
  ASSERT_EQ(Render(panel).find("Frozen Sword"), std::string::npos);

  OpenTokenShelf(panel, kShopWeaponTab);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Frozen Sword"), std::string::npos);
  EXPECT_EQ(rendered.find("Long Sword"), std::string::npos)
      << "the meso shelf is gone with the tab";
  // Priced in its own mark, not in meso. Read off the screen rather than out
  // of ToString: the mark is coloured, so escape codes sit between it and the
  // number in the string.
  EXPECT_GE(IndexWith(ScreenRows(panel), "● 1"), 0);
}

// Each tab deals in its own token, so the off-hand shelf asks in the off-hand
// token and never in the weapon one.
TEST_F(ShopPanelTest, EachTokenTabAsksInItsOwnToken) {
  CharacterInstance c = MakeCharacter(100000, 120, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips_, items_);
  OpenTokenShelf(panel, kShopWeaponTab);
  ASSERT_NE(panel.selected_token(), nullptr);
  EXPECT_EQ(panel.selected_token()->name(), "Weapon Token");

  ShopPanel other(c, equips_, items_);
  OpenTokenShelf(other, kShopEquipsTab);
  ASSERT_NE(other.selected_item(), nullptr);
  EXPECT_EQ(other.selected_item()->name(), "Frozen Medal");
  ASSERT_NE(other.selected_token(), nullptr);
  EXPECT_EQ(other.selected_token()->name(), "Secondary Token");
}

// A price is only worth reading against what the player holds, so the counter
// changes with the shelf.
TEST_F(ShopPanelTest, TheCounterCountsWhatTheShelfIsPaidIn) {
  CharacterInstance c = MakeCharacter(34567, 120, JOB_FIGHTER, /*stage=*/2);
  c.AddStackable(items_.at("weapon_token"), 3);
  ShopPanel panel(c, equips_, items_);
  ASSERT_NE(Render(panel).find("34,567"), std::string::npos);

  OpenTokenShelf(panel, kShopWeaponTab);
  EXPECT_EQ(Render(panel).find("34,567"), std::string::npos);
  EXPECT_GE(IndexWith(ScreenRows(panel), "● 3"), 0);
}

// The Cost column is right-aligned in screen columns, so the header, a meso
// price and a token price all end in the same place -- though a coin is two
// columns of four bytes and a token's mark one column of three.
TEST_F(ShopPanelTest, EveryCostCellEndsInTheSameColumn) {
  CharacterInstance c = MakeCharacter(100000, 120, JOB_FIGHTER, /*stage=*/2);
  ShopPanel meso(c, equips_, items_);
  int header = RightEdgeOf(meso, "Cost");
  EXPECT_GT(header, 0);
  EXPECT_EQ(RightEdgeOf(meso, "Long Sword"), header);

  ShopPanel tokens(c, equips_, items_);
  OpenTokenShelf(tokens, kShopWeaponTab);
  EXPECT_EQ(RightEdgeOf(tokens, "Cost"), header);
  ASSERT_NE(tokens.selected_item(), nullptr);
  EXPECT_EQ(RightEdgeOf(tokens, tokens.selected_item()->name()), header);
}

// Red is the reason: a token price the player cannot meet reddens, and the
// mark it is asked in does not -- a currency is not a refusal.
TEST_F(ShopPanelTest, APriceNoTokenCanMeetIsRed) {
  CharacterInstance poor = MakeCharacter(100000, 130, JOB_FIGHTER, 2);
  ShopPanel panel(poor, equips_, items_);
  OpenTokenShelf(panel, kShopWeaponTab);
  EXPECT_EQ(CellColor(panel, "Frozen Axe", "9"), kRed);
  EXPECT_EQ(CellColor(panel, "Frozen Axe", "●"), kTheme)
      << "the mark is the currency, not the reason";

  CharacterInstance rich = MakeCharacter(100000, 130, JOB_FIGHTER, 2);
  rich.AddStackable(items_.at("weapon_token"), 9);
  ShopPanel afford(rich, equips_, items_);
  OpenTokenShelf(afford, kShopWeaponTab);
  EXPECT_NE(CellColor(afford, "Frozen Axe", "9"), kRed);
}

// The pay row is drawn under every tab so the window keeps one height, but a
// blank one is not a stop: Up off the Etc list reaches the tab bar itself.
TEST_F(ShopPanelTest, TheEtcTabHasNoPayRowToStandOn) {
  CharacterInstance c = MakeCharacter(100000, 30, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopEtcTab);
  panel.OnEvent(ftxui::Event::ArrowDown);  // tab bar -> the first item
  ASSERT_NE(panel.selected_stackable(), nullptr);
  panel.OnEvent(ftxui::Event::ArrowUp);    // first item -> the tab bar again
  panel.OnEvent(ftxui::Event::ArrowLeft);  // which is where Left works
  EXPECT_NE(panel.selected_item(), nullptr) << "back on the Secondary shelf";
}

// The shop is drawn centred, so a row that came and went with the tab would
// move the whole window up the screen.
TEST_F(ShopPanelTest, TheWindowIsOneHeightUnderEveryTab) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  ftxui::Element weapon = panel.Render();
  weapon->ComputeRequirement();
  int rows = weapon->requirement().min_y;
  for (int tab = 0; tab < kNumShopTabs; ++tab) {
    ShopPanel other(c, equips_, items_);
    OpenShelf(other, static_cast<ShopTab>(tab));
    ftxui::Element element = other.Render();
    element->ComputeRequirement();
    EXPECT_EQ(element->requirement().min_y, rows) << "tab " << tab;
  }
}

// In the list Left and Right are not tab keys: changing the list under a cursor
// the player was moving through it would lose their place.
TEST_F(ShopPanelTest, TheListIgnoresLeftAndRight) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  EXPECT_FALSE(panel.OnEvent(ftxui::Event::ArrowRight));
  EXPECT_NE(panel.selected_item(), nullptr);
}

TEST_F(ShopPanelTest, TheEtcShelfShowsHowManyAreOwned) {
  CharacterInstance c = MakeCharacter(100000);
  c.AddStackable(items_.at("spell_trace"), 1234);
  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopEtcTab);
  EXPECT_NE(Render(panel).find("1,234"), std::string::npos);
}

// Reset is what the screen calls on the way in, so it has to land on Weapons
// however the player left it.
// --- the buy-back shelf ---

// The shelf holds both kinds at once, so the columns have to carry an equip
// and a stack without either one reading as the other.
TEST_F(ShopPanelTest, TheBuyBackShelfShowsBothKindsOfRow) {
  CharacterInstance c = MakeCharacter(100000);
  EquipPrototype sword = MakeItem("Gladius", 30, 20000);
  sword.set_sell_price(2000);
  ItemPrototype shell = MakeStackable("Green Snail Shell", 0, 200);
  shell.set_sell_price(7);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  c.SellEquip(0);
  c.AddStackable(shell, 40);
  c.SellStackable(ITEM_CATEGORY_ETC, 0, 40);

  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopBuyBackTab);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Qty"), std::string::npos);
  // The stack carries its count; the equip, being one item, carries none.
  // Each priced at what one of it sold for.
  EXPECT_NE(rendered.find("Green Snail Shell"), std::string::npos);
  EXPECT_NE(rendered.find("40"), std::string::npos);
  EXPECT_NE(rendered.find("Gladius"), std::string::npos);
  EXPECT_EQ(rendered.find("One-Handed Sword"), std::string::npos)
      << "the shelf has no type column";
  EXPECT_NE(rendered.find("2,000"), std::string::npos);
  // Newest on top: the stack was sold second.
  EXPECT_LT(RowIndexWith(panel, "Green Snail Shell"),
            RowIndexWith(panel, "Gladius"));
}

TEST_F(ShopPanelTest, AnEmptyShelfSaysSo) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopBuyBackTab);
  EXPECT_NE(Render(panel).find("(empty)"), std::string::npos);
  EXPECT_EQ(panel.selected_buy_back(), nullptr);
}

// The shelf is the player's own history, so nothing filters it. A weapon of
// another class or a level they have outgrown is still theirs to take back.
TEST_F(ShopPanelTest, TheShelfIsNotFilteredByClassOrLevel) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/1, JOB_SWORDMAN);
  EquipPrototype bow =
      MakeItem("Metus", 90, 250000, EQUIP_JOB_CATEGORY_BOWMAN, EQUIP_TYPE_BOW);
  bow.set_sell_price(25000);
  c.PickUp(std::make_unique<EquipInstance>(bow));
  c.SellEquip(0);
  ASSERT_FALSE(c.CanEquip(bow));

  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopBuyBackTab);
  EXPECT_NE(Render(panel).find("Metus"), std::string::npos);
  ASSERT_NE(panel.selected_buy_back(), nullptr);
  EXPECT_EQ(panel.selected_buy_back()->equip().equip_name(), "Metus");
}

// Exactly one of the three "what is selected" questions ever answers, or a
// caller asking all three acts on the wrong one.
TEST_F(ShopPanelTest, OnlyTheShelfAnswersOnTheShelf) {
  CharacterInstance c = MakeCharacter(100000);
  EquipPrototype sword = MakeItem("Gladius", 30, 20000);
  sword.set_sell_price(2000);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  c.SellEquip(0);

  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopBuyBackTab);
  EXPECT_NE(panel.selected_buy_back(), nullptr);
  EXPECT_EQ(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_stackable(), nullptr);

  // Walked back rather than opened again: OpenShelf steps right from wherever
  // the cursor is, and it is on the last tab.
  for (int i = 0; i < kShopBuyBackTab; ++i) {
    panel.OnEvent(ftxui::Event::ArrowLeft);
  }
  EXPECT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_buy_back(), nullptr);
}

// A trace sold and listed has to read as a trace, or it looks like the working
// item it is the wreck of.
TEST_F(ShopPanelTest, ATraceOnTheShelfSaysSo) {
  CharacterInstance c = MakeCharacter(100000);
  EquipPrototype sword = MakeItem("Gladius", 30, 20000);
  sword.set_sell_price(2000);
  Equip destroyed;
  destroyed.set_equip_name("Gladius");
  destroyed.set_stars(19);
  c.PickUp(std::make_unique<EquipTrace>(sword, destroyed));
  c.SellEquip(0);

  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopBuyBackTab);
  EXPECT_NE(Render(panel).find("Gladius Trace"), std::string::npos);
}

// The menu is what leads to Inspect and Buy, and the shelf offers both.
TEST_F(ShopPanelTest, TheMenuOpensOnAShelfRow) {
  CharacterInstance c = MakeCharacter(100000);
  EquipPrototype sword = MakeItem("Gladius", 30, 20000);
  sword.set_sell_price(2000);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  c.SellEquip(0);

  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopBuyBackTab);
  panel.OnEvent(ftxui::Event::ArrowDown);  // the bar -> the one row
  panel.OpenMenu();
  EXPECT_TRUE(panel.menu_open());
}

// The shelf stocks names half again its name column -- the magician books run
// to 32 characters. A name is cut to the column and slides under it while its
// row is selected, rather than widening the window or being lost.
TEST_F(ShopPanelTest, ALongNameIsCutToItsColumnAndNotPastIt) {
  const std::string kLongest = "Metallic Blue Book (Antistrophe)";
  std::map<std::string, EquipPrototype> shelf = equips_;
  shelf["metallic"] = MakeItem(kLongest, 10, 5000);

  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, shelf, items_);
  ShopPanel plain(c, equips_, items_);
  std::string rendered = Render(panel);

  EXPECT_EQ(rendered.find(kLongest), std::string::npos)
      << "the whole name fits, so this test proves nothing";
  EXPECT_NE(rendered.find(kLongest.substr(0, 26)), std::string::npos);
  EXPECT_EQ(RenderWidth(panel), RenderWidth(plain))
      << "a long name widened the shop";
}

TEST_F(ShopPanelTest, ReopeningComesBackToTheWeaponsTab) {
  CharacterInstance c = MakeCharacter(100000);
  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopEtcTab);
  ASSERT_NE(panel.selected_stackable(), nullptr);
  panel.Reset();
  EXPECT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_stackable(), nullptr);
}

}  // namespace
}  // namespace ms
