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

// The gear one preset wears: its own item where it has one, and the first
// preset's everywhere else. Points into the character's worn items, so it
// lasts only as long as nothing is equipped or taken off.
using WornGear = std::map<EquipSlot, const EquipInstance*>;

// What an AP stat reads with nothing spent on it, and the STR a fresh Beginner
// carries instead. The nine between them is AP the game spent on the
// character's behalf, so it counts as spent.
inline constexpr int kBaseStat = 4;
inline constexpr int kBeginnerStr = 13;

// What a character is called before the player names one, which doubles as the
// invitation to. A save from before the field comes forward with it.
inline constexpr char kDefaultUsername[] = "Set Username";

// The longest name the player may set. Every column showing one is sized from
// this; the boss arena's twelve-wide nameplate slides it instead.
inline constexpr int kMaxUsernameLength = 20;

// Every AP the game has handed a character at `level` and `job_stage`, spent
// and unspent. AP is only ever moved, never destroyed, so a save that does not
// come to this has drifted -- see CharacterInstance::ReconcileAp.
int ExpectedTotalAp(int level, int job_stage);

// The highest job stage the game has an advancement level for. A stage with no
// branches still counts, so anything walking every stage walks to here and
// keeps working when the next one lands.
inline constexpr int kMaxJobStage = 6;

// The last job stage this game has branches for. GMS goes on to a 6th, which
// kAdvancementLevels names and nothing grants, so a 5th job climbs to the
// cap.
inline constexpr int kLastJobStage = 5;

// The last job stage whose book SP buys. The 5th job's is bought with V Points
// instead, so no level pays SP toward it and its pool stays empty.
inline constexpr int kLastSpJobStage = 4;

// The advancement a job is at once it reaches `stage` (1 = 1st job). Returns
// JOB_ADVANCEMENT_UNSPECIFIED for a stage the job hasn't defined yet. NOT the
// beginner book, which no stage answers for -- see JOB_ADVANCEMENT_BEGINNER.
JobAdvancement AdvancementForJobStage(Job job, int stage);

// The highest level `skill` reaches. Its own max_level, unless its level is
// derived from the account's climb -- then it tops out where the LEVEL CAP
// does, so the ceiling moves with kMaxLevel and is not written in the data.
// ASK THIS, never max_level: the two differ for exactly one skill today.
int SkillMaxLevel(const Skill& skill);

// Damage tables `buff` costs the fight: one per stage it sheds, one per stack
// it gathers, and one for an ordinary buff. ASK THIS wherever the budget is
// counted -- see combat/constants.h kMaxBuffWindows.
int BuffWindowsFor(const Buff& buff);

// The job stage whose SP pool buys skills of `advancement` (1 = 1st job).
// Returns 0 for JOB_ADVANCEMENT_UNSPECIFIED.
int StageForAdvancement(JobAdvancement advancement);

// The job an advancement makes a character: the inverse of
// AdvancementForJobStage, which several jobs answer alike at stage 1 but only
// one does at the advancement's own stage.
Job JobForAdvancement(JobAdvancement advancement);

// The level at which a character at `stage` is offered their next
// advancement, and so the last level that stage spans. Zero past the last
// advancement the game defines.
int NextAdvancementLevel(int stage);

// The equip catalog keys handed over on advancing into `job`. Here rather than
// beside PerformJobAdvancement so the workbench underneath can ask too: one
// table, not two that drift.
std::vector<std::string> StarterEquipsFor(Job job);

// The weapons `job` is built around, in the order to name them. Not what it
// may legally hold but what its skills and damage assume, which is what a
// player choosing a job needs told. A 1st job names the one weapon it is
// handed, the fork not being real until the 2nd.
std::vector<EquipType> ExpectedWeapons(Job job);

// The advancement whose off hand `type` is. Two secondaries in one branch have
// identical stats, so this is all that stands between a Fighter and a Page's
// rosary.
JobAdvancement AdvancementForSecondary(EquipType type);

// The stat a job's damage is built on. STAT_FIELD_UNSPECIFIED for a job with
// no primary stat defined.
// TODO: Demon Avenger's primary stat is HP; Xenon's is STR+DEX+LUK combined.
StatField PrimaryStatField(Job job);

// The jobs `job` may advance into at `stage`, in the order to offer them.
// Empty for a stage whose choices do not exist yet, which keeps the UI from
// offering an advancement with nothing behind it.
std::vector<Job> JobChoicesForStage(Job job, int stage);

// What a run of levels hands over, totalled.
struct LevelGains {
  int ap = 0;
  int sp = 0;
  // Hyper SP, which is its own pool rather than a stage of the one above --
  // see Character::hyper_sp.
  int hyper_sp = 0;
};

// What climbing from `from_level` to `to_level` hands over. A SPAN, since one
// combat tick can carry a character past several thresholds. The SP totals
// across job stages: this is what was earned, not a pool to spend from.
LevelGains GainsForLevels(int from_level, int to_level);

// The level a character at `level` arrives at when they earn one, which is
// `level + 1` unless Burning carries them further. `other_levels` is every
// OTHER character on the account, in any order.
//
// Burning pays a character who is behind the rest of the account: two levels at
// a time below the highest character, three below the third highest, five
// below the tenth, and never past kBurningLevel.
int LevelAfterBurning(int level, const std::vector<int>& other_levels);

// Rows on the shop's buy-back shelf. One per sale, so a player who sold 300 of
// something in two goes finds two of them.
inline constexpr int kBuyBackSlots = 32;

class CharacterInstance {
 public:
  CharacterInstance(std::mt19937& rng, Character character);

  // Grants 5 AP and 3 SP into the job stage the new level's band belongs to.
  // NOT bounded by kTrialLevelCap: the cap is on what can be earned.
  void LevelUp();
  // When this character last cleared `boss` at `difficulty`, or 0 for never.
  // Whether it has expired is boss_reset.h's question.
  int64_t BossClearedAt(const std::string& boss,
                        const std::string& difficulty) const;
  // Records a clear at `now`, replacing any earlier one for the same pair.
  void RecordBossClear(const std::string& boss, const std::string& difficulty,
                       int64_t now);
  // When this character last claimed the dailies, or 0 for never. On the same
  // reset clock a daily boss is -- see dailies.h.
  int64_t DailiesClaimedAt() const {
    return character_.dailies_claimed_unix_seconds();
  }
  void RecordDailiesClaim(int64_t now) {
    character_.set_dailies_claimed_unix_seconds(now);
  }
  // Whether the scroll under `key` is pinned to the top of the list. The keys
  // are the frontend's; the CHARACTER keeps the record, which stats are worth
  // chasing being theirs rather than the account's.
  bool ScrollPinned(const std::string& key) const;
  // Pins it if it is loose, loosens it if it is pinned.
  void ToggleScrollPin(const std::string& key);
  // Adds amount to the character's accumulated EXP, leveling up as many times
  // as the new total allows. No-op once kTrialLevelCap is reached.
  //
  // `other_levels` is the rest of the account's characters, which is what
  // Burning reads -- see LevelAfterBurning. Empty burns nothing, so a caller
  // with no roster to hand gets one level per threshold.
  void AddExp(int64_t amount, const std::vector<int>& other_levels = {});
  // Advances into `next_job`: 5 bonus AP at stages 3 and 4, and a batch of SP
  // for the newly opened skill set.
  void AdvanceJob(Job next_job);
  // Puts every AP stat back in the pool and re-spends it for `job`: the four
  // drop to base, the primary rises to a fresh character's, and the rest is
  // unspent. HP and MP are level-up grants, not AP, and are untouched. Called
  // on advancement, so a Beginner's STR does not strand a Magician.
  void ResetStatsForJob(Job job);
  // Whether an advancement is reached but not taken. JobChoicesForStage says
  // what to offer and AdvanceJob takes the answer.
  bool CanAdvanceJob() const;
  // Returns false if `field` is unspecified or `amount` exceeds available AP.
  bool AllocateStat(StatField field, int amount = 1);
  // Spends `amount` SP to raise the learned level: the Hyper pool for a Hyper
  // Skill, the skill's job-stage pool otherwise. False when the pool is short,
  // the character is under required_level, or it would pass max_level.
  bool LearnSkill(const Skill& skill, int amount = 1);
  // Raises `field` by `amount` levels in `preset`. All or nothing: false and
  // nothing spent when locked, over the cap, or short of points.
  bool AllocateHyperStat(HyperStatField field, StatPreset preset,
                         int amount = 1);
  // Takes `amount` levels back off `field`, refunding what they cost. All or
  // nothing.
  bool RefundHyperStat(HyperStatField field, StatPreset preset, int amount = 1);
  // Puts every point in `preset` back in the pool. Free, and the only way out
  // of an allocation.
  void ResetHyperStats(StatPreset preset);
  // The same for the V Matrix: every levelled node goes back to nothing and
  // its points return. Free, and the only way out of a matrix. Takes the
  // catalog because a name alone does not say which levels are nodes.
  void ResetVMatrix(const std::map<std::string, Skill>& skills);
  // Whether the level and job requirements for `proto` are met. Asked of the
  // catalog as much as the bag, so it says nothing about a free slot.
  bool CanEquip(const EquipPrototype& proto) const;
  // Returns true if the character's level meets proto's required level.
  bool MeetsLevel(const EquipPrototype& proto) const;
  // Returns true if the character's job category matches proto's job filter.
  bool MeetsJob(const EquipPrototype& proto) const;
  // Appends `item` to the equip tab. False, and the item is dropped, when the
  // tab is full.
  bool PickUp(std::unique_ptr<EquipTabItem> item);
  // Takes the equip-tab item at `index` out of the bag and hands it over, the
  // counterpart of PickUp: for an item leaving the character for something
  // that is not a sale. Null for an index out of range.
  std::unique_ptr<EquipTabItem> TakeEquip(int index);
  // The stack counterpart: takes `count` off the `index`-th stack, clamped to
  // what is in it, and returns how many came out. The row leaves the tab when
  // it empties. Unlike SpendItem this names a ROW, so a bag holding the same
  // item on two of them takes from the one the player is pointing at.
  int TakeStack(int index, int count);
  // Throws away everything on the equip tab, worn gear untouched. The
  // workbench's: nothing in the game empties one.
  void ClearEquipInventory();
  /* Stackable items, which are of two sorts and reached through one door: a
   * currency goes to the purse and everything else to the Etc tab. Which of
   * the two an item is comes off its prototype -- see IsCurrency -- so a drop,
   * a shop and a trade all hand over what they have and are routed.
   */

  // Adds `count` of `proto` and returns how many went in. A currency always
  // takes all of them. An Etc drop tops up open stacks before opening new
  // ones, and takes what fits and loses the rest -- topping up costs no slot,
  // so a full tab can still absorb part of a drop.
  int AddItem(const ItemPrototype& proto, int count);

  // How many more copies of `proto` the character could take: one per free
  // slot for an equip, the room in every open stack plus a full one per free
  // slot for an Etc drop, and no limit at all for a currency.
  int RoomFor(const EquipPrototype& proto) const;
  int RoomFor(const ItemPrototype& proto) const;

  // Copies of `proto` worn plus carried. Traces do NOT count: a trace is the
  // record of a destroyed item, not a copy of it.
  int CountOwned(const EquipPrototype& proto) const;
  // How many of a stackable the character holds: the balance for a currency,
  // and the sum of every stack of it for an Etc drop. Matched on name, as
  // CountOwned is, and a name is never both.
  int64_t CountItem(const ItemPrototype& proto) const;
  int64_t CountItem(const std::string& name) const;
  // Spends `count` of one. All or nothing.
  bool SpendItem(const std::string& name, int64_t count);
  // Adds `amount` meso to the character's balance. No-op if amount <= 0.
  void AddMeso(int64_t amount);
  // Takes `amount` meso. All or nothing: false and nothing spent on a short
  // purse. Every price in the game is charged by the method that knows it;
  // this is for meso leaving for a reason the character does not own.
  bool SpendMeso(int64_t amount);
  // Adds `amount` honor, the pool an Inner Ability reset is paid out of. No-op
  // if amount <= 0.
  void AddHonor(int64_t amount);
  // Banks V Points. Negative amounts are ignored: points leave the pool only
  // by being spent on a node.
  void AddVPoints(int64_t amount);

  /* The potions. A buff is owned or rented, and on or off; the pair decides
   * both what it does and what it costs. See //src/character/consumables.h.
   */

  // Whether the character is high enough for the first buff to be had, and so
  // for the Buffs tab to open at all.
  bool consumables_unlocked() const {
    return character_.level() >= kConsumableUnlockLevel;
  }
  // Whether the character bought `type` outright, so nothing is ever charged
  // for it again.
  bool ConsumableOwned(ConsumableType type) const;
  // Whether the player has it switched on. Says nothing about their level: an
  // owned buff stays switched on through a character who cannot yet use it.
  bool ConsumableActive(ConsumableType type) const;
  // Whether it is doing anything: switched on and past its level. Everything
  // reading a buff's effect asks this rather than the two above.
  bool ConsumableInEffect(ConsumableType type) const;
  // Switches it on if it is off and off if it is on, and says which it now
  // is. Refuses a buff this character's level has not opened, leaving it off.
  bool ToggleConsumable(ConsumableType type);
  // Buys `type` outright. All or nothing: nothing happens when the purse is
  // short, it is already owned, or the level has not opened it.
  bool BuyConsumable(ConsumableType type);
  // Charges `procs` procs of `type` -- farmed seconds for one buff, boss
  // entries for the other -- and returns what was taken. A short purse pays
  // what it has and stops at 0; the buff works either way.
  int64_t ChargeConsumable(ConsumableType type, double procs);

  // Charges every buff paid for by the second over `seconds` of farming. The
  // ones charged per boss entry are left alone.
  int64_t ChargeFarmingConsumables(double seconds);
  // Sells up to `count` copies from the `index`-th Etc stack, erasing it once
  // it empties. Returns the meso earned, which is 0 for an item worth nothing
  // -- a sale, not a refusal. A currency is on no shelf and is never sold.
  int64_t SellStackable(int index, int count);
  // The shop's buy-back shelf, newest sale first. Reading it is the panel's
  // business; BuyBack is the only thing that takes one off.
  const google::protobuf::RepeatedPtrField<BuyBackEntry>& buy_backs() const {
    return character_.buy_backs();
  }
  // Buys back `count` copies from entry `index` at the price the sale paid.
  // Prototypes resolve by name, so an item dropped from data/ cannot be bought
  // back. All or nothing, on Buy's terms; buying part of a stackable row
  // leaves the rest on the shelf.
  bool BuyBack(int index, int count,
               const std::map<std::string, EquipPrototype>& equips,
               const std::map<std::string, ItemPrototype>& items);

  // Sells the equip-tab item at `index` and returns the meso earned. Zero is
  // not a refusal: scrolls and stars pay out at the base item's price and a
  // trace pays out at none, and all of them still go.
  int64_t SellEquip(int index);
  // Buys `count` copies of `proto` at shop_price each. All or nothing, and
  // each copy is a fresh item, so buying two puts two rows in the bag. A price
  // of zero is for sale and free; naming NO price is what puts an item outside
  // the shop.
  bool Buy(const EquipPrototype& proto, int count);
  // Buys `count` copies of `proto` with the token it names, out of the Etc
  // tab. The caller resolves `token`, holding the catalog. All or nothing on
  // Buy's terms.
  bool BuyWithToken(const EquipPrototype& proto, const ItemPrototype& token,
                    int count);
  // As above for a stackable. The price and the bag's room are both checked
  // up front, so a purchase that cannot finish takes nothing.
  bool Buy(const ItemPrototype& proto, int count);
  // Moves the item at `inventory_index` into the slot SlotToFill picks, the
  // displaced item taking its place in the bag. `preset` is the setup it goes
  // into; past the first it goes as that preset's OWN, leaving the others.
  bool Equip(int inventory_index, StatPreset preset = StatPreset::kFirst);
  // The slot this item would be worn in: the first free one of its family, or
  // the first of them once full. Nowhere to go means the item names no slot,
  // or its family already holds this same item -- GMS's rule that no two of
  // the four rings are the same ring. A one-slot family is exempt, a second
  // hat being the swap it looks like.
  EquipSlot SlotToFill(const EquipPrototype& proto,
                       StatPreset preset = StatPreset::kFirst) const;
  // Moves what `preset` wears in `slot` to the bag. False for an empty slot,
  // and for an INHERITED one: a preset may only take off its own.
  bool Unequip(EquipSlot slot, StatPreset preset = StatPreset::kFirst);
  // Applies `scroll` to the item in `slot`; kScrollFail for an empty one.
  //
  // Every worn-item upgrade below reaches the item `preset` SHOWS. An
  // inherited item is the first preset's own, so upgrading one from the Boss
  // tab upgrades what Farm wears: it is one item.
  ScrollOutcome ScrollEquipped(EquipSlot slot, const Scroll& scroll,
                               StatPreset preset = StatPreset::kFirst);
  // Applies `scroll` to the inventory item at `index`. Returns kScrollFail if
  // `index` is out of range; otherwise returns the result of Scroll().
  ScrollOutcome ScrollInventory(int index, const Scroll& scroll);
  // One star force attempt on the item in `slot`, which kStarForceDestroy
  // takes off. Spends StarForceCost first, and does not roll without it.
  StarForceOutcome StarForceEquipped(EquipSlot slot,
                                     StatPreset preset = StatPreset::kFirst);
  // Applies a star force attempt to the inventory item at `index`. On
  // kStarForceDestroy, removes the item from inventory. Priced as above.
  StarForceOutcome StarForceInventory(int index);
  // Drives a golden hammer into the item: one more upgrade slot for
  // kGoldenHammerCost. False, and nothing spent, when it cannot be done.
  bool HammerEquipped(EquipSlot slot, StatPreset preset = StatPreset::kFirst);
  bool HammerInventory(int index);

  // Recovers the trace at `trace_index` onto the body at `base_item_index`:
  // both leave the bag and a new item carries the trace's scroll stats and
  // RecoveryStars() stars. Returns that star count.
  int RecoverTrace(int trace_index, int base_item_index);

  // Files a bag tab into the Sort button's order. Every row moves, so no row
  // index survives one of these.
  void SortEquipTab();
  void SortStackTab();

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
  // What `preset` wears, its own items and the ones it inherits together.
  const WornGear& equipped(StatPreset preset = StatPreset::kFirst) const {
    return resolved_[IndexOf(preset)];
  }
  // The items `preset` holds of its OWN: everything for the first preset,
  // which holds the whole body, and what the Gear tab draws in white for the
  // others.
  const std::map<EquipSlot, EquipInstance>& own_gear(StatPreset preset) const {
    return worn_[IndexOf(preset)];
  }
  // The item `preset` wears in `slot`, its own or the one it inherits, or
  // nullptr for a slot it leaves empty.
  const EquipInstance* WornAt(StatPreset preset, EquipSlot slot) const;
  // Whether what `preset` wears in `slot` is the first preset's. What the Gear
  // tab dims a row for.
  bool InheritsSlot(StatPreset preset, EquipSlot slot) const;
  // The item stacks the bag's Etc tab lists, in pickup order. Ordinary drops
  // only -- the currencies are counted in the purse and are on no tab that
  // holds slots.
  const std::vector<StackableItem>& stackables() const {
    return etc_items_.items();
  }
  // The currencies the character holds, in the order the Token tab reads them.
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
  // Available Hyper SP, which no stage holds -- see Character::hyper_sp.
  int hyper_sp() const {
    return character_.hyper_sp();
  }

  // The account's Autoswap Presets switch, mirrored here because every stat
  // read holds the character and none holds the account.
  bool autoswap_presets() const {
    return autoswap_presets_;
  }
  void set_autoswap_presets(bool on) {
    autoswap_presets_ = on;
  }
  // The furthest any character on the account has reached, mirrored here for
  // the same reason -- Blessing of the Fairy is read wherever stats are. Never
  // BELOW this character's own level: the account's record is written at the
  // save, so a climb in progress is theirs alone until then.
  void set_account_max_level(int level) {
    account_max_level_ = level;
  }
  int account_max_level() const {
    return std::max(account_max_level_, character_.level());
  }
  // What the OTHER characters on the account have climbed, per job line,
  // mirrored here for the reason above. This character is NOT in it: they
  // fold themselves in at every read, their level climbing mid-session where
  // a slot's does not.
  void set_link_tally(LinkTally tally) {
    link_tally_ = std::move(tally);
  }
  const LinkTally& link_tally() const {
    return link_tally_;
  }
  // The link skills this character carries, their own line's excluded -- it
  // is held for free and is not in the list. See Character.link_skills.
  const google::protobuf::RepeatedPtrField<std::string>& link_skills() const {
    return character_.link_skills();
  }
  // Whether this character carries `skill`: their own line's, or one of the
  // equipped. False for anything that is not a link skill, and for every one
  // of them until the account has opened them.
  bool HoldsLinkSkill(const Skill& skill) const;
  // What `skill` stands at for this character, 0 for one they do not hold.
  // Held to the skill's own maximum: the ladder in the data runs to GMS's
  // top level whether or not the four job lines can reach it.
  int LinkSkillLevel(const Skill& skill) const;
  // Puts `name` on, or takes it off. Equipping refuses a full list and one
  // already on; both refuse a name that is not a link skill, which is the
  // caller's to check. Return whether anything moved.
  bool EquipLinkSkill(const std::string& name);
  bool UnequipLinkSkill(const std::string& name);
  // Fills the list up to kMaxEquippedLinkSkills from `skills`, in catalog
  // order, and drops names the catalog no longer has. Returns how many moved.
  // Called on loading a save: with no screen to equip them on, what the
  // account has unlocked is simply carried.
  int ReconcileLinkSkills(const std::map<std::string, Skill>& skills);
  // Which preset of `kind` the player has put in use, and the way to change
  // it. Read only while the autoswap is off; each kind keeps its own.
  StatPreset SlotInUse(PresetKind kind) const;
  void SetSlotInUse(PresetKind kind, StatPreset slot);
  // Swaps what two slots of `kind` hold, moving the choice of which is in use
  // with them: reordering presets is not asking to change the one in play.
  void SwapPresets(PresetKind kind, StatPreset a, StatPreset b);
  // Which preset of `kind` answers while the character is doing `activity`:
  // the autoswap's slot, or the one in use with the switch off.
  StatPreset SlotFor(PresetKind kind, Activity activity) const;
  // Every Hyper Stat point the character's level has ever paid out.
  int hyper_stat_points() const;
  // What is left of them once `preset` is paid for.
  int hyper_stat_points_left(StatPreset preset = StatPreset::kFirst) const;
  // The level `field` is raised to in `preset`.
  int hyper_stat_level(HyperStatField field,
                       StatPreset preset = StatPreset::kFirst) const;
  // What `field` is worth to this character right now, in the units the stat
  // is stated in. Zero for one they have not raised.
  double hyper_stat_bonus(HyperStatField field,
                          StatPreset preset = StatPreset::kFirst) const;
  // The highest level any of their stats may reach.
  int max_hyper_stat_level() const;

  // Honor left to spend on an Inner Ability reset.
  int64_t honor() const {
    return character_.honor();
  }
  // V Points left to spend on the V Matrix.
  int64_t v_points() const {
    return character_.v_points();
  }
  // Whether the character has taken the 5th advancement, which is what opens
  // the matrix and what makes their kills pay V Points at all.
  bool v_matrix_unlocked() const {
    return character_.job_stage() >= kFifthJobStage;
  }
  // Whether the character is high enough for their Inner Ability to pay --
  // and, since the two go together, for the panel to open at all.
  bool inner_ability_unlocked() const {
    return character_.level() >= kInnerAbilityUnlockLevel;
  }
  // Whether the ACCOUNT has opened the link skills. Read off the same
  // watermark Blessing of the Fairy is, so a character who reaches the level
  // themselves opens it the moment they do rather than at the next save.
  bool link_skills_unlocked() const {
    return !link_skills_off_ && account_max_level() >= kLinkSkillsLevel;
  }
  // Shuts them off outright, level or no level. For a SIM, which measures a
  // character standing alone: the balance numbers were taken before link
  // skills existed and have not been re-taken against them. See
  // TestOptions::link_skills.
  void set_link_skills_off(bool off) {
    link_skills_off_ = off;
  }
  // The three lines `preset` is holding, and the rank of the whole.
  const AbilityPreset& ability(StatPreset preset = StatPreset::kFirst) const {
    return PresetOf(character_.inner_ability(), preset);
  }
  // What one reset of `preset` would cost at the lines it is holding now.
  int64_t ability_reset_cost(StatPreset preset = StatPreset::kFirst) const;
  // Holds or frees the line at `index`, by the rules SetAbilityLineLocked
  // states. Returns whether anything changed.
  bool LockAbilityLine(int index, bool locked,
                       StatPreset preset = StatPreset::kFirst);
  // Pays a reset out of the honor pool and rerolls `preset`. All or nothing.
  bool ResetAbility(StatPreset preset = StatPreset::kFirst);
  // Writes `lines` into `preset` outright, paying no honor. For seeding a
  // character meant to arrive holding them: rolling for a named sheet takes
  // hundreds of tries and would hand back a different character every run.
  void SetAbility(const AbilityPreset& lines,
                  StatPreset preset = StatPreset::kFirst);

  // The points `skill` is bought with: the Hyper pool for a Hyper Skill, its
  // job stage's otherwise. Asked here so the panel, the screen and LearnSkill
  // cannot disagree.
  int SpFor(const Skill& skill) const {
    if (skill.v_node() != V_NODE_KIND_UNSPECIFIED) {
      // Counted in V Points rather than in levels: a node's levels are not
      // one point each, so what this buys is VNodeCostFor's to say.
      return static_cast<int>(std::min<int64_t>(v_points(), INT_MAX));
    }
    return skill.hyper() ? hyper_sp() : sp(StageForAdvancement(BookOf(skill)));
  }
  // What raising `skill` by `amount` levels costs in V Points, and 0 for
  // anything that is not a node.
  int VNodeCostFor(const Skill& skill, int amount) const;
  // How many more levels of `skill` can be bought now. A point a level for an
  // ordinary skill; for a node, as far up the ladder as the V Points reach,
  // which is not the same as how many are in the pool.
  int LevelsAffordable(const Skill& skill) const;
  // The learned level in `skill`, 0 for unlearned. A Vengeance form reads its
  // Benevolence skill's level, being the same row of the same book.
  int skill_level(const Skill& skill) const {
    if (skill.account_levels_per_level() > 0) {
      return DerivedSkillLevel(skill);
    }
    if (skill.link_line() != JOB_UNSPECIFIED) {
      return LinkSkillLevel(skill);
    }
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
  // Flips `skill` and says which it now is. Refuses one that is not a toggle
  // or is not learned.
  bool ToggleSkill(const Skill& skill);
  // Whether `advancement` is one this character has taken. Every first job
  // draws on the same stage-1 SP pool, so the stage alone does not say whose
  // book a skill is from.
  bool HasAdvancement(JobAdvancement advancement) const;
  // The book this character holds that lists `skill`. A shared skill sits in
  // several at once, and which one they took says whose SP paid for it.
  JobAdvancement BookHeldFor(const Skill& skill) const;
  // Whether any book this character holds lists `skill`.
  bool HasBookFor(const Skill& skill) const {
    return BookHeldFor(skill) != JOB_ADVANCEMENT_UNSPECIFIED;
  }
  // Whether whatever `skill` demands be learned first has been. LearnSkill
  // refuses when this is false; the skills tab asks so it can dim the row.
  bool MeetsSkillRequirement(const Skill& skill) const;
  // Whether `skill` comes from a book this character holds: their matrix for a
  // node, the advancement it names otherwise. Levels are keyed by display name
  // and branches share several, so this is what keeps another branch's copy
  // from folding in beside their own.
  bool HoldsSkillFrom(const Skill& skill) const;
  // Whether this character's matrix holds the node `skill`: a common node
  // reaches every job, every other names the 5th advancement that may buy
  // it.
  bool ReachesVNode(const Skill& skill) const;
  // The whole character as one proto, the live containers folded back in.
  // proto() alone does not carry those: they live in C++ containers and are
  // written here only when someone asks for the lot.
  Character ToProto() const;

  // The inverse, resolving each item's prototype by name. An item the
  // catalogs no longer hold is DROPPED rather than guessed at, so removing
  // something from data/ costs that item and not the character. Bypasses the
  // equip rules on purpose: what was worn stays worn.
  void RestoreFrom(const Character& saved,
                   const std::map<std::string, EquipPrototype>& equips,
                   const std::map<std::string, ItemPrototype>& items);

  // Puts the AP back on its books and returns what it was off by. Short, and
  // the difference arrives unspent; long, and it comes out of the pool first
  // and the stats after, the primary last. Called on loading a save.
  int ReconcileAp();

  // Puts learned skills back inside their books and returns how many points
  // moved. A skill past a max_level the data has since lowered is cut, and its
  // points are respent at random on the SAME book -- which is what keeps the
  // SP ledger straight, those points having come out of one stage's pool. The
  // pool is only the last resort. Called on loading a save.
  int ReconcileSkills(const std::map<std::string, Skill>& skills);

  // Puts every SP pool back on its books and returns how many points moved. A
  // short pool is handed the difference -- what a character already past a
  // rung when the ladder shipped is missing -- and a long one gives it up,
  // down to empty and no further. Every stage's pool and the Hyper pool, all
  // read off the LEVEL: an advancement hands over no SP. Called after
  // ReconcileSkills, which moves points between a book and its pool.
  int ReconcileSp(const std::map<std::string, Skill>& skills);

  // Puts both Hyper Stat allocations back inside what the level has paid for.
  // A stat past the cap is cut, a locked one emptied, and one that outspends
  // the pool gives up its HIGHEST levels first, those being the dear ones.
  int ReconcileHyperStats();

  // Sum of stats from everything `preset` wears. Updated for every preset at
  // once, since one item can be worn by all three.
  const EquipStats& equip_stats(StatPreset preset = StatPreset::kFirst) const {
    return equip_stats_[IndexOf(preset)];
  }
  // The share of that total the worn Arcane Symbols paid. Held apart because
  // a symbol grants FINAL stat: no %stat may multiply it, so the fold that
  // applies one takes this back off first.
  const EquipStats& symbol_stats(StatPreset preset = StatPreset::kFirst) const {
    return symbol_stats_[IndexOf(preset)];
  }
  // What the potentials on everything worn come to. Rebuilt with the equip
  // stats, off the same pass over the worn map.
  const PotentialTotals& potential_totals(
      StatPreset preset = StatPreset::kFirst) const {
    return potential_totals_[IndexOf(preset)];
  }
  // One cube into the item worn in `slot`, rerolling its lines and perhaps
  // ranking it up. The mechanism, charging NOTHING: BuyCube is the purchase.
  bool CubeWorn(EquipSlot slot, CubeType cube,
                StatPreset preset = StatPreset::kFirst);
  // One cube into the item worn in `slot`, charged kCubeCost: rolls what the
  // piece would become and hands it back WITHOUT putting it on. Taking the
  // roll is TakePotential's business -- a player offered worse keeps what they
  // have, and the cube is spent either way.
  std::optional<Potential> BuyCube(EquipSlot slot, CubeType cube,
                                   StatPreset preset = StatPreset::kFirst);
  // Puts `potential` on the item worn in `slot`, which is a player accepting
  // a roll. False, changing nothing, for an empty slot.
  bool TakePotential(EquipSlot slot, const Potential& potential,
                     StatPreset preset = StatPreset::kFirst);
  // One cube into a worn or bagged item, charged and taken whatever it rolls:
  // the pair the cubing screen presses.
  bool CubeEquipped(EquipSlot slot, CubeType cube,
                    StatPreset preset = StatPreset::kFirst);
  bool CubeInventory(int index, CubeType cube);
  // Spare copies of the Arcane Symbol for `slot` sitting in the equip bag.
  // Traces do not count, as they never do -- see CountOwned.
  int SpareSymbols(EquipSlot slot) const;
  // What each spare is worth in duplicates, in the order CombineSymbols would
  // eat them. See SymbolWorth.
  std::vector<int> SpareSymbolWorths(EquipSlot slot) const;
  // Absorbs up to `count` spares into the symbol worn in `slot` and throws
  // them away, returning how many it took. Raising the LEVEL is a step of its
  // own, paid for in meso -- see LevelUpSymbol.
  int CombineSymbols(EquipSlot slot, int count,
                     StatPreset preset = StatPreset::kFirst);
  // Raises the symbol worn in `slot` one level, charging the rung's meso and
  // carrying excess EXP into the next. Needs a symbol that has taken its
  // duplicates and a purse that covers it.
  bool LevelUpSymbol(EquipSlot slot, StatPreset preset = StatPreset::kFirst);
  // Arcane Force carried: the worn symbols plus what the Hyper Stat adds.
  // What every Arcane River map measures them against -- see
  // ArcaneFactorsFor.
  int arcane_force(Activity activity = Activity::kFarming) const;
  // Whether an item of this type contributes its attack as things stand:
  // throwing stars arm a claw and nothing else. equip_stats() applies it
  // already; it is public so the display can show an inert attack as inert.
  bool AttackCounts(const EquipPrototype& proto,
                    StatPreset preset = StatPreset::kFirst) const;
  // The type of weapon in hand, or EQUIP_TYPE_UNSPECIFIED with the slot empty.
  // What the skills that demand a particular weapon are asked against.
  EquipType weapon_type(StatPreset preset = StatPreset::kFirst) const;
  // Whether anything is worn in the secondary slot. See
  // Skill.requires_secondary, which waits on it.
  bool has_secondary(StatPreset preset = StatPreset::kFirst) const;

  // Teaches the character which sets their gear belongs to. DATA rather than
  // state -- what is earned comes from what is worn -- so it is handed over at
  // startup and never saved.
  void UseEquipSets(std::map<std::string, EquipSet> sets);
  // Every set tier the worn pieces have earned, cumulative: four pieces answer
  // with both the three and the four. Folded in beside the passives, a set
  // bonus granting what a passive grants.
  const std::vector<SkillEffect>& set_bonuses(
      StatPreset preset = StatPreset::kFirst) const {
    return set_bonuses_[IndexOf(preset)];
  }
  // The sets the character knows about, keyed as their data files were loaded.
  const std::map<std::string, EquipSet>& equip_sets() const {
    return equip_sets_;
  }
  // Whether the item named is worn right now, by display name.
  bool IsWearing(const std::string& item_name,
                 StatPreset preset = StatPreset::kFirst) const;
  // The item of `family` being worn, or "" for none. A set slot no single item
  // can fill -- a weapon belongs to one class -- names a family instead.
  std::string WornOfFamily(const std::string& family,
                           StatPreset preset = StatPreset::kFirst) const;
  // The piece of this member that is on, or "" for none. A slot naming several
  // alternates takes any one, and counts once however many are on.
  std::string WornOfMember(const EquipSetMember& member,
                           StatPreset preset = StatPreset::kFirst) const;
  // How many pieces of `set` are worn right now. What every tier is measured
  // against, and what the inspect screen greys its unearned tiers by.
  int PiecesWornOf(const EquipSet& set,
                   StatPreset preset = StatPreset::kFirst) const;
  // A copy of this character with `item` worn in `preset`, for pricing a piece
  // that is not theirs to wear yet -- the shop's shelf, another player's
  // sheet. Nothing leaves the copy, and nothing else about it moves: the bag
  // keeps whatever it held, the displaced item simply is not there. Handed
  // back unchanged when the item names no slot this character can fill.
  CharacterInstance Wearing(const EquipTabItem& item, StatPreset preset) const;

 private:
  // A copy of `other` carrying everything a stat is read off and an empty bag.
  // The bag is the one part of a character that cannot be copied at all -- it
  // holds the items by pointer -- and no stat is read off it. What Wearing
  // builds its probe on.
  struct WornOnly {};
  CharacterInstance(const CharacterInstance& other, WornOnly);

  // Buys `amount` levels of a V Matrix node out of the V Point pool, at what
  // its kind's ladder charges for the levels being crossed. All or nothing.
  bool LearnVNode(const Skill& skill, int amount);
  // Mirrors the account's switch -- see autoswap_presets(). Off is what the
  // game ships with, so a character with no account behind them keeps
  // whichever preset is in use.
  bool autoswap_presets_ = false;
  // Mirrors the account's record -- see account_max_level().
  int account_max_level_ = 0;
  // Mirrors what the account's OTHER characters have climbed -- see
  // link_tally().
  LinkTally link_tally_;
  // Whether the link skills are shut off for this character whatever the
  // account has reached -- see set_link_skills_off().
  bool link_skills_off_ = false;

  // What a skill nobody buys stands at: the account's climb over the levels
  // one of its own costs, held to the cap. See
  // Skill.account_levels_per_level.
  int DerivedSkillLevel(const Skill& skill) const {
    return std::min(SkillMaxLevel(skill),
                    account_max_level() / skill.account_levels_per_level());
  }

  // Gives a nameless character kDefaultUsername. Both doors a Character comes
  // in through call it, which is what makes username() never empty.
  void EnsureUsername();
  // Seeds both Inner Ability presets with the three lines every character is
  // handed, so a save written before it existed comes back holding them.
  void EnsureInnerAbility();
  // Spends one star force attempt's price, or returns false and spends
  // nothing. Both StarForce entry points call it before they roll.
  bool PayForStarForce(const EquipInstance& item);
  bool PayForHammer(const EquipInstance& item);
  bool PayForCube(const EquipInstance& item);
  // Puts a sale on the buy-back shelf, newest first, and drops the oldest row
  // once the shelf is full.
  void RecordSale(BuyBackEntry entry);
  // The two halves of BuyBack, which shares only the row lookup: "room" is a
  // slot for one and a share of every open stack for the other.
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
  // One preset's half of it: the resolved gear, then the totals off it.
  void RecomputePreset(StatPreset preset);
  // WornAt's mutable twin: how every upgrade reaches the item the preset it
  // is being asked from is showing.
  EquipInstance* WornIn(StatPreset preset, EquipSlot slot);
  // Takes what `preset` wears in `slot` off and hands it back. What Unequip
  // and a shattering star force both go through.
  std::optional<EquipInstance> TakeWorn(StatPreset preset, EquipSlot slot);
  // Rebuilds set_bonuses_: every tier of every known set that the worn pieces
  // reach. Cumulative, so a four-piece set contributes both its tiers.
  void RecomputeSetBonuses(StatPreset preset);
  std::mt19937& rng_;
  Character character_;
  InventoryInstance inventory_;
  // The worn items, one map per preset. The first holds the whole body; the
  // others hold only the slots they fill themselves -- see EquipPreset in
  // character.proto for why they cannot each hold a copy.
  std::array<std::map<EquipSlot, EquipInstance>, kNumStatPresets> worn_;
  // Each preset's gear resolved against the first, and the totals that come
  // off it. Rebuilt together by RecomputeEquipStats.
  std::array<WornGear, kNumStatPresets> resolved_;
  StackTab etc_items_;
  CurrencyPurse currencies_;
  std::array<EquipStats, kNumStatPresets> equip_stats_;
  std::array<EquipStats, kNumStatPresets> symbol_stats_;
  std::array<PotentialTotals, kNumStatPresets> potential_totals_;
  std::array<int, kNumStatPresets> arcane_force_ = {};
  // Meso the buffs have run up and not been charged for, always under 1. The
  // live tick charges three times a second, so without this a potion at 1,000
  // a second would quietly cost 999. Not saved.
  double consumable_debt_ = 0.0;
  std::map<std::string, EquipSet> equip_sets_;
  std::array<std::vector<SkillEffect>, kNumStatPresets> set_bonuses_;
};

}  // namespace ms

#endif  // MS_CHARACTER_H_
