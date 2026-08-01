/* ShopPanel is the full-screen buying screen, reached from the Shop tab of the
 * inventory. A row of tabs over a Name / Cost list of everything on sale, with
 * the player's meso beside the title so the price column has something to be
 * read against.
 *
 * Only the Equips tab exists so far. The tab row is here rather than waiting
 * for the second tab because the stock is already split by what it goes in --
 * a Use tab is a different list, not more rows of this one.
 *
 * The panel is a view: it moves its own cursor but never spends anything. The
 * controller reads selected_item() when the player presses Enter.
 */
#ifndef MS_SRC_FRONTEND_SHOP_PANEL_H_
#define MS_SRC_FRONTEND_SHOP_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Tabs of the shop, in display order.
enum ShopTab : int {
  kShopEquipsTab = 0,
  kNumShopTabs = 1,
};

class ShopPanel {
 public:
  ShopPanel(const CharacterInstance& character,
            const std::map<std::string, EquipPrototype>& equips);

  // Puts the cursor back on the first item. Call when the screen opens.
  void Reset();
  ftxui::Element Render() const;
  // Handles Up/Down along the list. Enter and Escape are left to the caller,
  // which owns the screen the one opens and the other closes. Returns true if
  // the event was consumed.
  bool OnEvent(ftxui::Event event);
  // The prototype under the cursor, or nullptr when the shop is empty.
  const EquipPrototype* selected_item() const;

 private:
  const CharacterInstance& character_;
  const std::map<std::string, EquipPrototype>& equips_;
  // Catalog keys of the stock, in display order. Fixed at construction: what
  // the shop sells does not change as the player buys.
  std::vector<std::string> stock_;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SHOP_PANEL_H_
