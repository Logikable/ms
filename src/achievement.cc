#include "src/achievement.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "src/character/familiar.h"
#include "src/character/guild.h"
#include "src/character/legion.h"
#include "src/character/symbol.h"
#include "src/game_state.h"
#include "src/protos/achievement.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/save.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

constexpr AchievementInfo kAchievements[] = {
    {ACHIEVEMENT_STARS_17, "Starstruck", "Obtain a 17★ item.", "stars"},
    {ACHIEVEMENT_STARS_22, "Superstar", "Obtain a 22★ item.", "stars"},
    {ACHIEVEMENT_STARS_25, "Supernova", "Obtain a 25★ item.", "stars"},
    {ACHIEVEMENT_TRIPLE_PRIME, "Triple Prime",
     "Roll three prime lines of the same stat, mpot or bpot.", nullptr},
    {ACHIEVEMENT_PERFECT_FLAME, "Perfect Flame", "Flame four useful T7 lines.",
     nullptr},
    {ACHIEVEMENT_FAMILIARS_MAXED, "Kennel Master",
     "Get every familiar to level 4 on one character.", "familiars"},
    {ACHIEVEMENT_LEGION_8000, "Legion 8000", "Reach Legion level 8,000.",
     "Legion levels"},
    {ACHIEVEMENT_ARCANE_SYMBOLS_MAXED, "Arcane Mastery",
     "Max every Arcane Symbol on one character.", "symbols"},
    {ACHIEVEMENT_SACRED_SYMBOLS_MAXED, "Sacred Mastery",
     "Max every Sacred Symbol on one character. Grand Sacred Symbols don't "
     "count.",
     "symbols"},
    {ACHIEVEMENT_GUILD_SKILLS_MAXED, "Guild Grandmaster",
     "Max all Guild Skills.", "Noblesse SP"},
    {ACHIEVEMENT_MESO_10B, "Ten Billion",
     "Hold 10,000,000,000 meso on one character, or in the bank.", "meso"},
    {ACHIEVEMENT_MESO_100B, "Hundred Billion",
     "Hold 100,000,000,000 meso on one character, or in the bank.", "meso"},
    {ACHIEVEMENT_MESO_1T, "Trillionaire",
     "Hold 1,000,000,000,000 meso on one character, or in the bank.", "meso"},
    {ACHIEVEMENT_LEVEL_300, "Level 300", "Reach level 300 on any character.",
     "levels"},
    {ACHIEVEMENT_ACCOUNT_90_DAYS, "Three Months",
     "Keep an account for 90 days.", "days"},
    {ACHIEVEMENT_ACCOUNT_365_DAYS, "Anniversary",
     "Keep an account for 365 days.", "days"},
    {ACHIEVEMENT_ACCOUNT_3_YEARS, "Three Years Old",
     "Keep an account for three years.", "days"},
};

constexpr int64_t kSecondsPerDay = 24 * 60 * 60;
constexpr int64_t kThreeYearsDays = 3 * 365;
constexpr int kAchievementLevel = 300;
constexpr int kAchievementLegionLevel = 8000;
constexpr int kFlameTopTier = 7;

// The effect a potential line grants, with its sizes folded together: Boss
// Damage 35% and 40% are both Boss Damage. The smaller sizes are Unique
// lines, never prime on a Legendary potential, so only primes meet here.
PotentialLineType EffectOf(PotentialLineType type) {
  switch (type) {
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_35:
    case POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40:
      return POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15;
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_35:
    case POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40:
      return POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30;
    case POTENTIAL_LINE_TYPE_COOLDOWN_2:
      return POTENTIAL_LINE_TYPE_COOLDOWN_1;
    default:
      return type;
  }
}

// The flame lines useful to a job with one main stat. Every job here is an
// Explorer, so these four are all of them.
struct FlameJob {
  FlameStat main;
  FlameStat pairs[3];
  FlameStat attack;
};

constexpr FlameJob kFlameJobs[] = {
    {FLAME_STAT_STR,
     {FLAME_STAT_STR_DEX, FLAME_STAT_STR_INT, FLAME_STAT_STR_LUK},
     FLAME_STAT_ATTACK},
    {FLAME_STAT_DEX,
     {FLAME_STAT_STR_DEX, FLAME_STAT_DEX_INT, FLAME_STAT_DEX_LUK},
     FLAME_STAT_ATTACK},
    {FLAME_STAT_INT,
     {FLAME_STAT_STR_INT, FLAME_STAT_DEX_INT, FLAME_STAT_INT_LUK},
     FLAME_STAT_MAGIC_ATTACK},
    {FLAME_STAT_LUK,
     {FLAME_STAT_STR_LUK, FLAME_STAT_DEX_LUK, FLAME_STAT_INT_LUK},
     FLAME_STAT_ATTACK},
};

bool UsefulTo(const FlameJob& job, FlameStat stat) {
  if (stat == job.main || stat == job.attack || stat == FLAME_STAT_ALL_STAT ||
      stat == FLAME_STAT_BOSS_DAMAGE || stat == FLAME_STAT_DAMAGE) {
    return true;
  }
  for (FlameStat pair : job.pairs) {
    if (stat == pair) {
      return true;
    }
  }
  return false;
}

// Every Equip anywhere under `message`: worn, in the bag, in a preset, in the
// bank. Walked by reflection, as the version 4 upgrade is, so a place to keep
// an item added later isn't missed.
void CollectEquips(const google::protobuf::Message& message,
                   std::vector<const Equip*>& out) {
  if (message.GetDescriptor() == Equip::descriptor()) {
    out.push_back(static_cast<const Equip*>(&message));
    return;
  }
  const google::protobuf::Reflection* reflection = message.GetReflection();
  std::vector<const google::protobuf::FieldDescriptor*> fields;
  reflection->ListFields(message, &fields);
  for (const google::protobuf::FieldDescriptor* field : fields) {
    if (field->cpp_type() !=
        google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      continue;
    }
    if (!field->is_repeated()) {
      CollectEquips(reflection->GetMessage(message, field), out);
      continue;
    }
    for (int i = 0; i < reflection->FieldSize(message, field); ++i) {
      CollectEquips(reflection->GetRepeatedMessage(message, field, i), out);
    }
  }
}

// The catalog's symbols of one kind, by display name, the way a held item
// names them.
template <typename Kind>
std::map<std::string, const EquipPrototype*> SymbolsOfKind(
    const std::map<std::string, EquipPrototype>& equips, Kind kind) {
  std::map<std::string, const EquipPrototype*> symbols;
  for (const auto& [stem, proto] : equips) {
    if (kind(proto)) {
      symbols[proto.name()] = &proto;
    }
  }
  return symbols;
}

// How many of `symbols` `held` has at its max level.
int MaxedSymbols(const std::map<std::string, const EquipPrototype*>& symbols,
                 const std::vector<const Equip*>& held) {
  std::set<std::string> maxed;
  for (const Equip* item : held) {
    auto found = symbols.find(item->equip_name());
    if (found != symbols.end() && SymbolMaxed(*found->second, *item)) {
      maxed.insert(found->first);
    }
  }
  return static_cast<int>(maxed.size());
}

int MaxedFamiliars(const FamiliarBook& book) {
  int maxed = 0;
  for (const FamiliarSpecies& species : FamiliarRoster()) {
    if (FamiliarLevel(book, species.name) >= kFamiliarMaxLevel) {
      ++maxed;
    }
  }
  return maxed;
}

bool IsSacredNotGrand(const EquipPrototype& proto) {
  return IsSacredSymbol(proto) && !IsGrandSacredSymbol(proto);
}

// The Noblesse SP that buys every Noblesse level.
int NoblesseSpToMax(const std::map<std::string, Skill>& skills) {
  int total = 0;
  for (const auto& [name, skill] : skills) {
    if (skill.guild() == GUILD_SKILL_NOBLESSE) {
      total += skill.max_level();
    }
  }
  return total;
}

}  // namespace

absl::Span<const AchievementInfo> AllAchievements() {
  return kAchievements;
}

const AchievementInfo& InfoFor(Achievement achievement) {
  for (const AchievementInfo& info : kAchievements) {
    if (info.achievement == achievement) {
      return info;
    }
  }
  LOG(FATAL) << "No achievement " << achievement;
}

bool IsTriplePrime(const Potential& potential) {
  if (potential.rank() != POTENTIAL_RANK_LEGENDARY ||
      potential.lines_size() != 3) {
    return false;
  }
  const PotentialLineType effect = EffectOf(potential.lines(0).type());
  for (const PotentialLine& line : potential.lines()) {
    if (line.rank() != POTENTIAL_RANK_LEGENDARY ||
        EffectOf(line.type()) != effect) {
      return false;
    }
  }
  return effect != POTENTIAL_LINE_TYPE_UNSPECIFIED;
}

bool IsPerfectFlame(const Equip& equip) {
  if (equip.flame_size() != 4) {
    return false;
  }
  for (const FlameLine& line : equip.flame()) {
    if (line.tier() != kFlameTopTier) {
      return false;
    }
  }
  for (const FlameJob& job : kFlameJobs) {
    bool all = true;
    for (const FlameLine& line : equip.flame()) {
      all = all && UsefulTo(job, line.stat());
    }
    if (all) {
      return true;
    }
  }
  return false;
}

bool IsMesoAchievement(Achievement achievement) {
  return achievement == ACHIEVEMENT_MESO_10B ||
         achievement == ACHIEVEMENT_MESO_100B ||
         achievement == ACHIEVEMENT_MESO_1T;
}

bool HasCounter(Achievement achievement) {
  return InfoFor(achievement).unit != nullptr;
}

std::map<Achievement, int64_t> AchievementTargets(const GameState& state) {
  return {
      {ACHIEVEMENT_STARS_17, 17},
      {ACHIEVEMENT_STARS_22, 22},
      {ACHIEVEMENT_STARS_25, 25},
      {ACHIEVEMENT_FAMILIARS_MAXED,
       static_cast<int64_t>(FamiliarRoster().size())},
      {ACHIEVEMENT_LEGION_8000, kAchievementLegionLevel},
      {ACHIEVEMENT_ARCANE_SYMBOLS_MAXED,
       static_cast<int64_t>(
           SymbolsOfKind(state.equips, IsArcaneSymbol).size())},
      {ACHIEVEMENT_SACRED_SYMBOLS_MAXED,
       static_cast<int64_t>(
           SymbolsOfKind(state.equips, IsSacredNotGrand).size())},
      {ACHIEVEMENT_GUILD_SKILLS_MAXED, NoblesseSpToMax(state.skills)},
      {ACHIEVEMENT_MESO_10B, 10'000'000'000},
      {ACHIEVEMENT_MESO_100B, 100'000'000'000},
      {ACHIEVEMENT_MESO_1T, 1'000'000'000'000},
      {ACHIEVEMENT_LEVEL_300, kAchievementLevel},
      {ACHIEVEMENT_ACCOUNT_90_DAYS, 90},
      {ACHIEVEMENT_ACCOUNT_365_DAYS, 365},
      {ACHIEVEMENT_ACCOUNT_3_YEARS, kThreeYearsDays},
  };
}

AchievementStanding StandingOf(const GameState& state,
                               int64_t now_unix_seconds) {
  std::vector<Character> characters;
  characters.reserve(state.inactive_characters.size() + 1);
  characters.push_back(state.character.ToProto());
  for (const CharacterSave& slot : state.inactive_characters) {
    characters.push_back(slot.character());
  }
  const Account account = state.account.ToProto();

  AchievementStanding standing;
  std::vector<const Equip*> items;
  CollectEquips(account.bank(), items);
  // One purse at a time, not their sum: a character's or the bank's.
  int64_t meso = account.bank().meso();
  int top_level = 0;
  int arcane_maxed = 0;
  int sacred_maxed = 0;
  int familiars_maxed = 0;
  std::vector<LegionMember> legion;
  const std::map<std::string, const EquipPrototype*> arcane =
      SymbolsOfKind(state.equips, IsArcaneSymbol);
  const std::map<std::string, const EquipPrototype*> sacred =
      SymbolsOfKind(state.equips, IsSacredNotGrand);
  for (const Character& character : characters) {
    std::vector<const Equip*> held;
    CollectEquips(character, held);
    arcane_maxed = std::max(arcane_maxed, MaxedSymbols(arcane, held));
    sacred_maxed = std::max(sacred_maxed, MaxedSymbols(sacred, held));
    familiars_maxed =
        std::max(familiars_maxed, MaxedFamiliars(character.familiars()));
    items.insert(items.end(), held.begin(), held.end());
    meso = std::max(meso, character.meso());
    top_level = std::max(top_level, character.level());
    legion.push_back({character.job(), character.level()});
  }

  int stars = 0;
  for (const Equip* item : items) {
    stars = std::max(stars, item->stars());
    if (IsTriplePrime(item->main_potential()) ||
        IsTriplePrime(item->bonus_potential())) {
      standing.met.insert(ACHIEVEMENT_TRIPLE_PRIME);
    }
    if (IsPerfectFlame(*item)) {
      standing.met.insert(ACHIEVEMENT_PERFECT_FLAME);
    }
  }

  // Zero only in a state no save or new game makes, which has no age.
  const int64_t created = account.created_unix_seconds();
  const int64_t days =
      created == 0 ? 0 : (now_unix_seconds - created) / kSecondsPerDay;
  standing.progress = {
      {ACHIEVEMENT_STARS_17, stars},
      {ACHIEVEMENT_STARS_22, stars},
      {ACHIEVEMENT_STARS_25, stars},
      {ACHIEVEMENT_FAMILIARS_MAXED, familiars_maxed},
      {ACHIEVEMENT_LEGION_8000,
       SummarizeLegion(std::move(legion)).legion_level},
      {ACHIEVEMENT_ARCANE_SYMBOLS_MAXED, arcane_maxed},
      {ACHIEVEMENT_SACRED_SYMBOLS_MAXED, sacred_maxed},
      {ACHIEVEMENT_GUILD_SKILLS_MAXED,
       NoblesseSpEarned(account.solo_clears(), state.bosses)},
      {ACHIEVEMENT_MESO_10B, meso},
      {ACHIEVEMENT_MESO_100B, meso},
      {ACHIEVEMENT_MESO_1T, meso},
      {ACHIEVEMENT_LEVEL_300, top_level},
      {ACHIEVEMENT_ACCOUNT_90_DAYS, days},
      {ACHIEVEMENT_ACCOUNT_365_DAYS, days},
      {ACHIEVEMENT_ACCOUNT_3_YEARS, days},
  };
  for (const auto& [achievement, target] : AchievementTargets(state)) {
    // A target of zero is a catalog without the thing counted, which nobody
    // can finish.
    if (target > 0 && standing.progress[achievement] >= target) {
      standing.met.insert(achievement);
    }
  }
  // The passives are the other half of the Guild Skills, and come with the
  // account's level rather than SP.
  if (state.account.max_level() < kGuildSkillsLevel) {
    standing.met.erase(ACHIEVEMENT_GUILD_SKILLS_MAXED);
  }
  return standing;
}

std::vector<Achievement> MetAchievements(const GameState& state,
                                         int64_t now_unix_seconds) {
  const std::set<Achievement> met = StandingOf(state, now_unix_seconds).met;
  std::vector<Achievement> ordered;
  for (const AchievementInfo& info : kAchievements) {
    if (met.count(info.achievement) != 0) {
      ordered.push_back(info.achievement);
    }
  }
  return ordered;
}

std::vector<Achievement> CheckAchievements(GameState& state,
                                           int64_t now_unix_seconds) {
  AchievementStanding standing = StandingOf(state, now_unix_seconds);
  std::vector<Achievement> earned;
  for (const AchievementInfo& info : kAchievements) {
    if (standing.met.count(info.achievement) != 0 &&
        state.account.RecordAchievement(info.achievement, now_unix_seconds)) {
      earned.push_back(info.achievement);
    }
  }
  state.achievement_progress = std::move(standing.progress);
  return earned;
}

}  // namespace ms
