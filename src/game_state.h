/* GameState holds all runtime game state: loaded static data, a seeded RNG,
 * and the active character. Constructed once at startup with the loaded equip
 * and scroll maps. Non-copyable because CharacterInstance holds a reference
 * to rng.
 */
#ifndef MS_SRC_GAME_STATE_H_
#define MS_SRC_GAME_STATE_H_

#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "src/account.h"
#include "src/character/character.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/save.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// The town a character begins in and is returned to when they die. A key into
// GameState::maps, and the one map name the code names for itself -- every
// other one is the player's choice.
inline constexpr char kHomeMap[] = "maple_island";

// The three states the game can be started in.
//
// kPlay is the game as a player meets it: a level 1 Beginner on Maple Island
// with a Sword and nothing else. kTest is the workbench -- a level 1 Beginner
// with the meso and the spread of items the upgrade and shop screens need to
// be exercised without playing up to them. kMax is the ceiling: the character
// a player who spent well is standing in at the level asked for, which is
// what a boss is measured against.
enum class GameMode {
  kPlay,
  kTest,
  kMax,
};

// What state a seeded character's gear arrives in. The workbench fills it
// from --hammered, --scrolled and --sf; kMax fills it from the level's own
// band. Every piece is treated the same: the question is what a character at
// this point of the ladder hits for, not what one lucky item does. All unset
// is gear as it drops, slots unspent.
struct GearSetup {
  // Both Golden Hammers driven in, which widens the upgrade shelf by two.
  bool hammered = false;
  // Every upgrade slot passed, with the trace that raises what the job fights
  // with. The shelf `hammered` widened is the shelf that gets scrolled.
  bool scrolled = false;
  // Stars, held to the item's own cap for its level. Zero leaves it unstarred.
  // Stars go on an item with nothing left to scroll -- the rule the upgrade
  // screen keeps -- so an item with slots needs `scrolled` behind this.
  int stars = 0;
  // The weapon alone, which is where the meso goes first and the one piece
  // worth taking past the rest. Zero leaves it on `stars` with everything
  // else.
  int weapon_stars = 0;
};

// What the workbench does with the book its job is standing in, as --skills
// names it. The books behind it are bought either way: they are not what the
// tester chose the job for.
enum class TestSkills {
  // Left unbought, with the SP in the pool.
  kZero,
  // Bought outright, to the last point.
  kMax,
};

// Where the workbench's character stands when --job says nothing: the top of
// the Hero line as far as it is written, holding the whole of what this game
// has to hand out. It moves up with the line rather than staying put -- a
// workbench with an advancement still waiting is one the tester has to finish
// before they can look at anything.
inline constexpr JobAdvancement kTestAdvancement = JOB_ADVANCEMENT_HERO;

// The workbench's own settings, one per flag. kPlay ignores every one of them.
//
// `job` is --job: the advancement to start at the top of. Unset takes the
// workbench's own job.
//
// `level` is --level: the level to arrive at. 0 leaves it to the job, which is
// the top of that job's own band.
//
// kMax reads `job` and `level` too, and fills `equips` and `skills` itself.
struct TestOptions {
  JobAdvancement job = JOB_ADVANCEMENT_UNSPECIFIED;
  int level = 0;
  GearSetup equips;
  TestSkills skills = TestSkills::kZero;
};

struct GameState {
  // Constructs the catalogs and puts the character, bag and map into the
  // state `mode` begins from. Anything the catalogs do not name is skipped, so
  // a state built for a test need not carry the game's data files.
  //
  // `test` is the workbench's flags, and applies to kTest alone.
  //
  // `seed` fixes the random stream. The game leaves it unset and gets a
  // different one every time; a sim or a test passes one so that a run it
  // repeats pays what it paid last time -- rewards are rolled, and two runs of
  // the same fight otherwise disagree.
  //
  // `sets` is handed to the character once the seeding has dressed them, since
  // what a set pays is worked out from what is worn. It is a constructor
  // argument and not something the caller does afterwards because the state
  // cannot be named before it is returned -- a caller who forgot would get a
  // character standing in a full set and paid nothing for it.
  GameState(std::map<std::string, EquipPrototype> equips,
            std::map<std::string, Scroll> scrolls,
            std::map<std::string, ItemPrototype> items,
            std::map<std::string, Mob> mobs,
            std::map<std::string, MapData> maps,
            std::map<std::string, Skill> skills = {},
            GameMode mode = GameMode::kPlay, TestOptions test = {},
            std::optional<unsigned int> seed = std::nullopt,
            std::map<std::string, EquipSet> sets = {});
  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  std::map<std::string, EquipPrototype> equips;
  std::map<std::string, Scroll> scrolls;
  std::map<std::string, ItemPrototype> items;
  std::map<std::string, Mob> mobs;
  std::map<std::string, MapData> maps;
  std::map<std::string, Skill> skills;
  // The equipment sets and what each tier of one pays. Kept rather than only
  // handed to the character, because a party member's sheet has to be rebuilt
  // against them to be inspected.
  std::map<std::string, EquipSet> equip_sets;
  // The bosses that can be fought, by data file stem. Filled in after
  // construction rather than passed to it: nothing the constructor seeds
  // depends on them, and a boss is not somewhere a new character stands.
  std::map<std::string, Boss> bosses;
  std::mt19937 rng;

  // The character being played. The others on the account are inert until a
  // character select exists to swap one in.
  CharacterInstance character;

  // What every character on the account shares: bindings, unlocks, and what
  // the player has been shown. See //src/account.h.
  AccountInstance account;

  // Which slot of the save the played character came from, and the characters
  // in the other slots. Held so that saving keeps them: nothing in a session
  // reads a character that is not being played.
  int active_character = 0;
  std::vector<CharacterSave> inactive_characters;

  // What every EXP award from combat is multiplied by. kTest gets a standing
  // bonus so the workbench can climb the level ladder in a sitting rather than
  // farming the early levels at play speed; kPlay earns what it earns.
  int exp_multiplier = 1;

  // Name of the map being farmed (key into `maps`); empty means none.
  std::string current_map;

  // When this character was started, as seconds since the Unix epoch. Stamped
  // here rather than at save time so that both of the ways it can go
  // unanswered settle themselves: a new game gets the moment it began, and a
  // save written before the field existed keeps this value instead of loading
  // a zero. A load overwrites it only when the save carries one.
  int64_t created_unix_seconds;

  // When the game was last written to disk, as seconds since the Unix epoch,
  // or 0 for a character that has never been saved. What offline progress is
  // measured from -- see //src/combat:offline.
  int64_t last_seen_unix_seconds = 0;

  // Total seconds with this character in play, across every session.
  // Accumulated by the frontend's tick from a monotonic clock, so changing the
  // system time or crossing a daylight-saving boundary neither grants nor takes
  // playtime.
  double playtime_seconds = 0.0;

  // Everybody else in the party, rebuilt from the sheets they sent, for the
  // skills of theirs that reach this character -- see DerivedStatsFor.
  //
  // Filled only while a party fight is on, and emptied when it ends: a party
  // stands in the lobby while its members farm alone, and nothing farmed is
  // shared yet. Whatever reads it need not ask which it is.
  std::vector<CharacterInstance> party;
};

// Hands over whatever climbing from `from_level` to `to_level` grants: the
// honor every level pays, and whatever the catalogs owe. Reaching 200 is
// handed a Vanishing Journey Arcane Symbol, which is the reason this takes the
// whole state -- the character cannot give themselves an item they have never
// heard of.
//
// Every site that can gain a level calls this, which is what keeps the honor
// on the level rather than on the fight that happened to pay for it.
//
// A span rather than a level, because one idle stretch can carry a character
// past several -- the same shape UpgradesUnlockedBetween has, and for the same
// reason.
void GrantLevelRewards(GameState& state, int from_level, int to_level);

}  // namespace ms

#endif  // MS_SRC_GAME_STATE_H_
