/* Arcane Force: the Arcane River stat, and how meeting a map's requirement
 * affects a fight.
 *
 * Every river map requires some Arcane Force, and how much of it the character
 * has scales both the damage they deal and the damage they take: a tenth of
 * their damage and 2.8x the monster's at the bottom, 1.5x theirs and none of
 * the monster's at the top. Arcane Symbols provide it; see symbol.h.
 */
#ifndef MS_SRC_CHARACTER_ARCANE_FORCE_H_
#define MS_SRC_CHARACTER_ARCANE_FORCE_H_

namespace ms {

// How meeting a map's force requirement (Arcane Force here, Sacred Power in
// sacred_power.h) affects the fight, as two multipliers. Both are 1 on maps
// with no requirement.
struct ForceFactors {
  double damage_dealt = 1.0;
  double damage_taken = 1.0;
};

// The factors for `owned` Arcane Force on a map requiring `required`. Follows
// GMS's table, stepped by the whole percent met, rounded down: 0.10 dealt and
// 2.8x taken with none met, 1.50 and 0x at 150%.
ForceFactors ArcaneFactorsFor(int owned, int required);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_ARCANE_FORCE_H_
