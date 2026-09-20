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
#include "src/protos/multiplayer.pb.h"
#include "src/protos/save.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// The town a character begins in and is returned to when they die. A key into
// GameState::maps, and the one map name the code names for itself -- every
// other one is the player's choice.
inline constexpr char kHomeMap[] = "maple_island";

// The three states the game can be started in. kPlay is the game as a player
// meets it. kTest is the workbench -- the meso and the spread of items the
// upgrade and shop screens need without playing up to them. kMax is the
// ceiling a player who spent well stands in, which a boss is measured
// against.
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
inline constexpr JobAdvancement kTestAdvancement = JOB_ADVANCEMENT_HERO_V;

// The workbench's own settings, one per flag; kPlay ignores them. `job` is the
// advancement to start at the top of and `level` the level to arrive at, 0
// leaving it to the job. kMax reads both and fills the rest itself.
//
// `autoswap_presets` is read by EVERY mode: the constructor is the only place
// to throw it, a GameState not being nameable before it is returned. The game
// ships it off and every sim turns it on.
struct TestOptions {
  JobAdvancement job = JOB_ADVANCEMENT_UNSPECIFIED;
  int level = 0;
  GearSetup equips;
  TestSkills skills = TestSkills::kZero;
  bool autoswap_presets = false;
  // Whether a kMax ceiling carries the LINK SKILLS, which means an account
  // with a character at the top of every job line. On for the game, whose
  // ceiling is a played-out account; off for the sims, whose balance numbers
  // were taken before link skills existed. Ignored by every other mode -- a
  // played account's roster is whatever the player made.
  bool link_skills = true;
};

struct GameState {
  // Puts the character, bag and map into the state `mode` begins from.
  // Anything the catalogs do not name is skipped, so a test's state need not
  // carry the game's data files. `test` applies to kTest alone.
  //
  // `seed` FIXES the random stream, so a repeated run pays what it paid last
  // time; the game leaves it unset. `sets` is a constructor argument because
  // the state cannot be named before it is returned, and a caller who forgot
  // would get a character in a full set paid nothing for it.
  GameState(std::map<std::string, EquipPrototype> equips,
            std::map<std::string, Scroll> scrolls,
            std::map<std::string, ItemPrototype> items,
            std::map<std::string, Mob> mobs,
            std::map<std::string, MapData> maps,
            std::map<std::string, Skill> skills = {},
            GameMode mode = GameMode::kPlay, TestOptions test = {},
            std::optional<unsigned int> seed = std::nullopt,
            std::map<std::string, EquipSet> sets = {},
            std::map<std::string, Boss> bosses = {});
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
  // The bosses that can be fought, by data file stem. Passed to the
  // constructor: --mode=max prices Ignore Defense against the toughest fight
  // the character's level has opened, so the seeding has to know them.
  std::map<std::string, Boss> bosses;
  std::mt19937 rng;

  // Hands the character what the ACCOUNT knows and they are read for: the
  // Autoswap Presets switch, how far the account has climbed, and what the
  // others have climbed per line. Every stat read asks the character and none
  // of them holds the account, so the two have to be put together here: when
  // the state is built, when a save arrives, and when the option is thrown.
  void MirrorAccount();

  // The same three, onto a sheet that is NOT the one in play: what putting
  // `theirs` into play would hand them. A character restored from a slot
  // carries none of it, so anything reading their stats without this -- the
  // character select's card -- shows a weaker character than playing them
  // does. `roster` is the account's slots and `theirs` the one being mirrored
  // onto, -1 for a list they are not in.
  void MirrorAccountOnto(CharacterInstance& into,
                         const std::vector<CharacterSave>& roster,
                         int theirs) const;

  // The character being played. The others on the account stay as the protos
  // they arrived as -- see //src/roster.h, which is what swaps one in.
  CharacterInstance character;

  // What every character on the account shares: bindings, unlocks, and what
  // the player has been shown. See //src/account.h.
  AccountInstance account;

  // Which slot of the save the played character came from, and the characters
  // in the other slots. Held so that saving keeps them: nothing in a session
  // reads a character that is not being played. See //src/roster.h, which is
  // where a slot is swapped, added or dropped.
  int played_slot = 0;
  std::vector<CharacterSave> inactive_characters;

  // Which slot carries the offline check: the character the next launch opens
  // on, and the only one an absence pays. The player sets it on the character
  // select, and playing somebody else does NOT take it off them -- which is
  // the whole point of the check. Equal to played_slot until they move it.
  int offline_slot = 0;

  // What every EXP award from combat is multiplied by. kTest gets a standing
  // bonus so the workbench can climb the level ladder in a sitting rather than
  // farming the early levels at play speed; kPlay earns what it earns.
  int exp_multiplier = 1;

  // Name of the map being farmed (key into `maps`); empty means none.
  std::string current_map;

  // What the player has switched on for a boss fight -- see the boss screen's
  // options row. NOT saved: practice pays nothing, and a switch remembered
  // across a restart would quietly cost somebody a real clear. It rides in
  // PlayerInfo so the server can hold a whole party to one set of them.
  BossOptions boss_options;

  // When the played character was last put into play, as seconds since the
  // Unix epoch: what the character select sorts the roster on. Stamped when
  // the game starts and again on every switch, so the character being played
  // is always the newest.
  int64_t last_played_unix_seconds;

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
  // skills of theirs that reach this character. Filled only while a party
  // FIGHT is on and emptied when it ends, so nothing reading it has to ask.
  std::vector<CharacterInstance> party;
};

// Puts a brand new character into play: a level-1 Beginner armed with the
// Sword and standing on Maple Island. Both doors onto one come through here
// -- the first launch, and Create on the character select -- so a new
// character is the same character whichever made them. Whatever was being
// played is overwritten, so a caller with a character to keep writes them
// somewhere first.
void SeedNewCharacter(GameState& state);

// Hands the character `amount` EXP and every level it buys: the Burning
// levels they are owed for standing behind the rest of the roster, and the
// rewards the whole span pays. The one door combat's EXP comes through, so a
// level costs and pays the same wherever it was earned.
void AwardExp(GameState& state, int64_t amount);

// Whatever climbing from `from_level` to `to_level` grants: the honor every
// level pays, and whatever the catalogs owe -- reaching 200 is handed a
// symbol, which is why this takes the whole state. EVERY site that can gain a
// level calls it, which keeps the honor on the level rather than on the fight.
// A span, since one idle stretch can carry a character past several.
void GrantLevelRewards(GameState& state, int from_level, int to_level);

// The level a piece of gear can first be OWNED at, which is not always the
// level it can be worn at: token-bought gear waits on the fight that pays for
// it. Root Abyss gear is worn at 150 and paid for by bosses that open at 200;
// AbsoLab is worn at 160 and paid for by bosses that open at 210. Everything
// else is owned the moment it can be worn.
int OwnedFromLevel(const EquipPrototype& proto);

}  // namespace ms

#endif  // MS_SRC_GAME_STATE_H_
