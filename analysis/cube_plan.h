/* Values a cube on one worn piece the way the shopper values a scroll or a
 * star: expected combat power for the meso it costs. GearShopper ranks it
 * against everything else, so there is no fixed order of pieces.
 *
 * A run of cubes is an optimal stopping problem. Every roll costs its price in
 * power, at the rate the rest of the shelf pays (CubeIncome::power_per_meso),
 * and rolling goes on while the piece is worth less than a reservation value:
 * the r with E[max(X, r)] - price = r, where X is a roll's gain. Rank-ups tie
 * the ranks together, so r is solved from Legendary down.
 *
 * Red and Green replace the lines and Black and White keep the better, yet
 * both play the same rule: below r a player rolls on whatever the last roll
 * left, and above it neither is worth a roll. What sets them apart is odds and
 * price, which is all the shelf compares. It undervalues goals that need two
 * specific lines, such as the -3s hat; both are meant to lose.
 *
 * A %meso or %drop line earns income rather than power, so it is valued over
 * the rest of the run instead (see CubeIncome).
 */
#ifndef MS_ANALYSIS_CUBE_PLAN_H_
#define MS_ANALYSIS_CUBE_PLAN_H_

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>

#include "analysis/sim_gear.h"
#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/game_state.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Draws used to value one cube. Enough that a good line isn't missed through a
// slot's worth of bad luck, few enough to keep a look cheap.
inline constexpr int kCubeSamples = 64;

// Weight given to an item the shopper may replace later. The user's call:
// cubing gear you'll outgrow is still worth something, since it carries you to
// the replacement, but less than cubing a piece you'll keep.
inline constexpr int kReplaceableNumerator = 1;
inline constexpr int kReplaceableDenominator = 4;

// Time left in the run and the current income, which together decide what a
// %meso or %drop line is worth.
struct CubeIncome {
  double seconds_left = 0.0;
  // Meso per second the character would earn at `meso_bonus` (MesoBonus's
  // result, both caps applied) and `drop_pct`. Empty for a caller with no
  // encounter, which values income lines at zero.
  std::function<double(double meso_bonus, double drop_pct)> rate;
  // Combat power a meso buys elsewhere on the shelf: what a roll costs in
  // power, so it sets where a run stops. Zero rolls until no roll could do
  // better.
  double power_per_meso = 0.0;
};

// The character as they are now, which every cube is valued against. Computing
// it needs a rebuild, so the shopper does it once per round.
struct CubeBasis {
  // Read while bossing, off the boss gear: what power is judged on.
  DerivedStats derived;
  // Read while farming, off the farm gear: what income is judged on.
  DerivedStats farm;
  // Stats from everything worn plus everything granted, before percentages are
  // applied: the sum TotalEquipStats folds, not its result. A potential can
  // change %ATT, so the fold is redone per candidate.
  EquipStats raw;
  // The passives WorthOf takes, before the potentials move them.
  PassiveOffense passives;
  // The potential totals `derived` was read with. Held rather than read off the
  // character, so a run of cubes can still be valued against where it began.
  PotentialTotals worn;
  // The fight lines are judged against. An ignored-defence line's value depends
  // on the fight, not the character, and changes threefold between Cygnus and
  // Lotus, so the monster itself is carried. See //analysis:yardstick.
  Yardstick yard;
};

CubeBasis CubeBasisFor(const GameState& state, const Yardstick& yard);

// A run of cubes on one slot under the stopping rule, and what it's expected
// to leave. Priced as a run because a character below a boss's defence wall
// gains nothing from any one roll but has a real chance over dozens.
struct CubeProgram {
  double cubes = 0.0;  // expected cubes before the rule stops
  double gain = 0.0;   // expected gain of the lines it stops on
  int64_t cost = 0;    // expected meso, cubes times the price
  // The reservation value at each rank, indexed by PotentialRank, in gain over
  // the piece's current lines and already scaled by `share`.
  std::array<double, PotentialRank_ARRAYSIZE> reserve{};
  // What a gain is worth on this piece: less on one the shopper may replace.
  double share = 1.0;

  bool worth() const {
    return cubes > 0.0 && gain > 0.0 && cost > 0;
  }
};

// The run of `cube` on what `gear` wears in `slot`; empty when the piece is
// already worth its reservation value, so no roll pays.
CubeProgram BestCubeProgram(const GameState& state, const CubeBasis& basis,
                            StatPreset gear, EquipSlot slot, CubeType cube,
                            const CubeIncome& income, std::mt19937& rng);

// A run of cubes on one piece, followed through under its program's rule:
// rolled while what the piece holds is worth less than its rank's reservation
// value. Every roll is valued against the lines the run began on, which is the
// frame the program's reservation values are in, so the run is priced once
// rather than once a cube.
class CubeRun {
 public:
  // Reads the piece as it is now. `program` must have been priced on it.
  CubeRun(const GameState& state, const Yardstick& yard, StatPreset gear,
          EquipSlot slot, CubeType cube, const CubeIncome& income,
          const CubeProgram& program);
  ~CubeRun();

  // Whether the rule rolls again on what the piece holds now.
  bool Continues(const GameState& state) const;
  // Whether a choosing cube's `rolled` should replace what the piece holds: the
  // better of the two states, each worth its lines or its rank's reservation
  // value, whichever is more. A higher rank is taken on its future even when
  // its lines are worse.
  bool Takes(const GameState& state, const Potential& rolled) const;

 private:
  struct Priced;
  // Gain of `potential` over the lines the run began on.
  double GainOf(const GameState& state, const Potential& potential) const;

  StatPreset gear_;
  EquipSlot slot_;
  PotentialTrack track_;
  CubeIncome income_;
  CubeProgram program_;
  CubeBasis basis_;
  std::unique_ptr<Priced> priced_;
};

// Whether the shopper is likely to replace what boss fights wear in `slot`: a
// higher-level piece the character can wear and afford. A weapon must match the
// type in hand. A tier priced in tokens counts only once a token is in the bag.
// The gain is discounted rather than refused, since cubing still helps the
// climb.
bool Replaceable(const GameState& state, EquipSlot slot);

}  // namespace ms

#endif  // MS_ANALYSIS_CUBE_PLAN_H_
