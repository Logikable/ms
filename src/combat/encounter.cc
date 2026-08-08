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
                       EquipType weapon, const Skill* skill, int level,
                       const std::vector<CombatType>& types,
                       const DerivedStats& derived, int attack_speed,
                       double speed_factor) {
  AttackOption attack;
  int delay_ms = kDefaultSwingDelayMs;
  if (skill != nullptr) {
    attack.name = skill->name();
    attack.max_enemies = std::max(1, skill->max_enemies());
    if (skill->base_delay_ms() > 0) {
      delay_ms = skill->base_delay_ms();
    }
  }
  attack.swing_seconds =
      SwingIntervalSeconds(delay_ms, attack_speed) * speed_factor;
  if (skill != nullptr) {
    attack.cooldown_seconds = skill->cooldown_seconds() * speed_factor;
  }
  OffenseStats offense = OffenseStatsFor(
      proto.job(), proto.level(), proto.allocated_stats(), equipped, weapon,
      skill, level, PassiveOffenseFor(derived));
  for (const CombatType& type : types) {
    attack.damage_per_hit.push_back(ExpectedAttackDamage(offense, *type.mob));
  }
  // Final Attack rides the character's own swing, not the skill's identity:
  // it is a plain hit worth its own percent, so it starts from the bare stat
  // line rather than from `offense` and takes neither its multiplier nor its
  // lines. Callers building an attack that fires on its own clock strip this
  // back off -- see ComputeCombatParams.
  if (derived.final_attack_pct > 0.0) {
    OffenseStats final_attack = OffenseStatsFor(
        proto.job(), proto.level(), proto.allocated_stats(), equipped, weapon,
        nullptr, 0, PassiveOffenseFor(derived));
    final_attack.skill_pct = derived.final_attack_pct;
    for (const CombatType& type : types) {
      attack.final_attack_damage.push_back(
          ExpectedAttackDamage(final_attack, *type.mob));
    }
  }
  return attack;
}

// The mob types this map spawns, each with what one of its hits costs the
// player. Types the mob catalog does not know are skipped.
void AddTypes(const GameState& state, const MapData& map,
              const DefenseStats& defense, CombatParams& params) {
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
}

// Whether a learned attack skill is one this character can swing right now.
bool Swingable(const GameState& state, const Skill& skill,
               EquipType weapon_type) {
  if (!DealsDamage(skill.kind())) {
    return false;
  }
  // Another branch's book can share a skill's display name, and learned levels
  // are keyed by that name -- so ask whose book this is before reading a level
  // off it. See CharacterInstance::HasAdvancement.
  if (!state.character.HasAdvancement(skill.job_advancement())) {
    return false;
  }
  // A skill the weapon in hand cannot swing is no option, however well
  // learned. The bare poke always is, so the character is never left with
  // nothing to attack with.
  return SkillAllowsWeapon(skill, weapon_type);
}

// Every attack the character could swing: the bare poke first, then one per
// learned attack skill, for the fight to pick between each swing. Skills that
// fire on their own clock go to auto_attacks instead.
//
// Learned passives apply to whichever attack is chosen, so the already
// resolved `derived` is handed to each option.
void AddAttacks(const GameState& state, const DerivedStats& derived,
                EquipType weapon_type, int attack_speed, double speed_factor,
                CombatParams& params) {
  const Character& proto = state.character.proto();
  const EquipStats total_stats = TotalEquipStats(state.character, derived);
  params.attacks.push_back(AttackFor(proto, total_stats, weapon_type, nullptr,
                                     0, params.types, derived, attack_speed,
                                     speed_factor));
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    int learned = state.character.skill_level(skill);
    if (learned <= 0 || !Swingable(state, skill, weapon_type)) {
      continue;
    }
    AttackOption attack =
        AttackFor(proto, total_stats, weapon_type, &skill, learned,
                  params.types, derived, attack_speed, speed_factor);
    if (skill.kind() != SKILL_KIND_AUTO_ATTACK) {
      params.attacks.push_back(std::move(attack));
      continue;
    }
    // A skill with no interval would fire every step, so an unset one is taken
    // as "does not fire" rather than "fires constantly".
    if (skill.cast_interval_seconds() <= 0.0) {
      continue;
    }
    attack.interval_seconds = skill.cast_interval_seconds() * speed_factor;
    attack.swing_seconds = 0.0;          // not swung, so never charged
    attack.final_attack_damage.clear();  // Final Attack follows a swing
    params.auto_attacks.push_back(std::move(attack));
  }
}

}  // namespace

CombatParams ComputeCombatParams(const GameState& state) {
  CombatParams params;
  params.map = state.current_map;
  std::map<std::string, MapData>::const_iterator map_it =
      state.maps.find(state.current_map);
  const std::map<EquipSlot, EquipInstance>& equipped =
      state.character.equipped();
  std::map<EquipSlot, EquipInstance>::const_iterator weapon_it =
      equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  if (map_it == state.maps.end() || weapon_it == equipped.end()) {
    return params;
  }
  const EquipPrototype& weapon = weapon_it->second.prototype();

  // Passives that speed the swing add stages on top of the weapon's own, up to
  // the fastest tier we model.
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  int attack_speed =
      std::min(static_cast<int>(ATTACK_SPEED_FASTEST_3),
               weapon.attack_speed() + derived.attack_speed_bonus);
  // The pace the whole encounter runs at, and the only thing here that asks
  // the character's level directly: the game stretches out as they climb.
  double speed_factor = GameSpeedFactor(state.character.proto().level());
  params.respawn_seconds = kRespawnIntervalSeconds * speed_factor;
  params.hit_seconds = kMobHitIntervalSeconds * speed_factor;
  params.max_player_hp = derived.max_hp;
  params.beat_heal_fraction = kBeatHealFraction;
  params.damage_reflect_pct = derived.damage_reflect_pct;

  // What the character brings to being hit is the same whichever mob is
  // hitting them, so it is resolved once and asked per type.
  DefenseStats defense;
  defense.level = state.character.proto().level();
  defense.def = derived.def;
  defense.damage_taken_pct = derived.damage_taken_pct;
  AddTypes(state, map_it->second, defense, params);
  if (params.types.empty()) {
    return params;
  }
  AddAttacks(state, derived, weapon.equip_type(), attack_speed, speed_factor,
             params);
  params.active = true;
  return params;
}

}  // namespace ms
