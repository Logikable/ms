#include "src/item/item.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/item/equip_stats.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

constexpr int kDefaultMaxStack = 200;

// Per-star primary stat gains for 1-15★ (index i is the gain for i★→(i+1)★).
constexpr int kPrimaryStatDeltas[15] = {
    2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};

// Per-star Max HP over the same range, for items GMS raises it on, and per-star
// Max MP for a weapon, which grows by the same amounts.
constexpr int kMaxHpDeltas[15] = {
    5, 5, 5, 10, 10, 15, 15, 20, 20, 25, 25, 25, 25, 25, 25,
};

// The attempts in 1-15★ where a glove gains a point of attack, GMS's substitute
// for the Max HP gloves don't get. Index i is the gain for i★→(i+1)★.
constexpr bool kGloveAttackStars[15] = {
    false, false, false, false, true, false, true, false,
    true,  false, true,  false, true, true,  true,
};

// The fraction of the item's existing value one star adds to a scaled stat.
// Attack grows by a fiftieth, defense by a twentieth.
constexpr int kAttackPercent = 2;
constexpr int kDefensePercent = 5;

// Stat and attack gained on reaching each star from 16★ to 30★, by the item's
// required level; index i is the gain for (i+15)★→(i+16)★. Weapons and other
// items use different attack columns, and the shared stat column ends at 23★.
// GMS's table stops at 25★; rows past it repeat the last step.
struct HighStarEntry {
  int stat;
  int weapon_att;
  int other_att;
};

// This range's maximum is 20★; entries 5-14 are padding.
constexpr HighStarEntry kHighStar128_137[15] = {
    {7, 6, 7}, {7, 7, 8}, {7, 7, 9}, {7, 8, 10}, {7, 9, 11},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},  {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},  {0, 0, 0},
};
constexpr HighStarEntry kHighStar138_149[15] = {
    {9, 7, 8},   {9, 8, 9},   {9, 8, 10},  {9, 9, 11},  {9, 10, 12},
    {9, 11, 13}, {9, 12, 15}, {0, 30, 17}, {0, 31, 19}, {0, 32, 21},
    {0, 33, 23}, {0, 34, 25}, {0, 35, 27}, {0, 36, 29}, {0, 37, 31},
};
constexpr HighStarEntry kHighStar150_159[15] = {
    {11, 8, 9},   {11, 9, 10},  {11, 9, 11}, {11, 10, 12}, {11, 11, 13},
    {11, 12, 14}, {11, 13, 16}, {0, 31, 18}, {0, 32, 20},  {0, 33, 22},
    {0, 34, 24},  {0, 35, 26},  {0, 36, 28}, {0, 37, 30},  {0, 38, 32},
};
constexpr HighStarEntry kHighStar160_199[15] = {
    {13, 9, 10},  {13, 9, 11},  {13, 10, 12}, {13, 11, 13}, {13, 12, 14},
    {13, 13, 15}, {13, 14, 17}, {0, 32, 19},  {0, 33, 21},  {0, 34, 23},
    {0, 35, 25},  {0, 36, 27},  {0, 37, 29},  {0, 38, 31},  {0, 39, 33},
};
constexpr HighStarEntry kHighStar200_249[15] = {
    {15, 13, 12}, {15, 13, 13}, {15, 14, 14}, {15, 14, 15}, {15, 15, 16},
    {15, 16, 17}, {15, 17, 19}, {0, 34, 21},  {0, 35, 23},  {0, 36, 25},
    {0, 37, 27},  {0, 38, 29},  {0, 39, 31},  {0, 40, 33},  {0, 41, 35},
};
// The weapon column is zero from 23★, since GMS has no star-forceable level
// 250+ weapon. TODO: Superior equipment has a different stat table and a 15★
// cap; handle it separately when Superior items are added.
constexpr HighStarEntry kHighStar250Plus[15] = {
    {17, 16, 14}, {17, 16, 15}, {17, 17, 16}, {17, 17, 17}, {17, 18, 18},
    {17, 19, 19}, {17, 20, 21}, {0, 0, 23},   {0, 0, 25},   {0, 0, 27},
    {0, 0, 29},   {0, 0, 31},   {0, 0, 33},   {0, 0, 35},   {0, 0, 37},
};

HighStarEntry HighStarGainAt(int required_level, int star_to) {
  int idx = star_to - 16;
  if (required_level >= 250) {
    return kHighStar250Plus[idx];
  }
  if (required_level >= 200) {
    return kHighStar200_249[idx];
  }
  if (required_level >= 160) {
    return kHighStar160_199[idx];
  }
  if (required_level >= 150) {
    return kHighStar150_159[idx];
  }
  if (required_level >= 138) {
    return kHighStar138_149[idx];
  }
  if (required_level >= 128) {
    return kHighStar128_137[idx];
  }
  return {0, 0, 0};
}

// Which of the four primary stats a star raises. Below 16★ it's the stats the
// item's job needs; from 16★ it's the stats the item already shows.
struct StatFlags {
  bool str = false;
  bool dex = false;
  bool int_ = false;
  bool luk = false;
};

StatFlags StatsForJobCategory(EquipJobCategory cat) {
  switch (cat) {
    case EQUIP_JOB_CATEGORY_WARRIOR:
    case EQUIP_JOB_CATEGORY_BOWMAN:
    case EQUIP_JOB_CATEGORY_PIRATE:
      return {true, true, false, false};
    case EQUIP_JOB_CATEGORY_MAGICIAN:
      return {false, false, true, true};
    case EQUIP_JOB_CATEGORY_THIEF:
      return {false, true, false, true};
    default:
      return {true, true, true, true};
  }
}

StatFlags PrimaryStatFlags(const EquipPrototype& proto) {
  if (proto.equip_job_categories_size() == 0) {
    return {true, true, true, true};
  }
  StatFlags flags;
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    StatFlags j = StatsForJobCategory(
        static_cast<EquipJobCategory>(proto.equip_job_categories(i)));
    flags.str |= j.str;
    flags.dex |= j.dex;
    flags.int_ |= j.int_;
    flags.luk |= j.luk;
  }
  return flags;
}

// GMS's "Category A": the slots whose stars raise Max HP. No overall, because
// this game splits top and bottom. No secondary either: GMS's list names the
// shield, and ours holds a medallion or book.
bool RaisesMaxHp(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
    case EQUIP_SLOT_HAT:
    case EQUIP_SLOT_TOP:
    case EQUIP_SLOT_BOTTOM:
    case EQUIP_SLOT_CAPE:
    case EQUIP_SLOT_RING:
    case EQUIP_SLOT_RING_2:
    case EQUIP_SLOT_RING_3:
    case EQUIP_SLOT_RING_4:
    case EQUIP_SLOT_PENDANT:
    case EQUIP_SLOT_PENDANT_2:
    case EQUIP_SLOT_BELT:
    case EQUIP_SLOT_SHOULDER:
      return true;
    case EQUIP_SLOT_UNSPECIFIED:
    case EQUIP_SLOT_PROJECTILE:
    case EQUIP_SLOT_SECONDARY:
    case EQUIP_SLOT_FACE_ACCESSORY:
    case EQUIP_SLOT_EYE_ACCESSORY:
    case EQUIP_SLOT_POCKET:
    // GMS's list includes neither, even though they look like armour. Gloves
    // get attack from stars instead; see kGloveAttackStars.
    case EQUIP_SLOT_GLOVES:
    case EQUIP_SLOT_SHOES:
    // Trophies take no stars at all, and hearts aren't Category A either; their
    // stars give attack.
    case EQUIP_SLOT_BADGE:
    case EQUIP_SLOT_EMBLEM:
    case EQUIP_SLOT_MEDAL:
    case EQUIP_SLOT_HEART:
    // Among accessories, GMS's list includes only the ring, pendant, belt and
    // shoulder.
    case EQUIP_SLOT_EARRINGS:
    // Symbols take no stars at all, so no category applies.
    case EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY:
    case EQUIP_SLOT_SYMBOL_CHU_CHU_ISLAND:
    case EQUIP_SLOT_SYMBOL_LACHELEIN:
    case EQUIP_SLOT_SYMBOL_ARCANA:
    case EQUIP_SLOT_SYMBOL_MORASS:
    case EQUIP_SLOT_SYMBOL_ESFERA:
      return false;
  }
  return false;
}

// The stats an item shows before stars, which GMS calls visible.
StatFlags VisibleStatFlags(const EquipStats& shown) {
  return {shown.str() > 0, shown.dex() > 0, shown.int_() > 0, shown.luk() > 0};
}

void AddToStats(EquipStats* gains, StatFlags flags, int delta) {
  if (flags.str) {
    gains->set_str(gains->str() + delta);
  }
  if (flags.dex) {
    gains->set_dex(gains->dex() + delta);
  }
  if (flags.int_) {
    gains->set_int_(gains->int_() + delta);
  }
  if (flags.luk) {
    gains->set_luk(gains->luk() + delta);
  }
}

// One attempt's gain in a scaled stat: GMS's 1 + RoundDown[stat x share], taken
// from the item's own stat plus the stars' gains so far, which makes the gains
// compound. A stat the item doesn't show gains nothing, however many stars.
int ScaledGain(int shown, int gained, int percent) {
  if (shown <= 0) {
    return 0;
  }
  return 1 + (shown + gained) * percent / 100;
}

// What one star's gain is computed from: the item's own properties, and the
// gains so far, which scaled stats take their next share from.
struct StarForceRun {
  EquipSlot slot;
  bool is_weapon;
  int required_level;
  StatFlags job_stats;
  StatFlags visible_stats;
  int shown_att;
  int shown_matt;
  int shown_def;
  EquipStats gains;
};

// One attempt in the 1-15★ range. The job's stats grow by a flat amount; attack
// and defense grow by a share of what the item already has.
void AddLowStar(int index, StarForceRun* run) {
  EquipStats& gains = run->gains;
  AddToStats(&gains, run->job_stats, kPrimaryStatDeltas[index]);
  if (RaisesMaxHp(run->slot)) {
    gains.set_max_hp(gains.max_hp() + kMaxHpDeltas[index]);
  }
  if (!run->is_weapon) {
    gains.set_def(gains.def() +
                  ScaledGain(run->shown_def, gains.def(), kDefensePercent));
    // A flat point instead of a share: gloves are the only armour whose attack
    // grows this low on the ladder, and only if they already show attack.
    if (run->slot == EQUIP_SLOT_GLOVES && kGloveAttackStars[index]) {
      if (run->shown_att > 0) {
        gains.set_attack(gains.attack() + 1);
      }
      if (run->shown_matt > 0) {
        gains.set_magic_attack(gains.magic_attack() + 1);
      }
    }
    return;
  }
  // Only weapons get Max MP from stars, and only weapons gain attack at all in
  // this range.
  gains.set_max_mp(gains.max_mp() + kMaxHpDeltas[index]);
  gains.set_attack(gains.attack() +
                   ScaledGain(run->shown_att, gains.attack(), kAttackPercent));
  gains.set_magic_attack(gains.magic_attack() + ScaledGain(run->shown_matt,
                                                           gains.magic_attack(),
                                                           kAttackPercent));
}

// One attempt at 16★ and up, where the table is by equipment level. Every stat
// the item shows grows, regardless of job, and armour starts gaining its own
// flat attack.
void AddHighStar(int star_to, StarForceRun* run) {
  HighStarEntry entry = HighStarGainAt(run->required_level, star_to);
  EquipStats& gains = run->gains;
  AddToStats(&gains, run->visible_stats, entry.stat);
  if (run->is_weapon) {
    if (run->shown_att > 0) {
      gains.set_attack(gains.attack() + entry.weapon_att);
    }
    if (run->shown_matt > 0) {
      gains.set_magic_attack(gains.magic_attack() + entry.weapon_att);
    }
    return;
  }
  // Armour gains this whether or not it had attack to begin with.
  gains.set_attack(gains.attack() + entry.other_att);
  gains.set_magic_attack(gains.magic_attack() + entry.other_att);
  // Only the level 250 band keeps gaining defense past 15★.
  if (run->required_level >= 250) {
    gains.set_def(gains.def() +
                  ScaledGain(run->shown_def, gains.def(), kDefensePercent));
  }
}

// A linear search of the catalog. Small enough that building an index would
// cost more than it saves, and every caller runs on a keypress.
template <typename Proto>
const Proto* FindByName(const std::map<std::string, Proto>& catalog,
                        const std::string& name) {
  for (const std::pair<const std::string, Proto>& entry : catalog) {
    if (entry.second.name() == name) {
      return &entry.second;
    }
  }
  return nullptr;
}

}  // namespace

std::vector<EquipSlot> SlotFamily(EquipSlot slot) {
  static const std::vector<EquipSlot> kRings = {
      EQUIP_SLOT_RING, EQUIP_SLOT_RING_2, EQUIP_SLOT_RING_3, EQUIP_SLOT_RING_4};
  static const std::vector<EquipSlot> kPendants = {EQUIP_SLOT_PENDANT,
                                                   EQUIP_SLOT_PENDANT_2};
  for (const std::vector<EquipSlot>* family : {&kRings, &kPendants}) {
    if (std::find(family->begin(), family->end(), slot) != family->end()) {
      return *family;
    }
  }
  return {slot};
}

EquipSlot BaseSlot(EquipSlot slot) {
  return SlotFamily(slot).front();
}

int SlotIndex(EquipSlot slot) {
  std::vector<EquipSlot> family = SlotFamily(slot);
  return static_cast<int>(std::find(family.begin(), family.end(), slot) -
                          family.begin());
}

void FillTokenShelves(const std::map<std::string, EquipPrototype>& equips,
                      std::map<std::string, ItemPrototype>& items) {
  std::map<std::string, int> levels;
  std::map<std::string, std::set<EquipSlot>> slots;
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    const EquipPrototype& proto = entry.second;
    if (proto.token_price() <= 0) {
      continue;
    }
    int& level = levels[proto.token_item()];
    level = std::max(level, proto.required_level());
    slots[proto.token_item()].insert(BaseSlot(proto.equip_slot()));
  }
  for (std::pair<const std::string, ItemPrototype>& entry : items) {
    ItemPrototype& token = entry.second;
    if (token.kind() != ITEM_KIND_TOKEN || levels.count(entry.first) == 0) {
      continue;
    }
    token.set_currency_level(levels[entry.first]);
    const std::set<EquipSlot>& fills = slots[entry.first];
    token.set_currency_slot(fills.size() == 1 ? *fills.begin()
                                              : EQUIP_SLOT_UNSPECIFIED);
  }
}

const std::string& ShortName(const ItemPrototype& proto) {
  return proto.short_name().empty() ? proto.name() : proto.short_name();
}

bool Supports(const EquipPrototype& proto, Upgrade upgrade) {
  for (int i = 0; i < proto.unsupported_upgrades_size(); ++i) {
    if (proto.unsupported_upgrades(i) == upgrade) {
      return false;
    }
  }
  return true;
}

bool TakesUpgradeSlots(const EquipPrototype& proto) {
  return Supports(proto, UPGRADE_SCROLL) && proto.upgrade_slots() > 0;
}

int TotalUpgradeSlots(const EquipPrototype& proto, const Equip& state) {
  return proto.upgrade_slots() + state.hammers();
}

int SellPrice(const EquipPrototype& proto) {
  if (proto.has_sell_price()) {
    return proto.sell_price();
  }
  return proto.shop_price() / 10;
}

void StackableItem::add_count(int delta) {
  count_ += delta;
}

int StackableItem::max_stack() const {
  if (prototype_.max_stack() > 0) {
    return prototype_.max_stack();
  }
  return kDefaultMaxStack;
}

const EquipPrototype* FindEquipByName(
    const std::map<std::string, EquipPrototype>& equips,
    const std::string& name) {
  return FindByName(equips, name);
}

const ItemPrototype* FindItemByName(
    const std::map<std::string, ItemPrototype>& items,
    const std::string& name) {
  return FindByName(items, name);
}

Equip EquipTabItem::SavedState() const {
  Equip saved = state_;
  saved.set_trace(is_trace());
  return saved;
}

std::unique_ptr<EquipTabItem> EquipTrace::Clone() const {
  return std::make_unique<EquipTrace>(*this);
}

EquipTrace::EquipTrace(EquipPrototype prototype, Equip state)
    : EquipTabItem(std::move(prototype), std::move(state)),
      display_name_(prototype_.name() + " Trace") {
}

EquipStats EquipTabItem::StarForceStatGains(int stars) const {
  if (stars < 0) {
    stars = state_.stars();
  }
  // What the item shows before any stars: its own stats plus successful
  // scrolls. Star force gains are deliberately left out: each star's share is
  // taken from these plus the gains so far, which the run tracks as it goes.
  const EquipStats shown_sources[] = {prototype_.base_stats(),
                                      state_.scroll_stats()};
  const EquipStats shown = SumEquipStats(shown_sources);

  StarForceRun run;
  run.slot = prototype_.equip_slot();
  run.is_weapon = run.slot == EQUIP_SLOT_PRIMARY_WEAPON;
  run.required_level = prototype_.required_level();
  run.job_stats = PrimaryStatFlags(prototype_);
  run.visible_stats = VisibleStatFlags(shown);
  run.shown_att = shown.attack();
  run.shown_matt = shown.magic_attack();
  run.shown_def = shown.def();

  for (int s = 0; s < stars; ++s) {
    if (s < 15) {
      AddLowStar(s, &run);
    } else {
      AddHighStar(s + 1, &run);
    }
  }
  return run.gains;
}

EquipStats EquipTabItem::stats() const {
  const EquipStats stat_sources[] = {
      prototype_.base_stats(), state_.scroll_stats(), StarForceStatGains()};
  return SumEquipStats(stat_sources);
}

int EquipTabItem::MaxStarsForLevel(int required_level) {
  if (required_level >= 138) {
    return 30;
  }
  if (required_level >= 128) {
    return 20;
  }
  if (required_level >= 118) {
    return 15;
  }
  if (required_level >= 108) {
    return 10;
  }
  if (required_level >= 95) {
    return 8;
  }
  return 5;
}

}  // namespace ms
