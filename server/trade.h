/* Every trade in progress: who is trading with whom, what each side has put
 * up, and the rules for opening one and walking out.
 *
 * A trade opens when one player asks for it and the other asks back. The one
 * who was asked is told by a notification, which is all this holds of them
 * until they answer -- there is no declining, only asking back or leaving the
 * asker to give up.
 *
 * Nothing here knows about sockets, and it cannot see a fight either: the
 * server passes that in, a player in one being as busy as a player already
 * trading. Like the lobby, the caller asks whether something happened and
 * afterwards takes the list of who has to be told.
 */
#ifndef MS_SERVER_TRADE_H_
#define MS_SERVER_TRADE_H_

#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/protos/multiplayer.pb.h"

namespace ms {

// What an ask came to. A refusal carries a reason for the client to act on and
// a sentence fit to show the player.
struct TradeResult {
  bool ok = false;
  Refused::Reason reason = Refused::REASON_UNSPECIFIED;
  std::string message;
};

// A player who has to be shown something the trade state cannot say on its
// own: being asked to trade, which reaches them wherever they are.
struct TradeNotice {
  std::string account_id;
  Notification notification;
};

// One side of a trade that went through, and what it pays them. The trade is
// gone by the time this is taken.
struct TradeCompletion {
  std::string account_id;
  TradeOffer received;
};

class Trades {
 public:
  // `seed` fixes the stream trade ids are drawn from.
  explicit Trades(unsigned int seed);

  // Opens a trade between the two, or joins the one `to` has already asked
  // `from` into. `to_in_fight` is what the trade desk cannot see for itself.
  TradeResult Request(const PlayerInfo& from, const PlayerInfo& to,
                      bool to_in_fight);
  // Puts up what `account_id` is offering, whole. Clears both acceptances:
  // what either side agreed to was the table as it stood. Quiet about a
  // player who is in no trade.
  void SetOffer(const std::string& account_id, const TradeOffer& offer);
  // Accepts the table as it stands, or takes that back.
  void SetAccept(const std::string& account_id, bool accepted);
  // Answers the finalize dialog. The trade goes through once both sides have
  // confirmed; a cancel clears this player's acceptance too, which is what
  // takes the dialog down for both of them.
  void SetConfirm(const std::string& account_id, bool confirmed);
  // Ends whatever trade `account_id` is in, for both of them. Quiet about a
  // player who is in none, which is what leaving a screen twice looks like.
  void Leave(const std::string& account_id);

  // Whether this player cannot be asked: they are trading, or they have been
  // asked and have not answered. One ask at a time, so an answer is never
  // ambiguous about which trade it means.
  bool Busy(const std::string& account_id) const;

  // The trade `account_id` is in, from their side. One with no id means they
  // are in none, which is what a player who has walked out, or was never in
  // one, is sent.
  TradeState StateFor(const std::string& account_id) const;

  // Accounts whose trade changed under them and need telling, cleared by the
  // taking. A player whose trade ended is in here too: what they need telling
  // is that they are in nothing.
  std::vector<std::string> TakeChanged();
  // What individual players have to be shown, cleared by the taking. Separate
  // from the above because an ask reaches a player who is in no trade yet and
  // has nothing to draw.
  std::vector<TradeNotice> TakeNotices();
  // The trades that went through, each side its own entry, cleared by the
  // taking. A completed trade is NOT in TakeChanged: its players are told by
  // this, and an empty state on top of it would read as a partner walking out.
  std::vector<TradeCompletion> TakeCompletions();

  int trade_count() const {
    return static_cast<int>(trades_.size());
  }

 private:
  // One trade. `opener` asked and `partner` was asked; until the partner asks
  // back, only the opener has a screen open on it.
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

  // Which side of `record` `account_id` is on, and the flags that side owns.
  static bool IsOpener(const Record& record, const std::string& account_id);
  // Puts the table back to nobody having agreed to it, both sides. Called
  // whenever what is on it changes.
  static void ClearAgreement(Record& record);
  // Pays both sides and tears the trade down. `record` must be one both sides
  // have confirmed.
  void Complete(Record& record);

  // The trade `account_id` is in, or null.
  Record* Find(const std::string& account_id);
  const Record* Find(const std::string& account_id) const;
  // The other side of `record` from `account_id`.
  static const std::string& Other(const Record& record,
                                  const std::string& account_id);
  // Notes that both sides of `record` have to be told.
  void NoteChanged(const Record& record);
  std::string NewTradeId();

  // By trade id, and who is in which. The index is what makes leaving one
  // lookup rather than a search through every trade.
  std::map<std::string, Record> trades_;
  std::map<std::string, std::string> trade_of_;
  std::vector<std::string> changed_;
  std::vector<TradeNotice> notices_;
  std::vector<TradeCompletion> completions_;
  std::mt19937 rng_;
};

}  // namespace ms

#endif  // MS_SERVER_TRADE_H_
