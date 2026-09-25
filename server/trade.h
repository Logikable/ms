/* Tracks every trade in progress: who is trading, what each side offers, and
 * the rules for opening and leaving a trade.
 *
 * A trade opens when one player requests it and the other requests back. There
 * is no decline. The server passes in whether a player is fighting.
 */
#ifndef MS_SERVER_TRADE_H_
#define MS_SERVER_TRADE_H_

#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/protos/multiplayer.pb.h"

namespace ms {

// The outcome of a request. A refusal carries a reason code for the client
// and a message to show the player.
struct TradeResult {
  bool ok = false;
  Refused::Reason reason = Refused::REASON_UNSPECIFIED;
  std::string message;
};

// A trade request notification for a player who is not in the trade yet.
struct TradeNotice {
  std::string account_id;
  Notification notification;
};

// One side of a completed trade and what they receive. The trade itself is
// already gone.
struct TradeCompletion {
  std::string account_id;
  TradeOffer received;
};

class Trades {
 public:
  // `seed` seeds the trade id generator.
  explicit Trades(unsigned int seed);

  // Opens a trade between the two, or joins one `to` already requested with
  // `from`. `to_in_fight` says whether `to` is in a fight, which makes them
  // busy.
  TradeResult Request(const PlayerInfo& from, const PlayerInfo& to,
                      bool to_in_fight);
  // Replaces `account_id`'s whole offer and clears both sides' acceptance,
  // since they agreed to the old offers. Does nothing outside a trade.
  void SetOffer(const std::string& account_id, const TradeOffer& offer);
  // Accepts the current offers, or withdraws acceptance.
  void SetAccept(const std::string& account_id, bool accepted);
  // Answers the finalize dialog. The trade completes once both sides confirm.
  // Cancelling also clears this player's acceptance, which closes the dialog
  // for both.
  void SetConfirm(const std::string& account_id, bool confirmed);
  // Ends `account_id`'s trade for both sides. Does nothing if they are in no
  // trade, which happens when a client leaves the screen twice.
  void Leave(const std::string& account_id);

  // Returns whether this player is trading or has an unanswered request. A
  // player gets one request at a time, so their answer is never ambiguous.
  bool Busy(const std::string& account_id) const;

  // Returns `account_id`'s trade from their side. A state with no id means
  // they are in no trade.
  TradeState StateFor(const std::string& account_id) const;

  // Returns and clears the accounts whose trade changed. Players whose trade
  // ended are included, so they learn they are in no trade.
  std::vector<std::string> TakeChanged();
  // Returns and clears pending notifications. These are separate because a
  // request reaches a player who has no trade screen open yet.
  std::vector<TradeNotice> TakeNotices();
  // Returns and clears completed trades, one entry per side. Completed trades
  // are left out of TakeChanged, because an empty state would look to the
  // client like the partner leaving.
  std::vector<TradeCompletion> TakeCompletions();

  int trade_count() const {
    return static_cast<int>(trades_.size());
  }

 private:
  // One trade. `opener` sent the request to `partner`. Until the partner
  // requests back (`joined`), only the opener has the trade screen open.
  struct Record {
    std::string id;
    std::string opener;
    std::string opener_name;
    std::string partner;
    std::string partner_name;
    bool joined = false;
    TradeOffer opener_offer;
    TradeOffer partner_offer;
    bool opener_accepted = false;
    bool partner_accepted = false;
    bool opener_confirmed = false;
    bool partner_confirmed = false;
  };

  // Returns whether `account_id` opened `record`.
  static bool IsOpener(const Record& record, const std::string& account_id);
  // Clears both sides' accept and confirm flags. Called whenever an offer
  // changes.
  static void ClearAgreement(Record& record);
  // Records both sides' payouts and removes the trade. Both sides must have
  // confirmed.
  void Complete(Record& record);

  // Returns `account_id`'s trade, or null.
  Record* Find(const std::string& account_id);
  const Record* Find(const std::string& account_id) const;
  // Returns the other player in `record`.
  static const std::string& Other(const Record& record,
                                  const std::string& account_id);
  // Marks both sides of `record` for an update, or only the opener if the
  // partner has not joined.
  void NoteChanged(const Record& record);
  // Removes `account_id` from the update list. Used after a trade completes,
  // when the player should get the completion instead of an empty state.
  void DropChanged(const std::string& account_id);
  std::string NewTradeId();

  // Trades by id, and each account's trade id so lookups need no search.
  std::map<std::string, Record> trades_;
  std::map<std::string, std::string> trade_of_;
  std::vector<std::string> changed_;
  std::vector<TradeNotice> notices_;
  std::vector<TradeCompletion> completions_;
  std::mt19937 rng_;
};

}  // namespace ms

#endif  // MS_SERVER_TRADE_H_
