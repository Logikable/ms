/* ShopPanel is the full-screen shop, reached from the Inventory's Shop tab: a
 * row of tabs over a Name / Cost list of what is for sale, with the player's
 * meso beside the title for comparing prices.
 *
 * The list has only what this character's class can use. Items are left out
 * because the player could never use them, not because they can't use them yet:
 * an item above their level still shows, in red, as something to save for.
 *
 * Four tabs: Weapon, Equips for everything else worn, Etc for stackables, and
 * the player's own buyback shelf. They are separate lists rather than more rows
 * of one list, which is what tabs are for. The bar stops at its ends, as in the
 * bag, and Left and Right move along it while the cursor is on it.
 *
 * Under the first two tabs is a second row, Meso and Token, because the same
 * shelf is stocked twice: what meso buys, and what tokens dropped by mobs buy.
 * The row is drawn under every tab, blank where there is nothing to choose, so
 * the window keeps one height.
 *
 * A narrow panel on the right shows the balances for a token shelf, one
 * currency per row. They don't go in the tab bar: the equipment shelf uses
 * seven currencies, and five of them share a glyph in different colours. Its
 * columns are reserved under every tab, blank where the shelf uses meso, so
 * moving along the pay bar doesn't shift the centred window sideways.
 *
 * The off-hands on the Equips shelf are hidden until the 2nd advancement, since
 * each belongs to one branch of a job and a 1st job isn't in a branch yet. The
 * accessories beside them fit anyone, so the shelf is never empty.
 *
 * The panel only displays: it moves its own cursor but never spends anything.
 * The controller reads selected_item() when the player presses Enter.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SHOP_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SHOP_PANEL_H_

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/frontend/widgets/marquee.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

// The shop's tabs, in display order.
enum ShopTab : int {
  kShopWeaponTab = 0,
  kShopEquipsTab = 1,
  kShopEtcTab = 2,
  // The player's own last 32 sales, bought back at what they sold for. Last
  // because it is the only shelf the shop didn't stock itself.
  kShopBuyBackTab = 3,
  kNumShopTabs = 4,
};

// The second row of tabs, which says what currency the shelf above uses. Only
// the Weapon and Equips tabs have one; the Etc shelf and the buyback shelf use
// only meso.
enum ShopPayTab : int {
  kShopMesoTab = 0,
  kShopTokenTab = 1,
  kNumShopPayTabs = 2,
};

// The entries of an item's context menu, in display order.
enum ShopMenuItem : int {
  kShopMenuInspect = 0,
  kShopMenuBuy = 1,
  kShopMenuClose = 2,
  kNumShopMenuItems = 3,
};

class ShopPanel {
 public:
  ShopPanel(const CharacterInstance& character,
            const std::map<std::string, EquipPrototype>& equips,
            const std::map<std::string, ItemPrototype>& items);
  // The catalogs are held by reference, so a temporary one would dangle.
  ShopPanel(const CharacterInstance& character,
            std::map<std::string, EquipPrototype>&& equips,
            const std::map<std::string, ItemPrototype>& items) = delete;
  ShopPanel(const CharacterInstance& character,
            const std::map<std::string, EquipPrototype>& equips,
            std::map<std::string, ItemPrototype>&& items) = delete;

  // Restocks and puts the cursor back on the first item. Call when the screen
  // opens, since the stock depends on the character's class. The cursor starts
  // in the list, since the player came to buy something and the bar is one key
  // away.
  void Reset();
  ftxui::Element Render() const;
  // Handles Up and Down along the list. Enter and Escape are left to the
  // caller, which owns the screen one opens and the other closes. Returns true
  // if the event was consumed.
  bool OnEvent(ftxui::Event event);
  // The equip under the cursor, or nullptr when the open tab isn't an equipment
  // tab or is empty. Exactly one of this, selected_stackable() and
  // selected_buy_back() returns something, so a caller can ask each and act on
  // whichever does.
  const EquipPrototype* selected_item() const;
  // The stackable under the cursor, on the same terms.
  const ItemPrototype* selected_stackable() const;
  // The buyback row under the cursor, on the same terms.
  const BuyBackEntry* selected_buy_back() const;
  // The token the selected item is bought with, or nullptr when the open shelf
  // uses meso. This tells a caller how to charge for the row selected_item()
  // names.
  const ItemPrototype* selected_token() const;
  // The row's position on the shelf, which BuyBack takes. The cursor position
  // is the shelf position, since the tab lists the whole shelf.
  int selected_row() const {
    return selected_;
  }

  // Opens the context menu on the selected item. Does nothing while the tab bar
  // has the cursor, or when the shop has nothing to open it on.
  void OpenMenu();
  bool menu_open() const;
  // Handles the context menu and returns the next screen: kShopMenu while it
  // stays open, kShopInspect or kShopBuy for the chosen entry, and kShop once
  // it closes. The menu closes itself on the way out, so a caller returning to
  // kShop finds the list as it was.
  Screen OnMenuEvent(ftxui::Event event);

 private:
  // The vertical focus zones, as in the bag: the two tab rows on top and the
  // stock list below. They form one ring: Up from the first item reaches the
  // bars, and Up from the top bar reaches the last item.
  enum Zone { kZoneTabs, kZonePay, kZoneList };

  // Whether the open tab has a second row of tabs to move onto. The row is
  // drawn either way so the window keeps one height, but an empty one isn't a
  // stop.
  bool HasPayRow() const;
  // The cursor's position in that ring: the tab bar is stop 0, the pay bar stop
  // 1 where there is one, and the stock rows are the stops after that.
  int CursorStop() const;
  // Moves the cursor `delta` stops around the ring, including the tab bar.
  void MoveCursor(int delta);
  // Moves one tab along whichever bar has the cursor and restocks. The ends
  // stop, as in the bag: stepping past the last tab does nothing.
  void StepTab(int direction);
  void StepPayTab(int direction);
  // Fills stock_ from the open tab.
  void Restock();
  // The number of rows in the open tab. The three shop shelves use stock_; the
  // buyback tab reads the character's shelf directly, since it has no catalog
  // keys.
  int RowCount() const;
  // Scrolls the window to the cursor with ScrollWindowStart, which keeps the
  // selection in the middle.
  void ScrollToCursor();
  // The tab chips, with the player's meso in the space they leave.
  ftxui::Element RenderTabBar() const;
  // The balances beside the shop, as tall as the window and blank under a shelf
  // that uses meso. See the note at the top of the file.
  ftxui::Element RenderTokenPanel() const;
  // One balance row of that panel: the currency's mark and the count.
  ftxui::Element RenderTokenBalance(const ItemPrototype& token) const;
  // The second row: Meso and Token, or a blank row under a tab that has
  // neither. Blank rather than absent, so the window keeps one height.
  ftxui::Element RenderPayBar() const;
  // The token a row is bought with, or nullptr for a row priced in meso.
  const ItemPrototype* RowToken(const EquipPrototype& proto) const;
  // Every currency the open shelf uses, in shelf order, or empty for a meso
  // shelf. Read from the shelf itself rather than the slot, so a later tier
  // that splits its tokens differently needs no change here. Works even while
  // the class filter leaves the visible list empty.
  std::vector<const ItemPrototype*> TabTokens() const;
  // One stock row. `cursor` is the two-column gutter the cursor is drawn in,
  // and `elapsed` is how long this row has been selected: zero for an
  // unselected row, which shows every other name from its start.
  ftxui::Element RenderEtcRow(
      const ItemPrototype& item, const std::string& cursor,
      std::chrono::steady_clock::duration elapsed) const;
  ftxui::Element RenderEquipRow(
      const EquipPrototype& proto, const std::string& cursor,
      std::chrono::steady_clock::duration elapsed) const;
  // One buyback row. A stackable fills the quantity column, and an equip, being
  // a single item, leaves it blank. Both are priced at what the sale paid for
  // one.
  ftxui::Element RenderBuyBackRow(
      const BuyBackEntry& entry, const std::string& cursor,
      std::chrono::steady_clock::duration elapsed) const;
  // The rows on screen with the scroll bar beside them, or "(empty)" over blank
  // rows when the shelf is empty. Always kVisibleRows tall: the panel is
  // centred, so a block that shrank with the list would move the whole window
  // up the screen.
  ftxui::Element RenderStock() const;

  const CharacterInstance& character_;
  const std::map<std::string, EquipPrototype>& equips_;
  const std::map<std::string, ItemPrototype>& items_;
  int tab_ = kShopWeaponTab;
  int pay_ = kShopMesoTab;
  // The catalog keys of the stock, in display order. Rebuilt by Reset(), the
  // only thing that changes it; buying doesn't.
  std::vector<std::string> stock_;
  Zone zone_ = kZoneList;
  int selected_ = 0;
  // The stock row at the top of the window. The list is longer than the screen,
  // so the panel keeps its own offset instead of using ftxui's yframe, because
  // the context menu is placed at a row number, and only an offset the panel
  // owns can give one.
  int first_visible_ = 0;
  ItemMenu menu_;
  bool menu_open_ = false;
  // How long the cursor has been on its row, for scrolling a long name. Mutable
  // because the render is where the move is noticed.
  mutable SelectionClock name_clock_;

  // The row the context menu opens at: the cursor's row within the visible
  // window.
  int MenuRow() const;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SHOP_PANEL_H_
