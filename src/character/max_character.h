/* The best a character can be at each level: what a player who spent well is
 * wearing and carrying when they get there.
 *
 * One fixed answer for every job. The %stat lines follow the job, as does
 * whether weapon lines use ATT or M.ATT, but nothing else here depends on the
 * job: the point of the mode is to measure fights against a known character,
 * not to find each job's optimum.
 *
 * Everything here is what //analysis:progression_sim's sweep had on reaching
 * each level -- the gear, stars, hammers, potentials, symbols, boss clears,
 * alts, V Matrix and Inner Ability -- read toward its better half (the fifth
 * of ten branches from the top), so a max character is a rich player, not an
 * impossible one. A level between two of its checkpoints takes the one below.
 */
#ifndef MS_SRC_CHARACTER_MAX_CHARACTER_H_
#define MS_SRC_CHARACTER_MAX_CHARACTER_H_

#include <map>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/item/flame.h"
#include "src/item/potential.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// What has been done to every item the character wears. Stars are capped at
// each item's own limit for its level, so a low-level item in a high-level
// outfit gets what it can, not what was asked for.
struct MaxGear {
  // The slots whose piece has both Golden Hammers used, adding two upgrade
  // slots, ending at the first EQUIP_SLOT_UNSPECIFIED. Every upgrade slot is
  // then scrolled.
  EquipSlot hammered[5] = {};
  int stars = 0;
  // The weapon's stars, set separately: it is starred first, but past 15 it
  // falls behind, having no spare copy to recover a boom with.
  int weapon_stars = 0;
  // The level whose potentials every item wears: 230 or 260, where
  // //analysis:progression_sim's sweep recorded them. 0 before cubing opens.
  int potential_level = 0;
};

// The gear a character at `level` has paid for.
MaxGear MaxGearForLevel(int level);

// The catalog keys a max character of `level` wears over their job's own
// weapon, secondary and ammunition, in the order to put them on.
std::vector<std::string> MaxOutfit(Job job, int level);

// The level of the symbol in `slot` a max character of `level` wears, or 0 for
// one they don't hold yet.
int MaxSymbolLevel(EquipSlot slot, int level);

// The potential on `track` that `slot` wears under `gear`, for a character
// whose damage is based on `primary`. Empty for a slot that takes no potential
// and for a level without that track.
//
// Every item of one kind has the same lines. The variety a real player ends up
// with is luck, not a decision, and a character whose stats change with the
// random seed makes fight measurements meaningless.
Potential MaxPotentialFor(EquipSlot slot, const MaxGear& gear,
                          StatField primary, PotentialTrack track);

// The flame a max character of `level` wears on `proto`, for a job whose damage
// is based on `primary` and helped by `secondary`. Empty below the first level
// with a flame band, and for an item that takes no flame. Like a potential,
// every item of one kind has the same lines.
FlameLines MaxFlameFor(const EquipPrototype& proto, int level,
                       StatField primary, StatField secondary);

// Spends the account's Noblesse SP, rated by the expected hit on the toughest
// boss the character's level has unlocked, the same target Ignore Defense is
// valued against below. Noblesse skills have one allocation for every preset,
// and bosses are what they are for.
void SpendMaxNoblesse(CharacterInstance& character,
                      const std::map<std::string, Skill>& skills,
                      const std::map<std::string, Boss>& bosses,
                      const std::map<std::string, Mob>& mobs);

// Spends the whole Hyper Stat pool on both presets, best value per point first,
// discarding any previous allocation. A stat's value is measured on this
// character instead of listed here (see hyper_plan.h), so the job's own stats
// decide. `skills` is the catalog combat power is read through; `bosses` and
// `mobs` are what Ignore Defense is valued against: the toughest fight the
// character's level has unlocked.
void SpendMaxHyperStats(CharacterInstance& character,
                        const std::map<std::string, Skill>& skills,
                        const std::map<std::string, Boss>& bosses,
                        const std::map<std::string, Mob>& mobs);

// Puts the best soul the character's level has unlocked on their weapon, from
// the Soul entry's level on: the
// highest tier any open boss's shard makes, with whichever of its seven lines
// hits that boss hardest. Measured, not listed, since the best line depends on
// the job. `items` is where a shard's tier is read.
void WearMaxSoul(CharacterInstance& character,
                 const std::map<std::string, Skill>& skills,
                 const std::map<std::string, Boss>& bosses,
                 const std::map<std::string, Mob>& mobs,
                 const std::map<std::string, ItemPrototype>& items);

// The level by which a max character has first beaten `boss` on `difficulty`
// alone, or 0 for a fight they haven't.
int MaxClearLevel(const std::string& boss, const std::string& difficulty);

// The alts a max account of `level` has levelled for their link skills: each
// one's line, named by its 2nd job, and level. None is on `played_line`.
struct MaxAlt {
  Job line = JOB_UNSPECIFIED;
  int level = 0;
};
std::vector<MaxAlt> MaxAlts(Job played_line, int level);

// The level a max character of `level` has bought `node` to.
int MaxMatrixLevel(const Skill& node, int level);

// The three Inner Ability lines `preset` has at `level`.
AbilityPreset MaxAbilityPreset(Activity preset, StatField primary, int level);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_MAX_CHARACTER_H_
