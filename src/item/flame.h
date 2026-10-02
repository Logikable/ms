/* Flames: GMS's Bonus Stats, four rolled lines an item carries on top of its
 * own stats, and the Rebirth Flames that reroll them.
 *
 * Every item is treated as flame advantaged (GMS's boss equipment): four
 * distinct lines, each with its own tier. Like a potential line, a flame line
 * stores its tier and the value is computed from the tier and the item.
 *
 * Departures from GMS, all deliberate: the pool drops Max MP, Req Level, DEF,
 * Speed and Jump, and every tier a flame can roll is equally likely (GMS's
 * Black flame rolls its top tier 1% of the time).
 *
 * Pure math over the protos, like potential.h. The caller charges for it.
 */
#ifndef MS_SRC_ITEM_FLAME_H_
#define MS_SRC_ITEM_FLAME_H_

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "src/protos/equip.pb.h"

namespace ms {

using FlameLines = google::protobuf::RepeatedPtrField<FlameLine>;

// The level both flames unlock at, account-wide like cubing.
inline constexpr int kFlameUnlockLevel = 240;

// Lines on every flamed item.
inline constexpr int kFlameLines = 4;

enum class FlameType {
  kBurning,
  kBlack,
};

// One flame on the shelf. Each rolls `tiers` tiers from `min_tier` up, all
// equally likely.
struct Flame {
  FlameType type;
  int64_t cost;
  int min_tier;
  // Whether the player chooses between the old lines and the roll, as with a
  // Black Cube, instead of the roll replacing them.
  bool choose;
};

inline constexpr int kFlameTiers = 4;

inline constexpr Flame kFlames[] = {
    {FlameType::kBurning, 3'000'000, 3, false},
    {FlameType::kBlack, 10'000'000, 4, true},
};

const Flame& FlameOf(FlameType type);

// Whether an item worn in `slot` takes flames at all. GMS refuses rings,
// shoulders, emblems, badges, medals, secondaries, hearts, totems, symbols and
// projectiles.
bool SlotTakesFlame(EquipSlot slot);

// The lines `proto` can roll, all equally likely. Boss Damage needs a weapon of
// level 90; off a weapon, ATT and MATT need level 60 and All Stats 70.
std::vector<FlameStat> FlamePool(const EquipPrototype& proto);

// What `line` grants on `proto`: points for the stats, ATT, MATT and Max HP,
// whole percents for All Stats, Boss Damage and Damage. A weapon's ATT and
// MATT are a share of its own base value, rounded up.
int FlameLineValue(const FlameLine& line, const EquipPrototype& proto);

// The flat part of `lines` on `proto`, shaped like the item's own stats: the
// stats, Max HP, ATT, MATT and Boss Damage. All Stats and Damage are percents
// of the wearer's totals and aren't here; see FlamePercents.
EquipStats FlameStats(const FlameLines& lines, const EquipPrototype& proto);

struct FlamePercents {
  int all_stat = 0;
  int damage = 0;
};
FlamePercents FlamePercentsOf(const FlameLines& lines,
                              const EquipPrototype& proto);

// A fresh set of kFlameLines distinct lines for `proto`. A roll identical to
// `current` is drawn again, as in GMS, so a reroll always changes something.
FlameLines RollFlame(FlameType flame, const EquipPrototype& proto,
                     const FlameLines& current, std::mt19937& rng);

// Display names: "Black Rebirth Flame", and "STR & DEX" for a line.
std::string FlameName(FlameType type);
std::string FlameStatName(FlameStat stat);
// "+42" or "+6%".
std::string FlameLineValueText(const FlameLine& line,
                               const EquipPrototype& proto);

}  // namespace ms

#endif  // MS_SRC_ITEM_FLAME_H_
