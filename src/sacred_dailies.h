/* What each Sacred Symbol daily pays: the share of its copies the character
 * would earn farming that area, so a character who can't farm Carcion doesn't
 * collect its daily for free.
 *
 * Each area is measured by fighting its map with the map refilling the moment
 * it empties (ClearTimes) and comparing a clear with the three respawn
 * intervals: as fast as the Frenzy Totem's pays the whole day, the Wild
 * Totem's 80%, the bare map's half, linear between, and slower nothing. The
 * areas are measured in order, and the first that pays nothing ends the run,
 * since every later area is harder.
 */
#ifndef MS_SRC_SACRED_DAILIES_H_
#define MS_SRC_SACRED_DAILIES_H_

#include <chrono>
#include <future>
#include <map>
#include <string>
#include <vector>

#include "src/game_state.h"
#include "src/protos/equip.pb.h"

namespace ms {

// One run of the longest skill cooldown, two minutes, so the window sees the
// character's bursts come and go. Stretched by game speed.
inline constexpr double kClearWindowGameSeconds = 120.0;

// Which clear stands for the area: the median, or the 90th percentile slowest.
enum class ClearStat {
  kMedian,
  kNinetieth,
};
inline constexpr ClearStat kSacredClearStat = ClearStat::kNinetieth;

// How often the measurement may run again; a sooner request reuses the last.
inline constexpr std::chrono::minutes kSacredRemeasure(5);

// One area's measurement.
struct SacredArea {
  const EquipPrototype* symbol = nullptr;
  std::string map;             // the catalog key it was measured on
  std::vector<double> clears;  // stretched seconds, in the order they ran
  double clear = 0.0;          // the one ClearStat picks
  double share = 0.0;          // of the day's copies, 0 to 1
};

// The map an area is farmed on: the one asking the least Sacred Power whose
// monsters drop `symbol_key`'s symbol. Empty if none does.
std::string SacredSymbolMap(const GameState& state,
                            const std::string& symbol_key);

// The share of a day's copies that a clear of `clear_seconds` earns at
// `speed_factor`'s game speed.
double SacredShare(double clear_seconds, double speed_factor);

// Every Sacred Symbol's area in order, up to and including the first that pays
// nothing. Measured at any advancement; the claim is what asks for the 6th.
std::vector<SacredArea> MeasureSacredAreas(const GameState& state,
                                           ClearStat stat = kSacredClearStat);

// The shares of `areas` by symbol name, the form the claim takes.
std::map<std::string, double> SacredShares(
    const std::vector<SacredArea>& areas);

// Measures a copy of the game on one background thread, so the result is ready
// by the time the player reaches the claim.
class SacredDailiesWorker {
 public:
  SacredDailiesWorker() = default;
  SacredDailiesWorker(const SacredDailiesWorker&) = delete;
  SacredDailiesWorker& operator=(const SacredDailiesWorker&) = delete;
  ~SacredDailiesWorker();

  // Starts measuring `state`'s character unless a run is going, or the last
  // one for this character started within kSacredRemeasure of `now`.
  void Start(const GameState& state, std::chrono::steady_clock::time_point now);

  // The shares for `state`'s character: the run under way when there is one,
  // else the last, else measured here. None below the 6th advancement.
  std::map<std::string, double> Shares(
      const GameState& state, std::chrono::steady_clock::time_point now);

 private:
  // Who a result is for, since switching characters makes it someone else's.
  static std::string WhoIs(const GameState& state);
  void Collect();

  std::future<std::map<std::string, double>> pending_;
  std::string pending_for_;
  std::map<std::string, double> last_;
  std::string last_for_;
  std::chrono::steady_clock::time_point started_;
};

}  // namespace ms

#endif  // MS_SRC_SACRED_DAILIES_H_
