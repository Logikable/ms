#include "server/lobby.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "server/ids.h"
#include "src/character/boss_reset.h"
#include "src/protos/boss.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

// Characters in a party id. Short: it is passed around in one message and
// read in log lines, and a handful of parties are ever open at once.
constexpr int kPartyIdCharacters = 8;

LobbyResult Refusal(Refused::Reason reason, const std::string& message) {
  LobbyResult result;
  result.reason = reason;
  result.message = message;
  return result;
}

LobbyResult Done() {
  LobbyResult result;
  result.ok = true;
  return result;
}

// The difficulty `boss_key` and `index` name, or null if they name none.
const BossDifficulty* FindDifficulty(const std::map<std::string, Boss>& bosses,
                                     const std::string& boss_key, int index) {
  std::map<std::string, Boss>::const_iterator boss = bosses.find(boss_key);
  if (boss == bosses.end() || index < 0 ||
      index >= boss->second.difficulties_size()) {
    return nullptr;
  }
  return &boss->second.difficulties(index);
}

// Whether `player` is holding a clear of this fight that has not expired by
// `now`. A boss with no reset period is one there is nothing to hold back.
bool ClearedAt(const PlayerInfo& player, const std::string& boss_key,
               const std::string& difficulty, ResetPeriod reset, int64_t now) {
  for (const BossClear& clear : player.boss_clears()) {
    if (clear.boss() == boss_key && clear.difficulty() == difficulty) {
      return !BossAvailable(clear.cleared_unix_seconds(), reset, now);
    }
  }
  return false;
}

// The member playing under `account_id`, or null.
PartyMember* FindMember(Party& party, const std::string& account_id) {
  for (PartyMember& member : *party.mutable_members()) {
    if (member.player().account_id() == account_id) {
      return &member;
    }
  }
  return nullptr;
}

// Puts everyone back to unready. Called whenever the party's membership or
// its leader changes: a promise made about a different party is not one to
// carry into a fight.
void ClearReady(Party& party) {
  for (PartyMember& member : *party.mutable_members()) {
    member.set_ready(false);
  }
}

}  // namespace

Lobby::Lobby(const std::map<std::string, Boss>& bosses, unsigned int seed)
    : bosses_(bosses), rng_(seed) {
}

LobbyResult Lobby::Create(const PlayerInfo& player) {
  if (Find(player.account_id()) != nullptr) {
    return Refusal(Refused::REASON_ALREADY_IN_PARTY,
                   "Leave your party before making another.");
  }

  Record record;
  record.party.set_id(NewPartyId());
  record.party.set_leader_account_id(player.account_id());
  *record.party.add_members()->mutable_player() = player;

  const std::string& id = record.party.id();
  party_of_[player.account_id()] = id;
  order_.push_back(id);
  NoteChanged(record.party);
  listing_changed_ = true;
  parties_[id] = record;
  return Done();
}

LobbyResult Lobby::Join(const PlayerInfo& player, const std::string& party_id) {
  if (Find(player.account_id()) != nullptr) {
    return Refusal(Refused::REASON_ALREADY_IN_PARTY,
                   "Leave your party before joining another.");
  }
  std::map<std::string, Record>::iterator found = parties_.find(party_id);
  if (found == parties_.end()) {
    return Refusal(Refused::REASON_PARTY_GONE, "That party is gone.");
  }
  Record& record = found->second;
  if (record.started) {
    return Refusal(Refused::REASON_FIGHT_STARTED,
                   "That party is already fighting.");
  }
  if (record.party.members_size() >= kMaxPartySize) {
    return Refusal(Refused::REASON_PARTY_FULL, "That party is full.");
  }

  *record.party.add_members()->mutable_player() = player;
  ClearReady(record.party);
  party_of_[player.account_id()] = party_id;
  NoteChanged(record.party);
  listing_changed_ = true;
  return Done();
}

LobbyResult Lobby::Leave(const std::string& account_id) {
  Record* record = Find(account_id);
  if (record == nullptr) {
    return Refusal(Refused::REASON_NOT_IN_PARTY, "You are not in a party.");
  }
  // Taken before the party is touched, so the one leaving is told as well as
  // the ones staying.
  NoteChanged(record->party);
  party_of_.erase(account_id);

  std::string id = record->party.id();
  if (!Remove(record->party, account_id)) {
    parties_.erase(id);
    order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
  }
  listing_changed_ = true;
  return Done();
}

LobbyResult Lobby::SetReady(const std::string& account_id, bool ready) {
  Record* record = Find(account_id);
  if (record == nullptr) {
    return Refusal(Refused::REASON_NOT_IN_PARTY, "You are not in a party.");
  }
  if (record->party.leader_account_id() == account_id) {
    return Refusal(Refused::REASON_NOT_A_MEMBER,
                   "The party leader is always ready.");
  }
  FindMember(record->party, account_id)->set_ready(ready);
  NoteChanged(record->party);
  return Done();
}

LobbyResult Lobby::Kick(const std::string& account_id,
                        const std::string& target) {
  Record* record = Find(account_id);
  if (record == nullptr) {
    return Refusal(Refused::REASON_NOT_IN_PARTY, "You are not in a party.");
  }
  if (record->party.leader_account_id() != account_id) {
    return Refusal(Refused::REASON_NOT_LEADER,
                   "Only the party leader can remove somebody.");
  }
  if (target == account_id) {
    return Refusal(Refused::REASON_NOT_A_MEMBER,
                   "Leave the party rather than removing yourself.");
  }
  if (FindMember(record->party, target) == nullptr) {
    return Refusal(Refused::REASON_NOT_A_MEMBER, "They are not in your party.");
  }

  // The one being removed is told before they are, so they hear it as a
  // member rather than as somebody the party no longer knows.
  NoteChanged(record->party);
  NoteEvent(target, PartyEvent::KICKED, "You were removed from the party.");
  party_of_.erase(target);
  Remove(record->party, target);
  listing_changed_ = true;
  return Done();
}

LobbyResult Lobby::Promote(const std::string& account_id,
                           const std::string& target) {
  Record* record = Find(account_id);
  if (record == nullptr) {
    return Refusal(Refused::REASON_NOT_IN_PARTY, "You are not in a party.");
  }
  if (record->party.leader_account_id() != account_id) {
    return Refusal(Refused::REASON_NOT_LEADER,
                   "Only the party leader can hand the party on.");
  }
  if (target == account_id) {
    return Refusal(Refused::REASON_NOT_A_MEMBER, "You already lead the party.");
  }
  if (FindMember(record->party, target) == nullptr) {
    return Refusal(Refused::REASON_NOT_A_MEMBER, "They are not in your party.");
  }

  record->party.set_leader_account_id(target);
  ClearReady(record->party);
  NoteChanged(record->party);
  NoteEvent(target, PartyEvent::PROMOTED, "You are now the party leader.");
  return Done();
}

LobbyResult Lobby::Start(const std::string& account_id,
                         const StartFight& request, int64_t now) {
  Record* record = Find(account_id);
  if (record == nullptr) {
    return Refusal(Refused::REASON_NOT_IN_PARTY, "You are not in a party.");
  }
  if (record->party.leader_account_id() != account_id) {
    return Refusal(Refused::REASON_NOT_LEADER,
                   "Only the party leader can start the fight.");
  }
  if (record->started) {
    return Refusal(Refused::REASON_FIGHT_STARTED,
                   "The fight has already started.");
  }
  LobbyResult allowed = CheckFight(record->party, request, now);
  if (!allowed.ok) {
    return allowed;
  }
  record->started = true;
  NoteChanged(record->party);
  listing_changed_ = true;
  return Done();
}

void Lobby::UpdatePlayer(const PlayerInfo& player) {
  Record* record = Find(player.account_id());
  if (record == nullptr) {
    return;
  }
  PartyMember* member = FindMember(record->party, player.account_id());
  if (member == nullptr) {
    return;
  }
  *member->mutable_player() = player;
  NoteChanged(record->party);
  listing_changed_ = true;
}

void Lobby::Disconnect(const std::string& account_id) {
  Leave(account_id);
}

PartyList Lobby::Listed() const {
  PartyList list;
  for (const std::string& id : order_) {
    std::map<std::string, Record>::const_iterator found = parties_.find(id);
    if (found == parties_.end() || found->second.started) {
      continue;
    }
    Party* listed = list.add_parties();
    *listed = found->second.party;
    // The listing goes to everyone connected whenever any party changes, and
    // it draws a leader's name and a capacity. Carrying every member's whole
    // sheet through that would be a save's worth of message per keystroke in
    // the lobby; a sheet is for the party you are in, and rides its state.
    for (PartyMember& member : *listed->mutable_members()) {
      member.mutable_player()->clear_sheet();
      member.mutable_player()->clear_boss_clears();
    }
  }
  return list;
}

Party Lobby::StateFor(const std::string& account_id) const {
  const Record* record = Find(account_id);
  return record == nullptr ? Party() : record->party;
}

std::vector<std::string> Lobby::TakeChanged() {
  std::sort(changed_.begin(), changed_.end());
  changed_.erase(std::unique(changed_.begin(), changed_.end()), changed_.end());
  std::vector<std::string> taken;
  taken.swap(changed_);
  return taken;
}

std::vector<LobbyEvent> Lobby::TakeEvents() {
  std::vector<LobbyEvent> taken;
  taken.swap(events_);
  return taken;
}

bool Lobby::TakeListingChanged() {
  bool changed = listing_changed_;
  listing_changed_ = false;
  return changed;
}

Lobby::Record* Lobby::Find(const std::string& account_id) {
  std::map<std::string, std::string>::iterator party =
      party_of_.find(account_id);
  if (party == party_of_.end()) {
    return nullptr;
  }
  std::map<std::string, Record>::iterator found = parties_.find(party->second);
  return found == parties_.end() ? nullptr : &found->second;
}

const Lobby::Record* Lobby::Find(const std::string& account_id) const {
  std::map<std::string, std::string>::const_iterator party =
      party_of_.find(account_id);
  if (party == party_of_.end()) {
    return nullptr;
  }
  std::map<std::string, Record>::const_iterator found =
      parties_.find(party->second);
  return found == parties_.end() ? nullptr : &found->second;
}

LobbyResult Lobby::CheckFight(const Party& party, const StartFight& request,
                              int64_t now) const {
  const BossDifficulty* difficulty =
      FindDifficulty(bosses_, request.boss_key(), request.difficulty_index());
  if (difficulty == nullptr) {
    return Refusal(Refused::REASON_UNKNOWN_BOSS,
                   "This server does not know that fight.");
  }
  if (difficulty->coming_soon()) {
    return Refusal(Refused::REASON_UNKNOWN_BOSS, "That fight is not open.");
  }
  // Three passes rather than one, so the leader is told the first thing that
  // stands in the party's way rather than whatever the first member's row
  // happens to be short of.
  for (const PartyMember& member : party.members()) {
    if (member.player().level() < difficulty->unlock_level()) {
      return Refusal(Refused::REASON_LEVEL_TOO_LOW,
                     "Someone doesn't meet the level requirement.");
    }
  }
  for (const PartyMember& member : party.members()) {
    if (!ClearedAt(member.player(), request.boss_key(), difficulty->name(),
                   difficulty->reset(), now)) {
      continue;
    }
    std::string when =
        difficulty->reset() == RESET_PERIOD_WEEKLY ? "this week" : "today";
    return Refusal(Refused::REASON_ALREADY_CLEARED,
                   absl::StrCat("Someone has already cleared ", when, "."));
  }
  for (const PartyMember& member : party.members()) {
    // The leader is ready by leading, and nothing is stored for them.
    if (!member.ready() &&
        member.player().account_id() != party.leader_account_id()) {
      return Refusal(Refused::REASON_NOT_READY, "Someone is not ready.");
    }
  }
  return Done();
}

bool Lobby::Remove(Party& party, const std::string& account_id) {
  for (int i = 0; i < party.members_size(); ++i) {
    if (party.members(i).player().account_id() == account_id) {
      party.mutable_members()->DeleteSubrange(i, 1);
      break;
    }
  }
  if (party.members_size() == 0) {
    return false;
  }
  ClearReady(party);
  if (party.leader_account_id() == account_id) {
    // The one who joined first takes it over, so a party outlives whoever
    // happened to make it.
    const std::string& heir = party.members(0).player().account_id();
    party.set_leader_account_id(heir);
    NoteEvent(heir, PartyEvent::PROMOTED, "You are now the party leader.");
  }
  return true;
}

void Lobby::NoteEvent(const std::string& account_id, PartyEvent::Kind kind,
                      const std::string& message) {
  LobbyEvent event;
  event.account_id = account_id;
  event.event.set_kind(kind);
  event.event.set_message(message);
  events_.push_back(event);
}

void Lobby::NoteChanged(const Party& party) {
  for (const PartyMember& member : party.members()) {
    changed_.push_back(member.player().account_id());
  }
}

std::string Lobby::NewPartyId() {
  while (true) {
    std::string id = RandomHexId(rng_, kPartyIdCharacters);
    if (parties_.find(id) == parties_.end()) {
      return id;
    }
  }
}

}  // namespace ms
