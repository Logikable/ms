/* GMS's real per-level mob EXP, for the levels the game has no mobs for.
 *
 * The map ladder stops at level 60, so nothing in //data can say what a level
 * 150 kill is worth. meso_curve_sim needs that to project the economy past
 * the cap. Regenerate with tools/fetch_gms_mob_exp.py.
 */
#ifndef MS_ANALYSIS_GMS_MOB_EXP_H_
#define MS_ANALYSIS_GMS_MOB_EXP_H_

namespace ms {

// EXP one kill yields a player training level-appropriately at `level`. Reads
// the upper quartile of that level's field mobs -- a player picks the best map
// open to them, not the average one. Returns 0 outside 1..299.
double GmsMobExpPerKill(int level);

}  // namespace ms

#endif  // MS_ANALYSIS_GMS_MOB_EXP_H_
