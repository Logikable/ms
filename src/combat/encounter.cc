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

// Whether `skill` is marked with `tag`. Null -- the bare poke -- carries none.
bool HasTag(const Skill* skill, SkillTag tag) {
  if (skill == nullptr) {
    return false;
  }
  for (int i = 0; i < skill->tags_size(); ++i) {
    if (skill->tags(i) == tag) {
      return true;
    }
  }
  return false;
}

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
  // A key-down skill fires at its own rate however fast the weapon swings, so
  // it is handed the stage the formula is the identity at rather than the
  // character's. The game's own pacing still stretches it -- that is about the
  // game running slower than GMS, not about the weapon.
  int stage = attack_speed;
  if (skill != nullptr) {
    attack.name = skill->name();
    attack.max_enemies = std::max(1, skill->max_enemies());
    if (skill->base_delay_ms() > 0) {
      delay_ms = skill->base_delay_ms();
    }
    if (skill->fixed_delay()) {
      stage = kUnscaledAttackSpeedStage;
    }
  }
  attack.swing_seconds = SwingIntervalSeconds(delay_ms, stage) * speed_factor;
  if (skill != nullptr) {
    attack.cooldown_seconds = skill->cooldown_seconds() * speed_factor;
    attack.heal_fraction =
        skill->base().heal_pct() + skill->per_level().heal_pct() * (level - 1);
  }
  OffenseStats offense = OffenseStatsFor(
      proto.job(), proto.level(), proto.allocated_stats(), equipped, weapon,
      skill, level, PassiveOffenseFor(derived));
  for (const CombatType& type : types) {
    attack.damage_per_hit.push_back(ExpectedAttackDamage(offense, *type.mob));
  }
  // Some swings open with a harder hit on a single enemy before spreading --
  // GMS's "strikes one, then detonates in place". Same character, same weapon,
  // the skill's other multiplier: only the target count differs, and that is
  // the fight's business rather than the damage chain's.
  if (skill != nullptr && skill->base().lead_pct() > 0.0) {
    OffenseStats lead = offense;
    lead.skill_pct =
        skill->base().lead_pct() + skill->per_level().lead_pct() * (level - 1);
    lead.lines = std::max(1, skill->lead_lines());
    for (const CombatType& type : types) {
      attack.lead_damage.push_back(ExpectedAttackDamage(lead, *type.mob));
    }
  }
  // Final Attack rides the swing, not the skill: a plain hit worth its own
  // percent, so it starts from the bare stat line and takes neither the skill's
  // multiplier nor its lines. An attack on its own clock strips it back off --
  // see ComputeCombatParams.
  //
  // A source naming a tag follows only the swings carrying it, which is how a
  // fire mage's ignores everything they cast that is not fire. What is left
  // sums: independent procs add in expectation.
  double final_attack_pct = 0.0;
  for (const FinalAttackSource& source : derived.final_attacks) {
    if (source.required_tag == SKILL_TAG_UNSPECIFIED ||
        HasTag(skill, source.required_tag)) {
      final_attack_pct += source.pct;
    }
  }
  if (final_attack_pct > 0.0) {
    OffenseStats final_attack = OffenseStatsFor(
        proto.job(), proto.level(), proto.allocated_stats(), equipped, weapon,
        nullptr, 0, PassiveOffenseFor(derived));
    final_attack.skill_pct = final_attack_pct;
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

// Whether the fight can spend a swing on this skill at all: an attack, or a
// cast with a lever behind it. A cast with nothing we model would take the
// slot and do nothing, so it is not offered.
bool Castable(const Skill& skill) {
  if (DealsDamage(skill.kind())) {
    return true;
  }
  return skill.kind() == SKILL_KIND_ACTIVE && skill.base().heal_pct() > 0.0;
}

// Whether a learned skill is one this character can swing right now.
bool Swingable(const GameState& state, const Skill& skill,
               EquipType weapon_type) {
  if (!Castable(skill)) {
    return false;
  }
  // Another branch's book can share a skill's display name, and learned levels
  // are keyed by that name -- so ask whose book this is before reading a level
  // off it. See CharacterInstance::HasAdvancement.
  if (!state.character.HasAdvancement(skill.job_advancement())) {
    return false;
  }
  // A skill the gear in hand cannot swing is no option, however well learned.
  // The bare poke always is, so the character is never left with nothing to
  // attack with.
  return SkillGearMet(state.character, skill);
}

// A skill's own-clock half, as a skill in its own right, so the same damage
// chain builds it. It keeps the parent's name because it is one skill to the
// player -- one row in the book, one SP ladder, one page -- and carries none of
// the parent's tags: what fires by itself is not the character's swing.
Skill AutoModeSkill(const Skill& skill) {
  Skill mode;
  mode.set_name(skill.name());
  mode.set_kind(SKILL_KIND_AUTO_ATTACK);
  *mode.mutable_base() = skill.auto_mode().base();
  *mode.mutable_per_level() = skill.auto_mode().per_level();
  mode.set_max_enemies(skill.auto_mode().max_enemies());
  mode.set_lines(skill.auto_mode().lines());
  return mode;
}

// Adds the own-clock half of a skill that has one, beside the swing it already
// is. Nothing for the skills that have none, which is all of them but one.
void AddAutoMode(const Character& proto, const EquipStats& equipped,
                 EquipType weapon_type, const Skill& skill, int level,
                 const DerivedStats& derived, double speed_factor,
                 CombatParams& params) {
  if (skill.auto_mode().cast_interval_seconds() <= 0.0) {
    return;
  }
  Skill mode = AutoModeSkill(skill);
  // The stage is not read: what this builds is paced by its own interval, and
  // nothing firing on its own clock answers to how fast the weapon swings.
  AttackOption attack =
      AttackFor(proto, equipped, weapon_type, &mode, level, params.types,
                derived, kUnscaledAttackSpeedStage, speed_factor);
  attack.swing_seconds = 0.0;          // not swung, so never charged
  attack.final_attack_damage.clear();  // Final Attack follows a swing
  attack.interval_seconds =
      skill.auto_mode().cast_interval_seconds() * speed_factor;
  params.auto_attacks.push_back(std::move(attack));
}

// A skill's empowered form, as a skill in its own right, so the same damage
// chain builds it. It takes a name of its own -- unlike an own-clock half,
// this really is a different swing, and it must not pick up the permanent
// bonus its parent hands the ordinary version.
Skill EmpoweredSkill(const Skill& skill, const std::string& target) {
  Skill form;
  form.set_name("Empowered " + target);
  form.set_kind(SKILL_KIND_ATTACK);
  *form.mutable_base() = skill.empowered_form().base();
  *form.mutable_per_level() = skill.empowered_form().per_level();
  form.set_max_enemies(skill.empowered_form().max_enemies());
  form.set_lines(skill.empowered_form().lines());
  return form;
}

// Attaches `skill`'s empowered form to every attack in `into` that it upgrades.
// The form takes the place of the attack it lands for, so it inherits the
// attack's pacing: an animation for a swing, nothing at all for a summon, which
// is paced by the clock the pulse it replaced would have run on.
void AttachEmpoweredForms(const GameState& state, const EquipStats& equipped,
                          EquipType weapon_type, const Skill& skill,
                          int learned, const DerivedStats& derived,
                          int attack_speed, double speed_factor,
                          CombatParams& params,
                          std::vector<AttackOption>& into) {
  // An empty name means the skill upgrades its own attack rather than another
  // skill's -- Creeping Toxin detonating its own pool.
  std::string target = skill.boosts_skill_name().empty()
                           ? skill.name()
                           : skill.boosts_skill_name();
  for (AttackOption& attack : into) {
    if (attack.name != target) {
      continue;
    }
    Skill form = EmpoweredSkill(skill, attack.name);
    std::shared_ptr<AttackOption> swing = std::make_shared<AttackOption>(
        AttackFor(state.character.proto(), equipped, weapon_type, &form,
                  learned, params.types, derived, attack_speed, speed_factor));
    swing->swing_seconds = attack.swing_seconds;
    // Final Attack follows the character's swing, and a summon's pulse is not
    // one -- so a form standing in for a pulse must not carry one either.
    if (attack.interval_seconds > 0.0) {
      swing->final_attack_damage.clear();
    }
    attack.empowered_every = skill.empowered_form().casts_per_trigger();
    attack.empowered = swing;
  }
}

// Attaches every empowered form, once every attack is built. A second pass
// because the skill carrying the form may be a passive naming its target by
// display name, so the target may not have been reached yet.
void AddEmpoweredForms(const GameState& state, const EquipStats& equipped,
                       EquipType weapon_type, const DerivedStats& derived,
                       int attack_speed, double speed_factor,
                       CombatParams& params) {
  int bonus = BonusSkillLevels(state.character, state.skills);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    if (skill.empowered_form().casts_per_trigger() <= 0) {
      continue;
    }
    int learned = EffectiveSkillLevel(state.character, skill, bonus);
    if (learned <= 0) {
      continue;
    }
    AttachEmpoweredForms(state, equipped, weapon_type, skill, learned, derived,
                         attack_speed, speed_factor, params, params.attacks);
    AttachEmpoweredForms(state, equipped, weapon_type, skill, learned, derived,
                         attack_speed, speed_factor, params,
                         params.auto_attacks);
  }
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
  int bonus = BonusSkillLevels(state.character, state.skills);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    int learned = EffectiveSkillLevel(state.character, skill, bonus);
    if (learned <= 0 || !Swingable(state, skill, weapon_type)) {
      continue;
    }
    AddAutoMode(proto, total_stats, weapon_type, skill, learned, derived,
                speed_factor, params);
    AttackOption attack =
        AttackFor(proto, total_stats, weapon_type, &skill, learned,
                  params.types, derived, attack_speed, speed_factor);
    // A cast is not a hit. The damage chain has no multiplier to apply to a
    // skill that deals none, so what it built is the bare poke's damage --
    // which a cast must not land, and which a Final Attack must not follow.
    if (attack.heal_fraction > 0.0) {
      std::fill(attack.damage_per_hit.begin(), attack.damage_per_hit.end(),
                0.0);
      attack.lead_damage.clear();
      attack.final_attack_damage.clear();
    }
    if (skill.kind() != SKILL_KIND_AUTO_ATTACK) {
      // What this swing counts toward the skills clocked by swings landed.
      // Unset is one, which is what an ordinary swing is worth.
      if (skill.hits_per_attack_count() > 1) {
        attack.count_weight = 1.0 / skill.hits_per_attack_count();
      }
      params.attacks.push_back(std::move(attack));
      continue;
    }
    attack.swing_seconds = 0.0;          // not swung, so never charged
    attack.final_attack_damage.clear();  // Final Attack follows a swing
    // Clocked by swings landed rather than by seconds passed.
    if (skill.attacks_per_cast() > 0) {
      attack.attacks_per_cast = skill.attacks_per_cast();
      params.triggered_attacks.push_back(std::move(attack));
      continue;
    }
    // A skill with no clock at all would fire every step, so naming neither is
    // taken as "does not fire" rather than "fires constantly".
    if (skill.cast_interval_seconds() <= 0.0) {
      continue;
    }
    attack.interval_seconds = skill.cast_interval_seconds() * speed_factor;
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
  params.hp_recover_pct = derived.hp_recover_pct;

  // What the character brings to being hit is the same whichever mob is
  // hitting them, so it is resolved once and asked per type.
  DefenseStats defense;
  defense.level = state.character.proto().level();
  defense.def = derived.def;
  defense.damage_taken_pct = derived.damage_taken_pct;
  defense.dodge_chance = derived.dodge_chance;
  AddTypes(state, map_it->second, defense, params);
  if (params.types.empty()) {
    return params;
  }
  AddAttacks(state, derived, weapon.equip_type(), attack_speed, speed_factor,
             params);
  AddEmpoweredForms(state, TotalEquipStats(state.character, derived),
                    weapon.equip_type(), derived, attack_speed, speed_factor,
                    params);
  params.active = true;
  return params;
}

}  // namespace ms
