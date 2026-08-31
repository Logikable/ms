/* Which of a character's two setups is in play.
 *
 * A character keeps two of everything the player tunes for an activity -- one
 * Hyper Stat allocation and one Inner Ability apiece -- and the game picks
 * between them by what is being done rather than by a switch the player
 * throws. Farming is what everything reads unless it is told otherwise; a boss
 * fight asks for the other.
 *
 * Its own header because it belongs to neither system: both read it, and
 * neither should have to depend on the other to say which setup it means.
 */
#ifndef MS_SRC_CHARACTER_STAT_PRESET_H_
#define MS_SRC_CHARACTER_STAT_PRESET_H_

namespace ms {

enum class StatPreset { kFarming, kBossing };

}  // namespace ms

#endif  // MS_SRC_CHARACTER_STAT_PRESET_H_
