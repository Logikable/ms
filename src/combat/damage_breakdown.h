/* damage_breakdown.h adds up one player's damage over a boss fight, by skill:
 * the table the fight ends on.
 *
 * A row is a skill as the player knows it. A Final Attack and a poison lent by
 * another skill are rows of their own; a boost, a form and a skill's own burn
 * are inside the skill they belong to. The fight decides which is which when
 * it files the line -- see DamageLine::credit.
 */
#ifndef MS_SRC_COMBAT_DAMAGE_BREAKDOWN_H_
#define MS_SRC_COMBAT_DAMAGE_BREAKDOWN_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ms {

// One skill's share of a fight. Uncapped: a line that overkilled counts at
// what it rolled.
struct BreakdownRow {
  std::string skill;
  double damage = 0.0;
  // Landings, not presses: a hold is a cast per pulse, a burn one per tick.
  int64_t casts = 0;
  // Numbers landed, one per monster each line reached.
  int64_t lines = 0;

  // The mean line, 0 for a row with none.
  double per_line() const;
};

// Everything `rows` add up to.
double TotalDamage(const std::vector<BreakdownRow>& rows);
// What `row` is of `total`, as a fraction; 0 against a total of nothing.
double DamageShare(const BreakdownRow& row, double total);

class DamageBreakdown {
 public:
  // Files one line. `cast` is the fight's number for the landing it belongs
  // to, never reused, so a cast is counted the first time its number is seen.
  void AddLine(const std::string& skill, double damage, int cast);
  // Seconds the fight ran: the ones cooldowns and buffs tick through.
  void AddSeconds(double seconds) {
    seconds_ += seconds;
  }

  double seconds() const {
    return seconds_;
  }
  double total() const {
    return total_;
  }
  // Heaviest first; a tie goes by name, so the order never flickers.
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
