#include "server/trade.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "server/ids.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

// Characters in a trade id. Short, like a party's: it is read in log lines,
// and a handful of trades are ever open at once.
constexpr int kTradeIdCharacters = 8;

TradeResult Refusal(Refused::Reason reason, const std::string& message) {
  TradeResult result;
  result.reason = reason;
  result.message = message;
  return result;
}

TradeResult Done() {
  TradeResult result;
  result.ok = true;
  return result;
}

}  // namespace

Trades::Trades(unsigned int seed) : rng_(seed) {
}

TradeResult Trades::Request(const PlayerInfo& from, const PlayerInfo& to,
                            bool to_in_fight) {
  if (from.account_id() == to.account_id()) {
    return Refusal(Refused::REASON_BUSY, "You cannot trade with yourself.");
  }
  if (Record* mine = Find(from.account_id()); mine != nullptr) {
    if (Other(*mine, from.account_id()) != to.account_id()) {
      return Refusal(Refused::REASON_BUSY, "You are already trading.");
    }
    // Asking again for the trade they are already in, which is what the
    // player who was asked presses to answer.
    if (!mine->joined && mine->partner == from.account_id()) {
      mine->joined = true;
      NoteChanged(*mine);
    }
    return Done();
  }
  if (to_in_fight || Find(to.account_id()) != nullptr) {
    return Refusal(Refused::REASON_BUSY, "They're currently busy.");
  }

  Record record;
  record.id = NewTradeId();
  record.opener = from.account_id();
  record.opener_name = from.name();
  record.partner = to.account_id();
  record.partner_name = to.name();

  trade_of_[record.opener] = record.id;
  trade_of_[record.partner] = record.id;
  changed_.push_back(record.opener);

  TradeNotice notice;
  notice.account_id = record.partner;
  notice.notification.add_lines("Trade request from");
  notice.notification.add_lines(record.opener_name);
  notices_.push_back(std::move(notice));

  trades_[record.id] = std::move(record);
  return Done();
}

void Trades::SetOffer(const std::string& account_id, const TradeOffer& offer) {
  Record* record = Find(account_id);
  if (record == nullptr) {
    return;
  }
  if (IsOpener(*record, account_id)) {
    record->opener_offer = offer;
  } else {
    record->partner_offer = offer;
  }
  ClearAgreement(*record);
  NoteChanged(*record);
}

void Trades::SetAccept(const std::string& account_id, bool accepted) {
  Record* record = Find(account_id);
  if (record == nullptr) {
    return;
  }
  if (IsOpener(*record, account_id)) {
    record->opener_accepted = accepted;
  } else {
    record->partner_accepted = accepted;
  }
  NoteChanged(*record);
}

void Trades::SetConfirm(const std::string& account_id, bool confirmed) {
  Record* record = Find(account_id);
  if (record == nullptr) {
    return;
  }
  const bool opener = IsOpener(*record, account_id);
  if (!confirmed) {
    // Backing out takes this player's acceptance with it, which is what pulls
    // the dialog down on both sides. The other player keeps theirs, so one
    // keypress here puts it back up.
    (opener ? record->opener_accepted : record->partner_accepted) = false;
    record->opener_confirmed = false;
    record->partner_confirmed = false;
    NoteChanged(*record);
    return;
  }
  // Only a table both sides have agreed to can be confirmed: a confirm on any
  // other is a message about a dialog that is no longer up.
  if (!record->opener_accepted || !record->partner_accepted) {
    return;
  }
  (opener ? record->opener_confirmed : record->partner_confirmed) = true;
  if (record->opener_confirmed && record->partner_confirmed) {
    Complete(*record);
    return;
  }
  NoteChanged(*record);
}

void Trades::Leave(const std::string& account_id) {
  Record* record = Find(account_id);
  if (record == nullptr) {
    return;
  }
  // Taken before the trade goes, so the one leaving is told as well as the
  // one left behind.
  NoteChanged(*record);
  std::string id = record->id;
  trade_of_.erase(record->opener);
  trade_of_.erase(record->partner);
  trades_.erase(id);
}

bool Trades::Busy(const std::string& account_id) const {
  return Find(account_id) != nullptr;
}

TradeState Trades::StateFor(const std::string& account_id) const {
  TradeState state;
  const Record* record = Find(account_id);
  if (record == nullptr) {
    return state;
  }
  state.set_id(record->id);
  state.set_partner_joined(record->joined);
  if (IsOpener(*record, account_id)) {
    state.set_partner_account_id(record->partner);
    state.set_partner_name(record->partner_name);
    *state.mutable_mine() = record->opener_offer;
    *state.mutable_theirs() = record->partner_offer;
    state.set_mine_accepted(record->opener_accepted);
    state.set_theirs_accepted(record->partner_accepted);
    state.set_mine_confirmed(record->opener_confirmed);
  } else {
    state.set_partner_account_id(record->opener);
    state.set_partner_name(record->opener_name);
    *state.mutable_mine() = record->partner_offer;
    *state.mutable_theirs() = record->opener_offer;
    state.set_mine_accepted(record->partner_accepted);
    state.set_theirs_accepted(record->opener_accepted);
    state.set_mine_confirmed(record->partner_confirmed);
  }
  return state;
}

std::vector<std::string> Trades::TakeChanged() {
  std::vector<std::string> changed;
  changed.swap(changed_);
  return changed;
}

std::vector<TradeNotice> Trades::TakeNotices() {
  std::vector<TradeNotice> notices;
  notices.swap(notices_);
  return notices;
}

std::vector<TradeCompletion> Trades::TakeCompletions() {
  std::vector<TradeCompletion> completions;
  completions.swap(completions_);
  return completions;
}

bool Trades::IsOpener(const Record& record, const std::string& account_id) {
  return record.opener == account_id;
}

void Trades::ClearAgreement(Record& record) {
  // Everything, both sides: a table nobody is looking at any more.
  record.opener_accepted = false;
  record.partner_accepted = false;
  record.opener_confirmed = false;
  record.partner_confirmed = false;
}

void Trades::Complete(Record& record) {
  // Each side is paid what the OTHER put up.
  completions_.push_back({record.opener, record.partner_offer});
  completions_.push_back({record.partner, record.opener_offer});
  // A note still queued from the first confirm would be SENT from the state
  // after this one, which is the empty state of a player in no trade -- and
  // that reads to a client as their partner walking out. The completion is
  // the only thing these two are owed.
  DropChanged(record.opener);
  DropChanged(record.partner);
  std::string id = record.id;
  trade_of_.erase(record.opener);
  trade_of_.erase(record.partner);
  trades_.erase(id);
}

Trades::Record* Trades::Find(const std::string& account_id) {
  std::map<std::string, std::string>::iterator in = trade_of_.find(account_id);
  if (in == trade_of_.end()) {
    return nullptr;
  }
  return &trades_.at(in->second);
}

const Trades::Record* Trades::Find(const std::string& account_id) const {
  std::map<std::string, std::string>::const_iterator in =
      trade_of_.find(account_id);
  if (in == trade_of_.end()) {
    return nullptr;
  }
  return &trades_.at(in->second);
}

const std::string& Trades::Other(const Record& record,
                                 const std::string& account_id) {
  if (IsOpener(record, account_id)) {
    return record.partner;
  }
  return record.opener;
}

void Trades::NoteChanged(const Record& record) {
  changed_.push_back(record.opener);
  // Not the one who has not answered yet: a trade state is what opens the
  // trade screen, and theirs opens when they press Trade back, not when they
  // are asked.
  if (record.joined) {
    changed_.push_back(record.partner);
  }
}

void Trades::DropChanged(const std::string& account_id) {
  changed_.erase(std::remove(changed_.begin(), changed_.end(), account_id),
                 changed_.end());
}

std::string Trades::NewTradeId() {
  return RandomHexId(rng_, kTradeIdCharacters);
}

}  // namespace ms
