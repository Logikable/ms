/* BuyPanel is the modal for buying copies of a single shop item -- the sell
 * dialog seen from the other side of the counter. It shows the item name and
 * the per-item and total cost above a shared AmountSelector.
 *
 * Two things differ from selling. The quantity opens at one rather than at the
 * whole amount, without the [1]/[MAX] shortcuts, because a shopper picks a
 * number rather than reaching for "as many as I can afford". And the total is
 * a cost rather than a gain, so it turns red and takes Confirm down with it
 * once it passes what the player holds.
 *
 * The panel owns no game state: Reset() seeds it with the item's price and the
 * player's meso, quantity() reports the chosen amount, and TakeConfirmed() /
 * TakeCancelled() each return true once.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BUY_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BUY_PANEL_H_

#include <cstdint>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/amount_selector.h"

namespace ms {

class BuyPanel {
 public:
  // The most that can be bought in one go, whatever the balance and the bag
  // allow. Four digits is as much as the field is meant to take.
  static constexpr int kMaxQuantity = 9999;

  // Seeds the panel for buying `item_name` at `unit_price` meso each, against
  // a balance of `meso`, with `room` copies' worth of space left in the bag
  // and `owned` copies already to the player's name.
  //
  // Quantity opens at one and is capped by whichever of the three ceilings
  // bites first: the balance, the room, and kMaxQuantity. The field will not
  // go past the cap, so the shop is never offered a number it would refuse.
  void Reset(const std::string& item_name, int unit_price, int64_t meso,
             int room, int owned);
  ftxui::Element Render() const;
  bool OnEvent(ftxui::Event event);
  int quantity() const {
    return selector_.value();
  }
  bool TakeConfirmed();
  bool TakeCancelled();

 private:
  // Meso the current quantity would cost.
  int64_t total() const;
  // Whether the current quantity is one the player could actually go through
  // with: at least one, and within the balance.
  bool Affordable() const;

  std::string item_name_;
  int unit_price_ = 0;
  int owned_ = 0;
  int64_t meso_ = 0;
  AmountSelector selector_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BUY_PANEL_H_
