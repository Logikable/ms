#include "server/lobby.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "server/ids.h"
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

}  // namespace

Lobby::Lobby(const std::map<std::string, Boss>& bosses, unsigned int seed)
    : bosses_(bosses), rng_(seed) {
}

LobbyResult Lobby::Create(const PlayerInfo& player,
                          const CreateParty& request) {
  if (Find(player.account_id()) != nullptr) {
    return Refusal(Refused::REASON_ALREADY_IN_PARTY,
                   "Leave your party before making another.");
  }
  LobbyResult allowed =
      CheckFight(player, request.boss_key(), request.difficulty_index());
  if (!allowed.ok) {
    return allowed;
  }

  Record record;
  record.party.set_id(NewPartyId());
  record.party.set_boss_key(request.boss_key());
  record.party.set_difficulty_index(request.difficulty_index());
  record.party.set_mode(request.mode() == PARTY_MODE_UNSPECIFIED
                            ? PARTY_MODE_SHARED
                            : request.mode());
  record.party.set_leader_account_id(player.account_id());
  *record.party.add_members() = player;

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
  LobbyResult allowed = CheckFight(player, record.party.boss_key(),
                                   record.party.difficulty_index());
  if (!allowed.ok) {
    return allowed;
  }

  *record.party.add_members() = player;
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

  Party& party = record->party;
  for (int i = 0; i < party.members_size(); ++i) {
    if (party.members(i).account_id() == account_id) {
      party.mutable_members()->DeleteSubrange(i, 1);
      break;
    }
  }
  std::string id = party.id();
  if (party.members_size() == 0) {
    parties_.erase(id);
    order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
  } else if (party.leader_account_id() == account_id) {
    // The one who joined first takes it over, so a party outlives whoever
    // happened to make it.
    party.set_leader_account_id(party.members(0).account_id());
  }
  listing_changed_ = true;
  return Done();
}

LobbyResult Lobby::Start(const std::string& account_id) {
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
  for (PlayerInfo& member : *record->party.mutable_members()) {
    if (member.account_id() == player.account_id()) {
      member = player;
      NoteChanged(record->party);
      listing_changed_ = true;
      return;
    }
  }
}

void Lobby::Disconnect(const std::string& account_id) {
  Leave(account_id);
}

PartyList Lobby::Listed() const {
  PartyList list;
  for (const std::string& id : order_) {
    std::map<std::string, Record>::const_iterator found = parties_.find(id);
    if (found != parties_.end() && !found->second.started) {
      *list.add_parties() = found->second.party;
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

LobbyResult Lobby::CheckFight(const PlayerInfo& player,
                              const std::string& boss_key,
                              int difficulty_index) const {
  const BossDifficulty* difficulty =
      FindDifficulty(bosses_, boss_key, difficulty_index);
  if (difficulty == nullptr) {
    return Refusal(Refused::REASON_UNKNOWN_BOSS,
                   "This server does not know that fight.");
  }
  if (difficulty->coming_soon()) {
    return Refusal(Refused::REASON_UNKNOWN_BOSS, "That fight is not open.");
  }
  if (player.level() < difficulty->unlock_level()) {
    return Refusal(Refused::REASON_LEVEL_TOO_LOW,
                   absl::StrCat("Level ", difficulty->unlock_level(),
                                " is needed for that fight."));
  }
  return Done();
}

void Lobby::NoteChanged(const Party& party) {
  for (const PlayerInfo& member : party.members()) {
    changed_.push_back(member.account_id());
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
