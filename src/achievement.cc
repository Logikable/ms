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
    {ACHIEVEMENT_STARS_17, "Starstruck", "Star an item to 17 stars."},
    {ACHIEVEMENT_STARS_22, "Superstar", "Star an item to 22 stars."},
    {ACHIEVEMENT_STARS_25, "Supernova", "Star an item to 25 stars."},
    {ACHIEVEMENT_TRIPLE_PRIME, "Triple Prime",
     "Roll three Legendary lines of one stat on a potential."},
    {ACHIEVEMENT_PERFECT_FLAME, "Perfect Flame",
     "Flame an item to four tier 7 lines, all useful to one job."},
    {ACHIEVEMENT_FAMILIARS_MAXED, "Kennel Master",
     "Raise every familiar to level 4 on one character."},
    {ACHIEVEMENT_LEGION_8000, "Legion 8000", "Reach Legion level 8,000."},
    {ACHIEVEMENT_ARCANE_SYMBOLS_MAXED, "Arcane Mastery",
     "Max every Arcane Symbol on one character."},
    {ACHIEVEMENT_SACRED_SYMBOLS_MAXED, "Sacred Mastery",
     "Max every Sacred Symbol on one character."},
    {ACHIEVEMENT_GUILD_SKILLS_MAXED, "Guild Grandmaster",
     "Earn every level of every Guild Skill."},
    {ACHIEVEMENT_MESO_10B, "Ten Billion", "Hold 10,000,000,000 meso."},
    {ACHIEVEMENT_MESO_100B, "Hundred Billion", "Hold 100,000,000,000 meso."},
    {ACHIEVEMENT_MESO_1T, "Trillionaire", "Hold 1,000,000,000,000 meso."},
    {ACHIEVEMENT_LEVEL_300, "Level 300", "Reach level 300."},
    {ACHIEVEMENT_ACCOUNT_90_DAYS, "Three Months",
     "Keep an account for 90 days."},
    {ACHIEVEMENT_ACCOUNT_365_DAYS, "One Year", "Keep an account for 365 days."},
    {ACHIEVEMENT_ACCOUNT_3_YEARS, "Three Years",
     "Keep an account for three years."},
};

constexpr int64_t kSecondsPerDay = 24 * 60 * 60;
// Three years with one leap day.
constexpr int64_t kThreeYearsDays = 3 * 365 + 1;
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

// Whether `held` has every one of `symbols`, each at its max level.
bool AllSymbolsMaxed(
    const std::map<std::string, const EquipPrototype*>& symbols,
    const std::vector<const Equip*>& held) {
  std::set<std::string> maxed;
  for (const Equip* item : held) {
    auto found = symbols.find(item->equip_name());
    if (found != symbols.end() && SymbolMaxed(*found->second, *item)) {
      maxed.insert(found->first);
    }
  }
  return !symbols.empty() && maxed.size() == symbols.size();
}

bool AllFamiliarsMaxed(const FamiliarBook& book) {
  for (const FamiliarSpecies& species : FamiliarRoster()) {
    if (FamiliarLevel(book, species.name) < kFamiliarMaxLevel) {
      return false;
    }
  }
  return true;
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

std::vector<Achievement> MetAchievements(const GameState& state,
                                         int64_t now_unix_seconds) {
  std::vector<Character> characters;
  characters.reserve(state.inactive_characters.size() + 1);
  characters.push_back(state.character.ToProto());
  for (const CharacterSave& slot : state.inactive_characters) {
    characters.push_back(slot.character());
  }
  const Account account = state.account.ToProto();

  std::set<Achievement> met;
  auto meet = [&met](bool condition, Achievement achievement) {
    if (condition) {
      met.insert(achievement);
    }
  };

  std::vector<const Equip*> items;
  CollectEquips(account.bank(), items);
  // One purse at a time, not their sum: a character's or the bank's.
  int64_t meso = account.bank().meso();
  int top_level = 0;
  std::vector<LegionMember> legion;
  const std::map<std::string, const EquipPrototype*> arcane =
      SymbolsOfKind(state.equips, IsArcaneSymbol);
  const std::map<std::string, const EquipPrototype*> sacred =
      SymbolsOfKind(state.equips, [](const EquipPrototype& proto) {
        return IsSacredSymbol(proto) && !IsGrandSacredSymbol(proto);
      });
  for (const Character& character : characters) {
    std::vector<const Equip*> held;
    CollectEquips(character, held);
    meet(AllSymbolsMaxed(arcane, held), ACHIEVEMENT_ARCANE_SYMBOLS_MAXED);
    meet(AllSymbolsMaxed(sacred, held), ACHIEVEMENT_SACRED_SYMBOLS_MAXED);
    meet(AllFamiliarsMaxed(character.familiars()), ACHIEVEMENT_FAMILIARS_MAXED);
    items.insert(items.end(), held.begin(), held.end());
    meso = std::max(meso, character.meso());
    top_level = std::max(top_level, character.level());
    legion.push_back({character.job(), character.level()});
  }

  for (const Equip* item : items) {
    meet(item->stars() >= 17, ACHIEVEMENT_STARS_17);
    meet(item->stars() >= 22, ACHIEVEMENT_STARS_22);
    meet(item->stars() >= 25, ACHIEVEMENT_STARS_25);
    meet(IsTriplePrime(item->main_potential()) ||
             IsTriplePrime(item->bonus_potential()),
         ACHIEVEMENT_TRIPLE_PRIME);
    meet(IsPerfectFlame(*item), ACHIEVEMENT_PERFECT_FLAME);
  }

  meet(SummarizeLegion(std::move(legion)).legion_level >=
           kAchievementLegionLevel,
       ACHIEVEMENT_LEGION_8000);
  meet(state.account.max_level() >= kGuildSkillsLevel &&
           NoblesseSpEarned(account.solo_clears(), state.bosses) >=
               NoblesseSpToMax(state.skills),
       ACHIEVEMENT_GUILD_SKILLS_MAXED);
  meet(meso >= 10'000'000'000, ACHIEVEMENT_MESO_10B);
  meet(meso >= 100'000'000'000, ACHIEVEMENT_MESO_100B);
  meet(meso >= 1'000'000'000'000, ACHIEVEMENT_MESO_1T);
  meet(top_level >= kAchievementLevel, ACHIEVEMENT_LEVEL_300);

  // Zero only in a state no save or new game makes, which has no age.
  const int64_t created = account.created_unix_seconds();
  const int64_t age = created == 0 ? 0 : now_unix_seconds - created;
  meet(age >= 90 * kSecondsPerDay, ACHIEVEMENT_ACCOUNT_90_DAYS);
  meet(age >= 365 * kSecondsPerDay, ACHIEVEMENT_ACCOUNT_365_DAYS);
  meet(age >= kThreeYearsDays * kSecondsPerDay, ACHIEVEMENT_ACCOUNT_3_YEARS);

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
  std::vector<Achievement> earned;
  for (Achievement achievement : MetAchievements(state, now_unix_seconds)) {
    if (state.account.RecordAchievement(achievement, now_unix_seconds)) {
      earned.push_back(achievement);
    }
  }
  return earned;
}

}  // namespace ms
