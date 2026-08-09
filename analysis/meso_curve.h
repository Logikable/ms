/* The meso a player has earned on reaching each level, 1 to the top of the
 * EXP table.
 *
 * The engine can only answer this up to the level cap, because that is where
 * the maps stop. This is the closed form that carries it the rest of the way:
 * the game's own EXP table and loot formulas, paired with GMS's real mob EXP
 * per level. Shared by meso_curve_sim, which prints it, and scroll_cost_sim,
 * which prices against it.
 */
#ifndef MS_ANALYSIS_MESO_CURVE_H_
#define MS_ANALYSIS_MESO_CURVE_H_

#include <vector>

namespace ms {

// What one kill of a level-appropriate mob pays. The player is assumed to
// train at their own level, which the map ladder keeps them near and which
// leaves them inside MesoLevelPenalty's free +/-10 band the whole way.
struct KillValue {
  double exp = 0.0;
  double meso = 0.0;
  double etc = 0.0;
};

// Where the drops run out, and what an Etc drop is worth. The defaults are the
// shipped game: every mob in data/mobs drops one Etc at 0.4, every item in
// data/items/etc sells for twice its mob's level, and GMS's Arcane River drops
// nothing from 200.
struct MesoCurveParams {
  int etc_stops_at = 200;
  int meso_stops_at = 100000;
  double etc_per_kill = 0.4;
  double etc_price_per_level = 2.0;
};

KillValue KillValueAt(int level, const MesoCurveParams& params);

// Cumulative meso earned on reaching each level, indexed by level. Index 0 and
// 1 are zero -- a level 1 character has earned nothing. Two curves rather than
// one, because the question is always what the Etc sales add on top of the
// meso drops rather than instead of them.
struct MesoCurve {
  std::vector<double> meso;
  std::vector<double> etc;

  // Meso plus Etc sales, the total a player who sells everything holds.
  double Total(int level) const;
  // What was earned between two levels: the money that actually pays for an
  // upgrade bought at `to`.
  double Earned(int from, int to) const;
};

MesoCurve BuildMesoCurve(const MesoCurveParams& params);

}  // namespace ms

#endif  // MS_ANALYSIS_MESO_CURVE_H_
