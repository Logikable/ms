/* Values a run of Rebirth Flames on one worn piece the way cube_plan values a
 * run of cubes: expected damage for the meso it costs, under the same optimal
 * stopping rule. A flame has no rank to climb, so the rule is one reservation
 * value r with E[max(X, r)] - price = r, X being a roll's gain.
 *
 * Burning replaces the lines and Black keeps the better; both roll on while
 * the piece is worth less than r. Only boss fights are judged: no flame line
 * earns income, and farming wears the boss pieces it shares.
 */
#ifndef MS_ANALYSIS_FLAME_PLAN_H_
#define MS_ANALYSIS_FLAME_PLAN_H_

#include <cstdint>
#include <memory>
#include <random>

#include "analysis/cube_plan.h"
#include "analysis/yardstick.h"
#include "src/game_state.h"
#include "src/item/flame.h"
#include "src/protos/equip.pb.h"

namespace ms {

// A run of one flame on one piece under the stopping rule, and what it's
// expected to leave.
struct FlameProgram {
  double flames = 0.0;  // expected flames before the rule stops
  double gain = 0.0;    // expected gain of the lines it stops on
  int64_t cost = 0;     // expected meso, flames times the price
  // The reservation value, in gain over the piece's current lines, already
  // scaled by `share`.
  double reserve = 0.0;
  // What a gain is worth on this piece: less on one the shopper may replace.
  double share = 1.0;

  bool worth() const {
    return flames > 0.0 && gain > 0.0 && cost > 0;
  }
};

// The run of `flame` on what boss fights wear in `slot`, with each roll costing
// `power_per_meso` times its price; empty when no roll pays.
FlameProgram BestFlameProgram(const GameState& state, const CubeBasis& basis,
                              EquipSlot slot, FlameType flame,
                              double power_per_meso, std::mt19937& rng);

// A run of flames on one piece, followed through under its program's rule.
// Every roll is valued against the lines the run began on.
class FlameRun {
 public:
  FlameRun(const GameState& state, const Yardstick& yard, EquipSlot slot,
           const FlameProgram& program);
  ~FlameRun();

  // Whether the rule flames again on what the piece holds now.
  bool Continues(const GameState& state) const;
  // Whether a Black flame's `rolled` beats what the piece holds.
  bool Takes(const GameState& state, const FlameLines& rolled) const;

 private:
  struct Priced;
  double GainOf(const FlameLines& lines) const;

  EquipSlot slot_;
  FlameProgram program_;
  std::unique_ptr<Priced> priced_;
};

}  // namespace ms

#endif  // MS_ANALYSIS_FLAME_PLAN_H_
