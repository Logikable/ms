/* BuyPanel is the dialog for buying copies of one shop item, the reverse of the
 * sell dialog. It shows the item name and the per-item and total cost above a
 * shared AmountSelector.
 *
 * Two things differ from selling. The quantity starts at one instead of the
 * whole amount, because a shopper picks a number rather than "as many as I can
 * afford"; [MAX] is still there for that. And the total is a cost rather than a
 * gain, so it turns red and disables Confirm once it exceeds what the player
 * has.
 *
 * The panel holds no game state: Reset() sets the item's price and the balance
 * to count against, quantity() reports the chosen amount, and OnEvent returns
 * the ConfirmChoice every dialog returns.
 *
 * A cap of zero (nothing affordable, or no room) draws a red reason under the
 * total, since otherwise the dialog only shows "0" and the player has to guess
 * which it was.
 *
 * A price isn't always in meso. An item on the shop's token shelf is priced in
 * its token, and the dialog counts in that instead: the same arithmetic against
 * a different balance, with the token's own mark.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BUY_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BUY_PANEL_H_

#include <cstdint>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/amount_selector.h"
#include "src/protos/item.pb.h"

namespace ms {

class BuyPanel {
 public:
  // The most that can be bought at once, whatever the balance and bag allow. A
  // spell trace stacks to 30,000 and is bought by the stack, so the limit has
  // to allow a full one.
  static constexpr int kMaxQuantity = 30000;

  // Sets up the panel for buying `item_name` at `unit_price` each against
  // `balance`, with `room` in the bag and `owned` already held. `token` is the
  // currency, or nullptr for meso. The quantity starts at one and is capped by
  // whichever limit is lowest, so the shop is never offered a number it would
  // refuse.
  void Reset(const std::string& item_name, int unit_price, int64_t balance,
             int room, int owned, const ItemPrototype* token = nullptr);
  ftxui::Element Render() const;
  ConfirmChoice OnEvent(ftxui::Event event);
  int quantity() const {
    return selector_.value();
  }

 private:
  // What the current quantity would cost, in whichever currency it is priced.
  int64_t total() const;
  // Why the dialog can offer nothing, or "" while it can offer something. A cap
  // of zero looks the same whichever limit caused it, so this row is the only
  // thing telling a full bag from an empty purse.
  std::string Reason() const;
  // One amount as the dialog draws it: the currency mark, then the number,
  // which turns red when the player can't pay.
  ftxui::Element Amount(int64_t value, bool red) const;
  // Whether the player could actually buy the current quantity: at least one,
  // and within the balance.
  bool Affordable() const;

  std::string item_name_;
  int unit_price_ = 0;
  int owned_ = 0;
  int room_ = 0;
  // The most this dialog can confirm, which is zero when a limit has closed it.
  // Kept because the reason row checks it.
  int cap_ = 0;
  int64_t balance_ = 0;
  // The catalog outlives every dialog, so the panel keeps the prototype instead
  // of a copy of its mark and colour.
  const ItemPrototype* token_ = nullptr;
  AmountSelector selector_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BUY_PANEL_H_
