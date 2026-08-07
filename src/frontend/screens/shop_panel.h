/* ShopPanel is the full-screen buying screen, reached from the Shop tab of the
 * inventory. A row of tabs over a Name / Cost list of what is on sale, with the
 * player's meso beside the title so the price column has something to be read
 * against.
 *
 * The list holds only what this character's class can hold. What is left out is
 * left out because they could never use it, not because they cannot use it yet
 * -- a level too high still shows, in red, as something to save toward.
 *
 * Only the Weapons tab exists so far, weapons being all the shop sells. The tab
 * row is here rather than waiting for the second tab because armour or potions
 * would be a different list, not more rows of this one. The bar is already a
 * stop the cursor can stand on, so a second tab is a tab and nothing else.
 *
 * The panel is a view: it moves its own cursor but never spends anything. The
 * controller reads selected_item() when the player presses Enter.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SHOP_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SHOP_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Tabs of the shop, in display order.
enum ShopTab : int {
  kShopWeaponsTab = 0,
  kNumShopTabs = 1,
};

// Entries of the context menu an item opens, in display order.
enum ShopMenuItem : int {
  kShopMenuInspect = 0,
  kShopMenuBuy = 1,
  kShopMenuClose = 2,
  kNumShopMenuItems = 3,
};

class ShopPanel {
 public:
  ShopPanel(const CharacterInstance& character,
            const std::map<std::string, EquipPrototype>& equips);

  // Restocks and puts the cursor back on the first item. Call when the screen
  // opens: the stock depends on the character's class, so a job advancement
  // between two visits changes it. The cursor lands in the list rather than on
  // the tab bar -- the shop is a screen the player came to in order to buy
  // something, and the bar is one key away.
  void Reset();
  ftxui::Element Render() const;
  // Handles Up/Down along the list. Enter and Escape are left to the caller,
  // which owns the screen the one opens and the other closes. Returns true if
  // the event was consumed.
  bool OnEvent(ftxui::Event event);
  // The prototype under the cursor, or nullptr when the shop is empty.
  const EquipPrototype* selected_item() const;

  // Opens the context menu over the selected item. Does nothing while the tab
  // bar holds the cursor, or when the shop has nothing to open it on.
  void OpenMenu();
  bool menu_open() const;
  // Drives the context menu and says what should be on screen afterwards:
  // kShopMenu while it stays up, kShopInspect or kShopBuy for the entry the
  // player chose, kShop once it closes. The menu closes itself on the way out,
  // so a caller that returns to kShop finds the list as it left it.
  Screen OnMenuEvent(ftxui::Event event);

 private:
  // The two vertical focus zones, as in the bag: the tab row on top, the stock
  // list below. They are one ring -- Up off the first item reaches the bar, and
  // Up off the bar reaches the last item.
  enum Zone { kZoneTabs, kZoneList };

  // Where the cursor stands in that ring: the tab bar is stop 0 and the stock
  // rows are the stops after it.
  int CursorStop() const;
  // Moves the cursor `delta` stops around the ring, tab bar included.
  void MoveCursor(int delta);
  // Scrolls the window by as little as it takes to put the cursor back inside
  // it. Does nothing while the cursor is already on screen, which is most
  // moves.
  void ScrollToCursor();

  const CharacterInstance& character_;
  const std::map<std::string, EquipPrototype>& equips_;
  // Catalog keys of the stock, in display order. Rebuilt by Reset(), which is
  // the only thing that changes it -- buying does not.
  std::vector<std::string> stock_;
  Zone zone_ = kZoneList;
  int selected_ = 0;
  // The stock row drawn at the top of the window. The list is longer than the
  // screen, so the panel keeps its own offset rather than handing the cursor to
  // ftxui's yframe -- the context menu is anchored at a row number, and only an
  // offset the panel owns can be turned into one.
  int first_visible_ = 0;
  ItemMenu menu_;
  bool menu_open_ = false;

  // The row the context menu starts on, held back far enough that the menu
  // ends inside the window rather than stretching it.
  int MenuRow() const;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SHOP_PANEL_H_
