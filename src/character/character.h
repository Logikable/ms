/* CharacterInstance represents a player character. It wraps a Character proto
 * (serializable state: level, job, job_stage, unspent AP and SP, allocated
 * stats, and learned skill levels) and exposes methods for leveling up, job
 * advancement, AP allocation, and inventory management. Inventory holds
 * EquipTabItem objects (EquipInstance for live items, EquipTrace for destroyed
 * items); equipped items are always EquipInstance. Character proto fields for
 * items are reserved for serialization. character.cc implements all methods.
 */
#ifndef MS_CHARACTER_H_
#define MS_CHARACTER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <vector>

#include "src/item/equip_instance.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// The advancement a job is at once it reaches `stage` (1 = 1st job). Returns
// JOB_ADVANCEMENT_UNSPECIFIED for a stage the job hasn't defined yet.
JobAdvancement AdvancementForJobStage(Job job, int stage);

// The job stage whose SP pool buys skills of `advancement` (1 = 1st job).
// Returns 0 for JOB_ADVANCEMENT_UNSPECIFIED.
int StageForAdvancement(JobAdvancement advancement);

// The stat a job's damage is built on. STAT_FIELD_UNSPECIFIED for a job with
// no primary stat defined.
// TODO: Demon Avenger's primary stat is HP; Xenon's is STR+DEX+LUK combined.
StatField PrimaryStatField(Job job);

// The jobs a character holding `job` may advance into on reaching `stage`
// (1 = 1st job), in the order they should be offered. Empty for a stage whose
// choices don't exist yet, which is what keeps the UI from offering an
// advancement it has nothing to fill.
//
// `job` is ignored at stage 1, where the only thing that arrives is a
// Beginner. Past that the choice is a branch off the job already held.
std::vector<Job> JobChoicesForStage(Job job, int stage);

// What a run of levels hands over, totalled.
struct LevelGains {
  int ap = 0;
  int sp = 0;
};

// The AP and SP granted by climbing from `from_level` to `to_level`, counting
// every level in between. Levelling several times before anything looks at the
// result is ordinary here -- one combat tick can carry a character past more
// than one threshold -- so this totals a span rather than answering for a
// single level.
//
// SP is totalled across job stages rather than kept in the per-stage pools
// LevelUp deposits it into: this answers "what did that just earn", which is a
// count, not a place to spend from. Returns nothing for a span that goes
// nowhere or backwards.
LevelGains GainsForLevels(int from_level, int to_level);

class CharacterInstance {
 public:
  CharacterInstance(std::mt19937& rng, Character character);

  // Increments level and grants 5 AP, plus 3 SP into the job stage whose level
  // band the new level falls in (levels 11-30 -> 1st job, 31-60 -> 2nd, ...).
  // Not bounded by kTrialLevelCap: the cap is on what can be earned, and this
  // is how a level is granted outright.
  void LevelUp();
  // Whether the player has ever opened the tab recorded under `key`. The keys
  // themselves are the frontend's (panel_util.h); the character only keeps the
  // record, because it is the character's progress and rides the save.
  bool TabSeen(const std::string& key) const;
  // Records that they have. Marking a tab already marked does nothing.
  void MarkTabSeen(const std::string& key);
  // Adds amount to the character's accumulated EXP, leveling up as many times
  // as the new total allows. No-op once kTrialLevelCap is reached.
  void AddExp(int64_t amount);
  // Increments job_stage, sets job to `next_job`, grants 5 bonus AP at stages
  // 3 and 4 (3rd and 4th job advancement), and grants a batch of SP for the
  // newly opened skill set.
  void AdvanceJob(Job next_job);
  // Puts every AP-allocated stat back in the pool and re-spends it for `job`:
  // the four stats drop to their base, the job's primary rises to the value a
  // fresh character of that job would carry, and what is left over becomes
  // unspent AP for the player to place. Allocated HP and MP are untouched --
  // those are level-up grants, not AP. Called on a job advancement, so a
  // Beginner's STR does not strand a Magician.
  void ResetStatsForJob(Job job);
  // Whether the character has reached an advancement it hasn't taken yet. The
  // UI offers the choice while this holds; JobChoicesForStage says what to
  // offer and AdvanceJob takes the answer.
  bool CanAdvanceJob() const;
  // Returns false if `field` is unspecified or `amount` exceeds available AP.
  bool AllocateStat(StatField field, int amount = 1);
  // Spends `amount` of the skill's job-stage SP to raise its learned level by
  // that much. Returns false if `amount` <= 0, that stage has less SP than
  // `amount`, or it would raise the level past skill.max_level().
  bool LearnSkill(const Skill& skill, int amount = 1);
  // Returns true if the character meets the level and job requirements to
  // equip the item described by `proto`.
  bool CanEquip(const EquipPrototype& proto) const;
  // Returns true if the character's level meets proto's required level.
  bool MeetsLevel(const EquipPrototype& proto) const;
  // Returns true if the character's job category matches proto's job filter.
  bool MeetsJob(const EquipPrototype& proto) const;
  // Appends item to inventory. Accepts any EquipTabItem subclass (EquipInstance
  // or EquipTrace); the caller is responsible for constructing the item.
  // Returns false and drops the item on the floor when the equip tab is full.
  bool PickUp(std::unique_ptr<EquipTabItem> item);
  // Adds `count` of the item described by `proto` to the Use/Etc stacks. Tops
  // up existing stacks of the same item first, then opens new stacks for any
  // overflow, each capped at the item's max_stack(). No-op if count <= 0.
  //
  // Takes what fits and loses the rest: topping up an existing stack costs no
  // slot, so a tab with none left can still absorb some of a drop. Returns the
  // number actually added.
  int AddStackable(const ItemPrototype& proto, int count);

  // How many more copies of `proto` the bag could take.
  //
  // For an equip that is simply the free slots, one copy apiece. For a
  // stackable it is the room in every stack of that item already open plus a
  // full stack for each free slot -- ten free slots, stacks of 200 and a stack
  // of 100 already open comes to 2100.
  int RoomFor(const EquipPrototype& proto) const;
  int RoomFor(const ItemPrototype& proto) const;

  // How many copies of `proto` the character has: worn plus carried.
  //
  // Traces do not count. A trace is the record of an item that was destroyed,
  // not a copy of it -- somebody deciding whether to buy another has none of
  // the thing itself.
  int CountOwned(const EquipPrototype& proto) const;
  // Adds `amount` meso to the character's balance. No-op if amount <= 0.
  void AddMeso(int64_t amount);
  // Sells up to `count` copies from the `index`-th stack in `category`,
  // crediting count * sell_price meso and removing the sold copies; erases the
  // stack entirely once it empties. No-op returning 0 if the index is out of
  // range, count <= 0, or the item is unsellable (sell_price 0). Returns the
  // meso earned.
  int64_t SellStackable(ItemCategory category, int index, int count);
  // Uses one copy from the `index`-th stack in `category`: applies the item's
  // effect and consumes it, erasing the stack once it empties. Returns false
  // and consumes nothing if the index is out of range or the item has no
  // effect -- an item that does nothing is not spent doing it.
  bool UseStackable(ItemCategory category, int index);
  // Buys `count` copies of `proto` at its shop_price each, deducting the meso
  // and putting the copies in the bag. All or nothing: returns false and
  // changes nothing if `count` is not positive, the item is not for sale, or
  // the character cannot pay for every copy. Each copy is a fresh item, so
  // buying two puts two separate rows in the bag.
  bool Buy(const EquipPrototype& proto, int count);
  // Moves the item at `inventory_index` into the slot indicated by its
  // EquipPrototype. If the slot was occupied, the displaced item is appended
  // to inventory. Returns false if `inventory_index` is out of range or the
  // prototype's equip_slot is unspecified.
  bool Equip(int inventory_index);
  // Moves the item in `slot` to inventory. Returns false if `slot` is
  // unspecified or unoccupied.
  bool Unequip(EquipSlot slot);
  // Applies `scroll` to the item in `slot`. Returns kScrollFail if the slot
  // is empty; otherwise returns the result of the underlying Scroll() call.
  ScrollOutcome ScrollEquipped(EquipSlot slot, const Scroll& scroll);
  // Applies `scroll` to the inventory item at `index`. Returns kScrollFail if
  // `index` is out of range; otherwise returns the result of Scroll().
  ScrollOutcome ScrollInventory(int index, const Scroll& scroll);
  // Applies a star force attempt to the item in `slot`. On kStarForceDestroy,
  // removes the item from equipped and recomputes equip stats.
  StarForceOutcome StarForceEquipped(EquipSlot slot);
  // Applies a star force attempt to the inventory item at `index`. On
  // kStarForceDestroy, removes the item from inventory.
  StarForceOutcome StarForceInventory(int index);
  // Recovers the EquipTrace at `trace_index` using the EquipInstance at
  // `base_item_index` as the replacement body. Both items are removed from
  // inventory and replaced with a new EquipInstance carrying the trace's scroll
  // stats and the star count from RecoveryStars(). The indices must refer to
  // valid, compatible inventory slots. Returns the recovery star count.
  int RecoverTrace(int trace_index, int base_item_index);

  const Character& proto() const {
    return character_;
  }
  const InventoryInstance& inventory() const {
    return inventory_;
  }
  std::vector<const EquipTrace*> traces() const;
  const std::map<EquipSlot, EquipInstance>& equipped() const {
    return equipped_;
  }
  // The `category`'s item stacks (ITEM_CATEGORY_USE or ITEM_CATEGORY_ETC), in
  // pickup order.
  const std::vector<StackableItem>& stackables(ItemCategory category) const {
    return StacksFor(category);
  }
  int64_t meso() const {
    return character_.meso();
  }
  // Available skill points for the given job stage (1 = 1st job, ...), or 0 if
  // none have been earned for it.
  int sp(int stage) const {
    return character_.sp_by_stage().contains(stage)
               ? character_.sp_by_stage().at(stage)
               : 0;
  }
  // The character's learned level in `skill` (0 = unlearned).
  int skill_level(const Skill& skill) const {
    return character_.skill_levels().contains(skill.name())
               ? character_.skill_levels().at(skill.name())
               : 0;
  }
  // Whether `advancement` is one this character has actually taken. Every
  // first job's skills draw on the same stage-1 SP pool, so the stage alone
  // does not say whose book a skill is from -- without this a Swordman could
  // spend their points on an Archer's.
  bool HasAdvancement(JobAdvancement advancement) const;
  // Whether the character has learned whatever `skill` demands be learned
  // first. True for a skill that demands nothing, which is most of them.
  // LearnSkill refuses when this is false; the skills tab asks it directly so
  // it can dim a row rather than let the player press an unspendable [+].
  bool MeetsSkillRequirement(const Skill& skill) const;
  // The whole character as one proto, with the live containers -- the equip
  // tab, the worn items and the Use/Etc stacks -- folded back into the fields
  // held for them. proto() alone does not carry those: they live in C++
  // containers and are only written here when someone asks for the lot.
  Character ToProto() const;

  // The inverse: replaces everything with what `saved` describes, resolving
  // each item's prototype by name against the catalogs.
  //
  // An item whose name is no longer in the catalogs is dropped rather than
  // guessed at, so removing something from data/ costs the player that item
  // and not their character. Bypasses the equip rules on purpose -- what was
  // worn stays worn, even if a later change to the data would refuse it now.
  void RestoreFrom(const Character& saved,
                   const std::map<std::string, EquipPrototype>& equips,
                   const std::map<std::string, ItemPrototype>& items);

  // Sum of stats from all currently equipped items. Updated automatically by
  // Equip, Unequip, and ScrollEquipped.
  const EquipStats& equip_stats() const {
    return equip_stats_;
  }
  // Whether an item of this type contributes its attack, given what is
  // equipped right now. Throwing stars arm a claw and nothing else; every
  // other item always counts. equip_stats() applies this itself -- it is
  // public so the display can show an inert attack as inert rather than
  // quietly disagreeing with the total.
  bool AttackCounts(const EquipPrototype& proto) const;

 private:
  // Recomputes equip_stats_ from the current equipped map.
  void RecomputeEquipStats();
  // The stack vector backing `category`. USE and ETC each have their own;
  // anything else falls back to the Etc stacks (fail safe).
  std::vector<StackableItem>& StacksFor(ItemCategory category);
  const std::vector<StackableItem>& StacksFor(ItemCategory category) const;

  std::mt19937& rng_;
  Character character_;
  InventoryInstance inventory_;
  std::map<EquipSlot, EquipInstance> equipped_;
  std::vector<StackableItem> use_items_;
  std::vector<StackableItem> etc_items_;
  EquipStats equip_stats_;
};

}  // namespace ms

#endif  // MS_CHARACTER_H_
