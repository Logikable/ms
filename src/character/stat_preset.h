/* The setups a character keeps, and which of them the game reads.
 *
 * Two questions, and they are not the same one. A StatPreset is a SLOT: one of
 * the allocations a character keeps of everything the player tunes -- a Hyper
 * Stat allocation and an Inner Ability apiece. It is what the preset row
 * selects and what spending goes into.
 *
 * An Activity is what the character is DOING, which is a different question:
 * the game reads the first slot while farming and the second while bossing,
 * and an activity decides more than the allocation -- which of the two
 * potions is in effect, and whether combat power counts boss damage.
 *
 * Its own header because it belongs to neither system: both read it, and
 * neither should have to depend on the other to say which setup it means.
 */
#ifndef MS_SRC_CHARACTER_STAT_PRESET_H_
#define MS_SRC_CHARACTER_STAT_PRESET_H_

namespace ms {

// One of the setups a character keeps, by position.
enum class StatPreset { kFirst, kSecond, kThird };

inline constexpr int kNumStatPresets = 3;

// What the character is doing, which is what picks a slot.
enum class Activity { kFarming, kBossing };

// A slot as an index into the presets a character holds, and back again. An
// index off the end folds to the first slot rather than reading past it.
inline int IndexOf(StatPreset preset) {
  return static_cast<int>(preset);
}

inline StatPreset StatPresetAt(int index) {
  if (index < 0 || index >= kNumStatPresets) {
    return StatPreset::kFirst;
  }
  return static_cast<StatPreset>(index);
}

// Which of the things a character keeps a preset of is being asked about.
// Each kind holds its own presets and its own choice of which is in use, so a
// character can have Hyper 1, Ability 3 and Gear 2 in play at once.
enum class PresetKind { kHyperStats, kInnerAbility, kEquip };

// The gear preset the boss drop roll reads, whatever the switch says and
// whatever is worn: there is no moment to change into drop gear before the
// drops fall, so the third preset is the one set aside for it.
inline constexpr StatPreset kDropPreset = StatPreset::kThird;

// The slot Autoswap Presets reads for `activity`: farming the first, bossing
// the second. The third is storage no activity names -- for gear, the Drop
// preset above, asked for by name.
inline StatPreset AutoswapSlotFor(Activity activity) {
  return activity == Activity::kBossing ? StatPreset::kSecond
                                        : StatPreset::kFirst;
}

}  // namespace ms

#endif  // MS_SRC_CHARACTER_STAT_PRESET_H_
