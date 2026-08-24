/* The lobby: every party waiting to fight, and the rules for making, joining
 * and leaving one.
 *
 * Nothing here knows about sockets. The server hands it what a client asked
 * for and it answers whether that happened; afterwards the server asks which
 * players need telling and sends them the state they are now in. So the rules
 * can be read and tested without a connection anywhere near them.
 */
#ifndef MS_SERVER_LOBBY_H_
#define MS_SERVER_LOBBY_H_

#include <map>
#include <random>
#include <string>
#include <vector>

#include "src/multiplayer/protocol.h"
#include "src/protos/boss.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// What an ask came to. A refusal carries a reason for the client to act on and
// a sentence fit to show the player.
struct LobbyResult {
  bool ok = false;
  Refused::Reason reason = Refused::REASON_UNSPECIFIED;
  std::string message;
};

// A player who has to be told something the party state cannot say on its
// own, and what to tell them.
struct LobbyEvent {
  std::string account_id;
  PartyEvent event;
};

class Lobby {
 public:
  // `bosses` is the fight catalog, owned by the caller and outliving the
  // lobby. `seed` fixes the stream party ids are drawn from.
  Lobby(const std::map<std::string, Boss>& bosses, unsigned int seed);

  LobbyResult Create(const PlayerInfo& player);
  LobbyResult Join(const PlayerInfo& player, const std::string& party_id);
  LobbyResult Leave(const std::string& account_id);
  // Says whether this player is ready to fight. The leader is always ready
  // and asking for them is refused.
  LobbyResult SetReady(const std::string& account_id, bool ready);
  // Leader only. `target` is the member's account.
  LobbyResult Kick(const std::string& account_id, const std::string& target);
  LobbyResult Promote(const std::string& account_id, const std::string& target);
  // Takes the party out of the list and into the fight `request` names.
  // Leader only, and refused unless every member may fight it.
  LobbyResult Start(const std::string& account_id, const StartFight& request);

  // Takes a character's new level or name into whatever party they are in.
  // Nothing to do for a player who is in none.
  void UpdatePlayer(const PlayerInfo& player);

  // Drops a player who has gone, leaving whatever party they were in the way
  // Leave would. Quiet about a player who was in none.
  void Disconnect(const std::string& account_id);

  // Every party open to be joined, in the order they were made. Without the
  // members' sheets: what the listing draws is a leader and a capacity.
  PartyList Listed() const;
  // The party `account_id` is in. One with no id means they are in none,
  // which is what a player who has left, or was never in one, is sent.
  Party StateFor(const std::string& account_id) const;

  // Accounts whose party changed under them and need telling, cleared by the
  // taking. A player who has left is in here too: what they need telling is
  // that they are in nothing.
  std::vector<std::string> TakeChanged();
  // What individual players have to be told, cleared by the taking. Separate
  // from the above because being kicked and walking out leave the same state
  // behind, and only the one who was kicked needs the difference.
  std::vector<LobbyEvent> TakeEvents();
  // Whether the public list has changed since it was last taken.
  bool TakeListingChanged();

  int party_count() const {
    return static_cast<int>(parties_.size());
  }

 private:
  // One party, and whether its fight has begun. A party in a fight is not
  // listed: nobody joins a fight in progress.
  struct Record {
    Party party;
    bool started = false;
  };

  // The party `account_id` is in, or null.
  Record* Find(const std::string& account_id);
  const Record* Find(const std::string& account_id) const;
  // Whether every member of `party` may fight what `request` names, and why
  // not. Names whoever falls short, since the leader asking is not
  // necessarily the one who does.
  LobbyResult CheckFight(const Party& party, const StartFight& request) const;
  // Drops the member `account_id` from `party` and hands the party on if they
  // were leading it. Records who was promoted. Returns false when that left
  // the party empty, which is when the caller has to erase it.
  bool Remove(Party& party, const std::string& account_id);
  // Notes that `account_id` has to be told `kind`.
  void NoteEvent(const std::string& account_id, PartyEvent::Kind kind,
                 const std::string& message);
  // Marks everyone in `party` as needing telling.
  void NoteChanged(const Party& party);
  std::string NewPartyId();

  const std::map<std::string, Boss>& bosses_;
  // By party id, in the order they were made -- which is the order they are
  // listed in, so a party does not jump around the screen.
  std::vector<std::string> order_;
  std::map<std::string, Record> parties_;
  // Which party each player is in. An index into the above rather than
  // anything the parties do not already say, so that leaving is one lookup
  // rather than a search through every party.
  std::map<std::string, std::string> party_of_;
  std::vector<std::string> changed_;
  std::vector<LobbyEvent> events_;
  bool listing_changed_ = false;
  std::mt19937 rng_;
};

}  // namespace ms

#endif  // MS_SERVER_LOBBY_H_
