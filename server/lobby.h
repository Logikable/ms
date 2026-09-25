/* The lobby holds every party waiting to fight and the rules for creating,
 * joining and leaving them. It knows nothing about sockets, so the rules are
 * testable without a network.
 */
#ifndef MS_SERVER_LOBBY_H_
#define MS_SERVER_LOBBY_H_

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/multiplayer/protocol.h"
#include "src/protos/boss.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// The outcome of a request. A refusal carries a reason code for the client
// and a message to show the player.
struct LobbyResult {
  bool ok = false;
  Refused::Reason reason = Refused::REASON_UNSPECIFIED;
  std::string message;
};

// A message for one player that the party state alone does not convey, such
// as being kicked.
struct LobbyEvent {
  std::string account_id;
  PartyEvent event;
};

class Lobby {
 public:
  // `bosses` is the boss catalog. The caller owns it, and it must outlive the
  // lobby. `seed` seeds the party id generator.
  Lobby(const std::map<std::string, Boss>& bosses, unsigned int seed);
  // The catalog is held by reference, so a temporary would dangle.
  Lobby(std::map<std::string, Boss>&& bosses, unsigned int seed) = delete;

  LobbyResult Create(const PlayerInfo& player);
  LobbyResult Join(const PlayerInfo& player, const std::string& party_id);
  LobbyResult Leave(const std::string& account_id);
  // Sets whether a member is ready to fight. The leader is always ready, so
  // this is refused for them.
  LobbyResult SetReady(const std::string& account_id, bool ready);
  // Leader only. `target` is the member's account.
  LobbyResult Kick(const std::string& account_id, const std::string& target);
  LobbyResult Promote(const std::string& account_id, const std::string& target);
  // Moves the party from the list into the fight `request` names. Leader
  // only, and refused unless every member can fight that boss. `now` is the
  // request's Unix time, used to check that nobody has already cleared the
  // boss this reset period.
  LobbyResult Start(const std::string& account_id, const StartFight& request,
                    int64_t now);

  // Puts a party back in the list after its fight ends, and clears everyone's
  // ready flag.
  void FinishFight(const std::string& party_id);

  // Updates a player's level or name in their party, if they are in one.
  void UpdatePlayer(const PlayerInfo& player);

  // Removes a disconnected player from their party, as Leave would. Does
  // nothing if they are in no party.
  void Disconnect(const std::string& account_id);

  // Returns every joinable party in creation order, without members' sheets.
  // The listing only shows each party's leader and size.
  PartyList Listed() const;
  // Returns the party `account_id` is in. A party with no id means they are
  // in none.
  Party StateFor(const std::string& account_id) const;

  // Returns and clears the accounts whose party changed. Players who just
  // left are included, so they learn they are in no party.
  std::vector<std::string> TakeChanged();
  // Returns and clears the per-player events. These are separate because
  // being kicked and leaving produce the same party state, and only a kicked
  // player needs to be told why.
  std::vector<LobbyEvent> TakeEvents();
  // Returns whether the public list changed since the last call, and resets
  // the flag.
  bool TakeListingChanged();

  int party_count() const {
    return static_cast<int>(parties_.size());
  }

 private:
  // A party and whether its fight has started. Parties in a fight are not
  // listed, since nobody can join a fight in progress.
  struct Record {
    Party party;
    bool started = false;
  };

  // The party `account_id` is in, or null.
  Record* Find(const std::string& account_id);
  const Record* Find(const std::string& account_id) const;
  // Checks that every member can fight what `request` names. A refusal says
  // what is wrong but not which member caused it.
  LobbyResult CheckFight(const Party& party, const StartFight& request,
                         int64_t now) const;
  // Removes `account_id` from `party`, promoting a new leader if they led it.
  // Returns false if the party is now empty, and the caller must erase it.
  bool Remove(Party& party, const std::string& account_id);
  // Queues event `kind` for `account_id`.
  void NoteEvent(const std::string& account_id, PartyEvent::Kind kind,
                 const std::string& message);
  // Marks every member of `party` as needing an update.
  void NoteChanged(const Party& party);
  std::string NewPartyId();

  const std::map<std::string, Boss>& bosses_;
  // Party ids in creation order. The list uses this order so parties do not
  // move around on screen.
  std::vector<std::string> order_;
  std::map<std::string, Record> parties_;
  // Maps each account to its party id. This duplicates what the parties hold,
  // so finding a player's party is one lookup instead of a search.
  std::map<std::string, std::string> party_of_;
  std::vector<std::string> changed_;
  std::vector<LobbyEvent> events_;
  bool listing_changed_ = false;
  std::mt19937 rng_;
};

}  // namespace ms

#endif  // MS_SERVER_LOBBY_H_
