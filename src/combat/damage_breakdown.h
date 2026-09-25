/* Totals one player's damage over a boss fight by skill, for the table shown
 * when the fight ends.
 *
 * Each row is a skill as the player knows it. Final Attacks and poisons applied
 * by another skill get their own rows; boosts, forms and a skill's own burn are
 * counted under the skill they belong to. The fight decides this when it
 * records each line (see DamageLine::credit).
 */
#ifndef MS_SRC_COMBAT_DAMAGE_BREAKDOWN_H_
#define MS_SRC_COMBAT_DAMAGE_BREAKDOWN_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ms {

// One skill's share of a fight. Not capped at the monster's HP: an overkill
// counts in full.
struct BreakdownRow {
  std::string skill;
  double damage = 0.0;
  // Number of times the skill landed, not key presses: a held skill counts once
  // per pulse, a burn once per tick.
  int64_t casts = 0;
  // Damage numbers landed, one per monster per line.
  int64_t lines = 0;

  // Average damage per line, or 0 if there are none.
  double per_line() const;
};

// One player's rows in the party's end-of-fight table.
struct PlayerBreakdown {
  std::string account_id;
  // Empty for the local player.
  std::string name;
  std::vector<BreakdownRow> rows;
};

// Sum of `rows`' damage.
double TotalDamage(const std::vector<BreakdownRow>& rows);
// `row`'s fraction of `total`, or 0 if `total` is 0.
double DamageShare(const BreakdownRow& row, double total);
// Sorts largest first. Ties keep their existing (name) order.
void SortHeaviestFirst(std::vector<BreakdownRow>& rows);
// The party's combined table: one row per skill name across all players,
// largest first.
std::vector<BreakdownRow> MergeBreakdowns(
    const std::vector<PlayerBreakdown>& players);

class DamageBreakdown {
 public:
  // Records one line. `cast` is the fight's unique ID for the landing it
  // belongs to, so each cast is counted the first time its ID appears.
  void AddLine(const std::string& skill, double damage, int cast);
  // Adds fight time, in the same seconds that cooldowns and buffs use.
  void AddSeconds(double seconds) {
    seconds_ += seconds;
  }

  double seconds() const {
    return seconds_;
  }
  double total() const {
    return total_;
  }
  // Largest first; ties sort by name so the order stays stable.
  std::vector<BreakdownRow> Rows() const;

 private:
  struct Tally {
    BreakdownRow row;
    int highest_cast = 0;
  };
  std::map<std::string, Tally> by_skill_;
  double total_ = 0.0;
  double seconds_ = 0.0;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_BREAKDOWN_H_
