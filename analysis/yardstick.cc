#include "analysis/yardstick.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "analysis/sim_boss.h"
#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/combat/encounter.h"
#include "src/combat/measure.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"

namespace ms {
namespace {

// The monster a character with no fight to aim at is ranked against: one of
// their own level, with no defence and no boss flag. Nothing they buy is
// wasted against it -- the point is only that damage still orders the shelf.
Mob StandIn(int level) {
  Mob mob;
  mob.set_name("Yardstick");
  mob.set_level(level);
  return mob;
}

// The body of `difficulty`'s objective phase: the part the fight is decided
// against, and so the part a purchase should be judged on.
Mob ObjectiveBody(const GameState& state, const BossDifficulty& difficulty) {
  int phase = BossObjectivePhase(state.mobs, difficulty);
  const Mob* toughest = nullptr;
  for (const Spawn& spawn : difficulty.phases(phase).spawns()) {
    std::map<std::string, Mob>::const_iterator it =
        state.mobs.find(spawn.mob());
    if (it == state.mobs.end()) {
      continue;
    }
    if (toughest == nullptr || it->second.max_hp() > toughest->max_hp()) {
      toughest = &it->second;
    }
  }
  return toughest == nullptr ? StandIn(state.character.proto().level())
                             : *toughest;
}

// The params of the fight the plan is aimed at, and how many bodies stand in
// it. The BOSS's where there is a fight to aim at, not the map's: a boss reads
// the bossing preset, halves reach, and is picked a different swing than a
// crowd.
CombatParams AimedParams(const GameState& state, int* enemies) {
  std::pair<std::string, int> fight;
  CombatParams params;
  *enemies = 1;
  if (AimedFight(state, &fight) &&
      state.bosses.find(fight.first) != state.bosses.end()) {
    const BossDifficulty& difficulty =
        state.bosses.at(fight.first).difficulties(fight.second);
    params = ComputeBossParams(state, fight.first, difficulty,
                               BossObjectivePhase(state.mobs, difficulty));
  }
  if (params.active) {
    return params;
  }
  params = ComputeCombatParams(state);
  int crowd = 0;
  for (const CombatType& type : params.types) {
    crowd += type.simultaneous;
  }
  *enemies = std::max(1, crowd);
  return params;
}

// How long the fight is played out for, in the stretched clock MeasureFight
// counts in. Wide enough to hold the slowest cycle in the character's book
// twice over: a window shorter than a cooldown sees the skill either always up
// or never, and the whole point of playing the fight is to find out which
// share of it each swing really had.
double ProfileWindow(const GameState& state) {
  constexpr double kFloorSeconds = 30.0;
  double cycle = 0.0;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (state.character.HasBookFor(entry.second)) {
      cycle = std::max(cycle, entry.second.cooldown_seconds());
    }
  }
  return std::max(kFloorSeconds, 2.0 * cycle) *
         GameSpeedFactor(state.character.proto().level());
}

// The catalog entry an AttackOption came from, by the name it carries: what
// the damage chain wants is the skill's own data, which the option has spent.
const Skill* SkillNamed(const GameState& state, const std::string& name) {
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (entry.second.name() == name) {
      return &entry.second;
    }
  }
  return nullptr;
}

// The least of the fight a swing must account for to be carried as a strand of
// its own. Every candidate is scored through every strand, a cost paid
// thousands of times a pass, and a swing worth a hundredth of the fight cannot
// reorder anything. What is dropped falls into the proportional credit the
// own-clock damage takes.
constexpr double kStrandFloor = 0.01;

// Everything the character really does to `target`, and how often. Each
// strand's rate is SOLVED rather than counted -- what the played fight saw the
// swing land, over what the closed form says one landing is worth -- which is
// what carries the clock into a form a candidate can be re-scored through.
//
// What runs on a clock of its own is credited across the strands in
// proportion: it scales with the character rather than any one swing, so
// crediting it to the main attack would flatter that attack.
std::vector<Strand> StrandsFor(const GameState& state, const Mob& target) {
  int enemies = 1;
  CombatParams params = AimedParams(state, &enemies);
  std::vector<Strand> strands;
  if (!params.active || params.attacks.empty()) {
    return strands;
  }
  Sequence played = MeasureFight(params, ProfileWindow(state), enemies);
  if (played.seconds <= 0.0 || played.damage <= 0.0) {
    return strands;
  }
  const Character& proto = state.character.proto();
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  EquipStats worn = TotalEquipStats(state.character, derived);
  PassiveOffense passives = PassiveOffenseFor(derived);
  double swung = 0.0;
  for (int i = 0; i < static_cast<int>(played.damage_by_attack.size()); ++i) {
    if (played.damage_by_attack[i] < kStrandFloor * played.damage ||
        i >= static_cast<int>(params.attacks.size())) {
      continue;
    }
    const Skill* skill = SkillNamed(state, params.attacks[i].name);
    if (skill == nullptr) {
      continue;
    }
    Strand strand;
    strand.swing = skill;
    strand.level = state.character.skill_level(*skill);
    double each = ExpectedAttackDamage(
        OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                        worn, state.character.weapon_type(), skill,
                        strand.level, passives),
        target);
    if (each <= 0.0) {
      continue;
    }
    strand.per_second = played.damage_by_attack[i] / (played.seconds * each);
    swung += played.damage_by_attack[i];
    strands.push_back(strand);
  }
  if (swung <= 0.0) {
    return strands;
  }
  // The own-clock share, spread over what was swung.
  double all = played.damage / swung;
  for (Strand& strand : strands) {
    strand.per_second *= all;
  }
  return strands;
}

// What a held yardstick is re-taken on: the kit, and what it is aimed at.
std::string KitKey(const GameState& state) {
  std::string key;
  const Character& proto = state.character.proto();
  absl::StrAppend(&key, proto.job(), "/", proto.level(), "/", state.current_map,
                  "\n");
  std::pair<std::string, int> fight;
  AimedFight(state, &fight);
  absl::StrAppend(&key, fight.first, "/", fight.second, "\n");
  for (const std::pair<const EquipSlot, const EquipInstance*>& item :
       state.character.equipped()) {
    absl::StrAppend(&key, item.second->name(), "\n");
  }
  for (const std::pair<const std::string, int32_t>& learned :
       proto.skill_levels()) {
    absl::StrAppend(&key, learned.first, "=", learned.second, "\n");
  }
  return key;
}

}  // namespace

Yardstick YardstickFor(const GameState& state) {
  Yardstick yard;
  std::pair<std::string, int> fight;
  if (AimedFight(state, &fight)) {
    std::map<std::string, Boss>::const_iterator boss =
        state.bosses.find(fight.first);
    if (boss != state.bosses.end()) {
      yard.target =
          ObjectiveBody(state, boss->second.difficulties(fight.second));
    }
  }
  if (yard.target.name().empty()) {
    yard.target = StandIn(state.character.proto().level());
  }
  yard.strands = StrandsFor(state, yard.target);
  return yard;
}

double WorthOf(const GameState& state, const Yardstick& yard,
               const EquipStats& stats, const PassiveOffense& passives) {
  const Character& proto = state.character.proto();
  double rate = 0.0;
  for (const Strand& strand : yard.strands) {
    rate +=
        strand.per_second *
        ExpectedAttackDamage(
            OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                            stats, state.character.weapon_type(), strand.swing,
                            strand.level, passives),
            yard.target);
  }
  return rate;
}

const Yardstick& HeldYardstick::For(const GameState& state) {
  std::string key = KitKey(state);
  if (!taken_ || key != key_) {
    held_ = YardstickFor(state);
    key_ = std::move(key);
    taken_ = true;
  }
  return held_;
}

}  // namespace ms
