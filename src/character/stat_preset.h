/* The setups a character keeps, and which one the game uses.
 *
 * These are two separate questions. A StatPreset is a slot: one of the setups a
 * character keeps for each thing the player tunes (Hyper Stats, Inner Ability,
 * gear, link skills). It is what the preset row selects and what points are
 * spent into.
 *
 * An Activity is what the character is doing: the game uses the first slot
 * while farming and the second while bossing. The activity decides more than
 * the setup, such as which potions are active and whether combat power counts
 * boss damage.
 *
 * This has its own header because it belongs to no single system: all of them
 * read it, and none should depend on another to name a setup.
 */
#ifndef MS_SRC_CHARACTER_STAT_PRESET_H_
#define MS_SRC_CHARACTER_STAT_PRESET_H_

namespace ms {

// One of the setups a character keeps, by position.
enum class StatPreset { kFirst, kSecond, kThird };

inline constexpr int kNumStatPresets = 3;

// What the character is doing, which picks a slot.
enum class Activity { kFarming, kBossing };

// Converts a slot to an index into the character's presets and back. An index
// past the end becomes the first slot instead of reading past the end.
inline int IndexOf(StatPreset preset) {
  return static_cast<int>(preset);
}

inline StatPreset StatPresetAt(int index) {
  if (index < 0 || index >= kNumStatPresets) {
    return StatPreset::kFirst;
  }
  return static_cast<StatPreset>(index);
}

// Which kind of preset is meant. Each kind has its own presets and its own
// active choice, so a character can use Hyper 1, Ability 3 and Gear 2 at once.
enum class PresetKind { kHyperStats, kInnerAbility, kEquip, kLinkSkills };

// The gear preset used for the boss drop roll, regardless of the switch or what
// is worn. There's no chance to change into drop gear before the drops fall, so
// the third preset is set aside for it.
inline constexpr StatPreset kDropPreset = StatPreset::kThird;

// The slot Autoswap Presets uses for `activity`: the first for farming, the
// second for bossing. No activity uses the third; for gear, it is the Drop
// preset above, requested by name.
inline StatPreset AutoswapSlotFor(Activity activity) {
  return activity == Activity::kBossing ? StatPreset::kSecond
                                        : StatPreset::kFirst;
}

}  // namespace ms

#endif  // MS_SRC_CHARACTER_STAT_PRESET_H_
