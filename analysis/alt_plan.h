/* Alts leveled for their link skills, as a main's purchase.
 *
 * An alt costs the main the hours spent playing it: offline time pays only the
 * character left offline, so every hour on an alt is an hour the main doesn't
 * farm. The meso the alt makes on the way goes to the main.
 *
 * Alt hours come from real climbs, one per line, burning against a roster whose
 * best character is at the last rung (see LevelAfterBurning), so they are
 * measured once per sweep and shared by every main.
 */
#ifndef MS_ANALYSIS_ALT_PLAN_H_
#define MS_ANALYSIS_ALT_PLAN_H_

#include <cstdint>
#include <map>
#include <vector>

#include "src/character/link.h"
#include "src/protos/character.pb.h"

namespace ms {

// What an alt of one line has played and kept on reaching each rung in
// kLinkRungLevels. -1 seconds for a rung its climb never reached.
struct AltLadder {
  double seconds[kLinkRungsPerLine] = {-1.0, -1.0, -1.0};
  int64_t meso[kLinkRungsPerLine] = {0, 0, 0};
};

// One ladder per line, keyed by the line's 2nd job (see LineOf).
using AltLadders = std::map<Job, AltLadder>;

// The level each line's alt stands at, keyed like AltLadders.
using AltLevels = std::map<Job, int>;

// Taking one line's alt up a rung: the level it stops at, and what the climb
// from where it stands costs in hours and pays in meso.
struct AltStep {
  Job line = JOB_UNSPECIFIED;
  int level = 0;
  double seconds = 0.0;
  int64_t meso = 0;
};

// The cheapest next rung for each link skill the main could raise, one step per
// skill. Lines of one branch raise the same skill, so the others are left out.
// The main's own line is skipped: its best character already counts.
std::vector<AltStep> AltSteps(const AltLadders& ladders, const AltLevels& alts,
                              Job main_job);

// The link tally the alts give. The main counts themselves; see LinkTally.
LinkTally AltTally(const AltLevels& alts);

}  // namespace ms

#endif  // MS_ANALYSIS_ALT_PLAN_H_
