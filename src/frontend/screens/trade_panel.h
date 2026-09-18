/* TradePanel is the screen two players put things on: your offer at the top
 * left, theirs at the top right, your bag along the bottom.
 *
 * Their window wears no name until they have joined -- one player is on this
 * screen first, every time, and an empty window under their name would read as
 * a trade they had already refused.
 *
 * Each offer leads with the two currencies on one line, drawn with the bag's
 * own marks and left aligned: a trade is read as "what is on this side", so
 * the numbers start where the eye already is and the room is left to the
 * right. The cursor walks your own two; theirs is only ever read.
 *
 * The panel is a view. It draws the trade it was last handed and asks the
 * connection for nothing: the controller reads what the cursor is on and does
 * the asking.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_TRADE_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_TRADE_PANEL_H_

#include <chrono>
#include <cstdint>

#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// What a side of a trade can hold. Items are not tradeable yet.
enum class TradeCurrency {
  kMeso,
  kSpellTraces,
};

class TradePanel {
 public:
  TradePanel(const CharacterInstance& character,
             const AccountInstance& account);

  // The trade as the panel should draw it. Handed in every frame.
  void SetTrade(const TradeState& trade);
  // Puts the cursor back on the first currency. Call when the screen opens.
  void Reset();
  // Moves the cursor `delta` stops along your own currency row, coming out
  // the other end.
  void MoveCursor(int delta);
  ftxui::Element Render() const;

  TradeCurrency selected() const;
  // What the cursor's currency is worth in your own hands, and how much of it
  // is already on the table. What the amount overlay opens on.
  int64_t held() const;
  int64_t offered() const;

 private:
  // One side's window: the currency line, and the room items will take.
  ftxui::Element RenderOffer(const TradeOffer& offer, const std::string& title,
                             bool mine) const;
  // The two currencies on one line, the cursor's on the selection band.
  ftxui::Element RenderCurrencies(const TradeOffer& offer, bool mine) const;
  // The bag along the bottom, read-only: what is tradeable is not settled, so
  // nothing here takes a cursor.
  ftxui::Element RenderBag() const;

  const CharacterInstance& character_;
  const AccountInstance& account_;
  TradeState trade_;
  // Where the cursor stands on your own currencies, as a TradeCurrency.
  int cursor_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_TRADE_PANEL_H_
