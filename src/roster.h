/* The characters on the account, and the four things character select does with
 * them.
 *
 * A session holds one character unpacked (GameState::character) and the rest as
 * protos. Keeping the two in agreement is done here instead of in the save or
 * the frontend: a slot index means the same thing to the file, the screen and
 * the offline check only if one place decides it.
 *
 * PutIntoPlay is the single way a character comes into play, whether from the
 * file at launch or from character select mid-session.
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
  // Which save slot this is; every call below takes it.
  int slot = 0;
  std::string name;
  std::string job;
  int level = 0;
  // The character being played, and the one with the offline check. Two flags,
  // since playing someone else doesn't move the check.
  bool played = false;
  bool offline = false;
};

// Every character on the account in slot order, with the played one written
// back into their slot. The save is written from this, and slots index it.
std::vector<CharacterSave> AllCharacters(const GameState& state);

// The played character as a save slot: their sheet, their map and their three
// timestamps.
CharacterSave PlayedCharacterSave(const GameState& state);

// The roster as character select lists it: most recently played first, then in
// slot order for characters not played since the timestamp was added.
std::vector<RosterEntry> Roster(const GameState& state);

// Unpacks `all[slot]` into play, keeps the rest alongside, and stamps the new
// character as played now. An out-of-range slot puts the first character in.
void PutIntoPlay(GameState& state, const std::vector<CharacterSave>& all,
                 int slot);

// Puts `slot` into play, first writing the current character back to their own
// slot. Their map comes back with them. Returns false and changes nothing for
// the slot already in play (choosing them is a resume) and for an out-of-range
// slot.
bool PlayCharacter(GameState& state, int slot);

// Creates a level-1 Beginner in a new slot and puts them into play. The offline
// check doesn't move: a new character doesn't take over farming until the
// player says so.
void CreateCharacter(GameState& state);

// Removes `slot`. Returns false and changes nothing for the account's last
// character, since someone must be playable.
//
// Deleting the played character swaps another one in, since the game always has
// exactly one in play; deleting the one with the offline check gives it to
// whoever ends up in play. Neither can leave the account stuck, because the
// last character can't be deleted.
bool DeleteCharacter(GameState& state, int slot);

// Moves the offline check to `slot`: the character the next launch opens on,
// and the only one an absence pays.
void SetOfflineCharacter(GameState& state, int slot);

}  // namespace ms

#endif  // MS_SRC_ROSTER_H_
