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

#include "src/character/consumables.h"
#include "src/character/hyper_stats.h"
#include "src/character/inner_ability.h"
#include "src/item/equip_instance.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// What an AP stat reads with nothing spent on it, and the STR a fresh Beginner
// carries instead of it. The nine between them is AP the game handed the
// character at creation and spent on their behalf, so it counts as spent.
inline constexpr int kBaseStat = 4;
inline constexpr int kBeginnerStr = 13;

// What a character is called before the player names one, which doubles as the
// invitation to. Every character carries it: a save from before the field was
// written comes forward with it rather than with a blank row.
inline constexpr char kDefaultUsername[] = "Set Username";

// The longest name the player may set, GMS's own limit. The Character panel is
// a fixed width, so a name is capped rather than allowed to widen it.
inline constexpr int kMaxUsernameLength = 12;

// Every AP the game has ever handed a character at `level` holding
// `job_stage` advancements, spent and unspent together. AP is only ever moved
// between the pool and the stats, never destroyed, so a save whose books do
// not come to this has drifted -- see CharacterInstance::ReconcileAp.
int ExpectedTotalAp(int level, int job_stage);

// The highest job stage the game has an advancement level for. A stage with no
// branches written yet still counts -- AdvancementForJobStage simply answers
// JOB_ADVANCEMENT_UNSPECIFIED for it -- so anything walking every stage a job
// could be at walks to here and keeps working when the next one lands.
inline constexpr int kMaxJobStage = 6;

// The advancement a job is at once it reaches `stage` (1 = 1st job). Returns
// JOB_ADVANCEMENT_UNSPECIFIED for a stage the job hasn't defined yet.
JobAdvancement AdvancementForJobStage(Job job, int stage);

// The job stage whose SP pool buys skills of `advancement` (1 = 1st job).
// Returns 0 for JOB_ADVANCEMENT_UNSPECIFIED.
int StageForAdvancement(JobAdvancement advancement);

// The job an advancement makes a character -- the inverse of
// AdvancementForJobStage, which several jobs answer alike at stage 1 but only
// one does at the stage the advancement belongs to. JOB_UNSPECIFIED for an
// advancement no job takes.
Job JobForAdvancement(JobAdvancement advancement);

// The level at which a character at `stage` is offered their next
// advancement, and so the last level that stage spans. Zero past the last
// advancement the game defines.
int NextAdvancementLevel(int stage);

// The equip catalog keys a character is handed on advancing into `job`, or an
// empty list for a job with no starting gear defined. Here rather than beside
// PerformJobAdvancement so that the workbench, which is underneath it, can ask
// the same question -- one table, not two that drift.
std::vector<std::string> StarterEquipsFor(Job job);

// The weapons `job` is built around, in the order to name them. Not what it
// may legally hold -- a Swordman may hold any warrior weapon -- but what its
// skills and its damage assume, which is what a player choosing a job needs
// told. Empty for a job with no weapons defined.
//
// A 1st job names the one weapon it is handed on advancing, since the fork is
// not real until the 2nd. A 3rd job names its 2nd job's pair: nothing about
// the weapon changes on the way up.
std::vector<EquipType> ExpectedWeapons(Job job);

// The advancement whose off hand `type` is, or JOB_ADVANCEMENT_UNSPECIFIED for
// a type that is not a secondary at all. The stats of two secondaries in one
// branch are identical, so this is the only thing standing between a Fighter
// and a Page's rosary.
JobAdvancement AdvancementForSecondary(EquipType type);

// The stat a job's damage is built on. STAT_FIELD_UNSPECIFIED for a job with
// no primary stat defined.
// TODO: Demon Avenger's primary stat is HP; Xenon's is STR+DEX+LUK combined.
StatField PrimaryStatField(Job job);

// The jobs a character holding `job` may advance into at `stage` (1 = 1st
// job), in the order to offer them. Empty for a stage whose choices do not
// exist yet, which keeps the UI from offering an advancement with nothing
// behind it. `job` is ignored at stage 1, where every character is a Beginner.
std::vector<Job> JobChoicesForStage(Job job, int stage);

// What a run of levels hands over, totalled.
struct LevelGains {
  int ap = 0;
  int sp = 0;
  // Hyper SP, which is its own pool rather than a stage of the one above --
  // see Character::hyper_sp.
  int hyper_sp = 0;
};

// What climbing from `from_level` to `to_level` hands over. A span rather than
// one level, because a single combat tick can carry a character past several
// thresholds. SP is totalled across job stages: this answers "what did that
// just earn", which is a count, not a place to spend from.
LevelGains GainsForLevels(int from_level, int to_level);

// Rows on the shop's buy-back shelf. One per sale, so a player who sold 300 of
// something in two goes finds two of them.
inline constexpr int kBuyBackSlots = 32;

class CharacterInstance {
 public:
  CharacterInstance(std::mt19937& rng, Character character);

  // Increments level and grants 5 AP, plus 3 SP into the job stage whose level
  // band the new level falls in (levels 11-30 -> 1st job, 31-60 -> 2nd, ...).
  // Not bounded by kTrialLevelCap: the cap is on what can be earned, and this
  // is how a level is granted outright.
  void LevelUp();
  // When this character last cleared `boss` at `difficulty`, as a Unix time,
  // or 0 for never. Whether that clear has expired is the reset clock's
  // question -- see boss_reset.h.
  int64_t BossClearedAt(const std::string& boss,
                        const std::string& difficulty) const;
  // Records a clear at `now`, replacing any earlier one for the same pair.
  void RecordBossClear(const std::string& boss, const std::string& difficulty,
                       int64_t now);
  // Whether the scroll recorded under `key` is pinned to the top of the scroll
  // list. The keys are the frontend's; the character keeps the record because
  // which stats are worth chasing is this character's business, not the
  // account's.
  bool ScrollPinned(const std::string& key) const;
  // Pins it if it is loose, loosens it if it is pinned.
  void ToggleScrollPin(const std::string& key);
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
  // Spends `amount` SP to raise the skill's learned level by that much -- the
  // Hyper SP pool for a Hyper Skill, and the skill's job-stage SP for every
  // other. Returns false if `amount` <= 0, the pool has less than `amount`,
  // the character is below skill.required_level(), or it would raise the level
  // past skill.max_level().
  bool LearnSkill(const Skill& skill, int amount = 1);
  // Raises `field` by `amount` levels in `preset`, spending the points every
  // one of them costs. All or nothing: returns false and spends nothing when
  // the stat is locked, the levels would pass the cap, or the pool is short.
  bool AllocateHyperStat(HyperStatField field, StatPreset preset,
                         int amount = 1);
  // Puts every point in `preset` back in the pool. Free, and the only way out
  // of an allocation.
  void ResetHyperStats(StatPreset preset);
  // Returns true if the character meets the level and job requirements to
  // equip the item described by `proto`. Asked of the catalog as much as of
  // the bag, so it says nothing about whether a slot is free for it -- that
  // is SlotToFill.
  bool CanEquip(const EquipPrototype& proto) const;
  // Returns true if the character's level meets proto's required level.
  bool MeetsLevel(const EquipPrototype& proto) const;
  // Returns true if the character's job category matches proto's job filter.
  bool MeetsJob(const EquipPrototype& proto) const;
  // Appends item to inventory. Accepts any EquipTabItem subclass (EquipInstance
  // or EquipTrace); the caller is responsible for constructing the item.
  // Returns false and drops the item on the floor when the equip tab is full.
  bool PickUp(std::unique_ptr<EquipTabItem> item);
  // Throws away everything on the equip tab, worn gear untouched. The
  // workbench's, so a tester opens a bag holding only what they put there --
  // nothing in the game empties one.
  void ClearEquipInventory();
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
  // How many copies of a stackable the character is carrying, summed across
  // every stack of it. Matched on name, as CountOwned is.
  int CountStackable(const ItemPrototype& proto) const;
  int CountStackable(ItemCategory category, const std::string& name) const;
  // Spends `count` copies of a named stackable, emptying stacks as it goes.
  // All or nothing: returns false and takes nothing if the character is not
  // holding that many.
  bool ConsumeStackable(ItemCategory category, const std::string& name,
                        int count);
  // Adds `amount` meso to the character's balance. No-op if amount <= 0.
  void AddMeso(int64_t amount);
  // Adds `amount` honor, the pool an Inner Ability reset is paid out of. No-op
  // if amount <= 0. Nothing in the game calls this yet.
  void AddHonor(int64_t amount);

  /* The potions. A pot is owned or rented, and on or off; the pair decides
   * both what it does and what it costs. See //src/character/consumables.h.
   */

  // Whether the character is high enough for the first pot to be had, and so
  // for the Pots tab to open at all.
  bool consumables_unlocked() const {
    return character_.level() >= kConsumableUnlockLevel;
  }
  // Whether the character bought `type` outright, so nothing is ever charged
  // for it again.
  bool ConsumableOwned(ConsumableType type) const;
  // Whether the player has it switched on. Says nothing about their level: an
  // owned pot stays switched on through a character who cannot yet use it.
  bool ConsumableActive(ConsumableType type) const;
  // Whether it is actually doing anything -- switched on, and at the level it
  // opens at. Everything reading a pot's effect asks this rather than the two
  // above.
  bool ConsumableInEffect(ConsumableType type) const;
  // Switches it on if it is off and off if it is on, and says which it now
  // is. Refuses a pot this character's level has not opened, leaving it off.
  bool ToggleConsumable(ConsumableType type);
  // Buys `type` outright at its permanent price. All or nothing: takes no
  // meso and buys nothing when the purse is short, when it is already owned,
  // or when the level has not opened it.
  bool BuyConsumable(ConsumableType type);
  // Charges `procs` procs of `type` -- seconds of farming for one pot, boss
  // entries for the other -- and returns the meso actually taken. A purse that
  // cannot cover it pays what it has and stops at 0; the pot works either way.
  // Nothing at all for a pot that is owned, off, or not yet open.
  int64_t ChargeConsumable(ConsumableType type, double procs);
  // Sells up to `count` copies from the `index`-th stack in `category`,
  // crediting count * sell_price meso and removing the sold copies; erases the
  // stack entirely once it empties. No-op returning 0 if the index is out of
  // range or count <= 0. Returns the meso earned, which is 0 for an item worth
  // nothing -- that is a sale, not a refusal, and it is how a stack of
  // currency is thrown away.
  int64_t SellStackable(ItemCategory category, int index, int count);
  // The shop's buy-back shelf, newest sale first. Reading it is the panel's
  // business; BuyBack is the only thing that takes one off.
  const google::protobuf::RepeatedPtrField<BuyBackEntry>& buy_backs() const {
    return character_.buy_backs();
  }
  // Buys back `count` copies from buy-back entry `index`, at the price the
  // sale paid. `count` is ignored for an equip, which is always one item.
  // Prototypes are resolved by name against the catalogs, as RestoreFrom does,
  // so an item dropped from data/ cannot be bought back.
  //
  // All or nothing, on the same terms as Buy: returns false and changes
  // nothing unless the character can pay for every copy and the bag can hold
  // them. Buying part of a stackable row leaves the rest of it on the shelf.
  bool BuyBack(int index, int count,
               const std::map<std::string, EquipPrototype>& equips,
               const std::map<std::string, ItemPrototype>& items);

  // Sells the equip-tab item at `index`, crediting its prototype's sell_price
  // and removing it from the bag. Returns the meso earned, or 0 if the index
  // is out of range.
  //
  // Zero is not a refusal: an item worth nothing still goes. What the copy
  // carries is worth nothing either -- scrolls and stars pay out at the base
  // item's price, and a trace pays out at none.
  int64_t SellEquip(int index);
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
  //
  // An item priced at zero is for sale and costs nothing. Not naming a price
  // at all is what puts an item outside the shop.
  bool Buy(const EquipPrototype& proto, int count);
  // Buys `count` copies of `proto` with the token it names, spending
  // `count * token_price` of them out of the Etc tab. The caller resolves
  // `token` from the prototype's token_item, since only it holds the catalog.
  //
  // All or nothing on the same terms as Buy. An item with no token price, or
  // an item passed as a token that is not one, buys nothing.
  bool BuyWithToken(const EquipPrototype& proto, const ItemPrototype& token,
                    int count);
  // As above for a stackable. All or nothing on the same terms: the price is
  // checked in one go and the bag's room up front, so a purchase the character
  // cannot finish never takes the meso for the part of it that would fit.
  bool Buy(const ItemPrototype& proto, int count);
  // Moves the item at `inventory_index` into the slot SlotToFill picks for it.
  // If that slot was occupied, the displaced item takes the position the
  // equipped one leaves. Returns false if `inventory_index` is out of range or
  // the item has nowhere to go.
  bool Equip(int inventory_index);
  // The slot this item would be worn in: the first free slot of its family,
  // the first of them once they are all full, and EQUIP_SLOT_UNSPECIFIED when
  // the item cannot be worn at all.
  //
  // Nowhere to go means one of two things. An item that names no slot is not
  // equipment. A family that already holds this same item is GMS's rule that
  // no two of the four rings are the same ring -- a one-slot family is exempt,
  // because putting a second copy of a hat on is the swap it looks like.
  EquipSlot SlotToFill(const EquipPrototype& proto) const;
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
  // removes the item from equipped and recomputes equip stats. Spends
  // StarForceCost first, and returns kStarForceNoMeso without rolling if the
  // character cannot afford it.
  StarForceOutcome StarForceEquipped(EquipSlot slot);
  // Applies a star force attempt to the inventory item at `index`. On
  // kStarForceDestroy, removes the item from inventory. Priced as above.
  StarForceOutcome StarForceInventory(int index);
  // Drives a golden hammer into the item in `slot` / at `index`: one more
  // upgrade slot for kGoldenHammerCost. Returns false, spending nothing, when
  // the slot is empty, the item will take no more hammers, or the purse will
  // not cover it.
  bool HammerEquipped(EquipSlot slot);
  bool HammerInventory(int index);

  // Recovers the EquipTrace at `trace_index` using the EquipInstance at
  // `base_item_index` as the replacement body. Both items are removed from
  // inventory and replaced with a new EquipInstance carrying the trace's scroll
  // stats and the star count from RecoveryStars(). The indices must refer to
  // valid, compatible inventory slots. Returns the recovery star count.
  int RecoverTrace(int trace_index, int base_item_index);

  // The player's name for this character. Never empty: a character built or
  // loaded without one answers kDefaultUsername.
  const std::string& username() const {
    return character_.name();
  }
  // Names the character. Silently ignores an empty name -- the panel treats
  // one as "leave it alone", and nothing else should be able to clear it.
  void SetUsername(const std::string& name);

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
  // Available Hyper SP, which no stage holds -- see Character::hyper_sp.
  int hyper_sp() const {
    return character_.hyper_sp();
  }
  // Every Hyper Stat point the character's level has ever paid out.
  int hyper_stat_points() const;
  // What is left of them once `preset` is paid for.
  int hyper_stat_points_left(StatPreset preset = StatPreset::kFarming) const;
  // The level `field` is raised to in `preset`.
  int hyper_stat_level(HyperStatField field,
                       StatPreset preset = StatPreset::kFarming) const;
  // What `field` is worth to this character right now, in the units the stat
  // is stated in. Zero for one they have not raised.
  double hyper_stat_bonus(HyperStatField field,
                          StatPreset preset = StatPreset::kFarming) const;
  // The highest level any of their stats may reach.
  int max_hyper_stat_level() const;

  // Honor left to spend on an Inner Ability reset.
  int64_t honor() const {
    return character_.honor();
  }
  // Whether the character is high enough for their Inner Ability to pay --
  // and, since the two go together, for the panel to open at all.
  bool inner_ability_unlocked() const {
    return character_.level() >= kInnerAbilityUnlockLevel;
  }
  // The three lines `preset` is holding, and the rank of the whole.
  const AbilityPreset& ability(StatPreset preset = StatPreset::kFarming) const {
    return PresetOf(character_.inner_ability(), preset);
  }
  // What one reset of `preset` would cost at the lines it is holding now.
  int64_t ability_reset_cost(StatPreset preset = StatPreset::kFarming) const;
  // Holds or frees the line at `index`, by the rules SetAbilityLineLocked
  // states. Returns whether anything changed.
  bool LockAbilityLine(int index, bool locked,
                       StatPreset preset = StatPreset::kFarming);
  // Pays the reset out of the honor pool and rerolls `preset`. All or nothing:
  // takes no honor and rolls nothing if the pool is short or the panel is not
  // open to this character yet.
  bool ResetAbility(StatPreset preset = StatPreset::kFarming);

  // The points `skill` is bought with: the Hyper pool for a Hyper Skill, and
  // its job stage's for every other. Asked here so the panel offering the
  // skill, the screen counting out the points and LearnSkill itself cannot
  // disagree about which pool is being spent.
  int SpFor(const Skill& skill) const {
    return skill.hyper() ? hyper_sp()
                         : sp(StageForAdvancement(skill.job_advancement()));
  }
  // The character's learned level in `skill` (0 = unlearned).
  //
  // A Vengeance form is learned to whatever its Benevolence skill was bought
  // to: it is the same row of the same book, so it reads that skill's level
  // rather than one of its own. See Skill.replaces_skill_name.
  int skill_level(const Skill& skill) const {
    const std::string& key = skill.replaces_skill_name().empty()
                                 ? skill.name()
                                 : skill.replaces_skill_name();
    return character_.skill_levels().contains(key)
               ? character_.skill_levels().at(key)
               : 0;
  }
  // Whether the player has `name` switched on. False for a skill that is not
  // a toggle, which never appears in the list. See Skill.toggle.
  bool SkillToggledOn(const std::string& name) const;
  // Switches `skill` on if it is off and off if it is on, and says which it
  // now is. Refuses a skill that is not a toggle or is not learned, leaving it
  // off: nothing the player has not bought can be switched on.
  bool ToggleSkill(const Skill& skill);
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

  // Puts the character's AP back on its books and returns what it was off by,
  // zero when it already balanced. Short, and the difference arrives as
  // unspent AP; long, and it comes out of the pool first and off the stats
  // after, the primary last. Called on loading a save, where a character grown
  // under an older set of rules is the thing that can be off.
  int ReconcileAp();

  // Puts the character's learned skills back inside their books and returns
  // how many points had to move. A skill taught past a max_level the data has
  // since lowered is cut to that max, and every point it gives up is spent
  // again on a random skill of the SAME book that can still take one -- below
  // its own max, its requirement met, its level reached.
  //
  // Same book because that is what keeps the SP ledger straight: those points
  // came out of one stage's pool, which never moves. A book whose total is the
  // SP its levels pay always has somewhere to put them, so the pool is only
  // the last resort. Called on loading a save, beside ReconcileAp.
  int ReconcileSkills(const std::map<std::string, Skill>& skills);

  // Puts both Hyper Stat allocations back inside what the character's level
  // has paid for, and returns how many points had to move. A stat past the
  // level cap is cut to it, a locked one is emptied, and an allocation that
  // outspends the pool gives up its highest levels first -- they are the
  // expensive ones. Called on loading a save, beside ReconcileAp.
  int ReconcileHyperStats();

  // Sum of stats from all currently equipped items. Updated automatically by
  // Equip, Unequip, and ScrollEquipped.
  const EquipStats& equip_stats() const {
    return equip_stats_;
  }
  // Spare copies of the Arcane Symbol for `slot` sitting in the equip bag.
  // Traces do not count, as they never do -- see CountOwned.
  int SpareSymbols(EquipSlot slot) const;
  // Absorbs up to `count` of those spares into the symbol worn in `slot`, each
  // worth one EXP plus whatever it had banked itself, and throws them away.
  // Returns how many it took, which is 0 if that slot holds no symbol.
  //
  // What the EXP buys is not bought here: raising the level is a step of its
  // own, and it is paid for in meso -- see LevelUpSymbol.
  int CombineSymbols(EquipSlot slot, int count);
  // Raises the Arcane Symbol worn in `slot` one level, charging the meso the
  // rung costs and carrying any excess EXP into the next one. Returns false
  // and takes nothing unless the slot holds a symbol that has taken its
  // duplicates and the purse covers the price.
  bool LevelUpSymbol(EquipSlot slot);
  // Arcane Force the character carries: what the worn Arcane Symbols come to,
  // plus what the Hyper Stat adds. What every Arcane River map measures them
  // against -- see ArcaneFactorsFor. Zero for anyone wearing no symbol and
  // holding no points there, which is everyone below level 200.
  int arcane_force(StatPreset preset = StatPreset::kFarming) const;
  // Whether an item of this type contributes its attack, given what is
  // equipped right now. Throwing stars arm a claw and nothing else; every
  // other item always counts. equip_stats() applies this itself -- it is
  // public so the display can show an inert attack as inert rather than
  // quietly disagreeing with the total.
  bool AttackCounts(const EquipPrototype& proto) const;
  // The type of weapon in hand, or EQUIP_TYPE_UNSPECIFIED with the slot empty.
  // What the skills that demand a particular weapon are asked against.
  EquipType weapon_type() const;
  // Whether anything is worn in the secondary slot. Nothing in the catalog
  // goes there yet, so this is false for every shipped character -- see
  // Skill.requires_secondary, which waits on it.
  bool has_secondary() const;

  // Teaches the character which sets their gear belongs to. Data rather than
  // state: what is earned comes from what is worn, so this is handed over once
  // at startup and never saved. A caller with no sets in play need never call
  // it, and the character then earns nothing from any of them.
  void UseEquipSets(std::map<std::string, EquipSet> sets);
  // Every set tier the worn pieces have earned, in no particular order. Tiers
  // are cumulative, so four pieces of a set answer with both its three and its
  // four. Read by DerivedStatsFor, which folds them in beside the passives --
  // a set bonus grants what a passive grants.
  const std::vector<SkillEffect>& set_bonuses() const {
    return set_bonuses_;
  }
  // The sets the character knows about, keyed as their data files were loaded.
  const std::map<std::string, EquipSet>& equip_sets() const {
    return equip_sets_;
  }
  // Whether the item named is worn right now, by display name.
  bool IsWearing(const std::string& item_name) const;
  // The item of `family` being worn, by display name, or "" for none. A set
  // slot that no single item can fill -- a weapon belongs to one class -- names
  // a family instead, and this is what answers it.
  std::string WornOfFamily(const std::string& family) const;
  // The piece of this member that is on, or "" for none. A slot naming
  // several alternates is filled by any one of them, and only ever by one.
  std::string WornOfMember(const EquipSetMember& member) const;
  // How many pieces of `set` are worn right now. What every tier is measured
  // against, and what the inspect screen greys its unearned tiers by.
  int PiecesWornOf(const EquipSet& set) const;

 private:
  // Gives a nameless character kDefaultUsername. Both doors a Character comes
  // in through call it, which is what makes username() never empty.
  void EnsureUsername();
  // Seeds both Inner Ability presets with the three lines every character is
  // handed. Runs at construction, so a save written before Inner Ability
  // existed comes back holding them.
  void EnsureInnerAbility();
  // Spends one star force attempt's price, or returns false and spends
  // nothing. Both StarForce entry points call it before they roll.
  bool PayForStarForce(const EquipInstance& item);
  bool PayForHammer(const EquipInstance& item);
  // Puts a sale on the buy-back shelf, newest first, and drops the oldest row
  // once the shelf is full.
  void RecordSale(BuyBackEntry entry);
  // The two halves of BuyBack, which shares only the lookup of the row. Each
  // owns its own affordability and room checks, because what "room" means is
  // a slot for one and a share of every open stack for the other.
  bool BuyBackEquip(int index, const BuyBackEntry& entry,
                    const std::map<std::string, EquipPrototype>& equips);
  bool BuyBackStack(int index, const BuyBackEntry& entry, int count,
                    const std::map<std::string, ItemPrototype>& items);

  // One allocation's half of ReconcileHyperStats, returning the points it had
  // to take back.
  int ReconcileHyperPreset(StatPreset preset);
  // Recomputes equip_stats_, arcane_force_ and set_bonuses_ from the current
  // equipped map.
  void RecomputeEquipStats();
  // Rebuilds set_bonuses_: every tier of every known set that the worn pieces
  // reach. Cumulative, so a four-piece set contributes both its tiers.
  void RecomputeSetBonuses();
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
  int arcane_force_ = 0;
  // Meso the pots have run up and not yet been charged for, always under 1.
  // The live tick charges three times a second, so without this a potion at
  // 1,000 a second would quietly cost 999. Not saved: it is worth less than
  // the smallest coin.
  double consumable_debt_ = 0.0;
  std::map<std::string, EquipSet> equip_sets_;
  std::vector<SkillEffect> set_bonuses_;
};

}  // namespace ms

#endif  // MS_CHARACTER_H_
