/* How much of each boss drop a run used while it still mattered.
 *
 * A drop matters from the first clear of a boss that drops it until the last
 * look the boss preset still wears something it is for: the piece itself, a
 * piece its currency buys, or one its box gives. Farm and drop presets are
 * left out: nobody upgrades those, so they would hold a window open for good.
 * Over that window this counts what dropped and what was used, so drop rates
 * can be set against what a run really needs rather than against the tiers
 * alone.
 */
#ifndef MS_ANALYSIS_DROP_USE_H_
#define MS_ANALYSIS_DROP_USE_H_

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/combat/boss/boss_run.h"
#include "src/game_state.h"
#include "src/protos/item.pb.h"

namespace ms {

// One drop over one run's window.
struct DropUseResult {
  std::string name;
  // A piece of equipment, as against a currency or a box.
  bool equipment = false;
  int required_level = 0;
  double start_day = 0.0;
  double end_day = 0.0;
  int64_t dropped = 0;
  // The day the first copy dropped, or -1 if none did.
  double first_day = -1.0;
  // The most days the window went without a copy: from its opening to the
  // first, between two, or from the last to the window's end.
  double longest_drought = 0.0;
  // For a piece, the most copies worn at once plus the held copies booms used
  // up; for a currency or a box, what left the bag.
  int64_t used = 0;
  // Whether the boss preset ever wore something it is for. One it never wore
  // closes at its first clear, with what that clear paid.
  bool worn = false;
  // The item drop rate the window's clears rolled at, 2.5 for +250%.
  double low_drop = 0.0;
  double high_drop = 0.0;
};

class DropUse {
 public:
  // Reads every built boss's drop table for the equipment, currencies and boxes
  // it pays.
  explicit DropUse(const GameState& state);

  // Counts a won fight's drops, opening the window of every drop that
  // difficulty of `boss` has.
  void Clear(const std::string& boss, const std::string& difficulty,
             const BossReward& reward, double day);

  // Extends the window of every drop the character still has a use for.
  // `held_spares_used` is GearSpend's count, for the whole run so far.
  void Look(const GameState& state,
            const std::map<std::string, int>& held_spares_used, double day);

  // Every drop whose window opened.
  std::vector<DropUseResult> Finish() const;

 private:
  enum class Kind { kPiece, kCurrency, kBox, kPickBox, kRingBox };
  struct Row {
    Kind kind = Kind::kPiece;
    std::string key;  // the catalog key of the item or piece
    // The fights that drop it, as "boss/difficulty".
    std::set<std::string> fights;
    // What a currency buys or a pick box gives, by name.
    std::set<std::string> pieces;
    BoxContents box;
    RingBox ring_box;
    int required_level = 0;
    // This run so far.
    bool open = false;
    double start_day = 0.0;
    int64_t dropped = 0;
    double first_day = -1.0;
    double last_copy_day = 0.0;
    double longest_drought = 0.0;
    double low_drop = 0.0;
    double high_drop = 0.0;
    int most_worn = 0;
    bool seen_in_use = false;
    DropUseResult last_use;
  };

  // How many copies of a piece the boss preset wears, or for the other kinds
  // how many pieces they are for it wears.
  int InUse(const GameState& state, const Row& row) const;
  DropUseResult Snapshot(const GameState& state, const std::string& name,
                         const Row& row,
                         const std::map<std::string, int>& held_spares_used,
                         double day) const;

  std::map<std::string, Row> rows_;
};

}  // namespace ms

#endif  // MS_ANALYSIS_DROP_USE_H_
