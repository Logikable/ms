#include "src/roster.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "src/character/job_name.h"
#include "src/game_state.h"
#include "src/protos/save.pb.h"

namespace ms {
namespace {

int64_t Now() {
  return static_cast<int64_t>(std::time(nullptr));
}

// Where the played character sits among the others: their slot, held inside
// the list a save actually has room for.
int PlayedSlot(const GameState& state) {
  return std::clamp(state.played_slot, 0,
                    static_cast<int>(state.inactive_characters.size()));
}

// Records `played` as the slot in play and keeps everyone else as the protos
// they arrived as. The played character is not restored from `all`: they are
// already unpacked, and a round trip through their own snapshot would only
// risk losing something.
void KeepBeside(GameState& state, const std::vector<CharacterSave>& all,
                int played) {
  state.played_slot = played;
  state.inactive_characters.clear();
  for (int i = 0; i < static_cast<int>(all.size()); ++i) {
    if (i != played) {
      state.inactive_characters.push_back(all[i]);
    }
  }
}

}  // namespace

CharacterSave PlayedCharacterSave(const GameState& state) {
  CharacterSave slot;
  *slot.mutable_character() = state.character.ToProto();
  slot.set_current_map(state.current_map);
  slot.set_created_unix_seconds(state.created_unix_seconds);
  slot.set_playtime_seconds(static_cast<int64_t>(state.playtime_seconds));
  slot.set_last_played_unix_seconds(state.last_played_unix_seconds);
  return slot;
}

std::vector<CharacterSave> AllCharacters(const GameState& state) {
  const std::vector<CharacterSave>& others = state.inactive_characters;
  int played = PlayedSlot(state);
  std::vector<CharacterSave> all;
  all.reserve(others.size() + 1);
  for (int i = 0; i < played; ++i) {
    all.push_back(others[i]);
  }
  all.push_back(PlayedCharacterSave(state));
  for (std::size_t i = played; i < others.size(); ++i) {
    all.push_back(others[i]);
  }
  return all;
}

std::vector<RosterEntry> Roster(const GameState& state) {
  std::vector<CharacterSave> all = AllCharacters(state);
  std::vector<RosterEntry> rows;
  for (int i = 0; i < static_cast<int>(all.size()); ++i) {
    const Character& character = all[i].character();
    RosterEntry row;
    row.slot = i;
    row.name = character.name();
    row.job = ShortJobName(character.job());
    row.level = character.level();
    row.played = i == PlayedSlot(state);
    row.offline = i == state.offline_slot;
    rows.push_back(std::move(row));
  }
  // Stable, so characters who share a stamp -- everybody on a save written
  // before the clock existed -- stay in the order the account made them.
  std::stable_sort(rows.begin(), rows.end(),
                   [&all](const RosterEntry& a, const RosterEntry& b) {
                     return all[a.slot].last_played_unix_seconds() >
                            all[b.slot].last_played_unix_seconds();
                   });
  return rows;
}

void PutIntoPlay(GameState& state, const std::vector<CharacterSave>& all,
                 int slot) {
  if (all.empty()) {
    return;
  }
  if (slot < 0 || slot >= static_cast<int>(all.size())) {
    slot = 0;
  }
  // What the character leaving play reached, which is what the newcomer's
  // unlocks are measured against. The file learns this at save time, but the
  // session has to learn it here: a level 1 character made by an account
  // standing at 210 must not be walked through the early game again.
  state.account.RecordProgress(state.character.proto().level(),
                               state.character.proto().job_stage());
  const CharacterSave& arriving = all[slot];
  state.character.RestoreFrom(arriving.character(), state.equips, state.items);
  // A save written under older rules is the one thing that can arrive with its
  // books unbalanced, so this is the door to check all four at: the AP and the
  // SP against the levels that paid them, the skills against the maximums the
  // data states now, and the Hyper Stats against the points the level pays.
  // SP last -- ReconcileSkills moves points between a book and its pool.
  state.character.ReconcileAp();
  state.character.ReconcileSkills(state.skills);
  state.character.ReconcileHyperStats();
  state.character.ReconcileSp(state.skills);
  state.current_map = arriving.current_map();
  state.playtime_seconds = static_cast<double>(arriving.playtime_seconds());
  // Left alone when the slot has no creation time to give -- one written
  // before the field existed. The state was stamped when it was built, so
  // holding on to that reads as "created now" rather than as the epoch.
  if (arriving.created_unix_seconds() != 0) {
    state.created_unix_seconds = arriving.created_unix_seconds();
  }
  // Now, whichever door this is: being put into play IS logging in, and the
  // roster sorts on it.
  state.last_played_unix_seconds = Now();

  // After the account, whose switch and record it reads.
  state.MirrorAccount();
  KeepBeside(state, all, slot);
}

bool PlayCharacter(GameState& state, int slot) {
  std::vector<CharacterSave> all = AllCharacters(state);
  if (slot < 0 || slot >= static_cast<int>(all.size()) ||
      slot == PlayedSlot(state)) {
    return false;
  }
  PutIntoPlay(state, all, slot);
  return true;
}

void CreateCharacter(GameState& state) {
  std::vector<CharacterSave> all = AllCharacters(state);
  // An empty slot on the end, which PutIntoPlay unpacks into a blank
  // character before the seeding dresses them.
  all.push_back(CharacterSave());
  PutIntoPlay(state, all, static_cast<int>(all.size()) - 1);
  state.created_unix_seconds = Now();
  SeedNewCharacter(state);
}

bool DeleteCharacter(GameState& state, int slot) {
  std::vector<CharacterSave> all = AllCharacters(state);
  if (all.size() <= 1 || slot < 0 || slot >= static_cast<int>(all.size())) {
    return false;
  }
  int played = PlayedSlot(state);
  int offline = state.offline_slot;
  all.erase(all.begin() + slot);
  // A slot above the hole shifts down; the deleted one itself has no slot to
  // land on and is settled below.
  bool played_deleted = played == slot;
  bool offline_deleted = offline == slot;
  if (played > slot) {
    --played;
  }
  if (offline > slot) {
    --offline;
  }
  if (played_deleted) {
    // Somebody has to be in play. The offline character is the honest pick:
    // they are who the account would have opened on anyway.
    played = offline_deleted ? 0 : offline;
    PutIntoPlay(state, all, played);
    played = state.played_slot;
  } else {
    KeepBeside(state, all, played);
  }
  if (offline_deleted) {
    // The check cannot be left on a character who is gone, and whoever is in
    // play is the one the player is looking at.
    offline = played;
  }
  state.offline_slot = std::clamp(offline, 0, static_cast<int>(all.size()) - 1);
  return true;
}

void SetOfflineCharacter(GameState& state, int slot) {
  int count = static_cast<int>(state.inactive_characters.size()) + 1;
  if (slot < 0 || slot >= count) {
    return;
  }
  state.offline_slot = slot;
}

}  // namespace ms
