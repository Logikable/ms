/* CharacterInstance is a player character. It wraps a Character proto (level,
 * job, job stage, unspent AP and SP, allocated stats, learned skill levels) and
 * adds methods for levelling, job advancement, AP allocation and inventory.
 *
 * The inventory holds EquipTabItem objects: EquipInstance for live items and
 * EquipTrace for destroyed ones. Worn items are always EquipInstance. The
 * Character proto's item fields are only filled in for saving.
 */
#ifndef MS_CHARACTER_H_
#define MS_CHARACTER_H_

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <vector>

#include "src/character/consumables.h"
#include "src/character/equip_presets.h"
#include "src/character/hyper_stats.h"
#include "src/character/inner_ability.h"
#include "src/character/link.h"
#include "src/character/skill_placement.h"
#include "src/item/currency.h"
#include "src/item/equip_instance.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/item/stack_tab.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// The gear one preset wears: its own item in a slot where it has one, and the
// first preset's item everywhere else. It points into the character's worn
// items, so it is only valid until something is equipped or taken off.
using WornGear = std::map<EquipSlot, const EquipInstance*>;

// An AP stat's value with nothing spent on it, and the STR a new Beginner
// starts with. The nine points between them count as AP the game spent for the
// character.
inline constexpr int kBaseStat = 4;
inline constexpr int kBeginnerStr = 13;

// The name a character has before the player sets one, which also prompts them
// to set it. Older saves without a name get this one.
inline constexpr char kDefaultUsername[] = "Set Username";

// The longest name a player may set. Every column showing a name is sized from
// this, except the boss arena's twelve-wide nameplate, which scrolls it.
inline constexpr int kMaxUsernameLength = 20;

// All the AP a character at `level` and `job_stage` has received, spent and
// unspent. AP is only ever moved, never destroyed, so a save whose total
// differs has drifted; see CharacterInstance::ReconcileAp.
int ExpectedTotalAp(int level, int job_stage);

// The highest job stage with an advancement level defined. It counts stages
// that have no branches yet, so code looping over every stage keeps working
// when the next one is added.
inline constexpr int kMaxJobStage = 6;

// The last job stage that has branches. GMS has a 6th, which kAdvancementLevels
// names but nothing grants, so a 5th job levels to the cap.
inline constexpr int kLastJobStage = 5;

// The last job stage whose book is bought with SP. 5th job skills cost V
// Points, so no level pays SP toward that stage and its pool stays empty.
inline constexpr int kLastSpJobStage = 4;

// The advancement a job reaches at `stage` (1 = 1st job). Returns
// JOB_ADVANCEMENT_UNSPECIFIED for a stage the job hasn't defined yet. No stage
// maps to the beginner book; see JOB_ADVANCEMENT_BEGINNER.
JobAdvancement AdvancementForJobStage(Job job, int stage);

// The highest level `skill` can reach. Normally its own max_level, but a skill
// whose level comes from the account's progress tops out at the level cap, so
// its ceiling follows kMaxLevel instead of being written in the data. Always
// use this rather than max_level.
int SkillMaxLevel(const Skill& skill);

// How many damage tables `buff` uses in a fight: one per stage it drops, one
// per stack it builds, and one for an ordinary buff. Use this wherever the
// budget is counted; see kMaxBuffWindows in combat/constants.h.
int BuffWindowsFor(const Buff& buff);

// The job stage whose SP pool buys skills of `advancement` (1 = 1st job).
// Returns 0 for JOB_ADVANCEMENT_UNSPECIFIED.
int StageForAdvancement(JobAdvancement advancement);

// The job an advancement turns a character into. It is the inverse of
// AdvancementForJobStage: several jobs share an advancement at stage 1, but
// only one reaches it at its own stage.
Job JobForAdvancement(JobAdvancement advancement);

// The level at which a character at `stage` is offered their next advancement,
// which is also the last level of that stage. Zero past the last advancement.
int NextAdvancementLevel(int stage);

// Equip catalog keys given on advancing into `job`. It is a free function so
// the workbench can use the same table as PerformJobAdvancement.
std::vector<std::string> StarterEquipsFor(Job job);

// The weapons `job` is built around, in display order. This is what its skills
// and damage assume, not everything it may equip, which is what a player
// choosing a job needs to know. A 1st job lists only the weapon it is given,
// since the choice isn't real until 2nd job.
std::vector<EquipType> ExpectedWeapons(Job job);

// The advancement whose secondary weapon `type` is. Two secondaries in one
// branch have identical stats, so this is what tells a Fighter's secondary from
// a Page's.
JobAdvancement AdvancementForSecondary(EquipType type);

// The stat a job's damage is built on. STAT_FIELD_UNSPECIFIED for a job with no
// primary stat defined. TODO: Demon Avenger's primary stat is HP; Xenon's is
// STR+DEX+LUK combined.
StatField PrimaryStatField(Job job);

// The job's secondary stat, which counts a quarter as much in the damage
// formula. STAT_FIELD_UNSPECIFIED for a job with no branch. AddStatsByBranch
// pairs the two the same way.
StatField SecondaryStatField(Job job);

// The jobs `job` can advance into at `stage`, in display order. Empty for a
// stage whose choices don't exist yet, so the UI never offers an empty
// advancement.
std::vector<Job> JobChoicesForStage(Job job, int stage);

// What a range of levels grants, totalled.
struct LevelGains {
  int ap = 0;
  int sp = 0;
  // Hyper SP, which is its own pool rather than a stage of the one above. See
  // Character::hyper_sp.
  int hyper_sp = 0;
};

// What levelling from `from_level` to `to_level` grants. It takes a range
// because one combat tick can pass several levels. SP is totalled across job
// stages: this is what was earned, not a pool to spend from.
LevelGains GainsForLevels(int from_level, int to_level);

// The level a character at `level` reaches when they level up: `level + 1`,
// unless Burning takes them further. `other_levels` holds every other character
// on the account, in any order.
//
// Burning helps a character who is behind the rest of the account. They gain
// two levels at a time below the highest character, three below the third
// highest and five below the tenth, never going past kBurningLevel.
int LevelAfterBurning(int level, const std::vector<int>& other_levels);

// Rows on the shop's buy-back shelf. Each sale is one row, so selling 300 of
// something in two sales makes two rows.
inline constexpr int kBuyBackSlots = 32;

class CharacterInstance {
 public:
  CharacterInstance(std::mt19937& rng, Character character);
  CharacterInstance(CharacterInstance&&) = default;

  // Grants 5 AP, and 3 SP to the job stage the new level belongs to. It ignores
  // kTrialLevelCap, which only limits what can be earned.
  void LevelUp();
  // When this character last cleared `boss` at `difficulty`, or 0 for never.
  // boss_reset.h decides whether the clear has expired.
  int64_t BossClearedAt(const std::string& boss,
                        const std::string& difficulty) const;
  // Records a clear at `now`, replacing any earlier one for the same pair.
  void RecordBossClear(const std::string& boss, const std::string& difficulty,
                       int64_t now);
  // When this character last claimed the dailies, or 0 for never. They reset on
  // the same clock as daily bosses; see dailies.h.
  int64_t DailiesClaimedAt() const {
    return character_.dailies_claimed_unix_seconds();
  }
  void RecordDailiesClaim(int64_t now) {
    character_.set_dailies_claimed_unix_seconds(now);
  }
  // Whether the scroll under `key` is pinned to the top of the list. The keys
  // come from the frontend. The character keeps the pins because which stats
  // are worth chasing depends on the character, not the account.
  bool ScrollPinned(const std::string& key) const;
  // Pins it if unpinned, unpins it if pinned.
  void ToggleScrollPin(const std::string& key);
  // Adds `amount` to the character's EXP and levels up as many times as the new
  // total allows. Does nothing once kTrialLevelCap is reached.
  //
  // `other_levels` is the rest of the account's characters, which Burning
  // reads; see LevelAfterBurning. With it empty there is no Burning, so a
  // caller without the roster gets one level per threshold.
  void AddExp(int64_t amount, const std::vector<int>& other_levels = {});
  // Advances into `next_job`, with 5 bonus AP at stages 3 and 4. It grants no
  // SP; levels pay that.
  void AdvanceJob(Job next_job);
  // Returns every AP stat to the pool and re-spends it for `job`. The four
  // stats drop to base, the primary rises to a new character's value, and the
  // rest is left unspent. HP and MP come from levels, not AP, and are
  // unchanged. Called on advancement, so a Beginner's STR isn't wasted on a
  // Magician.
  void ResetStatsForJob(Job job);
  // Whether the character has reached an advancement but not taken it.
  // JobChoicesForStage says what to offer, and AdvanceJob takes the choice.
  bool CanAdvanceJob() const;
  // Returns false if `field` is unspecified or `amount` exceeds available AP.
  bool AllocateStat(StatField field, int amount = 1);
  // Spends `amount` SP to raise the skill's level: from the Hyper pool for a
  // Hyper Skill, otherwise from the skill's job-stage pool. Returns false if
  // the pool is short, the character is under required_level, or it would pass
  // max_level.
  bool LearnSkill(const Skill& skill, int amount = 1);
  // Raises `field` by `amount` levels in `preset`. All or nothing: returns
  // false and spends nothing if the stat is locked, over the cap, or short of
  // points.
  bool AllocateHyperStat(HyperStatField field, StatPreset preset,
                         int amount = 1);
  // Removes `amount` levels from `field` and refunds their cost. All or
  // nothing.
  bool RefundHyperStat(HyperStatField field, StatPreset preset, int amount = 1);
  // Returns every point in `preset` to the pool. It is free, and the only way
  // to undo an allocation.
  void ResetHyperStats(StatPreset preset);
  // The same for the V Matrix: every levelled node resets and its points are
  // returned. Free, and the only way to undo a matrix. It takes the catalog
  // because a name alone doesn't say which skills are nodes.
  void ResetVMatrix(const std::map<std::string, Skill>& skills);
  // Whether the character meets the level and job requirements for `proto`. It
  // is used for catalog items as well as bag items, so it doesn't check for a
  // free slot.
  bool CanEquip(const EquipPrototype& proto) const;
  // Returns true if the character's level meets proto's required level.
  bool MeetsLevel(const EquipPrototype& proto) const;
  // Returns true if the character's job category matches proto's job filter.
  bool MeetsJob(const EquipPrototype& proto) const;
  // Adds `item` to the equip tab. Returns false, and the item is lost, if the
  // tab is full.
  bool PickUp(std::unique_ptr<EquipTabItem> item);
  // Removes the equip-tab item at `index` from the bag and returns it. This is
  // the opposite of PickUp, for an item leaving the character for something
  // other than a sale. Returns null if `index` is out of range.
  std::unique_ptr<EquipTabItem> TakeEquip(int index);
  // The stack version: removes up to `count` from the `index`-th stack and
  // returns how many came out. An empty row is removed. Unlike SpendItem this
  // takes a row, so when the same item is in two rows it takes from the one the
  // player chose.
  int TakeStack(int index, int count);
  // Deletes everything on the equip tab, leaving worn gear. Only the workbench
  // uses it.
  void ClearEquipInventory();
  /* Stackable items come in two kinds that share one entry point: currencies go
   * to the purse and everything else to the Etc tab. The item's prototype says
   * which (see IsCurrency), so drops, shops and trades all hand items over the
   * same way.
   */

  // Adds `count` of `proto` and returns how many were added. A currency always
  // takes all of them. An Etc drop fills existing stacks before starting new
  // ones, and keeps what fits and loses the rest. Filling existing stacks needs
  // no slot, so a full tab can still take part of a drop.
  int AddItem(const ItemPrototype& proto, int count);

  // How many more copies of `proto` the character can hold: one per free slot
  // for an equip, the room left in open stacks plus a full stack per free slot
  // for an Etc drop, and no limit for a currency.
  int RoomFor(const EquipPrototype& proto) const;
  int RoomFor(const ItemPrototype& proto) const;

  // Copies of `proto` worn plus carried. Traces don't count: a trace records a
  // destroyed item, it isn't a copy.
  int CountOwned(const EquipPrototype& proto) const;
  // How many of a stackable the character holds: the balance for a currency,
  // and the total of every stack for an Etc drop. It matches by name, like
  // CountOwned; no name is both kinds.
  int64_t CountItem(const ItemPrototype& proto) const;
  int64_t CountItem(const std::string& name) const;
  // Spends `count` of one. All or nothing.
  bool SpendItem(const std::string& name, int64_t count);
  // Adds `amount` meso to the character's balance. Does nothing if amount <= 0.
  void AddMeso(int64_t amount);
  // Takes `amount` meso. All or nothing: returns false and spends nothing if
  // the character can't afford it. Every price in the game is charged by the
  // method that knows it; this is for meso spent on something the character
  // doesn't own.
  bool SpendMeso(int64_t amount);
  // Adds `amount` honor, which pays for Inner Ability resets. Does nothing if
  // amount <= 0.
  void AddHonor(int64_t amount);
  // Adds V Points. Negative amounts are ignored, since points only leave the
  // pool by being spent on a node.
  void AddVPoints(int64_t amount);

  /* The potions. A buff is either owned or rented, and on or off; together
   * these decide what it does and what it costs. See
   * //src/character/consumables.h.
   */

  // Whether the character is high enough level for the first buff, and so for
  // the Buffs tab to open.
  bool consumables_unlocked() const {
    return character_.level() >= kConsumableUnlockLevel;
  }
  // Whether the character bought `type` outright, so it is never charged again.
  bool ConsumableOwned(ConsumableType type) const;
  // Whether the player has it switched on, regardless of level. An owned buff
  // can stay switched on for a character who can't use it yet.
  bool ConsumableActive(ConsumableType type) const;
  // Whether it has any effect: switched on and the character meets its level.
  // Anything reading a buff's effect should use this rather than the two above.
  bool ConsumableInEffect(ConsumableType type) const;
  // Switches it on if off and off if on, and returns the new state. It refuses
  // a buff the character's level hasn't unlocked, leaving it off.
  bool ToggleConsumable(ConsumableType type);
  // Buys `type` outright. All or nothing: does nothing if the character can't
  // afford it, already owns it, or hasn't reached its level.
  bool BuyConsumable(ConsumableType type);
  // Charges for `procs` uses of `type` (seconds farmed for one buff, boss
  // entries for the other) and returns the amount taken. If the character can't
  // afford it all, they pay what they have and stop at 0; the buff works either
  // way.
  int64_t ChargeConsumable(ConsumableType type, double procs);

  // Charges every per-second buff for `seconds` of farming. Buffs charged per
  // boss entry are left alone.
  int64_t ChargeFarmingConsumables(double seconds);
  // Sells up to `count` from the `index`-th Etc stack, removing it once empty.
  // Returns the meso earned. An item worth nothing returns 0, which is still a
  // sale. Currencies aren't on any shelf and can't be sold.
  int64_t SellStackable(int index, int count);
  // The shop's buy-back shelf, newest sale first. Only BuyBack removes entries.
  const google::protobuf::RepeatedPtrField<BuyBackEntry>& buy_backs() const {
    return character_.buy_backs();
  }
  // Buys back `count` copies from entry `index` at the price the sale paid.
  // Prototypes are looked up by name, so an item removed from data/ can't be
  // bought back. All or nothing, like Buy. Buying part of a stack leaves the
  // rest on the shelf.
  bool BuyBack(int index, int count,
               const std::map<std::string, EquipPrototype>& equips,
               const std::map<std::string, ItemPrototype>& items);

  // Sells the equip-tab item at `index` and returns the meso earned. Zero is
  // not a refusal: scrolled and starred items sell at the base item's price,
  // traces sell for nothing, and all of them are still removed.
  int64_t SellEquip(int index);
  // Buys `count` copies of `proto` at shop_price each. All or nothing. Each
  // copy is a new item, so buying two adds two rows. A price of zero means
  // free; having no price at all is what keeps an item out of the shop.
  bool Buy(const EquipPrototype& proto, int count);
  // Buys `count` copies of `proto` with the token it names, taken from the Etc
  // tab. The caller looks up `token` from the catalog. All or nothing, like
  // Buy.
  bool BuyWithToken(const EquipPrototype& proto, const ItemPrototype& token,
                    int count);
  // The same for a stackable. Price and bag space are both checked first, so a
  // purchase that can't finish takes nothing.
  bool Buy(const ItemPrototype& proto, int count);
  // Moves the item at `inventory_index` into the slot SlotToFill picks, and the
  // item it replaces goes into the bag. `preset` is the setup it goes into. For
  // any preset but the first, the item becomes that preset's own and the others
  // are unchanged.
  bool Equip(int inventory_index, StatPreset preset = StatPreset::kFirst);
  // The slot this item would go in: the slot where a copy of the same item is
  // already worn, otherwise the first free slot of its family, otherwise the
  // first slot of the family. A second copy replaces the first because in GMS
  // no two of the four rings can be the same ring. EQUIP_SLOT_UNSPECIFIED means
  // the item names no slot.
  EquipSlot SlotToFill(const EquipPrototype& proto,
                       StatPreset preset = StatPreset::kFirst) const;
  // Moves what `preset` wears in `slot` to the bag. Returns false for an empty
  // slot, and for an inherited one: a preset can only remove its own items.
  bool Unequip(EquipSlot slot, StatPreset preset = StatPreset::kFirst);
  // Applies `scroll` to the item in `slot`; kScrollFail if the slot is empty.
  //
  // Every worn-item upgrade below affects the item `preset` shows. An inherited
  // item belongs to the first preset, so upgrading it from the Boss tab also
  // upgrades what Farm wears, since it is the same item.
  ScrollOutcome ScrollEquipped(EquipSlot slot, const Scroll& scroll,
                               StatPreset preset = StatPreset::kFirst);
  // Applies `scroll` to the inventory item at `index`. Returns kScrollFail if
  // `index` is out of range; otherwise returns the result of Scroll().
  ScrollOutcome ScrollInventory(int index, const Scroll& scroll);
  // One star force attempt on the item in `slot`. kStarForceDestroy removes the
  // item. It pays StarForceCost first, and doesn't roll if it can't.
  StarForceOutcome StarForceEquipped(EquipSlot slot,
                                     StatPreset preset = StatPreset::kFirst);
  // One star force attempt on the inventory item at `index`. On
  // kStarForceDestroy, removes the item from inventory. Priced as above.
  StarForceOutcome StarForceInventory(int index);
  // Uses a golden hammer on the item for one more upgrade slot, at
  // kGoldenHammerCost. Returns false, and spends nothing, if it can't be done.
  bool HammerEquipped(EquipSlot slot, StatPreset preset = StatPreset::kFirst);
  bool HammerInventory(int index);

  // Restores the trace at `trace_index` onto the item at `base_item_index`.
  // Both leave the bag, and a new item gets the trace's scroll stats and
  // RecoveryStars() stars. Returns that star count.
  int RecoverTrace(int trace_index, int base_item_index);

  // Sorts the equip tab into the Sort button's order. Every row may move, so no
  // row index stays valid afterwards.
  void SortEquipTab();
  void SortStackTab();

  // The player's name for this character. Never empty: a character created or
  // loaded without one returns kDefaultUsername.
  const std::string& username() const {
    return character_.name();
  }
  // Sets the character's name. An empty name is ignored, because the panel uses
  // it to mean "no change" and nothing else should be able to clear the name.
  void SetUsername(const std::string& name);

  const Character& proto() const {
    return character_;
  }
  const InventoryInstance& inventory() const {
    return inventory_;
  }
  std::vector<const EquipTrace*> traces() const;
  // What `preset` wears, its own items and inherited ones together.
  const WornGear& equipped(StatPreset preset = StatPreset::kFirst) const {
    return resolved_[IndexOf(preset)];
  }
  // The items `preset` holds as its own. For the first preset that is every
  // worn item. For the others it is what the Gear tab shows in white.
  const std::map<EquipSlot, EquipInstance>& own_gear(StatPreset preset) const {
    return worn_[IndexOf(preset)];
  }
  // The item `preset` wears in `slot`, its own or inherited, or nullptr if the
  // slot is empty. A preset doesn't inherit an item it already wears a copy of
  // elsewhere in the same family, because no preset may show two of one ring.
  // That slot reads as empty.
  const EquipInstance* WornAt(StatPreset preset, EquipSlot slot) const;
  // Whether what `preset` wears in `slot` is inherited from the first preset.
  // The Gear tab dims those rows.
  bool InheritsSlot(StatPreset preset, EquipSlot slot) const;
  // The item stacks on the bag's Etc tab, in pickup order. Only ordinary drops;
  // currencies are kept in the purse and don't take slots.
  const std::vector<StackableItem>& stackables() const {
    return etc_items_.items();
  }
  // The currencies the character holds, in the Token tab's order.
  const CurrencyPurse& currencies() const {
    return currencies_;
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
  // Available Hyper SP, which belongs to no stage; see Character::hyper_sp.
  int hyper_sp() const {
    return character_.hyper_sp();
  }

  // The account's Autoswap Presets setting, copied here because every stat
  // calculation has the character but not the account.
  bool autoswap_presets() const {
    return autoswap_presets_;
  }
  void set_autoswap_presets(bool on) {
    autoswap_presets_ = on;
  }
  // The highest level any character on the account has reached, copied here for
  // the same reason, since Blessing of the Fairy is read wherever stats are. It
  // is never below this character's own level: the account's record is only
  // written on save, so until then a new level is only on the character.
  void set_account_max_level(int level) {
    account_max_level_ = level;
  }
  int account_max_level() const {
    return std::max(account_max_level_, character_.level());
  }
  // What the other characters on the account have reached, per job line, copied
  // here for the same reason. This character is not included: its own level is
  // added at every read, because it can change mid-session.
  void set_link_tally(LinkTally tally) {
    link_tally_ = std::move(tally);
  }
  const LinkTally& link_tally() const {
    return link_tally_;
  }
  // The link skills `slot` has equipped. The character's own line's link skill
  // is free and not in the list. See Character.link_skills.
  const google::protobuf::RepeatedPtrField<std::string>& link_skills(
      StatPreset slot = StatPreset::kFirst) const;
  // Whether this character has `skill` while doing `activity`: either their own
  // line's, or one equipped in the preset that activity uses. False for
  // anything that is not a link skill.
  bool HoldsLinkSkill(const Skill& skill,
                      Activity activity = Activity::kFarming) const;
  // The level of `skill` for this character, or 0 if they don't have it. It is
  // capped at the skill's own maximum, because the data's ladder runs to GMS's
  // top level even where the four job lines can't reach it.
  int LinkSkillLevel(const Skill& skill,
                     Activity activity = Activity::kFarming) const;
  // The level `skill` would have if equipped, whatever is equipped now; the
  // screen offering it shows this. The character's own line's skill is at least
  // 1 from level 1, and another line's waits for its first rung.
  int LinkSkillLevelOffered(const Skill& skill) const;
  // Equips `name` in `slot`, or removes it. Equipping fails if the preset is
  // full or already has it. Both fail for a name that is not a link skill,
  // which the caller must check. Returns whether anything changed.
  bool EquipLinkSkill(const std::string& name, StatPreset slot);
  bool UnequipLinkSkill(const std::string& name, StatPreset slot);
  // Fills every preset up to kMaxEquippedLinkSkills from `skills`, in catalog
  // order, and removes names the catalog no longer has. Returns how many
  // changed. Called on loading a save: every preset carries what the account
  // has unlocked until the player changes it.
  int ReconcileLinkSkills(const std::map<std::string, Skill>& skills);
  // Which preset of `kind` the player has selected. Only used while autoswap is
  // off. Each kind has its own.
  StatPreset SlotInUse(PresetKind kind) const;
  void SetSlotInUse(PresetKind kind, StatPreset slot);
  // Swaps what two slots of `kind` hold, and the in-use marker moves with them:
  // reordering presets shouldn't change which one is in use.
  void SwapPresets(PresetKind kind, StatPreset a, StatPreset b);
  // Which preset of `kind` applies while doing `activity`: the autoswap slot,
  // or the selected one when autoswap is off.
  StatPreset SlotFor(PresetKind kind, Activity activity) const;
  // Every Hyper Stat point the character's level has granted.
  int hyper_stat_points() const;
  // Points left after paying for `preset`.
  int hyper_stat_points_left(StatPreset preset = StatPreset::kFirst) const;
  // The level of `field` in `preset`.
  int hyper_stat_level(HyperStatField field,
                       StatPreset preset = StatPreset::kFirst) const;
  // What `field` is currently worth to this character, in the stat's own units.
  // Zero if they haven't raised it.
  double hyper_stat_bonus(HyperStatField field,
                          StatPreset preset = StatPreset::kFirst) const;
  // The highest level any of their Hyper Stats may reach.
  int max_hyper_stat_level() const;

  // Honor left to spend on Inner Ability resets.
  int64_t honor() const {
    return character_.honor();
  }
  // V Points left to spend on the V Matrix.
  int64_t v_points() const {
    return character_.v_points();
  }
  // Whether the character has taken the 5th advancement. That opens the matrix
  // and makes their kills drop V Points.
  bool v_matrix_unlocked() const {
    return character_.job_stage() >= kFifthJobStage;
  }
  // Whether the character is high enough level for Inner Ability to take
  // effect, and for its panel to open.
  bool inner_ability_unlocked() const {
    return character_.level() >= kInnerAbilityUnlockLevel;
  }
  // Whether link skills are turned off; see set_link_skills_off().
  bool link_skills_off() const {
    return link_skills_off_;
  }
  // Turns them all off, own line included. Sims use it to measure a character
  // alone, because the balance numbers were taken before link skills existed
  // and haven't been re-taken. See TestOptions::link_skills.
  void set_link_skills_off(bool off) {
    link_skills_off_ = off;
  }
  // The three lines `preset` has, and its overall rank.
  const AbilityPreset& ability(StatPreset preset = StatPreset::kFirst) const {
    return PresetOf(character_.inner_ability(), preset);
  }
  // What one reset of `preset` would cost with its current lines.
  int64_t ability_reset_cost(StatPreset preset = StatPreset::kFirst) const;
  // Locks or unlocks the line at `index`, by the rules in SetAbilityLineLocked.
  // Returns whether anything changed.
  bool LockAbilityLine(int index, bool locked,
                       StatPreset preset = StatPreset::kFirst);
  // Pays for a reset from honor and rerolls `preset`. All or nothing.
  bool ResetAbility(StatPreset preset = StatPreset::kFirst);
  // Sets `preset` to `lines` directly, paying no honor. For seeding a character
  // who should start with those lines: rolling for them would take hundreds of
  // tries and give a different character every run.
  void SetAbility(const AbilityPreset& lines,
                  StatPreset preset = StatPreset::kFirst);

  // The points `skill` is bought with: the Hyper pool for a Hyper Skill,
  // otherwise its job stage's pool. It lives here so the panel, the screen and
  // LearnSkill can't disagree.
  int SpFor(const Skill& skill) const {
    if (skill.v_node() != V_NODE_KIND_UNSPECIFIED) {
      // This returns V Points, not levels: a node's levels don't cost one point
      // each, so VNodeCostFor decides what they buy.
      return static_cast<int>(std::min<int64_t>(v_points(), INT_MAX));
    }
    return skill.hyper() ? hyper_sp() : sp(StageForAdvancement(BookOf(skill)));
  }
  // The V Point cost of raising `skill` by `amount` levels, or 0 for anything
  // that is not a node.
  int VNodeCostFor(const Skill& skill, int amount) const;
  // How many more levels of `skill` the character can buy now. One per point
  // for an ordinary skill. For a node, as far up the ladder as the V Points
  // reach, which differs from the number of points.
  int LevelsAffordable(const Skill& skill) const;
  // The learned level of `skill`, 0 if unlearned. A Vengeance form reads its
  // Benevolence skill's level, since both are one row of the same book.
  int skill_level(const Skill& skill,
                  Activity activity = Activity::kFarming) const {
    if (skill.account_levels_per_level() > 0) {
      return DerivedSkillLevel(skill);
    }
    if (skill.link_line() != JOB_UNSPECIFIED) {
      return LinkSkillLevel(skill, activity);
    }
    const std::string& key = skill.replaces_skill_name().empty()
                                 ? skill.name()
                                 : skill.replaces_skill_name();
    return character_.skill_levels().contains(key)
               ? character_.skill_levels().at(key)
               : 0;
  }
  // Whether the player has `name` switched on. False for a skill that is not a
  // toggle. See Skill.toggle.
  bool SkillToggledOn(const std::string& name) const;
  // Flips `skill` and returns its new state. Refuses a skill that is not a
  // toggle or is not learned.
  bool ToggleSkill(const Skill& skill);
  // Whether this character has taken `advancement`. Every 1st job draws on the
  // same stage-1 SP pool, so the stage alone doesn't say which book a skill
  // came from.
  bool HasAdvancement(JobAdvancement advancement) const;
  // The book this character holds that lists `skill`. A shared skill is in
  // several books, and the one they took says whose SP paid for it.
  JobAdvancement BookHeldFor(const Skill& skill) const;
  // Whether any book this character holds lists `skill`.
  bool HasBookFor(const Skill& skill) const {
    return BookHeldFor(skill) != JOB_ADVANCEMENT_UNSPECIFIED;
  }
  // Whether the character has learned whatever `skill` requires first.
  // LearnSkill refuses when this is false, and the skills tab uses it to dim
  // the row.
  bool MeetsSkillRequirement(const Skill& skill) const;
  // Whether `skill` comes from a book this character holds: their matrix for a
  // node, otherwise the advancement it names. Levels are keyed by display name
  // and branches share several names, so this stops another branch's copy from
  // counting alongside their own. For a link skill, `activity` decides, since
  // which link skills are equipped depends on the preset.
  bool HoldsSkillFrom(const Skill& skill,
                      Activity activity = Activity::kFarming) const;
  // Whether this character's matrix can hold the node `skill`. A common node is
  // open to every job; any other names the 5th advancement that may buy it.
  bool ReachesVNode(const Skill& skill) const;
  // The whole character as one proto, with the live containers written back in.
  // proto() alone doesn't include them: they live in C++ containers and are
  // only written out here.
  Character ToProto() const;

  // The inverse, looking up each item's prototype by name. An item missing from
  // the catalogs is dropped rather than guessed at, so removing something from
  // data/ loses that item but not the character. It skips the equip rules on
  // purpose, so what was worn stays worn.
  void RestoreFrom(const Character& saved,
                   const std::map<std::string, EquipPrototype>& equips,
                   const std::map<std::string, ItemPrototype>& items);

  // Corrects the AP total and returns how far off it was. If short, the
  // difference is added unspent. If over, it comes out of the unspent pool
  // first and then the stats, primary stat last. Called on loading a save.
  int ReconcileAp();

  // Fits learned skills back within their books and returns how many points
  // moved. A skill above a max_level the data has since lowered is cut, and its
  // points are re-spent at random in the same book. This keeps the SP totals
  // right, since those points came from one stage's pool. Returning them to the
  // pool is the last resort. Called on loading a save.
  int ReconcileSkills(const std::map<std::string, Skill>& skills);

  // Corrects every SP pool and returns how many points moved. A short pool gets
  // the difference, which covers a character already past a rung when that
  // ladder was added. An overfull pool loses the excess, down to empty but no
  // further. Every stage's pool and the Hyper pool are computed from the level,
  // since advancing grants no SP. Called after ReconcileSkills, which moves
  // points between a book and its pool.
  int ReconcileSp(const std::map<std::string, Skill>& skills);

  // Fits both Hyper Stat allocations within what the level has paid for. A stat
  // past the cap is cut, a locked one is emptied, and if the allocation spends
  // more than the pool, its highest (most expensive) levels are removed first.
  int ReconcileHyperStats();

  // Sum of stats from everything `preset` wears. All presets are updated at
  // once, since one item can be worn by all three.
  const EquipStats& equip_stats(StatPreset preset = StatPreset::kFirst) const {
    return equip_stats_[IndexOf(preset)];
  }
  // The part of that total from worn Arcane Symbols. It is kept separate
  // because a symbol grants final stats that no %stat may multiply, so the step
  // that applies %stat subtracts this first.
  const EquipStats& symbol_stats(StatPreset preset = StatPreset::kFirst) const {
    return symbol_stats_[IndexOf(preset)];
  }
  // The totals of the potentials on everything worn. Rebuilt together with the
  // equip stats, in the same pass over the worn items.
  const PotentialTotals& potential_totals(
      StatPreset preset = StatPreset::kFirst) const {
    return potential_totals_[IndexOf(preset)];
  }
  // Uses one cube on the item worn in `slot`, rerolling its lines and possibly
  // raising its rank. This charges nothing; BuyCube is the purchase.
  bool CubeWorn(EquipSlot slot, CubeType cube,
                StatPreset preset = StatPreset::kFirst);
  // Cubes the item until its rank is `want`, giving up after `rolls`. It makes
  // the same rolls as CubeWorn but recomputes stats once at the end instead of
  // after every roll, since reaching a rank takes tens of rolls and nothing
  // reads the totals in between. Returns whether it reached the rank.
  bool CubeWornUpTo(EquipSlot slot, CubeType cube, PotentialRank want,
                    int rolls, StatPreset preset = StatPreset::kFirst);
  // Uses one cube on the item worn in `slot`, charging kCubeCost. It returns
  // the rolled potential without applying it; TakePotential applies it. A
  // player offered something worse keeps what they have, and the cube is spent
  // either way.
  std::optional<Potential> BuyCube(EquipSlot slot, CubeType cube,
                                   StatPreset preset = StatPreset::kFirst);
  // Puts `potential` on the item worn in `slot`, when a player accepts a roll.
  // Returns false, changing nothing, for an empty slot.
  bool TakePotential(EquipSlot slot, const Potential& potential,
                     StatPreset preset = StatPreset::kFirst);
  // Uses one cube on a worn or bagged item, charging for it and applying
  // whatever it rolls. This is what the cubing screen calls.
  bool CubeEquipped(EquipSlot slot, CubeType cube,
                    StatPreset preset = StatPreset::kFirst);
  bool CubeInventory(int index, CubeType cube);
  // Spare copies of the Arcane Symbol for `slot` in the equip bag. Traces don't
  // count, as elsewhere; see CountOwned.
  int SpareSymbols(EquipSlot slot) const;
  // What each spare is worth in duplicates, in the order CombineSymbols uses
  // them. See SymbolWorth.
  std::vector<int> SpareSymbolWorths(EquipSlot slot) const;
  // Absorbs up to `count` spares into the symbol worn in `slot`, deletes them,
  // and returns how many it used. Raising the symbol's level is a separate step
  // that costs meso; see LevelUpSymbol.
  int CombineSymbols(EquipSlot slot, int count,
                     StatPreset preset = StatPreset::kFirst);
  // Raises the symbol worn in `slot` one level, charging that level's meso cost
  // and carrying excess EXP into the next level. It needs a symbol with enough
  // duplicates absorbed, and enough meso.
  bool LevelUpSymbol(EquipSlot slot, StatPreset preset = StatPreset::kFirst);
  // The character's Arcane Force: worn symbols plus the Hyper Stat. Every
  // Arcane River map checks it; see ArcaneFactorsFor.
  int arcane_force(Activity activity = Activity::kFarming) const;
  // The character's Sacred Power, which every Grandis map checks; see
  // SacredFactorsFor. It is always 0 for now because Sacred Symbols aren't in
  // the game yet.
  int sacred_power(Activity activity = Activity::kFarming) const;
  // Whether an item of this type adds its attack right now: throwing stars only
  // count with a claw. equip_stats() already applies this. It is public so the
  // display can show inactive attack as inactive.
  bool AttackCounts(const EquipPrototype& proto,
                    StatPreset preset = StatPreset::kFirst) const;
  // The type of weapon in hand, or EQUIP_TYPE_UNSPECIFIED if the slot is empty.
  // Skills that need a particular weapon check this.
  EquipType weapon_type(StatPreset preset = StatPreset::kFirst) const;
  // Whether anything is worn in the secondary slot. See
  // Skill.requires_secondary.
  bool has_secondary(StatPreset preset = StatPreset::kFirst) const;

  // Gives the character the equipment set definitions. They are data, not
  // state, since set bonuses come from what is worn, so they are passed in at
  // startup and never saved.
  void UseEquipSets(std::map<std::string, EquipSet> sets);
  // Every set tier the worn pieces have earned, cumulatively: four pieces give
  // both the three-piece and four-piece tiers. They are added alongside the
  // passives, since a set bonus grants the same things a passive does.
  const std::vector<SkillEffect>& set_bonuses(
      StatPreset preset = StatPreset::kFirst) const {
    return set_bonuses_[IndexOf(preset)];
  }
  // The sets the character knows about, keyed by how their data files were
  // loaded.
  const std::map<std::string, EquipSet>& equip_sets() const {
    return equip_sets_;
  }
  // Whether the item with this display name is currently worn.
  bool IsWearing(const std::string& item_name,
                 StatPreset preset = StatPreset::kFirst) const;
  // The item of `family` being worn, or "" for none. A set slot that no single
  // item can fill, such as a weapon that differs by class, names a family
  // instead.
  std::string WornOfFamily(const std::string& family,
                           StatPreset preset = StatPreset::kFirst) const;
  // The worn piece that fills this member, or "" for none. A member listing
  // several alternatives accepts any one, and counts once however many are
  // worn.
  std::string WornOfMember(const EquipSetMember& member,
                           StatPreset preset = StatPreset::kFirst) const;
  // How many pieces of `set` are currently worn. Every tier is measured against
  // this, and the inspect screen greys out unearned tiers by it.
  int PiecesWornOf(const EquipSet& set,
                   StatPreset preset = StatPreset::kFirst) const;
  // A copy of this character with `item` worn in `preset`, for pricing a piece
  // they don't own yet, such as a shop item or another player's gear. Only the
  // copy changes. The bag is unchanged, and the item that was in the slot is
  // simply gone. If the item names no slot this character can fill, the copy is
  // returned unchanged.
  //
  // `slot` picks which slot to try it in, so a ring or pendant can be compared
  // against any of the ones the player is wearing rather than the one Equip
  // would choose. If unset, SlotToFill picks, matching Equip.
  CharacterInstance Wearing(const EquipTabItem& item, StatPreset preset,
                            std::optional<EquipSlot> slot = std::nullopt) const;

 private:
  // Private so a character is never copied by accident; only a probe like
  // Wearing needs a copy. Defaulted so a new member can't be left out.
  CharacterInstance(const CharacterInstance&) = default;

  // Buys `amount` levels of a V Matrix node with V Points, at the cost its
  // kind's ladder charges for those levels. All or nothing.
  bool LearnVNode(const Skill& skill, int amount);
  // Copy of the account setting; see autoswap_presets(). Off is the default, so
  // a character with no account keeps whichever preset is selected.
  bool autoswap_presets_ = false;
  // Copy of the account's record; see account_max_level().
  int account_max_level_ = 0;
  // Copy of what the account's other characters have reached; see link_tally().
  LinkTally link_tally_;
  // Whether link skills are turned off for this character regardless of the
  // account; see set_link_skills_off().
  bool link_skills_off_ = false;

  // The level of a skill nobody buys: the account's highest level divided by
  // the levels each skill level costs, capped. See
  // Skill.account_levels_per_level.
  int DerivedSkillLevel(const Skill& skill) const {
    return std::min(SkillMaxLevel(skill),
                    account_max_level() / skill.account_levels_per_level());
  }

  // Gives a nameless character kDefaultUsername. Both ways a Character is
  // loaded call it, which is why username() is never empty.
  void EnsureUsername();
  // Gives both Inner Ability presets the three starting lines, so older saves
  // from before Inner Ability get them too.
  void EnsureInnerAbility();
  // Pays for one star force attempt, or returns false and spends nothing. Both
  // StarForce methods call it before rolling.
  bool PayForStarForce(const EquipInstance& item);
  bool PayForHammer(const EquipInstance& item);
  bool PayForCube(const EquipInstance& item);
  // Adds a sale to the front of the buy-back shelf, and drops the oldest row
  // once the shelf is full.
  void RecordSale(BuyBackEntry entry);
  // The two halves of BuyBack, which only share the row lookup. For an equip,
  // room means a free slot; for a stackable, it means space in open stacks.
  bool BuyBackEquip(int index, const BuyBackEntry& entry,
                    const std::map<std::string, EquipPrototype>& equips);
  bool BuyBackStack(int index, const BuyBackEntry& entry, int count,
                    const std::map<std::string, ItemPrototype>& items);

  // Reconciles one allocation for ReconcileHyperStats and returns the points it
  // took back.
  int ReconcileHyperPreset(StatPreset preset);
  // Recomputes equip_stats_, symbol_stats_, arcane_force_ and set_bonuses_ from
  // the worn items.
  void RecomputeEquipStats();
  // Does that for one preset: resolves its gear, then computes the totals.
  void RecomputePreset(StatPreset preset);
  // The mutable version of WornAt. Every upgrade uses it to reach the item the
  // given preset shows.
  EquipInstance* WornIn(StatPreset preset, EquipSlot slot);
  // Removes what `preset` wears in `slot` and returns it. Unequip and a
  // destroyed star force attempt both use this.
  std::optional<EquipInstance> TakeWorn(StatPreset preset, EquipSlot slot);
  // Rebuilds set_bonuses_: every tier of every known set that the worn pieces
  // reach. Tiers are cumulative, so a four-piece set gives both its tiers.
  void RecomputeSetBonuses(StatPreset preset);
  std::mt19937& rng_;
  Character character_;
  InventoryInstance inventory_;
  // The worn items, one map per preset. The first holds every slot. The others
  // hold only the slots they fill themselves; see EquipPreset in
  // character.proto for why they can't each hold a copy.
  std::array<std::map<EquipSlot, EquipInstance>, kNumStatPresets> worn_;
  // Each preset's gear resolved against the first preset, and the totals
  // computed from it. Rebuilt together by RecomputeEquipStats.
  std::array<WornGear, kNumStatPresets> resolved_;
  StackTab etc_items_;
  CurrencyPurse currencies_;
  std::array<EquipStats, kNumStatPresets> equip_stats_;
  std::array<EquipStats, kNumStatPresets> symbol_stats_;
  std::array<PotentialTotals, kNumStatPresets> potential_totals_;
  std::array<int, kNumStatPresets> arcane_force_ = {};
  // Meso the buffs have used but not yet been charged, always under 1. The live
  // tick charges several times a second, so without this a potion at 1,000 a
  // second would lose fractions and cost 999. Not saved.
  double consumable_debt_ = 0.0;
  std::map<std::string, EquipSet> equip_sets_;
  std::array<std::vector<SkillEffect>, kNumStatPresets> set_bonuses_;
};

}  // namespace ms

#endif  // MS_CHARACTER_H_
