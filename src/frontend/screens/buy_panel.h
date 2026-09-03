/* BuyPanel is the modal for buying copies of a single shop item -- the sell
 * dialog seen from the other side of the counter. It shows the item name and
 * the per-item and total cost above a shared AmountSelector.
 *
 * Two things differ from selling. The quantity opens at one rather than at the
 * whole amount, because a shopper picks a number rather than reaching for "as
 * many as I can afford" -- [MAX] is still there for the player who is. And the
 * total is a cost rather than a gain, so it turns red and takes Confirm down
 * with it once it passes what the player holds.
 *
 * The panel owns no game state: Reset() seeds it with the item's price and the
 * balance it is counted against, quantity() reports the chosen amount, and
 * OnEvent answers with the ConfirmChoice every dialog answers with.
 *
 * A cap of zero -- nothing affordable, or nowhere to put it -- draws a red
 * reason under the total, since the dialog otherwise says only "0" and leaves
 * the player to guess which of the two it was.
 *
 * A price is not always meso. An item off the shop's token shelf is priced in
 * the token it names, and the dialog then counts in that instead -- the same
 * arithmetic against a different balance, with the token's own mark on it.
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
  // The most that can be bought in one go, whatever the balance and the bag
  // allow. A spell trace stacks to 30,000 and is bought by the stack, so the
  // ceiling has to clear a full one.
  static constexpr int kMaxQuantity = 30000;

  // Seeds the panel for buying `item_name` at `unit_price` each, against a
  // balance of `balance`, with `room` copies' worth of space left in the bag
  // and `owned` copies already to the player's name. `token` is the currency
  // the price is asked in, or nullptr for meso.
  //
  // Quantity opens at one and is capped by whichever of the three ceilings
  // bites first: the balance, the room, and kMaxQuantity. The field will not
  // go past the cap, so the shop is never offered a number it would refuse.
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
  // Why the dialog can offer nothing, or "" while it can offer something. A
  // cap of zero is the same dead end whichever ceiling closed it, so the row
  // that says which is the only thing telling a full bag from an empty purse.
  std::string Reason() const;
  // One amount as the dialog draws it: the mark, then the number, which is the
  // half that reddens when the player cannot pay it.
  ftxui::Element Amount(int64_t value, bool red) const;
  // Whether the current quantity is one the player could actually go through
  // with: at least one, and within the balance.
  bool Affordable() const;

  std::string item_name_;
  int unit_price_ = 0;
  int owned_ = 0;
  int room_ = 0;
  // The most this dialog may be confirmed for, which is zero when a ceiling
  // has closed it. Kept because the reason row asks whether one has.
  int cap_ = 0;
  int64_t balance_ = 0;
  // The catalog outlives every dialog, so the panel holds the prototype rather
  // than a copy of its mark and colour.
  const ItemPrototype* token_ = nullptr;
  AmountSelector selector_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BUY_PANEL_H_
