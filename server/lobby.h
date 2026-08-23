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

#include "src/protos/boss.pb.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {

// The most players in one party. Hilla's arena has three places to stand, and
// a party has to fit the smallest arena it can be taken into.
inline constexpr int kMaxPartySize = 3;

// What an ask came to. A refusal carries a reason for the client to act on and
// a sentence fit to show the player.
struct LobbyResult {
  bool ok = false;
  Refused::Reason reason = Refused::REASON_UNSPECIFIED;
  std::string message;
};

class Lobby {
 public:
  // `bosses` is the fight catalog, owned by the caller and outliving the
  // lobby. `seed` fixes the stream party ids are drawn from.
  Lobby(const std::map<std::string, Boss>& bosses, unsigned int seed);

  LobbyResult Create(const PlayerInfo& player, const CreateParty& request);
  LobbyResult Join(const PlayerInfo& player, const std::string& party_id);
  LobbyResult Leave(const std::string& account_id);
  // Takes the party out of the list and into its fight. Leader only.
  LobbyResult Start(const std::string& account_id);

  // Drops a player who has gone, leaving whatever party they were in the way
  // Leave would. Quiet about a player who was in none.
  void Disconnect(const std::string& account_id);

  // Every party open to be joined, in the order they were made.
  PartyList Listed() const;
  // The party `account_id` is in. One with no id means they are in none,
  // which is what a player who has left, or was never in one, is sent.
  Party StateFor(const std::string& account_id) const;

  // Accounts whose party changed under them and need telling, cleared by the
  // taking. A player who has left is in here too: what they need telling is
  // that they are in nothing.
  std::vector<std::string> TakeChanged();
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
  // Whether `player` may fight what `request` names, and why not.
  LobbyResult CheckFight(const PlayerInfo& player, const std::string& boss_key,
                         int difficulty_index) const;
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
  bool listing_changed_ = false;
  std::mt19937 rng_;
};

}  // namespace ms

#endif  // MS_SERVER_LOBBY_H_
