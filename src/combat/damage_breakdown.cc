#include "src/combat/damage_breakdown.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace ms {

double BreakdownRow::per_line() const {
  return lines > 0 ? damage / static_cast<double>(lines) : 0.0;
}

void SortHeaviestFirst(std::vector<BreakdownRow>& rows) {
  std::stable_sort(rows.begin(), rows.end(),
                   [](const BreakdownRow& a, const BreakdownRow& b) {
                     return a.damage > b.damage;
                   });
}

double TotalDamage(const std::vector<BreakdownRow>& rows) {
  double total = 0.0;
  for (const BreakdownRow& row : rows) {
    total += row.damage;
  }
  return total;
}

double DamageShare(const BreakdownRow& row, double total) {
  return total > 0.0 ? row.damage / total : 0.0;
}

void DamageBreakdown::AddLine(const std::string& skill, double damage,
                              int cast) {
  Tally& tally = by_skill_[skill];
  tally.row.skill = skill;
  tally.row.damage += damage;
  ++tally.row.lines;
  // A cast's lines on a second monster come after a later cast's on the
  // first -- a hold files every pulse on one before the next -- so only a
  // number past every one seen is new.
  if (cast > tally.highest_cast) {
    tally.highest_cast = cast;
    ++tally.row.casts;
  }
  total_ += damage;
}

std::vector<BreakdownRow> DamageBreakdown::Rows() const {
  std::vector<BreakdownRow> rows;
  for (const std::pair<const std::string, Tally>& entry : by_skill_) {
    rows.push_back(entry.second.row);
  }
  SortHeaviestFirst(rows);
  return rows;
}

std::vector<BreakdownRow> MergeBreakdowns(
    const std::vector<PlayerBreakdown>& players) {
  std::map<std::string, BreakdownRow> by_skill;
  for (const PlayerBreakdown& player : players) {
    for (const BreakdownRow& row : player.rows) {
      BreakdownRow& merged = by_skill[row.skill];
      merged.skill = row.skill;
      merged.damage += row.damage;
      merged.casts += row.casts;
      merged.lines += row.lines;
    }
  }
  std::vector<BreakdownRow> rows;
  for (const std::pair<const std::string, BreakdownRow>& entry : by_skill) {
    rows.push_back(entry.second);
  }
  SortHeaviestFirst(rows);
  return rows;
}

}  // namespace ms
