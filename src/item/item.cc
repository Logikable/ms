#include "src/item/item.h"

#include "src/item/equip_stats.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

constexpr int kUseDefaultMaxStack = 9999;
constexpr int kEtcDefaultMaxStack = 200;

// Per-star primary stat deltas for 1-15★ (index i = gain for i★→(i+1)★).
constexpr int kPrimaryStatDeltas[15] = {
    2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};

// Per-star Max HP over the same range, for the items GMS raises it on, and
// per-star Max MP for a weapon, which climbs by the same amounts.
constexpr int kMaxHpDeltas[15] = {
    5, 5, 5, 10, 10, 15, 15, 20, 20, 25, 25, 25, 25, 25, 25,
};

// The share of what the item already carries that one star adds to a scaled
// stat. Attack climbs by a fiftieth, defense by a twentieth.
constexpr int kAttackPercent = 2;
constexpr int kDefensePercent = 5;

// Stat and attack gained on reaching a given star (16-30★), by the item's
// required level. Index i = the gain for (i+15)★→(i+16)★.
//
// A weapon and everything else read different attack columns: at 20★ a Lv150
// weapon takes +11 and a Lv150 hat +13. The stat column is shared, and runs
// out at 23★ -- past there only attack climbs.
//
// GMS's own table stops at 25★. The rows past it continue the step the last
// rows take: +1 an attempt on a weapon, +2 on everything else.
struct HighStarEntry {
  int stat;
  int weapon_att;
  int other_att;
};

// Max stars for this range is 20★; entries 5-14 are padding.
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
// The weapon column is zero from 23★ on: no star-forceable Lv250+ weapon
// exists in GMS.
// TODO: Superior equipment has a different stat table and a 15★ cap; handle
// separately when Superior items are added.
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

// Which of the four primary stats a star raises. Two rules pick them: below
// 16★ it is the stats the item's job needs, above it the stats the item
// already shows.
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
// this game splits top and bottom and always will. No off hand either: GMS's
// list names the shield, and ours holds a medallion or a book.
bool RaisesMaxHp(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
    case EQUIP_SLOT_HAT:
    case EQUIP_SLOT_TOP:
    case EQUIP_SLOT_BOTTOM:
    case EQUIP_SLOT_CAPE:
    case EQUIP_SLOT_RING:
    case EQUIP_SLOT_PENDANT:
    case EQUIP_SLOT_BELT:
    case EQUIP_SLOT_SHOULDER:
      return true;
    case EQUIP_SLOT_UNSPECIFIED:
    case EQUIP_SLOT_PROJECTILE:
    case EQUIP_SLOT_SECONDARY:
    case EQUIP_SLOT_FACE_ACCESSORY:
    case EQUIP_SLOT_EYE_ACCESSORY:
      return false;
  }
  return false;
}

// The stats an item shows before any star, which is what GMS calls visible.
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

// One attempt's worth of a scaled stat: GMS's 1 + RoundDown[stat x share],
// taken from the drop's own stat plus what the stars have added so far, which
// is what makes the gains compound. A stat the item does not show gains
// nothing, however many stars go into it.
int ScaledGain(int shown, int gained, int percent) {
  if (shown <= 0) {
    return 0;
  }
  return 1 + (shown + gained) * percent / 100;
}

// What one star is worked out against: the item's own facts, and the gains so
// far, which the scaled stats take their next share from.
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

// One attempt in the 1-15★ range. The job's own stats climb by a flat amount;
// attack and defense climb by a share of what the item already carries.
void AddLowStar(int index, StarForceRun* run) {
  EquipStats& gains = run->gains;
  AddToStats(&gains, run->job_stats, kPrimaryStatDeltas[index]);
  if (RaisesMaxHp(run->slot)) {
    gains.set_max_hp(gains.max_hp() + kMaxHpDeltas[index]);
  }
  if (!run->is_weapon) {
    gains.set_def(gains.def() +
                  ScaledGain(run->shown_def, gains.def(), kDefensePercent));
    return;
  }
  // The weapon is the only thing whose stars raise Max MP, and the only thing
  // whose attack climbs at all this far down.
  gains.set_max_mp(gains.max_mp() + kMaxHpDeltas[index]);
  gains.set_attack(gains.attack() +
                   ScaledGain(run->shown_att, gains.attack(), kAttackPercent));
  gains.set_magic_attack(gains.magic_attack() + ScaledGain(run->shown_matt,
                                                           gains.magic_attack(),
                                                           kAttackPercent));
}

// One attempt at 16★ and up, where the table is by equipment level. Every stat
// the item shows climbs, the job no longer deciding which, and armour starts
// gaining flat attack of its own.
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
  // Armour gains this whether or not it shows any attack to begin with.
  gains.set_attack(gains.attack() + entry.other_att);
  gains.set_magic_attack(gains.magic_attack() + entry.other_att);
  // Only the Lv250 band keeps climbing in defense past 15★.
  if (run->required_level >= 250) {
    gains.set_def(gains.def() +
                  ScaledGain(run->shown_def, gains.def(), kDefensePercent));
  }
}

// One pass over the catalog. Small enough that an index would cost more to
// build than the walk it saves, and every caller is on a keypress.
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

bool Supports(const EquipPrototype& proto, Upgrade upgrade) {
  for (int i = 0; i < proto.unsupported_upgrades_size(); ++i) {
    if (proto.unsupported_upgrades(i) == upgrade) {
      return false;
    }
  }
  return true;
}

void StackableItem::add_count(int delta) {
  count_ += delta;
}

int StackableItem::max_stack() const {
  if (prototype_.max_stack() > 0) {
    return prototype_.max_stack();
  }
  switch (prototype_.category()) {
    case ITEM_CATEGORY_USE:
      return kUseDefaultMaxStack;
    case ITEM_CATEGORY_ETC:
      return kEtcDefaultMaxStack;
    default:
      return 1;
  }
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

EquipTrace::EquipTrace(EquipPrototype prototype, Equip state)
    : EquipTabItem(std::move(prototype), std::move(state)),
      display_name_(prototype_.name() + " Trace") {
}

EquipStats EquipTabItem::StarForceStatGains(int stars) const {
  if (stars < 0) {
    stars = state_.stars();
  }
  // What the item shows before any star: the drop's own stats and whatever
  // scrolls passed. The star force gains are deliberately not in it -- each
  // star's share is taken from these plus the gains so far, which the run
  // carries as it goes.
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
