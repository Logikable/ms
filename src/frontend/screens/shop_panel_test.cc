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
#include "src/frontend/testing/screen_text.h"
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

// An item the shop doesn't stock: it has no price at all, which differs from a
// price of zero, since the shop gives some items away for nothing.
EquipPrototype MakeUnpricedItem(
    const std::string& name, int level,
    EquipJobCategory job = EQUIP_JOB_CATEGORY_UNIVERSAL,
    EquipType type = EQUIP_TYPE_ONE_HANDED_SWORD,
    EquipSlot slot = EQUIP_SLOT_PRIMARY_WEAPON);

// The same item on the token shelf: it names a token instead of a price, which
// is what puts it there.
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

ItemPrototype MakeToken(const std::string& name, CurrencyColor color,
                        const std::string& mark = "●") {
  ItemPrototype p;
  p.set_name(name);
  p.set_currency_mark(mark);
  p.set_currency_color(color);
  return p;
}

ItemPrototype MakeStackable(const std::string& name, int price, int stack) {
  ItemPrototype p;
  p.set_name(name);
  p.set_shop_price(price);
  p.set_max_stack(stack);
  return p;
}

class ShopPanelTest : public testing::Test {
 protected:
  // A screen cut to the panel's own size, with the panel drawn on it. Measured
  // from the element rather than with ftxui::Dimension::Fit, which clips to the
  // terminal; a test has none, so Fit would silently cut the shop to the 80
  // columns of the fallback.
  ftxui::Screen Draw(const ShopPanel& panel) {
    ftxui::Element element = panel.Render();
    element->ComputeRequirement();
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(element->requirement().min_x),
        ftxui::Dimension::Fixed(element->requirement().min_y));
    ftxui::Render(screen, element);
    return screen;
  }

  std::string Render(const ShopPanel& panel) {
    return Draw(panel).ToString();
  }

  // The index of the first rendered row containing `needle`, or -1.
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
    element->ComputeRequirement();
    return element->requirement().min_y;
  }

  int RenderWidth(const ShopPanel& panel) {
    ftxui::Element element = panel.Render();
    element->ComputeRequirement();
    return element->requirement().min_x;
  }

  // The panel drawn as the game shows it, centred on a terminal of the given
  // size, returning every row of that terminal, not just the rows the panel
  // covers.
  //
  // `Render` fits the screen to the panel, which suits tests of the panel's own
  // size but not the menu: anything drawn outside the window would be clipped
  // before it could be read.
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

  // The index of the first row containing `needle`, or -1.
  static int IndexWith(const std::vector<std::string>& rows,
                       const std::string& needle) {
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      if (rows[i].find(needle) != std::string::npos) {
        return i;
      }
    }
    return -1;
  }

  // The index of the last row containing `needle`, for finding a window's
  // bottom border once a menu has drawn its own.
  static int LastIndexWith(const std::vector<std::string>& rows,
                           const std::string& needle) {
    for (int i = static_cast<int>(rows.size()) - 1; i >= 0; --i) {
      if (rows[i].find(needle) != std::string::npos) {
        return i;
      }
    }
    return -1;
  }

  // The colour of the cell containing `cell`, on the row containing
  // `row_needle`.
  //
  // Reads the screen's pixels rather than its escape codes, because ftxui maps
  // colours to whatever palette it thinks the terminal has, and a test process
  // has no terminal, so the escape codes describe the fallback rather than the
  // colour.
  //
  // Per cell rather than per row, because a row has three things that turn red
  // for three different reasons, and checking whether anything on the row is
  // red couldn't tell an unaffordable price from a level too high.
  ftxui::Color CellColor(const ShopPanel& panel, const std::string& row_needle,
                         const std::string& cell) {
    ftxui::Screen screen = Draw(panel);
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string row;
      // A row is searched as bytes and read as columns, which differ: a border
      // is one column but three bytes, so the byte where a match starts is
      // nowhere near the column it is drawn in.
      std::vector<int> column_of_byte;
      for (int x = 0; x < screen.dimx(); ++x) {
        // Unpainted cells hold an empty string, not a space. Dropping them
        // would join text that isn't actually adjacent.
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

  // The column of the last drawn character on the row containing `row_needle`.
  // Used to check whether two cells are right-aligned: a coin and a token mark
  // have different byte and column counts, so the row's text can't answer it.
  int RightEdgeOf(const ShopPanel& panel, const std::string& row_needle) {
    ftxui::Screen screen = Draw(panel);
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string row;
      for (int x = 0; x < screen.dimx(); ++x) {
        std::string ch = screen.PixelAt(x, y).character;
        row += ch.empty() ? " " : ch;
      }
      if (row.find(row_needle) == std::string::npos) {
        continue;
      }
      // Past the border and the scroll bar's column, both drawn whatever the
      // cost cell says.
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
    // The character's advancement, not just their job line: an off-hand
    // requires the branch, and a 2nd job is a stage as well as a job.
    proto.set_job_stage(stage);
    CharacterInstance c(rng_, std::move(proto));
    c.AddMeso(meso);
    return c;
  }

  // Moves along the bar to `tab` from wherever the panel opened. Up puts the
  // cursor on the bar, and Left and Right only work from there. Up twice, since
  // the pay row is between the list and the tab bar.
  static void OpenShelf(ShopPanel& panel, ShopTab tab) {
    panel.OnEvent(ftxui::Event::ArrowUp);
    panel.OnEvent(ftxui::Event::ArrowUp);
    for (int i = 0; i < tab; ++i) {
      panel.OnEvent(ftxui::Event::ArrowRight);
    }
  }

  // Puts the cursor on the Token half of the open tab's pay row.
  static void OpenTokenShelf(ShopPanel& panel, ShopTab tab) {
    OpenShelf(panel, tab);
    panel.OnEvent(ftxui::Event::ArrowDown);  // tab bar to pay bar
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
      // The off-hands of two warrior branches, so tests can check both what the
      // shelf shows and what it holds back.
      {"medallion",
       MakeItem("Powers Medallion", 30, 10000, EQUIP_JOB_CATEGORY_WARRIOR,
                EQUIP_TYPE_MEDALLION, EQUIP_SLOT_SECONDARY)},
      {"rosary", MakeItem("Holy Rosary", 30, 10000, EQUIP_JOB_CATEGORY_WARRIOR,
                          EQUIP_TYPE_ROSARY, EQUIP_SLOT_SECONDARY)},
      {"heirloom", MakeUnpricedItem("Heirloom", 10)},
      // The token shelves: one weapon and one off-hand that only a token can
      // buy.
      {"frozen_sword", MakeTokenItem("Frozen Sword", 120, "weapon_token",
                                     /*count=*/1, EQUIP_JOB_CATEGORY_WARRIOR)},
      // Nine tokens, at a level without a nine in it, so the price is the only
      // 9 on its row and a test can read its colour alone.
      {"frozen_axe", MakeTokenItem("Frozen Axe", 130, "weapon_token",
                                   /*count=*/9, EQUIP_JOB_CATEGORY_WARRIOR)},
      {"frozen_medal",
       MakeTokenItem("Frozen Medal", 120, "secondary_token", /*count=*/1,
                     EQUIP_JOB_CATEGORY_WARRIOR, EQUIP_TYPE_MEDALLION,
                     EQUIP_SLOT_SECONDARY)},
      // A second currency on the same shelf: the Equips shelf uses one token
      // for the off-hands and another for the shoulders.
      {"frozen_shoulder",
       MakeTokenItem("Frozen Shoulder", 140, "shoulder_token", /*count=*/2,
                     EQUIP_JOB_CATEGORY_WARRIOR, EQUIP_TYPE_UNSPECIFIED,
                     EQUIP_SLOT_SHOULDER)},
  };

  std::map<std::string, ItemPrototype> items_{
      {"spell_trace", MakeStackable("Spell Trace", 5000, 30000)},
      // Unpriced, so the Etc shelf never shows it.
      {"shell", MakeStackable("Snail Shell", 0, 200)},
      {"weapon_token", MakeToken("Weapon Token", CURRENCY_COLOR_THEME)},
      {"secondary_token", MakeToken("Secondary Token", CURRENCY_COLOR_ORANGE)},
      {"shoulder_token",
       MakeToken("Shoulder Token", CURRENCY_COLOR_THEME, "▲")},
  };

  // The shopper most tests use: enough meso that the price never matters, at
  // level 1 with no job yet. Tests that need another build their own and leave
  // these two alone.
  CharacterInstance shopper_ = MakeCharacter(100000);
  ShopPanel shop_{shopper_, equips_, items_};
};

// The shop opens with its title, on the Weapon tab, showing its stock.
TEST_F(ShopPanelTest, OpensOnTheWeaponTabOverItsStock) {
  std::string rendered = Render(shop_);
  EXPECT_NE(rendered.find("Shop"), std::string::npos);
  EXPECT_NE(rendered.find("Weapon"), std::string::npos);
  EXPECT_NE(rendered.find("Meso"), std::string::npos) << "the second row";
  EXPECT_NE(rendered.find("Token"), std::string::npos);
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Cost"), std::string::npos);
  EXPECT_NE(rendered.find("Long Sword"), std::string::npos);
  EXPECT_NE(rendered.find("5,000"), std::string::npos);
  EXPECT_NE(rendered.find("10,000"), std::string::npos);
  // Priced at zero, so not stocked; the panel shows what ShopEquipStock
  // returns.
  EXPECT_EQ(rendered.find("Heirloom"), std::string::npos);
}

// The player's balance is next to the prices, so the column can be compared
// against it.
TEST_F(ShopPanelTest, ShowsTheBalance) {
  CharacterInstance c = MakeCharacter(34567);
  ShopPanel panel(c, equips_, items_);
  EXPECT_NE(Render(panel).find("34,567"), std::string::npos);
}

TEST_F(ShopPanelTest, OpensOnTheFirstItemAndWalksTheList) {
  ASSERT_NE(shop_.selected_item(), nullptr);
  EXPECT_EQ(shop_.selected_item()->name(), "Long Sword");
  EXPECT_NE(Render(shop_).find("> Long Sword"), std::string::npos);
  shop_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(shop_.selected_item()->name(), "Machete");
  shop_.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(shop_.selected_item()->name(), "Long Sword");
}

// --- the two tab bars are stops in the same ring ---

// The caret and the white chip are never both on screen, so the caret's
// position shows which of the three has the keys.
TEST_F(ShopPanelTest, ArrowUpFromTheFirstItemLandsOnTheBars) {
  ASSERT_NE(Render(shop_).find("> Long Sword"), std::string::npos);
  shop_.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(Render(shop_).find("> Long Sword"), std::string::npos);
}

TEST_F(ShopPanelTest, ArrowUpFromTheTopBarLandsOnTheLastItem) {
  shop_.OnEvent(ftxui::Event::ArrowUp);  // first item to pay bar
  shop_.OnEvent(ftxui::Event::ArrowUp);  // pay bar to tab bar
  shop_.OnEvent(ftxui::Event::ArrowUp);  // tab bar to the last item
  EXPECT_EQ(shop_.selected_item()->name(), "Scimitar");
  EXPECT_NE(Render(shop_).find("> Scimitar"), std::string::npos);
}

TEST_F(ShopPanelTest, DownFromTheLastItemReturnsToTheBar) {
  shop_.OnEvent(ftxui::Event::ArrowUp);  // straight to the last item
  shop_.OnEvent(ftxui::Event::ArrowUp);
  shop_.OnEvent(ftxui::Event::ArrowUp);
  ASSERT_NE(Render(shop_).find("> Scimitar"), std::string::npos);
  shop_.OnEvent(ftxui::Event::ArrowDown);  // past the bottom to the tab bar
  EXPECT_EQ(Render(shop_).find("> Scimitar"), std::string::npos);
}

// Enter on a bar isn't Enter on an item. Opening the menu there would put a
// context menu over a row the cursor isn't on.
TEST_F(ShopPanelTest, NoContextMenuOpensFromEitherBar) {
  shop_.OnEvent(ftxui::Event::ArrowUp);  // first item to pay bar
  shop_.OpenMenu();
  EXPECT_FALSE(shop_.menu_open());
  shop_.OnEvent(ftxui::Event::ArrowUp);  // pay bar to tab bar
  shop_.OpenMenu();
  EXPECT_FALSE(shop_.menu_open());
}

TEST_F(ShopPanelTest, ResetPutsTheCursorBackInTheList) {
  shop_.OnEvent(ftxui::Event::ArrowUp);  // first item to tab bar
  shop_.Reset();
  EXPECT_NE(Render(shop_).find("> Long Sword"), std::string::npos);
}

TEST_F(ShopPanelTest, ResetReturnsToTheTop) {
  shop_.OnEvent(ftxui::Event::ArrowDown);
  shop_.Reset();
  EXPECT_EQ(shop_.selected_item()->name(), "Long Sword");
}

// The list shows what the player can buy without arithmetic on every row.
TEST_F(ShopPanelTest, RedsOutPricesBeyondTheBalance) {
  CharacterInstance c = MakeCharacter(7000, /*level=*/99);
  ShopPanel panel(c, equips_, items_);
  EXPECT_NE(CellColor(panel, "Long Sword", "5,000"), kRed)
      << "5,000 is affordable on 7,000";
  EXPECT_EQ(CellColor(panel, "Gladius", "20,000"), kRed) << "20,000 is not";
}

// Every weapon in the shop goes in the same slot, so the column shows the
// weapon type instead. There is no class column either, since the list has
// nothing for another class.
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

// An item above the character's level is still listed, in red, as something to
// save for. An item for a class they can never be isn't, which is why a Job
// column would be pointless.
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

// Universal items are for everyone, so nobody gets an empty shop.
TEST_F(ShopPanelTest, StocksUniversalItemsForAnyClass) {
  CharacterInstance c = MakeCharacter(100000, /*level=*/99, JOB_MAGICIAN);
  ShopPanel panel(c, equips_, items_);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Long Sword"), std::string::npos);
  EXPECT_EQ(rendered.find("Machete"), std::string::npos);
}

// The stock depends on the class, so advancing has to change it. Reset does
// this, and it runs every time the screen opens.
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
  EXPECT_FALSE(shop_.menu_open());
  std::string rendered = Render(shop_);
  EXPECT_EQ(rendered.find("Inspect"), std::string::npos);
  EXPECT_EQ(rendered.find("Close"), std::string::npos);

  shop_.OpenMenu();
  EXPECT_TRUE(shop_.menu_open());
  rendered = Render(shop_);
  EXPECT_NE(rendered.find("Inspect"), std::string::npos);
  EXPECT_NE(rendered.find("Buy"), std::string::npos);
  EXPECT_NE(rendered.find("Close"), std::string::npos);
}

TEST_F(ShopPanelTest, MenuEntriesGoToTheirScreens) {
  shop_.OpenMenu();
  EXPECT_EQ(shop_.OnMenuEvent(ftxui::Event::Return), kShopInspect)
      << "the menu opens on Inspect";

  shop_.OpenMenu();
  shop_.OnMenuEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(shop_.OnMenuEvent(ftxui::Event::Return), kShopBuy);

  shop_.OpenMenu();
  shop_.OnMenuEvent(ftxui::Event::ArrowDown);
  shop_.OnMenuEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(shop_.OnMenuEvent(ftxui::Event::Return), kShop);
}

// Whatever the entry, the menu closes on the way out, or it would still be over
// the list behind the screen it opened.
TEST_F(ShopPanelTest, ChoosingAnEntryClosesTheMenu) {
  shop_.OpenMenu();
  shop_.OnMenuEvent(ftxui::Event::Return);
  EXPECT_FALSE(shop_.menu_open());
  EXPECT_EQ(Render(shop_).find("Inspect"), std::string::npos);
}

TEST_F(ShopPanelTest, EscapeClosesTheMenuAndStaysInTheShop) {
  shop_.OpenMenu();
  EXPECT_EQ(shop_.OnMenuEvent(ftxui::Event::Escape), kShop);
  EXPECT_FALSE(shop_.menu_open());
}

TEST_F(ShopPanelTest, TheMenuStaysUpWhileWalkingIt) {
  shop_.OpenMenu();
  EXPECT_EQ(shop_.OnMenuEvent(ftxui::Event::ArrowDown), kShopMenu);
  EXPECT_EQ(shop_.OnMenuEvent(ftxui::Event::ArrowUp), kShopMenu);
  EXPECT_EQ(shop_.OnMenuEvent(ftxui::Event::Character('x')), kShopMenu)
      << "the menu is modal over the list";
  EXPECT_TRUE(shop_.menu_open());
}

// The menu reopens on Inspect every time, rather than where it was left.
TEST_F(ShopPanelTest, TheMenuReopensOnItsFirstEntry) {
  shop_.OpenMenu();
  shop_.OnMenuEvent(ftxui::Event::ArrowDown);
  shop_.OnMenuEvent(ftxui::Event::Escape);
  shop_.OpenMenu();
  EXPECT_EQ(shop_.OnMenuEvent(ftxui::Event::Return), kShopInspect);
}

// The menu is drawn floating, so however far down the list it opens, it takes
// no room from the panel and can't stretch it. Checks every item, including the
// last, where the overlay extends furthest past the window.
TEST_F(ShopPanelTest, TheMenuDoesNotGrowThePanel) {
  int closed = RenderHeight(shop_);
  for (int i = 0; i < 4; ++i) {
    shop_.OpenMenu();
    EXPECT_EQ(RenderHeight(shop_), closed)
        << "the menu grew the panel with the cursor on item " << i;
    shop_.OnMenuEvent(ftxui::Event::Escape);
    shop_.OnEvent(ftxui::Event::ArrowDown);
  }
}

// This is why it floats: on the last item the menu extends below the shop
// instead of being held inside it, so it still opens on its item's row.
TEST_F(ShopPanelTest, TheMenuDrawsPastTheBottomBorder) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  panel.OnEvent(ftxui::Event::ArrowUp);  // first item to pay bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // pay bar to tab bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // tab bar to the last item
  // Measured with the menu closed: the window is one height whatever it holds,
  // so its bottom border doesn't depend on the menu.
  int border = IndexWith(ScreenRows(panel), "\u2570");
  ASSERT_GE(border, 0);
  panel.OpenMenu();
  int last_entry = IndexWith(ScreenRows(panel), "Close");
  ASSERT_GE(last_entry, 0);
  EXPECT_GT(last_entry, border) << "the menu is being held inside the window "
                                   "instead of hanging out of it";
}

// The menu opens on the cursor's row wherever that is. Checked at the bottom of
// a list long enough that there is no room below.
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
  // The menu's top border is on the selected item's row, so its first entry is
  // on the row below.
  int item_row = IndexWith(rows, "> Item 11");
  int entry_row = IndexWith(rows, "Inspect");
  ASSERT_GE(item_row, 0);
  ASSERT_GE(entry_row, 0);
  EXPECT_EQ(entry_row, item_row + 1);
}

// The same on an item with plenty of room below it, so the placement isn't only
// right at one end of the list.
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

// Extending out of the window is fine; extending off the terminal isn't, since
// none of it would be drawn. On a screen with only one row to spare, the menu
// gives up its placement and moves up to sit on the last row.
TEST_F(ShopPanelTest, TheMenuSlidesBackOntoAShortScreen) {
  for (int i = 0; i < 3; ++i) {
    shop_.OnEvent(ftxui::Event::ArrowDown);
  }
  shop_.OpenMenu();
  constexpr int kHeight = 13;
  std::vector<std::string> rows = ScreenRows(shop_, /*width=*/100, kHeight);
  // The lowest bottom-left corner on screen is the menu's own, and it must be
  // on screen.
  EXPECT_EQ(LastIndexWith(rows, "\u2570"), kHeight - 1);
  EXPECT_NE(IndexWith(rows, "Close"), -1);
}

// Shorter still, and the menu and the rows above it no longer fit together. It
// stays whole and lets the blank space that positions it run off the top,
// rather than keeping its placement and losing its last entry.
TEST_F(ShopPanelTest, TheMenuStaysWholeOnAShortTerminal) {
  for (int i = 0; i < 3; ++i) {
    shop_.OnEvent(ftxui::Event::ArrowDown);
  }
  shop_.OpenMenu();
  constexpr int kHeight = 12;
  std::vector<std::string> rows = ScreenRows(shop_, /*width=*/100, kHeight);
  EXPECT_EQ(LastIndexWith(rows, "\u2570"), kHeight - 1);
  EXPECT_NE(IndexWith(rows, "Inspect"), -1);
  EXPECT_NE(IndexWith(rows, "Close"), -1);
}

// --- a list longer than the window ---

// Fifteen rows on screen at once, so a warrior's list, the longest of any
// class, nearly fits, and the window still fits a modest terminal.
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

// The cursor stays in the middle of the window, so there is always list on both
// sides to read. This is ScrollWindowStart, which every list uses.
TEST_F(ShopPanelTest, KeepsTheCursorInTheMiddleOfTheWindow) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  for (int i = 0; i < 10; ++i) {
    panel.OnEvent(ftxui::Event::ArrowDown);
  }
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("> Item 10"), std::string::npos);
  EXPECT_NE(rendered.find("Item 03"), std::string::npos) << "seven above it";
  EXPECT_EQ(rendered.find("Item 02"), std::string::npos);
  EXPECT_NE(rendered.find("Item 17"), std::string::npos) << "seven below it";
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

// Wrapping around the ring is the one move that goes a long way at once.
TEST_F(ShopPanelTest, WrappingToTheLastItemScrollsToTheFoot) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(30);
  ShopPanel panel(c, many, items_);
  panel.OnEvent(ftxui::Event::ArrowUp);  // first item to pay bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // pay bar to tab bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // tab bar to the last item
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

// One width, for the same reasons and more: the open tab, the price of its most
// expensive item, and how much meso the player has must not change it. A
// centred window that changes width shifts sideways under the reader.
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

  // A price three digits longer than anything the catalog stocks today.
  std::map<std::string, EquipPrototype> dear = equips_;
  dear["dear"] = MakeItem("Fafnir Windwing Shooter", 100, 9999999);
  ShopPanel expensive(rich, dear, items_);
  EXPECT_EQ(RenderWidth(expensive), width) << "a dear item widened it";
  EXPECT_NE(Render(expensive).find("9,999,999"), std::string::npos)
      << "and its price is still shown in full";
  // Both prices end in the same column. Each row has exactly one coin, so the
  // bytes it takes are the same on both and the offsets can be compared.
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

// The panel is one height whatever the tab holds. It is centred, so a shelf
// that shrank the window would move the title and column header up the screen
// whenever the player moved along the bar.
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

// The bar shows where in the list the window is, so it must move with it.
TEST_F(ShopPanelTest, TheScrollBarFollowsTheWindow) {
  CharacterInstance c = MakeCharacter(100000);
  std::map<std::string, EquipPrototype> many = ManyItems(60);
  ShopPanel panel(c, many, items_);
  int at_top = RowIndexWith(panel, "\u2503");
  ASSERT_GE(at_top, 0);
  panel.OnEvent(ftxui::Event::ArrowUp);  // first item to pay bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // pay bar to tab bar
  panel.OnEvent(ftxui::Event::ArrowUp);  // tab bar to the last item
  EXPECT_GT(RowIndexWith(panel, "\u2503"), at_top);
}

// The menu is placed at the cursor's row in the window, not its position in the
// stock, which is why the panel keeps the scroll offset itself.
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
  shop_.OpenMenu();
  shop_.Reset();
  EXPECT_FALSE(shop_.menu_open());
}

// Nothing to act on, so no menu opens.
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

// The bar reads left to right in the order a player meets the shelves: weapons,
// then the rest of what is worn, then consumables.
TEST_F(ShopPanelTest, TheBarReadsWeaponEquipsEtc) {
  std::string rendered = Render(shop_);
  size_t weapon = rendered.find("Weapon");
  size_t equips = rendered.find("Equips");
  size_t etc = rendered.find("Etc");
  ASSERT_NE(equips, std::string::npos);
  EXPECT_LT(weapon, equips);
  EXPECT_LT(equips, etc);
}

// The meso counter is drawn on the same row as the chips, in the space the
// chips leave, so it never covers a tab.
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
  // A separate shelf, not more rows of weapons.
  EXPECT_EQ(rendered.find("Long Sword"), std::string::npos);
  ASSERT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_item()->name(), "Powers Medallion");
}

// An off-hand belongs to one branch of one job, and a 1st job isn't in a branch
// yet, so there is nothing on the shelf for them. The accessories that fit
// anyone aren't in this test's catalog.
TEST_F(ShopPanelTest, TheEquipShelfHasNoOffHandBeforeTheSecondJob) {
  CharacterInstance c = MakeCharacter(100000, 30, JOB_SWORDMAN);
  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopEquipsTab);
  EXPECT_NE(Render(panel).find("(empty)"), std::string::npos);
  EXPECT_EQ(panel.selected_item(), nullptr);
}

// The shelf has everything worn that isn't a weapon, so a ring is next to the
// off-hands. The kind of ring doesn't matter, so rings have no equip type, and
// the type column shows the slot instead, as the bag does for the same item.
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

// A price of zero is still a price: the shelf lists the item and the cost
// column shows it, unlike an unstocked item, which isn't listed.
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

// Left and Right only work on the bar. Up from the first row moves the cursor
// there, as in the bag.
TEST_F(ShopPanelTest, RightOnTheBarOpensTheEtcShelf) {
  EXPECT_EQ(Render(shop_).find("Spell Trace"), std::string::npos);
  OpenShelf(shop_, kShopEtcTab);
  std::string rendered = Render(shop_);
  EXPECT_NE(rendered.find("Spell Trace"), std::string::npos);
  EXPECT_NE(rendered.find("5,000"), std::string::npos);
  // Unpriced, so the shelf never stocks it.
  EXPECT_EQ(rendered.find("Snail Shell"), std::string::npos);
  // The weapons are gone with the tab, not just scrolled out of view.
  EXPECT_EQ(rendered.find("Long Sword"), std::string::npos);
}

// A tab is a different list, so the two selections must not be confused: asking
// for an equip on the Etc shelf must return nothing.
TEST_F(ShopPanelTest, OnlyOneOfTheTwoSelectionsEverAnswers) {
  EXPECT_NE(shop_.selected_item(), nullptr);
  EXPECT_EQ(shop_.selected_stackable(), nullptr);
  OpenShelf(shop_, kShopEtcTab);
  EXPECT_EQ(shop_.selected_item(), nullptr);
  ASSERT_NE(shop_.selected_stackable(), nullptr);
  EXPECT_EQ(shop_.selected_stackable()->name(), "Spell Trace");
}

TEST_F(ShopPanelTest, TheEndsOfTheBarAreWalls) {
  shop_.OnEvent(ftxui::Event::ArrowUp);
  shop_.OnEvent(ftxui::Event::ArrowUp);
  shop_.OnEvent(ftxui::Event::ArrowLeft);
  EXPECT_NE(shop_.selected_item(), nullptr) << "stepped off the left end";
  for (int i = 0; i < kNumShopTabs; ++i) {
    shop_.OnEvent(ftxui::Event::ArrowRight);
  }
  // One step more than there are tabs, so a bar that wrapped would be back on
  // Weapons. Buy-Back is the last shelf, and the only one with a Qty column.
  EXPECT_NE(Render(shop_).find("Qty"), std::string::npos)
      << "stepped off the right end";
}

// --- the pay row ---

// The same shelf, for the other price. What meso buys and what a token buys are
// different lists, and neither includes anything from the other.
TEST_F(ShopPanelTest, TheTokenTabHoldsWhatATokenBuys) {
  CharacterInstance c = MakeCharacter(100000, 120, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips_, items_);
  ASSERT_EQ(Render(panel).find("Frozen Sword"), std::string::npos);

  OpenTokenShelf(panel, kShopWeaponTab);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Frozen Sword"), std::string::npos);
  EXPECT_EQ(rendered.find("Long Sword"), std::string::npos)
      << "the meso shelf is gone with the tab";
  // Priced in its own mark, not meso. Read from the screen rather than
  // ToString, since the mark is coloured and escape codes sit between it and
  // the number in the string.
  EXPECT_GE(IndexWith(ScreenRows(panel), "● 1"), 0);
}

// Each tab uses its own token, so the off-hand shelf prices in the off-hand
// token and never the weapon one.
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

// The tab bar shows meso whatever the shelf uses: the token balances have their
// own panel.
TEST_F(ShopPanelTest, TheBarCountsMesoOnEveryShelf) {
  CharacterInstance c = MakeCharacter(34567, 120, JOB_FIGHTER, /*stage=*/2);
  c.AddItem(items_.at("weapon_token"), 3);
  ShopPanel panel(c, equips_, items_);
  ASSERT_NE(Render(panel).find("34,567"), std::string::npos);

  OpenTokenShelf(panel, kShopWeaponTab);
  EXPECT_NE(Render(panel).find("34,567"), std::string::npos);
  EXPECT_GE(IndexWith(ScreenRows(panel), "●    3"), 0);
}

// The Equips shelf uses two tokens, and the panel shows both, one row each. A
// balance it left out would be one the player can't compare against.
TEST_F(ShopPanelTest, ThePanelShowsEveryCurrencyTheShelfTakes) {
  CharacterInstance c = MakeCharacter(100000, 140, JOB_FIGHTER, /*stage=*/2);
  c.AddItem(items_.at("secondary_token"), 3);
  c.AddItem(items_.at("shoulder_token"), 7);
  ShopPanel panel(c, equips_, items_);
  OpenTokenShelf(panel, kShopEquipsTab);

  std::vector<std::string> rows = ScreenRows(panel);
  int first = IndexWith(rows, "●    3");
  EXPECT_GE(first, 0);
  EXPECT_EQ(IndexWith(rows, "▲    7"), first + 1)
      << "a row each, in shelf order";
  EXPECT_NE(Render(panel).find("Tokens"), std::string::npos);
  // One shelf with two currencies, so the header names neither, and each row
  // shows its own.
  EXPECT_EQ(Render(panel).find("● Cost"), std::string::npos);
  EXPECT_NE(Render(panel).find("Cost"), std::string::npos);
}

// The panel appears with a token shelf, and its columns are kept under every
// other tab: the shop is centred, and a panel that came and went would shift
// the whole window sideways on every step along the pay bar.
TEST_F(ShopPanelTest, ThePanelStandsOnlyOverATokenShelf) {
  CharacterInstance c = MakeCharacter(100000, 140, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips_, items_);
  int width = RenderWidth(panel);
  int height = RenderHeight(panel);
  EXPECT_EQ(Render(panel).find("Tokens"), std::string::npos)
      << "the weapon shelf opens on meso";

  OpenTokenShelf(panel, kShopEquipsTab);
  EXPECT_NE(Render(panel).find("Tokens"), std::string::npos);
  EXPECT_EQ(RenderWidth(panel), width);
  EXPECT_EQ(RenderHeight(panel), height) << "the two windows close on a line";
}

// This is why the balances left the tab bar: the game's shelves use currencies
// that share a glyph, so colour is what tells the rows apart, and it has to be
// on the mark.
TEST_F(ShopPanelTest, ThePanelMarksEachBalanceInItsOwnColour) {
  std::map<std::string, ItemPrototype> items = items_;
  items["gold_token"] = MakeToken("Gold Token", CURRENCY_COLOR_GOLD, "▲");
  std::map<std::string, EquipPrototype> equips = equips_;
  equips["gold_shoulder"] = MakeTokenItem(
      "Gold Shoulder", 150, "gold_token", /*count=*/2,
      EQUIP_JOB_CATEGORY_WARRIOR, EQUIP_TYPE_UNSPECIFIED, EQUIP_SLOT_SHOULDER);

  CharacterInstance c = MakeCharacter(100000, 150, JOB_FIGHTER, /*stage=*/2);
  c.AddItem(items.at("shoulder_token"), 7);
  c.AddItem(items.at("gold_token"), 8);
  ShopPanel panel(c, equips, items);
  OpenTokenShelf(panel, kShopEquipsTab);

  EXPECT_EQ(CellColor(panel, "▲    7", "▲"), kTheme);
  EXPECT_EQ(CellColor(panel, "▲    8", "▲"), kGold)
      << "two balances, one glyph, and only the colour between them";
}

// Four digits with no separator: the panel has a fixed width, and a comma in
// the count would take a column the shop can't spare.
TEST_F(ShopPanelTest, ThePanelCountsWithoutCommas) {
  CharacterInstance c = MakeCharacter(100000, 140, JOB_FIGHTER, /*stage=*/2);
  c.AddItem(items_.at("secondary_token"), 4321);
  ShopPanel panel(c, equips_, items_);
  OpenTokenShelf(panel, kShopEquipsTab);
  EXPECT_GE(IndexWith(ScreenRows(panel), "● 4321"), 0);
}

// A shelf that uses one token still shows it on its Cost column.
TEST_F(ShopPanelTest, AShelfOfOneCurrencyMarksItsCostColumn) {
  CharacterInstance c = MakeCharacter(100000, 140, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips_, items_);
  OpenTokenShelf(panel, kShopWeaponTab);
  EXPECT_NE(Render(panel).find("● Cost"), std::string::npos);
}

// The Cost column is right-aligned in screen columns, so the header, a meso
// price and a token price all end in the same place, even though a coin is two
// columns and four bytes and a token's mark is one column and three bytes.
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

// Red marks the reason: a token price the player can't meet turns red, and the
// mark it is priced in doesn't, since a currency isn't a refusal.
TEST_F(ShopPanelTest, APriceNoTokenCanMeetIsRed) {
  CharacterInstance poor = MakeCharacter(100000, 130, JOB_FIGHTER, 2);
  ShopPanel panel(poor, equips_, items_);
  OpenTokenShelf(panel, kShopWeaponTab);
  EXPECT_EQ(CellColor(panel, "Frozen Axe", "9"), kRed);
  EXPECT_EQ(CellColor(panel, "Frozen Axe", "●"), kTheme)
      << "the mark is the currency, not the reason";

  CharacterInstance rich = MakeCharacter(100000, 130, JOB_FIGHTER, 2);
  rich.AddItem(items_.at("weapon_token"), 9);
  ShopPanel afford(rich, equips_, items_);
  OpenTokenShelf(afford, kShopWeaponTab);
  EXPECT_NE(CellColor(afford, "Frozen Axe", "9"), kRed);
}

// The pay row is drawn under every tab so the window keeps one height, but a
// blank one isn't a stop: Up from the Etc list reaches the tab bar itself.
TEST_F(ShopPanelTest, TheEtcTabHasNoPayRowToStandOn) {
  CharacterInstance c = MakeCharacter(100000, 30, JOB_FIGHTER, /*stage=*/2);
  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopEtcTab);
  panel.OnEvent(ftxui::Event::ArrowDown);  // tab bar to the first item
  ASSERT_NE(panel.selected_stackable(), nullptr);
  panel.OnEvent(ftxui::Event::ArrowUp);    // first item back to the tab bar
  panel.OnEvent(ftxui::Event::ArrowLeft);  // where Left works
  EXPECT_NE(panel.selected_item(), nullptr) << "back on the Secondary shelf";
}

// The shop is centred, so a row that came and went with the tab would move the
// whole window up the screen.
TEST_F(ShopPanelTest, TheWindowIsOneHeightUnderEveryTab) {
  ftxui::Element weapon = shop_.Render();
  weapon->ComputeRequirement();
  int rows = weapon->requirement().min_y;
  for (int tab = 0; tab < kNumShopTabs; ++tab) {
    ShopPanel other(shopper_, equips_, items_);
    OpenShelf(other, static_cast<ShopTab>(tab));
    ftxui::Element element = other.Render();
    element->ComputeRequirement();
    EXPECT_EQ(element->requirement().min_y, rows) << "tab " << tab;
  }
}

// In the list, Left and Right don't switch tabs: changing the list while the
// player moves through it would lose their place.
TEST_F(ShopPanelTest, TheListIgnoresLeftAndRight) {
  EXPECT_FALSE(shop_.OnEvent(ftxui::Event::ArrowRight));
  EXPECT_NE(shop_.selected_item(), nullptr);
}

TEST_F(ShopPanelTest, TheEtcShelfShowsHowManyAreOwned) {
  CharacterInstance c = MakeCharacter(100000);
  c.AddItem(items_.at("spell_trace"), 1234);
  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopEtcTab);
  EXPECT_NE(Render(panel).find("1,234"), std::string::npos);
}

// --- the buy-back shelf ---

// The shelf has both kinds of item at once, so the columns must show an equip
// and a stack without either looking like the other.
TEST_F(ShopPanelTest, TheBuyBackShelfShowsBothKindsOfRow) {
  CharacterInstance c = MakeCharacter(100000);
  EquipPrototype sword = MakeItem("Gladius", 30, 20000);
  sword.set_sell_price(2000);
  ItemPrototype shell = MakeStackable("Green Snail Shell", 0, 200);
  shell.set_sell_price(7);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  c.SellEquip(0);
  c.AddItem(shell, 40);
  c.SellStackable(0, 40);

  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopBuyBackTab);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Qty"), std::string::npos);
  // The stack shows its count; the equip, being one item, shows none. Each is
  // priced at what one of it sold for.
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
  OpenShelf(shop_, kShopBuyBackTab);
  EXPECT_NE(Render(shop_).find("(empty)"), std::string::npos);
  EXPECT_EQ(shop_.selected_buy_back(), nullptr);
}

// The shelf is the player's own history, so nothing filters it. A weapon of
// another class or one they have outgrown is still theirs to buy back.
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

// Exactly one of the three "what is selected" functions returns something, or a
// caller asking all three would act on the wrong one.
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

  // Stepped back rather than opened again, because OpenShelf steps right from
  // wherever the cursor is, and it is on the last tab.
  for (int i = 0; i < kShopBuyBackTab; ++i) {
    panel.OnEvent(ftxui::Event::ArrowLeft);
  }
  EXPECT_NE(panel.selected_item(), nullptr);
  EXPECT_EQ(panel.selected_buy_back(), nullptr);
}

// A sold trace must be labelled as a trace, or it would look like the working
// item it is the remains of.
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

// The menu leads to Inspect and Buy, and the shelf offers both.
TEST_F(ShopPanelTest, TheMenuOpensOnAShelfRow) {
  CharacterInstance c = MakeCharacter(100000);
  EquipPrototype sword = MakeItem("Gladius", 30, 20000);
  sword.set_sell_price(2000);
  c.PickUp(std::make_unique<EquipInstance>(sword));
  c.SellEquip(0);

  ShopPanel panel(c, equips_, items_);
  OpenShelf(panel, kShopBuyBackTab);
  panel.OnEvent(ftxui::Event::ArrowDown);  // the bar to the one row
  panel.OpenMenu();
  EXPECT_TRUE(panel.menu_open());
}

// The shelf has names half again as long as its name column (the magician books
// reach 32 characters). A name is cut to the column and scrolls while its row
// is selected, instead of widening the window or being lost.
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
  OpenShelf(shop_, kShopEtcTab);
  ASSERT_NE(shop_.selected_stackable(), nullptr);
  shop_.Reset();
  EXPECT_NE(shop_.selected_item(), nullptr);
  EXPECT_EQ(shop_.selected_stackable(), nullptr);
}

// Every shelf: the token shelves have a balance panel beside the window, and
// the widest row on the shelf sets the width of the whole screen.
TEST_F(ShopPanelTest, NoShelfWeldsARowToTheRightBorder) {
  CharacterInstance c = MakeCharacter(34567, /*level=*/200);
  ShopPanel panel(c, equips_, items_);
  for (int tab = 0; tab < kNumShopTabs; ++tab) {
    panel.Reset();
    panel.OnEvent(ftxui::Event::ArrowUp);
    panel.OnEvent(ftxui::Event::ArrowUp);
    for (int step = 0; step < tab; ++step) {
      panel.OnEvent(ftxui::Event::ArrowRight);
    }
    EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty()) << tab;
  }
}
}  // namespace
}  // namespace ms
