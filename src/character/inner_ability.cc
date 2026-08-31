#include "src/character/inner_ability.h"

#include <cstdint>
#include <random>
#include <set>
#include <vector>

#include "src/protos/character.pb.h"

namespace ms {
namespace {

// One row of the table: what a type is worth at each rank, and how heavily it
// is favoured there. Rare first, Legendary last, and a zero in either column
// says GMS does not offer the pairing.
struct AbilityRow {
  AbilityLineType type;
  int value[4];
  int weight[4];
};

// GMS states its weights as percentages of a 45-line pool, to four decimal
// places. Within one rank they are all multiples of one another -- All Stats
// at Epic is 2.7804% and STR exactly one and a half of it -- so the table is
// written in those multiples. A roll normalises over the types still
// available, which makes the scale free and the ratios everything.
constexpr AbilityRow kRows[] = {
    {ABILITY_LINE_TYPE_STR, {10, 20, 30, 40}, {45, 45, 45, 45}},
    {ABILITY_LINE_TYPE_DEX, {10, 20, 30, 40}, {45, 45, 45, 45}},
    {ABILITY_LINE_TYPE_INT, {10, 20, 30, 40}, {45, 45, 45, 45}},
    {ABILITY_LINE_TYPE_LUK, {10, 20, 30, 40}, {45, 45, 45, 45}},
    {ABILITY_LINE_TYPE_ALL_STATS, {10, 20, 30, 40}, {40, 30, 20, 20}},
    {ABILITY_LINE_TYPE_MAX_HP, {150, 300, 450, 600}, {43, 30, 25, 20}},
    {ABILITY_LINE_TYPE_MAX_HP_PCT, {0, 0, 10, 20}, {0, 0, 20, 20}},
    {ABILITY_LINE_TYPE_ATTACK, {0, 12, 21, 30}, {0, 20, 15, 25}},
    {ABILITY_LINE_TYPE_MAGIC_ATTACK, {0, 12, 21, 30}, {0, 20, 15, 25}},
    {ABILITY_LINE_TYPE_CRIT_RATE, {0, 10, 20, 30}, {0, 10, 5, 5}},
    {ABILITY_LINE_TYPE_BOSS_DAMAGE, {0, 0, 10, 20}, {0, 0, 10, 25}},
    {ABILITY_LINE_TYPE_NORMAL_DAMAGE, {3, 5, 8, 10}, {35, 30, 20, 20}},
    {ABILITY_LINE_TYPE_BUFF_DURATION, {13, 25, 38, 50}, {40, 15, 10, 10}},
    {ABILITY_LINE_TYPE_ITEM_DROP, {5, 10, 15, 20}, {40, 30, 20, 20}},
    {ABILITY_LINE_TYPE_MESO, {5, 10, 15, 20}, {40, 30, 20, 20}},
    {ABILITY_LINE_TYPE_ATTACK_SPEED, {0, 0, 0, 1}, {0, 0, 0, 5}},
};

// Honor a reset costs, by rank and then by lines held. The Unique and
// Legendary rows are GMS's own, and what a lock adds doubles from one to the
// next: +1500/+2500 becomes +3000/+5000. GMS prices no lock below Unique,
// where it allows none; halving that ladder twice gives the two rows here.
constexpr int64_t kResetCost[4][kMaxLockedAbilityLines + 1] = {
    {100, 500, 1100},
    {200, 1000, 2200},
    {1500, 3000, 5500},
    {8000, 11000, 16000},
};

// Chance a reset carries the ability up a rank.
constexpr double kRankUpChance[4] = {0.05, 0.02, 0.01, 0.0};

// The rungs below the ability's own that its 2nd and 3rd lines roll on. A Rare
// or Epic ability rolls them Rare; the two ranks above split.
constexpr double kEpicChanceUnderUnique = 0.30;
constexpr double kUniqueChanceUnderLegendary = 0.15;

int RankIndex(AbilityRank rank) {
  return rank - ABILITY_RANK_RARE;
}

bool RankInRange(AbilityRank rank) {
  return rank >= ABILITY_RANK_RARE && rank <= ABILITY_RANK_LEGENDARY;
}

const AbilityRow* RowFor(AbilityLineType type) {
  for (const AbilityRow& row : kRows) {
    if (row.type == type) {
      return &row;
    }
  }
  return nullptr;
}

// The rank the ability comes out of a reset at: the one it went in with, or
// one higher. Never lower -- GMS lets an ability fall and this game does not.
AbilityRank RolledAbilityRank(AbilityRank rank, std::mt19937& rng) {
  std::bernoulli_distribution ranks_up(AbilityRankUpChance(rank));
  if (!ranks_up(rng)) {
    return rank;
  }
  return static_cast<AbilityRank>(rank + 1);
}

// The rank one of the two lines under the top rolls at.
AbilityRank RolledLineRank(AbilityRank ability, std::mt19937& rng) {
  if (ability == ABILITY_RANK_LEGENDARY) {
    std::bernoulli_distribution unique(kUniqueChanceUnderLegendary);
    return unique(rng) ? ABILITY_RANK_UNIQUE : ABILITY_RANK_EPIC;
  }
  if (ability == ABILITY_RANK_UNIQUE) {
    std::bernoulli_distribution epic(kEpicChanceUnderUnique);
    return epic(rng) ? ABILITY_RANK_EPIC : ABILITY_RANK_RARE;
  }
  return ABILITY_RANK_RARE;
}

// A line rolled at `rank`, avoiding every type already on the ability. Adds
// what it picked to `taken`, since no two lines may share a type.
AbilityLine RollLine(AbilityRank rank, std::set<AbilityLineType>& taken,
                     std::mt19937& rng) {
  int total = 0;
  for (const AbilityRow& row : kRows) {
    if (taken.count(row.type) == 0) {
      total += row.weight[RankIndex(rank)];
    }
  }
  std::uniform_int_distribution<int> pick(1, total);
  int landed = pick(rng);
  for (const AbilityRow& row : kRows) {
    if (taken.count(row.type) != 0) {
      continue;
    }
    landed -= row.weight[RankIndex(rank)];
    if (landed <= 0) {
      taken.insert(row.type);
      AbilityLine line;
      line.set_type(row.type);
      line.set_rank(rank);
      return line;
    }
  }
  // Unreachable: the pool at any rank is far wider than the two types two
  // held lines can spend.
  return AbilityLine();
}

}  // namespace

const AbilityPreset& PresetOf(const InnerAbility& ability, StatPreset preset) {
  if (preset == StatPreset::kBossing) {
    return ability.bossing();
  }
  return ability.farming();
}

AbilityPreset& PresetOf(InnerAbility& ability, StatPreset preset) {
  if (preset == StatPreset::kBossing) {
    return *ability.mutable_bossing();
  }
  return *ability.mutable_farming();
}

AbilityPreset DefaultAbilityPreset() {
  AbilityPreset preset;
  preset.set_rank(kDefaultAbilityRank);
  for (int i = 0; i < kAbilityLines; ++i) {
    AbilityLine& line = *preset.add_lines();
    line.set_type(kDefaultAbilityLineType);
    line.set_rank(kDefaultAbilityRank);
  }
  return preset;
}

int AbilityLineValue(AbilityLineType type, AbilityRank rank) {
  const AbilityRow* row = RowFor(type);
  if (row == nullptr || !RankInRange(rank)) {
    return 0;
  }
  return row->value[RankIndex(rank)];
}

int AbilityTypeWeight(AbilityLineType type, AbilityRank rank) {
  const AbilityRow* row = RowFor(type);
  if (row == nullptr || !RankInRange(rank)) {
    return 0;
  }
  return row->weight[RankIndex(rank)];
}

int64_t AbilityResetCost(AbilityRank rank, int locked) {
  if (!RankInRange(rank) || locked < 0 || locked > kMaxLockedAbilityLines) {
    return 0;
  }
  return kResetCost[RankIndex(rank)][locked];
}

double AbilityRankUpChance(AbilityRank rank) {
  if (!RankInRange(rank)) {
    return 0.0;
  }
  return kRankUpChance[RankIndex(rank)];
}

int LockedAbilityLines(const AbilityPreset& preset) {
  int held = 0;
  for (const AbilityLine& line : preset.lines()) {
    held += line.locked() ? 1 : 0;
  }
  return held;
}

bool SetAbilityLineLocked(AbilityPreset& preset, int index, bool locked) {
  if (index < 0 || index >= preset.lines_size()) {
    return false;
  }
  AbilityLine& line = *preset.mutable_lines(index);
  if (line.locked() == locked) {
    return false;
  }
  if (locked && LockedAbilityLines(preset) >= kMaxLockedAbilityLines) {
    return false;
  }
  line.set_locked(locked);
  return true;
}

void RerollAbility(AbilityPreset& preset, std::mt19937& rng) {
  const AbilityRank rank = RolledAbilityRank(preset.rank(), rng);

  // A held line on top already carrying the new rank keeps the top slot. Any
  // other arrangement wants a fresh line above it, which is what pushes the
  // held lines down.
  const bool top_holds_rank = preset.lines_size() > 0 &&
                              preset.lines(0).locked() &&
                              preset.lines(0).rank() == rank;

  std::set<AbilityLineType> taken;
  std::vector<AbilityLine> held;
  std::vector<int> held_slots;
  for (int i = top_holds_rank ? 1 : 0; i < preset.lines_size(); ++i) {
    if (preset.lines(i).locked()) {
      taken.insert(preset.lines(i).type());
      held.push_back(preset.lines(i));
      held_slots.push_back(i);
    }
  }

  AbilityLine rolled[kAbilityLines];
  bool filled[kAbilityLines] = {false};
  auto claim = [&](int slot, const AbilityLine& line) {
    rolled[slot] = line;
    filled[slot] = true;
  };

  if (top_holds_rank) {
    taken.insert(preset.lines(0).type());
    claim(0, preset.lines(0));
  } else {
    claim(0, RollLine(rank, taken, rng));
  }

  // Each held line keeps its slot where the top line has not taken it, and
  // otherwise slides to the first one free below.
  for (size_t i = 0; i < held.size(); ++i) {
    for (int slot = held_slots[i]; slot < kAbilityLines; ++slot) {
      if (!filled[slot]) {
        claim(slot, held[i]);
        break;
      }
    }
  }

  // Whatever is left rolls on the rungs below the ability's own.
  for (int slot = 0; slot < kAbilityLines; ++slot) {
    if (!filled[slot]) {
      claim(slot, RollLine(RolledLineRank(rank, rng), taken, rng));
    }
  }

  preset.set_rank(rank);
  preset.clear_lines();
  for (const AbilityLine& line : rolled) {
    *preset.add_lines() = line;
  }
}

}  // namespace ms
