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

// The played character's slot, clamped to the range the save's list has.
int PlayedSlot(const GameState& state) {
  return std::clamp(state.played_slot, 0,
                    static_cast<int>(state.inactive_characters.size()));
}

// Records `played` as the slot in play and keeps everyone else as their protos.
// The played character isn't restored from `all`: they're already unpacked, and
// a round trip through their own snapshot could only lose something.
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
  // Stable, so characters with the same timestamp (everyone in a save written
  // before the timestamp existed) stay in the order the account created them.
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
  // What the character leaving play reached, which the new character's unlocks
  // are checked against. The file records this at save time, but the session
  // must record it here: a level 1 character made by an account at 210 mustn't
  // be taken through the early game again.
  state.account.RecordProgress(state.character.proto().level(),
                               state.character.proto().job_stage());
  const CharacterSave& arriving = all[slot];
  state.character.RestoreFrom(arriving.character(), state.equips, state.items);
  // A save written under older rules is the only thing that can arrive with
  // inconsistent points, so all four are checked here: AP and SP against the
  // levels that paid for them, skills against the data's current maximums, and
  // Hyper Stats against the points the level gives. SP last, since
  // ReconcileSkills moves points between a book and its pool.
  state.character.ReconcileAp();
  state.character.ReconcileSkills(state.skills);
  state.character.ReconcileHyperStats();
  state.character.ReconcileSp(state.skills);
  state.character.ReconcileLinkSkills(state.skills);
  state.current_map = arriving.current_map();
  state.playtime_seconds = static_cast<double>(arriving.playtime_seconds());
  // Left alone when the slot has no creation time, meaning it was written
  // before the field existed. The state was stamped when it was built, so
  // keeping that reads as "created now" instead of the epoch.
  if (arriving.created_unix_seconds() != 0) {
    state.created_unix_seconds = arriving.created_unix_seconds();
  }
  // Now, however this was reached: putting a character into play counts as
  // logging in, and the roster sorts by it.
  state.last_played_unix_seconds = Now();

  // After the others are in place: the link skill tally is read from the rest
  // of the roster, and the account's setting and progress from the account.
  KeepBeside(state, all, slot);
  state.MirrorAccount();
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
  // An empty slot at the end, which PutIntoPlay unpacks into a blank character
  // before seeding dresses them.
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
  // Slots above the removed one shift down; the deleted slot itself has nowhere
  // to go and is handled below.
  bool played_deleted = played == slot;
  bool offline_deleted = offline == slot;
  if (played > slot) {
    --played;
  }
  if (offline > slot) {
    --offline;
  }
  if (played_deleted) {
    // Someone has to be in play. The offline character is the natural choice,
    // since the account would have opened on them anyway.
    played = offline_deleted ? 0 : offline;
    PutIntoPlay(state, all, played);
    played = state.played_slot;
  } else {
    KeepBeside(state, all, played);
  }
  if (offline_deleted) {
    // The offline check can't stay on a deleted character, and whoever is in
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
