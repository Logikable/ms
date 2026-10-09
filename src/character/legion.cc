#include "src/character/legion.h"

#include <algorithm>
#include <vector>

#include "src/character/job_branch.h"

namespace ms {

namespace {

// The lowest level of each rank from B, from GMS.
constexpr int kRankLevels[] = {60, 100, 140, 200, 250};
// The member count at each tier's first rank; each later rank adds one.
constexpr int kTierMemberSlots[] = {9, 18, 27, 36, 41};
constexpr int kRanksPerTier = 5;
constexpr const char* kTierNames[] = {"Nameless", "Renowned", "Heroic",
                                      "Legendary", "Supreme"};
constexpr const char* kNumerals[] = {"I", "II", "III", "IV", "V"};
// The expanded stats' cap from each rank on: GMS's outer grid, which reached 6,
// 13, 21, 30 and 40 squares per area at these ranks.
constexpr struct {
  int rank;
  int cap;
} kExpandedCaps[] = {{12, 40}, {10, 30}, {8, 21}, {6, 13}, {4, 6}};

// A job effect at each rank B to SSS, from GMS.
constexpr int kStatEffect[] = {10, 20, 40, 80, 100};
constexpr double kMaxHpEffect[] = {0.02, 0.03, 0.04, 0.05, 0.06};
constexpr double kCritRateEffect[] = {0.01, 0.02, 0.03, 0.04, 0.05};

}  // namespace

CharacterRank CharacterRankFor(int level) {
  CharacterRank rank = CharacterRank::kNone;
  for (int i = 0; i < 5; ++i) {
    if (level >= kRankLevels[i]) {
      rank = static_cast<CharacterRank>(i + 1);
    }
  }
  return rank;
}

std::string CharacterRankName(CharacterRank rank) {
  switch (rank) {
    case CharacterRank::kB:
      return "B";
    case CharacterRank::kA:
      return "A";
    case CharacterRank::kS:
      return "S";
    case CharacterRank::kSS:
      return "SS";
    case CharacterRank::kSSS:
      return "SSS";
    case CharacterRank::kNone:
      break;
  }
  return "";
}

int LegionPointsFor(CharacterRank rank) {
  return static_cast<int>(rank);
}

int LegionRankFor(int legion_level) {
  return std::clamp(legion_level / kLegionLevelPerRank, 0, kLegionRanks);
}

std::string LegionRankName(int rank) {
  if (rank <= 0 || rank > kLegionRanks) {
    return "";
  }
  const int tier = (rank - 1) / kRanksPerTier;
  const int step = (rank - 1) % kRanksPerTier;
  return std::string(kTierNames[tier]) + " Legion Rank " + kNumerals[step];
}

int LegionMemberSlots(int rank) {
  rank = std::clamp(rank, 1, kLegionRanks);
  return kTierMemberSlots[(rank - 1) / kRanksPerTier] +
         (rank - 1) % kRanksPerTier;
}

bool IsExpandedLegionStat(LegionStat stat) {
  return stat >= LEGION_STAT_STATUS_RESISTANCE;
}

int LegionStatCap(LegionStat stat, int rank) {
  if (stat == LEGION_STAT_UNSPECIFIED) {
    return 0;
  }
  if (!IsExpandedLegionStat(stat)) {
    return kLegionBaseStatCap;
  }
  for (const auto& step : kExpandedCaps) {
    if (rank >= step.rank) {
      return step.cap;
    }
  }
  return 0;
}

double LegionPerPoint(LegionStat stat) {
  switch (stat) {
    case LEGION_STAT_STR:
    case LEGION_STAT_DEX:
    case LEGION_STAT_INT:
    case LEGION_STAT_LUK:
      return 5.0;
    case LEGION_STAT_MAX_HP:
    case LEGION_STAT_MAX_MP:
      return 250.0;
    case LEGION_STAT_ATTACK:
    case LEGION_STAT_MAGIC_ATTACK:
    case LEGION_STAT_STATUS_RESISTANCE:
      return 1.0;
    case LEGION_STAT_EXP:
      return 0.0025;
    case LEGION_STAT_CRIT_DAMAGE:
      return 0.005;
    case LEGION_STAT_CRIT_RATE:
    case LEGION_STAT_BOSS_DAMAGE:
    case LEGION_STAT_NORMAL_DAMAGE:
    case LEGION_STAT_BUFF_DURATION:
    case LEGION_STAT_IED:
      return 0.01;
    case LEGION_STAT_UNSPECIFIED:
    case LegionStat_INT_MIN_SENTINEL_DO_NOT_USE_:
    case LegionStat_INT_MAX_SENTINEL_DO_NOT_USE_:
      break;
  }
  return 0.0;
}

LegionJobEffects LegionJobEffectFor(Job job, CharacterRank rank) {
  LegionJobEffects effects;
  if (rank == CharacterRank::kNone) {
    return effects;
  }
  const int i = static_cast<int>(rank) - 1;
  switch (LineOf(job)) {
    case JOB_FIGHTER:
    case JOB_PAGE:
      effects.str = kStatEffect[i];
      break;
    case JOB_SPEARMAN:
      effects.max_hp_pct = kMaxHpEffect[i];
      break;
    // GMS gives F/P Max MP%, which nothing here needs; the user's call.
    case JOB_FIRE_POISON_WIZARD:
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_CLERIC:
      effects.int_ = kStatEffect[i];
      break;
    case JOB_HUNTER:
      effects.dex = kStatEffect[i];
      break;
    case JOB_CROSSBOWMAN:
    case JOB_ASSASSIN:
      effects.crit_rate = kCritRateEffect[i];
      break;
    case JOB_BANDIT:
      effects.luk = kStatEffect[i];
      break;
    default:
      break;
  }
  return effects;
}

LegionSummary SummarizeLegion(std::vector<LegionMember> members) {
  LegionSummary summary;
  members.erase(std::remove_if(members.begin(), members.end(),
                               [](const LegionMember& member) {
                                 return CharacterRankFor(member.level) ==
                                        CharacterRank::kNone;
                               }),
                members.end());
  std::sort(members.begin(), members.end(),
            [](const LegionMember& a, const LegionMember& b) {
              return a.level > b.level;
            });
  for (int i = 0;
       i < static_cast<int>(members.size()) && i < kLegionLevelsCounted; ++i) {
    summary.legion_level += members[i].level;
  }
  summary.rank = LegionRankFor(summary.legion_level);
  summary.members = std::min(static_cast<int>(members.size()),
                             LegionMemberSlots(summary.rank));
  for (int i = 0; i < summary.members; ++i) {
    summary.points += LegionPointsFor(CharacterRankFor(members[i].level));
  }
  LegionJobEffects& total = summary.job_effects;
  for (const LegionMember& member : members) {
    const LegionJobEffects one =
        LegionJobEffectFor(member.job, CharacterRankFor(member.level));
    total.str += one.str;
    total.dex += one.dex;
    total.int_ += one.int_;
    total.luk += one.luk;
    total.max_hp_pct += one.max_hp_pct;
    total.crit_rate += one.crit_rate;
  }
  return summary;
}

const LegionPreset& PresetOf(const Legion& legion, StatPreset slot) {
  if (IndexOf(slot) >= legion.presets_size()) {
    return LegionPreset::default_instance();
  }
  return legion.presets(IndexOf(slot));
}

LegionPreset& PresetOf(Legion& legion, StatPreset slot) {
  while (legion.presets_size() < kNumStatPresets) {
    legion.add_presets();
  }
  return *legion.mutable_presets(IndexOf(slot));
}

int LegionPointsSpent(const LegionPreset& preset) {
  int spent = 0;
  for (const auto& [stat, points] : preset.points()) {
    spent += points;
  }
  return spent;
}

std::map<LegionStat, int> EffectiveLegionPoints(const LegionPreset& preset,
                                                int rank, int points) {
  // The proto map has no order; the grant must have one.
  std::map<LegionStat, int> effective;
  for (const auto& [stat, spent] : preset.points()) {
    if (!LegionStat_IsValid(stat)) {
      continue;
    }
    const LegionStat key = static_cast<LegionStat>(stat);
    const int capped = std::min(spent, LegionStatCap(key, rank));
    if (capped > 0) {
      effective[key] = capped;
    }
  }
  int left = points;
  for (std::map<LegionStat, int>::iterator it = effective.begin();
       it != effective.end();) {
    it->second = std::min(it->second, left);
    left -= it->second;
    it = it->second == 0 ? effective.erase(it) : std::next(it);
  }
  return effective;
}

int SpendLegionPoints(Legion& legion, StatPreset slot, LegionStat stat,
                      int delta, const LegionSummary& summary) {
  if (stat == LEGION_STAT_UNSPECIFIED || !LegionStat_IsValid(stat)) {
    return 0;
  }
  LegionPreset& preset = PresetOf(legion, slot);
  const int now = preset.points().contains(stat) ? preset.points().at(stat) : 0;
  int target = std::max(now + delta, 0);
  if (delta > 0) {
    const int left = summary.points - LegionPointsSpent(preset);
    target = std::max(
        now, std::min({target, LegionStatCap(stat, summary.rank), now + left}));
  }
  if (target == 0) {
    preset.mutable_points()->erase(stat);
  } else {
    (*preset.mutable_points())[stat] = target;
  }
  return target - now;
}

void ResetLegionPreset(Legion& legion, StatPreset slot) {
  PresetOf(legion, slot).clear_points();
}

void SwapLegionPresets(Legion& legion, StatPreset a, StatPreset b) {
  PresetOf(legion, a);  // pads every preset
  legion.mutable_presets()->SwapElements(IndexOf(a), IndexOf(b));
  if (legion.slot_in_use() == IndexOf(a)) {
    legion.set_slot_in_use(IndexOf(b));
  } else if (legion.slot_in_use() == IndexOf(b)) {
    legion.set_slot_in_use(IndexOf(a));
  }
}

}  // namespace ms
