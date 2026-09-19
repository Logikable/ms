/* The characters on the account, and the four things the character select
 * does to them.
 *
 * A session holds one character unpacked -- GameState::character -- and the
 * rest as the protos they arrived as. The bookkeeping that keeps those two
 * halves agreeing is here rather than in the save or the frontend: a slot
 * index means the same thing to the file, to the screen and to the offline
 * check only if one place decides it.
 *
 * PutIntoPlay is the one door a character comes through, whether from the
 * file at launch or from the character select mid-session.
 */
#ifndef MS_SRC_ROSTER_H_
#define MS_SRC_ROSTER_H_

#include <string>
#include <vector>

#include "src/game_state.h"
#include "src/protos/save.pb.h"

namespace ms {

// One character as the select screen lists them.
struct RosterEntry {
  // Which slot of the save this is: what every call below takes.
  int slot = 0;
  std::string name;
  std::string job;
  int level = 0;
  // The character being played, and the one carrying the offline check. Two
  // flags rather than one: playing somebody else does not move the check.
  bool played = false;
  bool offline = false;
};

// Every character on the account in slot order, the played one folded back
// into theirs. What the save is written from, and what a slot indexes.
std::vector<CharacterSave> AllCharacters(const GameState& state);

// The played character as a slot of the save: their sheet, their map and
// their three clocks.
CharacterSave PlayedCharacterSave(const GameState& state);

// The roster as the character select lists it: most recently played first,
// and in slot order among characters who have never been played since the
// clock existed.
std::vector<RosterEntry> Roster(const GameState& state);

// Unpacks `all[slot]` into play and keeps the rest beside it, stamping the
// newcomer as played now. Out of range puts the first character in.
void PutIntoPlay(GameState& state, const std::vector<CharacterSave>& all,
                 int slot);

// Puts `slot` into play, writing whoever was being played back to their own
// slot first. Their map comes back with them. Returns false, changing
// nothing, for the slot already in play -- picking them is a resume -- and
// for one out of range.
bool PlayCharacter(GameState& state, int slot);

// Makes a level-1 Beginner in a slot of their own and puts them into play.
// The offline check does not move: a new character does not take over the
// farming until the player says so.
void CreateCharacter(GameState& state);

// Drops `slot`. Returns false and changes nothing for the last character on
// the account -- there has to be somebody to play.
//
// Deleting the character being played swaps another one in, the game holding
// exactly one at all times; deleting the one with the offline check hands it
// to whoever ends up in play. Neither can strand the account, because the
// last character cannot be deleted.
bool DeleteCharacter(GameState& state, int slot);

// Moves the offline check to `slot`: the character the next launch opens on,
// and the only one an absence pays.
void SetOfflineCharacter(GameState& state, int slot);

}  // namespace ms

#endif  // MS_SRC_ROSTER_H_
