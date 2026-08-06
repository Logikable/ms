#include "src/combat/encounter.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/progression.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// How often the mob the player is engaged with hits back, in GMS-scale
// seconds, before the game's own pacing stretches it. The tuning knob for how
// dangerous an over-levelled map is: everything else about damage taken is the
// GMS formula, and this is the one number we chose.
//
// Only the one mob swings, however many are on the map. A hit per mob would
// make a crowded beginner map deadlier than a sparse high-level one, which is
// backwards.
constexpr double kMobHitIntervalSeconds = 1.5;

// How much of the player's HP pool a respawn beat gives back. The other half
// of the knob above: together they set how far above their level a map stays
// survivable. A map whose mobs take less than this out of the player between
// beats can be held indefinitely, so a character can get through it by
// enduring it rather than only by killing fast enough to keep clearing it.
constexpr double kBeatHealFraction = 0.10;

// One attack's damage against every mob type on the map. `skill` is null for
// the bare poke, which hits one target for the character's plain 100% swing.
// `equipped` is everything the character wears plus everything their passives
// grant, already summed -- the two are indistinguishable to the damage chain.
AttackOption AttackFor(const Character& proto, const EquipStats& equipped,
                       const Skill* skill, int level,
                       const std::vector<CombatType>& types,
                       const DerivedStats& derived) {
  AttackOption attack;
  if (skill != nullptr) {
    attack.name = skill->name();
    attack.max_enemies = std::max(1, skill->max_enemies());
  }
  PassiveOffense passives{derived.crit_rate, derived.mastery};
  OffenseStats offense =
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      equipped, skill, level, passives);
  for (const CombatType& type : types) {
    attack.damage_per_hit.push_back(ExpectedAttackDamage(offense, *type.mob));
  }
  // Final Attack rides the character's own swing, not the skill's identity:
  // it is a plain hit worth its own percent, so it starts from the bare stat
  // line rather than from `offense` and takes neither its multiplier nor its
  // lines. Callers building an attack that fires on its own clock strip this
  // back off -- see ComputeCombatParams.
  if (derived.final_attack_pct > 0.0) {
    OffenseStats final_attack =
        OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                        equipped, nullptr, 0, passives);
    final_attack.skill_pct = derived.final_attack_pct;
    for (const CombatType& type : types) {
      attack.final_attack_damage.push_back(
          ExpectedAttackDamage(final_attack, *type.mob));
    }
  }
  return attack;
}

// Whether `skill` can be swung with `weapon`. A skill that names no weapon
// type is swingable with anything, which is the usual case.
bool SwingableWith(const Skill& skill, EquipType weapon) {
  if (skill.required_equip_type_size() == 0) {
    return true;
  }
  for (int i = 0; i < skill.required_equip_type_size(); ++i) {
    if (skill.required_equip_type(i) == weapon) {
      return true;
    }
  }
  return false;
}

}  // namespace

CombatParams ComputeCombatParams(const GameState& state) {
  CombatParams params;
  params.map = state.current_map;
  std::map<std::string, MapData>::const_iterator map_it =
      state.maps.find(state.current_map);
  if (map_it == state.maps.end()) {
    return params;
  }
  const MapData& map = map_it->second;

  const std::map<EquipSlot, EquipInstance>& equipped =
      state.character.equipped();
  std::map<EquipSlot, EquipInstance>::const_iterator weapon_it =
      equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (weapon_it == equipped.end()) {
    return params;
  }
  const EquipPrototype& weapon = weapon_it->second.prototype();

  // Passive skills that speed the swing add stages on top of the weapon's own
  // attack speed, capped at the fastest tier we model. Faster swings are a
  // flat DPS gain, so this is resolved once and folded into the swing clock.
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  int attack_speed =
      std::min(static_cast<int>(ATTACK_SPEED_FASTEST_3),
               weapon.attack_speed() + derived.attack_speed_bonus);
  // The pace the whole encounter runs at, and the only thing here that asks
  // the character's level directly: the game stretches out as they climb.
  double speed_factor = GameSpeedFactor(state.character.proto().level());
  params.swing_seconds =
      SwingIntervalSeconds(BaseAttackDelayMs(weapon.equip_type()),
                           attack_speed) *
      speed_factor;
  params.respawn_seconds = kRespawnIntervalSeconds * speed_factor;
  params.hit_seconds = kMobHitIntervalSeconds * speed_factor;
  params.max_player_hp = derived.max_hp;
  params.beat_heal_fraction = kBeatHealFraction;
  // What the character brings to being hit is the same whichever mob is
  // hitting them, so it is resolved once and asked per type below.
  DefenseStats defense;
  defense.level = state.character.proto().level();
  defense.def = derived.def;
  defense.damage_taken_pct = derived.damage_taken_pct;
  for (const MapData::Spawn& spawn : map.spawns()) {
    std::map<std::string, Mob>::const_iterator mob_it =
        state.mobs.find(spawn.mob());
    if (mob_it == state.mobs.end()) {
      continue;
    }
    CombatType type;
    type.mob = &mob_it->second;
    type.simultaneous = spawn.count();
    type.damage_to_player = ExpectedDamageTaken(defense, *type.mob);
    params.types.push_back(std::move(type));
  }
  if (params.types.empty()) {
    return params;
  }

  // Every attack the character could swing with: the bare poke first, then one
  // per learned attack skill. The fight picks between them each swing. Learned
  // passives (crit rate and the like) apply to whichever attack is chosen, so
  // the already-resolved `derived` is handed to each option.
  const Character& proto = state.character.proto();
  const EquipStats total_stats = TotalEquipStats(state.character, derived);
  params.attacks.push_back(
      AttackFor(proto, total_stats, nullptr, 0, params.types, derived));
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    if (!DealsDamage(skill.kind())) {
      continue;
    }
    // Another branch's book can share a skill's display name, and learned
    // levels are keyed by that name -- so ask whose book this is before
    // reading a level off it. See CharacterInstance::HasAdvancement.
    if (!state.character.HasAdvancement(skill.job_advancement())) {
      continue;
    }
    // A skill the weapon in hand cannot swing is not an option, however well
    // learned. The bare poke always is, so the character is never left with
    // nothing to attack with.
    if (!SwingableWith(skill, weapon.equip_type())) {
      continue;
    }
    int learned = state.character.skill_level(skill);
    if (learned <= 0) {
      continue;
    }
    AttackOption attack =
        AttackFor(proto, total_stats, &skill, learned, params.types, derived);
    if (skill.kind() == SKILL_KIND_AUTO_ATTACK) {
      // A skill with no interval would fire every step, so an unset one is
      // taken as "does not fire" rather than "fires constantly".
      if (skill.cast_interval_seconds() <= 0.0) {
        continue;
      }
      attack.interval_seconds = skill.cast_interval_seconds() * speed_factor;
      // Final Attack follows a swing, and this is not one.
      attack.final_attack_damage.clear();
      params.auto_attacks.push_back(std::move(attack));
    } else {
      params.attacks.push_back(std::move(attack));
    }
  }
  params.active = true;
  return params;
}

}  // namespace ms
