#include "src/character/familiar.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/combat/damage.h"
#include "src/protos/equip.pb.h"
#include "src/protos/familiar.pb.h"

namespace ms {
namespace {

// The user's roster, in order of the monster's level. The eight missing here
// take GMS's levels from maplestorywiki, at their original maps where the wiki
// lists several (El Nath's Yeti and Werewolf, Minar's Rash, Memory Lane's
// Memory Guardian).
constexpr FamiliarSpecies kRoster[] = {
    {"Snail", 1},
    {"Blue Snail", 2},
    {"Red Snail", 5},
    {"Slime", 7},
    {"Orange Mushroom", 10},
    {"Green Mushroom", 10},
    {"Bubbling", 10},
    {"Horny Mushroom", 12},
    {"Stirge", 43},
    {"Jr. Wraith", 44},
    {"Jr. Cellion", 70},
    {"Beetle", 103},
    {"Rash", 103},
    {"Yeti", 117},
    {"Werewolf", 122},
    {"Lycanthrope", 123},
    {"Memory Monk", 131},
    {"Memory Guardian", 133},
    {"Mutant Snail", 162},
    {"Mutant Orange Mushroom", 164},
};

constexpr int64_t kLevelCosts[kFamiliarMaxLevel] = {1'000, 9'000, 50'000,
                                                    200'000};

// The total of familiar levels each beginner skill level needs.
constexpr int kSkillThresholds[] = {1, 3, 8, 20, 40, 80};

struct LineValue {
  FamiliarLineType type;
  int value;
};

// The user's tables. Each rank lists only what it rolls; a type missing from a
// rank is worth 0 there.
constexpr LineValue kRare[] = {
    {FAMILIAR_LINE_TYPE_STR, 6},        {FAMILIAR_LINE_TYPE_DEX, 6},
    {FAMILIAR_LINE_TYPE_INT, 6},        {FAMILIAR_LINE_TYPE_LUK, 6},
    {FAMILIAR_LINE_TYPE_MAX_HP, 6},     {FAMILIAR_LINE_TYPE_ATTACK, 3},
    {FAMILIAR_LINE_TYPE_STR_PCT, 1},    {FAMILIAR_LINE_TYPE_DEX_PCT, 1},
    {FAMILIAR_LINE_TYPE_INT_PCT, 1},    {FAMILIAR_LINE_TYPE_LUK_PCT, 1},
    {FAMILIAR_LINE_TYPE_MAX_HP_PCT, 1},
};
constexpr LineValue kEpic[] = {
    {FAMILIAR_LINE_TYPE_STR, 12},       {FAMILIAR_LINE_TYPE_DEX, 12},
    {FAMILIAR_LINE_TYPE_INT, 12},       {FAMILIAR_LINE_TYPE_LUK, 12},
    {FAMILIAR_LINE_TYPE_MAX_HP, 12},    {FAMILIAR_LINE_TYPE_ATTACK, 6},
    {FAMILIAR_LINE_TYPE_STR_PCT, 2},    {FAMILIAR_LINE_TYPE_DEX_PCT, 2},
    {FAMILIAR_LINE_TYPE_INT_PCT, 2},    {FAMILIAR_LINE_TYPE_LUK_PCT, 2},
    {FAMILIAR_LINE_TYPE_MAX_HP_PCT, 2},
};
constexpr LineValue kUnique[] = {
    {FAMILIAR_LINE_TYPE_STR_PCT, 3},
    {FAMILIAR_LINE_TYPE_DEX_PCT, 3},
    {FAMILIAR_LINE_TYPE_INT_PCT, 3},
    {FAMILIAR_LINE_TYPE_LUK_PCT, 3},
    {FAMILIAR_LINE_TYPE_MAX_HP_PCT, 3},
    {FAMILIAR_LINE_TYPE_ALL_STATS_PCT, 2},
    {FAMILIAR_LINE_TYPE_ATTACK_PCT, 3},
    {FAMILIAR_LINE_TYPE_CRIT_RATE, 4},
    {FAMILIAR_LINE_TYPE_DAMAGE_PCT, 3},
    {FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_30, 30},
    {FAMILIAR_LINE_TYPE_BOSS_DAMAGE_20, 20},
    {FAMILIAR_LINE_TYPE_BOSS_DAMAGE_30, 30},
};
constexpr LineValue kLegendary[] = {
    {FAMILIAR_LINE_TYPE_STR_PCT, 6},
    {FAMILIAR_LINE_TYPE_DEX_PCT, 6},
    {FAMILIAR_LINE_TYPE_INT_PCT, 6},
    {FAMILIAR_LINE_TYPE_LUK_PCT, 6},
    {FAMILIAR_LINE_TYPE_MAX_HP_PCT, 6},
    {FAMILIAR_LINE_TYPE_ALL_STATS_PCT, 3},
    {FAMILIAR_LINE_TYPE_ATTACK_PCT, 6},
    {FAMILIAR_LINE_TYPE_CRIT_RATE, 8},
    {FAMILIAR_LINE_TYPE_CRIT_DAMAGE, 3},
    {FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_35, 35},
    {FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_40, 40},
    {FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40, 40},
    {FAMILIAR_LINE_TYPE_BOSS_DROP_RATE, 100},
};

absl::Span<const LineValue> TableFor(PotentialRank rank) {
  switch (rank) {
    case POTENTIAL_RANK_RARE:
      return kRare;
    case POTENTIAL_RANK_EPIC:
      return kEpic;
    case POTENTIAL_RANK_UNIQUE:
      return kUnique;
    case POTENTIAL_RANK_LEGENDARY:
      return kLegendary;
    default:
      return {};
  }
}

FamiliarLine RollLine(PotentialRank rank, std::mt19937& rng) {
  const std::vector<FamiliarLineType> pool = FamiliarPool(rank);
  std::uniform_int_distribution<int> pick(0, static_cast<int>(pool.size()) - 1);
  FamiliarLine line;
  line.set_type(pool[pick(rng)]);
  line.set_rank(rank);
  return line;
}

Familiar* FindMutable(FamiliarBook& book, const std::string& name) {
  for (Familiar& familiar : *book.mutable_familiars()) {
    if (familiar.name() == name) {
      return &familiar;
    }
  }
  return nullptr;
}

// Adds one line to `totals`. Boss damage is summed here and capped once every
// summoned line is in.
void AddLine(const FamiliarLine& line, FamiliarTotals& totals) {
  static_assert(FamiliarLineType_ARRAYSIZE == 24,
                "a new familiar line needs somewhere to land");
  const int value = FamiliarLineValue(line.type(), line.rank());
  const double share = value / 100.0;
  PotentialTotals& lines = totals.lines;
  switch (line.type()) {
    case FAMILIAR_LINE_TYPE_STR:
      lines.flat.set_str(lines.flat.str() + value);
      break;
    case FAMILIAR_LINE_TYPE_DEX:
      lines.flat.set_dex(lines.flat.dex() + value);
      break;
    case FAMILIAR_LINE_TYPE_INT:
      lines.flat.set_int_(lines.flat.int_() + value);
      break;
    case FAMILIAR_LINE_TYPE_LUK:
      lines.flat.set_luk(lines.flat.luk() + value);
      break;
    case FAMILIAR_LINE_TYPE_MAX_HP:
      lines.flat.set_max_hp(lines.flat.max_hp() + value);
      break;
    case FAMILIAR_LINE_TYPE_ATTACK:
      totals.attack += value;
      break;
    case FAMILIAR_LINE_TYPE_STR_PCT:
      lines.str_pct += share;
      break;
    case FAMILIAR_LINE_TYPE_DEX_PCT:
      lines.dex_pct += share;
      break;
    case FAMILIAR_LINE_TYPE_INT_PCT:
      lines.int_pct += share;
      break;
    case FAMILIAR_LINE_TYPE_LUK_PCT:
      lines.luk_pct += share;
      break;
    case FAMILIAR_LINE_TYPE_MAX_HP_PCT:
      lines.max_hp_pct += share;
      break;
    case FAMILIAR_LINE_TYPE_ALL_STATS_PCT:
      lines.str_pct += share;
      lines.dex_pct += share;
      lines.int_pct += share;
      lines.luk_pct += share;
      break;
    case FAMILIAR_LINE_TYPE_ATTACK_PCT:
      lines.attack_pct += share;
      lines.magic_attack_pct += share;
      break;
    case FAMILIAR_LINE_TYPE_CRIT_RATE:
      lines.crit_rate += share;
      break;
    case FAMILIAR_LINE_TYPE_DAMAGE_PCT:
      lines.damage_pct += share;
      break;
    case FAMILIAR_LINE_TYPE_CRIT_DAMAGE:
      lines.crit_dmg += share;
      break;
    case FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_30:
    case FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_35:
    case FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_40:
      lines.ied = CombineIgnoredDefense(lines.ied, share);
      break;
    case FAMILIAR_LINE_TYPE_BOSS_DAMAGE_20:
    case FAMILIAR_LINE_TYPE_BOSS_DAMAGE_30:
    case FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40:
      lines.boss_pct += share;
      break;
    case FAMILIAR_LINE_TYPE_BOSS_DROP_RATE:
      totals.boss_drop = true;
      break;
    case FAMILIAR_LINE_TYPE_UNSPECIFIED:
      break;
  }
}

}  // namespace

absl::Span<const FamiliarSpecies> FamiliarRoster() {
  return kRoster;
}

const FamiliarPreset& StarterFamiliars() {
  static const FamiliarPreset* const starters = [] {
    auto* preset = new FamiliarPreset;
    for (int i = 0; i < kMaxSummonedFamiliars; ++i) {
      preset->add_names(kRoster[i].name);
    }
    return preset;
  }();
  return *starters;
}

bool IsFamiliar(const std::string& name) {
  for (const FamiliarSpecies& species : kRoster) {
    if (name == species.name) {
      return true;
    }
  }
  return false;
}

int64_t FamiliarLevelCost(int level) {
  if (level < 1 || level > kFamiliarMaxLevel) {
    return 0;
  }
  return kLevelCosts[level - 1];
}

PotentialRank FamiliarRank(int level) {
  if (level < 1 || level > kFamiliarMaxLevel) {
    return POTENTIAL_RANK_UNSPECIFIED;
  }
  return static_cast<PotentialRank>(level);
}

int FamiliarLineValue(FamiliarLineType type, PotentialRank rank) {
  for (const LineValue& entry : TableFor(rank)) {
    if (entry.type == type) {
      return entry.value;
    }
  }
  return 0;
}

std::vector<FamiliarLineType> FamiliarPool(PotentialRank rank) {
  std::vector<FamiliarLineType> pool;
  for (const LineValue& entry : TableFor(rank)) {
    pool.push_back(entry.type);
  }
  return pool;
}

std::vector<FamiliarLine> RollFamiliarLines(PotentialRank rank,
                                            std::mt19937& rng) {
  std::vector<FamiliarLine> lines;
  lines.push_back(RollLine(rank, rng));
  PotentialRank second = rank;
  if (rank > POTENTIAL_RANK_RARE) {
    std::bernoulli_distribution prime(kFamiliarPrimeChance);
    if (!prime(rng)) {
      second = static_cast<PotentialRank>(rank - 1);
    }
  }
  lines.push_back(RollLine(second, rng));
  return lines;
}

const Familiar* FindFamiliar(const FamiliarBook& book,
                             const std::string& name) {
  for (const Familiar& familiar : book.familiars()) {
    if (familiar.name() == name) {
      return &familiar;
    }
  }
  return nullptr;
}

int FamiliarLevel(const FamiliarBook& book, const std::string& name) {
  const Familiar* familiar = FindFamiliar(book, name);
  if (familiar == nullptr) {
    return 0;
  }
  return familiar->level();
}

std::string FamiliarDisplayName(const FamiliarBook& book,
                                const std::string& name) {
  const Familiar* familiar = FindFamiliar(book, name);
  if (familiar == nullptr || familiar->nickname().empty()) {
    return name;
  }
  return familiar->nickname();
}

bool RenameFamiliar(FamiliarBook& book, const std::string& name,
                    const std::string& nickname) {
  if (!IsFamiliar(name)) {
    return false;
  }
  Familiar* familiar = FindMutable(book, name);
  if (familiar == nullptr) {
    familiar = book.add_familiars();
    familiar->set_name(name);
  }
  familiar->set_nickname(nickname);
  return true;
}

bool CanLevelFamiliar(const FamiliarBook& book, const std::string& name) {
  if (!IsFamiliar(name)) {
    return false;
  }
  const int level = FamiliarLevel(book, name);
  return level < kFamiliarMaxLevel &&
         book.exp() >= FamiliarLevelCost(level + 1);
}

bool LevelUpFamiliar(FamiliarBook& book, const std::string& name,
                     std::mt19937& rng) {
  if (!CanLevelFamiliar(book, name)) {
    return false;
  }
  Familiar* familiar = FindMutable(book, name);
  if (familiar == nullptr) {
    familiar = book.add_familiars();
    familiar->set_name(name);
  }
  const int level = familiar->level() + 1;
  book.set_exp(book.exp() - FamiliarLevelCost(level));
  familiar->set_level(level);
  familiar->clear_lines();
  for (FamiliarLine& line : RollFamiliarLines(FamiliarRank(level), rng)) {
    *familiar->add_lines() = std::move(line);
  }
  return true;
}

bool CubeFamiliar(FamiliarBook& book, const std::string& name,
                  std::mt19937& rng) {
  Familiar* familiar = FindMutable(book, name);
  if (familiar == nullptr || familiar->level() < 1) {
    return false;
  }
  familiar->clear_lines();
  for (FamiliarLine& line :
       RollFamiliarLines(FamiliarRank(familiar->level()), rng)) {
    *familiar->add_lines() = std::move(line);
  }
  return true;
}

int TotalFamiliarLevels(const FamiliarBook& book) {
  int total = 0;
  for (const Familiar& familiar : book.familiars()) {
    total += familiar.level();
  }
  return total;
}

int FamiliarSkillLevel(int total_levels) {
  int level = 0;
  for (int threshold : kSkillThresholds) {
    if (total_levels >= threshold) {
      ++level;
    }
  }
  return level;
}

int FamiliarLevelsForSkill(int level) {
  if (level < 1 || level > static_cast<int>(std::size(kSkillThresholds))) {
    return 0;
  }
  return kSkillThresholds[level - 1];
}

FamiliarTotals SummonedFamiliarTotals(
    const FamiliarBook& book,
    const google::protobuf::RepeatedPtrField<std::string>& summoned) {
  FamiliarTotals totals;
  int counted = 0;
  for (const std::string& name : summoned) {
    if (counted == kMaxSummonedFamiliars) {
      break;
    }
    const Familiar* familiar = FindFamiliar(book, name);
    if (familiar == nullptr) {
      continue;
    }
    ++counted;
    for (const FamiliarLine& line : familiar->lines()) {
      AddLine(line, totals);
    }
  }
  totals.lines.boss_pct =
      std::min(totals.lines.boss_pct, kFamiliarBossDamageCap);
  return totals;
}

const FamiliarPreset& PresetOf(const SummonedFamiliars& summoned,
                               StatPreset slot) {
  if (IndexOf(slot) >= summoned.presets_size()) {
    return StarterFamiliars();
  }
  return summoned.presets(IndexOf(slot));
}

FamiliarPreset& PresetOf(SummonedFamiliars& summoned, StatPreset slot) {
  while (summoned.presets_size() < kNumStatPresets) {
    *summoned.add_presets() = StarterFamiliars();
  }
  return *summoned.mutable_presets(IndexOf(slot));
}

}  // namespace ms
