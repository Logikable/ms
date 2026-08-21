/* What a star force run costs before anyone attempts it.
 *
 * Every attempt is paid for whether it lands, fails or destroys the item, so
 * the price of a star is not what the star costs -- it is that price over the
 * chance of getting it, plus every attempt spent getting back what a boom
 * took. From 15 stars up the fight is against destruction as much as failure,
 * and a trace recovery hands the item back several stars down, which is a
 * loop: the same star can be paid for many times over.
 *
 * That makes the answer a linear system rather than a sum, which is what this
 * solves. Both the prices and the odds are the game's own -- StarForceCost
 * and EquipInstance::RateAt -- so this cannot drift from what a player is
 * actually charged.
 */
#ifndef MS_ANALYSIS_STAR_FORCE_CURVE_H_
#define MS_ANALYSIS_STAR_FORCE_CURVE_H_

namespace ms {

// What one run comes to, on average over many players doing the same thing.
struct StarForceRun {
  double meso = 0.0;      // every attempt priced, the failures included
  double attempts = 0.0;  // clicks, whatever each one did
  double booms = 0.0;     // times the item is destroyed and recovered
};

// The expected cost of taking one item of `required_level` from `from` stars
// to `to`, recovering the item every time it booms and carrying on from
// whatever star the trace hands back.
//
// `booms` is left as a count rather than priced: what a replacement copy costs
// depends on where the item came from, and a drop-only item has no price at
// all. Multiply it by whatever one copy is worth.
//
// Zero for a run that goes nowhere or past the game's last star. `to` above
// what the item's level can hold is the caller's to check.
StarForceRun StarForceRunTo(int required_level, int from, int to);

}  // namespace ms

#endif  // MS_ANALYSIS_STAR_FORCE_CURVE_H_
