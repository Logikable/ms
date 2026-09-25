/* GameState holds all runtime game state: loaded static data, a seeded RNG, and
 * the active character. Built once at startup with the loaded catalogs. Not
 * copyable, because CharacterInstance holds a reference to rng.
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

// The town a character starts in and returns to when they die. A key into
// GameState::maps, and the only map name the code refers to directly; every
// other map is the player's choice.
inline constexpr char kHomeMap[] = "maple_island";

// The three modes the game can start in. kPlay is the game as a player sees it.
// kTest is the workbench, with the meso and items the upgrade and shop screens
// need without playing up to them. kMax is a character who spent well, which
// bosses are measured against.
enum class GameMode {
  kPlay,
  kTest,
  kMax,
};

// The upgrade state of a seeded character's gear. The workbench sets it from
// --hammered, --scrolled and --sf; kMax sets it from the level's band. Every
// piece is treated alike: the question is what a character at this stage hits
// for, not what one lucky item does. All unset means gear as it drops, with
// slots unspent.
struct GearSetup {
  // Both Golden Hammers used, adding two upgrade slots.
  bool hammered = false;
  // Every upgrade slot passed, with the trace for the job's main stat. The
  // slots `hammered` added are scrolled too.
  bool scrolled = false;
  // Stars, capped at the item's own limit for its level. Zero leaves it
  // unstarred. Stars only go on an item with no slots left to scroll, the
  // upgrade screen's rule, so an item with slots also needs `scrolled`.
  int stars = 0;
  // Stars for the weapon alone, which is where meso goes first and the one
  // piece worth taking further. Zero uses `stars` like everything else.
  int weapon_stars = 0;
};

// What the workbench does with its current job's book, as --skills says.
// Earlier books are bought either way, since they aren't why the tester chose
// the job.
enum class TestSkills {
  // Left unbought, with the SP unspent.
  kZero,
  // Fully bought.
  kMax,
};

// Where the workbench character starts when --job isn't given: the top of the
// Hero line as far as it's written, with everything this game gives out. It
// moves up as the line grows, since a workbench with an advancement still
// pending would have to be finished before the tester could look at anything.
inline constexpr JobAdvancement kTestAdvancement = JOB_ADVANCEMENT_HERO_V;

// The workbench settings, one per flag; kPlay ignores them. `job` is the
// advancement to start at the top of, and `level` the level to reach (0 lets
// the job decide). kMax reads both and fills in the rest itself.
//
// `autoswap_presets` is read by every mode, because the constructor is the only
// place to set it before the GameState exists. The game ships with it off and
// every sim turns it on.
struct TestOptions {
  JobAdvancement job = JOB_ADVANCEMENT_UNSPECIFIED;
  int level = 0;
  GearSetup equips;
  TestSkills skills = TestSkills::kZero;
  bool autoswap_presets = false;
  // Whether a kMax character gets link skills, which means an account with a
  // character at the top of every job line. On for the game, where max mode is
  // a fully played account; off for the sims, whose balance numbers predate
  // link skills. Other modes ignore it: a real account's roster is whatever the
  // player made.
  bool link_skills = true;
};

struct GameState {
  // Sets up the character, bag and map for `mode`. Anything the catalogs don't
  // have is skipped, so a test's state doesn't need the game's data files.
  // `test` applies to kTest only.
  //
  // `seed` fixes the random stream so a repeated run gives the same results;
  // the game leaves it unset. `sets` is a constructor argument because the
  // state can't be referenced before it's returned, and a caller who forgot
  // would get a character in a full set with no set bonus.
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
  // The equipment sets and what each tier gives. Stored here as well as given
  // to the character, because a party member's sheet must be rebuilt against
  // them to be inspected.
  std::map<std::string, EquipSet> equip_sets;
  // The bosses that can be fought, by data file stem. Passed to the constructor
  // because --mode=max prices Ignore Defense against the toughest fight the
  // character's level has unlocked, so seeding needs them.
  std::map<std::string, Boss> bosses;
  std::mt19937 rng;

  // Gives the character what the account knows that their stats depend on: the
  // Autoswap Presets setting, the account's progress, and the other characters'
  // progress per line. Stat reads ask the character, which doesn't hold the
  // account, so the two are combined here: when the state is built, when a save
  // loads, and when the option changes.
  void MirrorAccount();

  // The same three, applied to a character who isn't the one in play: what
  // putting `into` in play would give them. A character restored from a slot
  // has none of it, so anything reading their stats without this (the character
  // select's card) shows them weaker than playing them does. `roster` is the
  // account's slots, and `theirs` the slot of the one being updated, or -1 if
  // not in the list.
  void MirrorAccountOnto(CharacterInstance& into,
                         const std::vector<CharacterSave>& roster,
                         int theirs) const;

  // The character being played. The others on the account stay as protos; see
  // //src/roster.h, which swaps one in.
  CharacterInstance character;

  // What every character on the account shares: bindings, unlocks, and what the
  // player has been shown. See //src/account.h.
  AccountInstance account;

  // Which save slot the played character came from, and the characters in the
  // other slots. Kept so saving preserves them: nothing in a session reads a
  // character that isn't being played. See //src/roster.h, which swaps, adds
  // and removes slots.
  int played_slot = 0;
  std::vector<CharacterSave> inactive_characters;

  // Which slot has the offline check: the character the next launch opens on,
  // and the only one an absence pays. The player sets it on character select,
  // and playing someone else doesn't move it, which is the point of the check.
  // Equal to played_slot until the player moves it.
  int offline_slot = 0;

  // The multiplier on combat EXP. kTest gets a bonus so the workbench can level
  // up in one sitting instead of farming the early levels at normal speed;
  // kPlay earns normally.
  int exp_multiplier = 1;

  // The name of the map being farmed (a key into `maps`); empty means none.
  std::string current_map;

  // The player's boss fight settings, from the boss screen's options row. Not
  // saved: practice pays nothing, and a setting remembered across a restart
  // could quietly cost someone a real clear. Sent in PlayerInfo so the server
  // can make a whole party use one set.
  BossOptions boss_options;

  // When the played character was last put into play, in seconds since the Unix
  // epoch, which character select sorts by. Set when the game starts and on
  // every switch, so the played character is always the most recent.
  int64_t last_played_unix_seconds;

  // When this character was created, in seconds since the Unix epoch. Set here
  // instead of at save time so both missing cases work: a new game gets the
  // time it started, and a save written before this field existed keeps this
  // value instead of loading zero. Loading overwrites it only if the save has
  // one.
  int64_t created_unix_seconds;

  // When the game was last saved, in seconds since the Unix epoch, or 0 if this
  // character has never been saved. Offline progress is measured from this; see
  // //src/combat:offline.
  int64_t last_seen_unix_seconds = 0;

  // Total seconds this character has been in play, across all sessions. Added
  // up by the frontend's tick from a monotonic clock, so changing the system
  // time or a daylight-saving change neither adds nor removes playtime.
  double playtime_seconds = 0.0;

  // The other party members, rebuilt from the sheets they sent, for their
  // skills that affect this character. Filled only during a party fight and
  // cleared when it ends, so readers don't need to check.
  std::vector<CharacterInstance> party;
};

// Puts a brand new character into play: a level-1 Beginner wearing the Sword on
// Maple Island. Both ways to get one (the first launch, and Create on character
// select) come through here, so a new character is the same either way.
// Overwrites whatever was being played, so a caller with a character to keep
// must save them first.
void SeedNewCharacter(GameState& state);

// Gives the character `amount` EXP and every level it buys, plus the Burning
// levels owed for being behind the rest of the roster and the rewards for the
// whole level range. All combat EXP comes through here, so a level costs and
// pays the same wherever it's earned.
void AwardExp(GameState& state, int64_t amount);

// Grants whatever levelling from `from_level` to `to_level` gives: honor for
// each level, and anything the catalogs provide (reaching 200 gives a symbol,
// which is why this takes the whole state). Every place that can gain a level
// calls this, so honor is tied to levels, not fights. It takes a range, since
// one idle period can cover several levels.
void GrantLevelRewards(GameState& state, int from_level, int to_level);

// The earliest level at which a piece of gear can be owned, which isn't always
// the level it can be worn at: token-bought gear waits for the fight that pays
// for it. Root Abyss gear is worn at 150 and paid for by bosses opening at 200;
// AbsoLab is worn at 160 and paid for by bosses opening at 210. Everything else
// can be owned as soon as it can be worn.
int OwnedFromLevel(const EquipPrototype& proto);

}  // namespace ms

#endif  // MS_SRC_GAME_STATE_H_
