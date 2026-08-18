#include "src/combat/encounter.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/span.h"
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

// Takes the meso-drop sources back out of a copy of the character's passives.
// What throws a meso is the character swinging, so a pulse on a clock of its
// own carries none however the passive reads.
void StripMesoDrops(DerivedStats& derived) {
  std::vector<FinalAttackSource> kept;
  for (const FinalAttackSource& source : derived.final_attacks) {
    if (!source.per_line) {
      kept.push_back(source);
    }
  }
  derived.final_attacks = std::move(kept);
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
    attack.cooldown_seconds = CooldownAt(*skill, level) * speed_factor;
    attack.heal_fraction =
        skill->base().heal_pct() + skill->per_level().heal_pct() * (level - 1);
  }
  OffenseStats offense = OffenseStatsFor(
      proto.job(), proto.level(), proto.allocated_stats(), equipped, weapon,
      skill, level, PassiveOffenseFor(derived));
  for (const CombatType& type : types) {
    attack.damage_per_hit.push_back(ExpectedAttackDamage(offense, *type.mob));
  }
  attack.groups.push_back({attack.damage_per_hit, RollsFor(offense)});
  if (skill != nullptr) {
    attack.pierce_gain_pct = skill->pierce_gain_pct();
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
    // The shadow copies the opening hit as it copies every other line of the
    // swing -- it is the same swing, landed on one enemy instead of all of
    // them. Reset here because lead.lines just changed under it.
    lead.mirror_lines = lead.lines;
    for (const CombatType& type : types) {
      attack.lead_damage.push_back(ExpectedAttackDamage(lead, *type.mob));
    }
    attack.lead_rolls = RollsFor(lead);
  }
  // A swing that lands two hits at once: the hammer, and the brand it leaves
  // exploding. Same character, same weapon, same reach -- what differs is the
  // multiplier and what it adds against an ordinary monster, so each half is
  // priced on its own and the two are summed into the one swing.
  if (skill != nullptr) {
    for (const SwingHit& hit : skill->extra_hit()) {
      OffenseStats extra = offense;
      extra.skill_pct =
          hit.base().skill_pct() + hit.per_level().skill_pct() * (level - 1);
      extra.normal_skill_pct = hit.base().normal_skill_pct() +
                               hit.per_level().normal_skill_pct() * (level - 1);
      // A hit that crits harder than the rest of the swing. Added to what the
      // character brought rather than replacing it, so 1.00 is certainty
      // whatever they have bought -- see SwingHit.
      extra.crit_rate +=
          hit.base().crit_rate() + hit.per_level().crit_rate() * (level - 1);
      extra.lines = std::max(1, hit.lines());
      // The shadow copies it as it copies the rest of the swing. Reset here
      // because the line count just changed under it.
      extra.mirror_lines = extra.lines;
      HitGroup group;
      group.rolls = RollsFor(extra);
      for (std::size_t i = 0; i < types.size(); ++i) {
        group.damage.push_back(ExpectedAttackDamage(extra, *types[i].mob));
        attack.damage_per_hit[i] += group.damage.back();
      }
      attack.groups.push_back(std::move(group));
    }
  }
  // Final Attack rides the swing, not the skill: a plain hit worth its own
  // percent, so it starts from the bare stat line and takes neither the skill's
  // multiplier nor its lines. An attack on its own clock strips it back off --
  // see ComputeCombatParams.
  //
  // A source naming a tag follows only the swings carrying it, which is how a
  // fire mage's ignores everything they cast that is not fire. Every source
  // that survives keeps its own entry, since each rolls on its own.
  //
  // A source rolling per line rolls the swing's line count of times: four
  // lines knock four mesos loose where a Final Attack rolls once. The shadow's
  // copies are not the character's lines and do not count.
  int swing_lines = skill != nullptr ? SkillLinesAt(*skill, level) : 1;
  OffenseStats follow =
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      equipped, weapon, nullptr, 0, PassiveOffenseFor(derived));
  // The shadow mimics the swing, and this is what the swing set off rather
  // than the swing. Same line the skill's own multiplier and lines are already
  // dropped on, two comments up.
  follow.mirror_lines = 0;
  attack.final_attack_damage.assign(types.size(), 0.0);
  for (const FinalAttackSource& source : derived.final_attacks) {
    if (source.required_tag != SKILL_TAG_UNSPECIFIED &&
        !HasTag(skill, source.required_tag)) {
      continue;
    }
    FinalAttackRoll roll;
    roll.chance = source.chance;
    roll.count = source.per_line ? swing_lines : 1;
    follow.skill_pct = source.damage_pct;
    roll.rolls = RollsFor(follow);
    for (std::size_t i = 0; i < types.size(); ++i) {
      roll.damage.push_back(ExpectedAttackDamage(follow, *types[i].mob));
      attack.final_attack_damage[i] +=
          roll.damage.back() * roll.chance * roll.count;
    }
    attack.final_attack_rolls.push_back(std::move(roll));
  }
  if (attack.final_attack_rolls.empty()) {
    attack.final_attack_damage.clear();
    attack.final_attack_rolls.clear();
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

// One of a skill's own-clock halves, as a skill in its own right, so the same
// damage chain builds it. It keeps the parent's name because it is one skill to
// the player -- one row in the book, one SP ladder, one page -- and carries
// none of the parent's tags: what fires by itself is not the character's swing.
Skill AutoModeSkill(const Skill& skill, const AutoMode& mode) {
  Skill built;
  built.set_name(skill.name());
  built.set_kind(SKILL_KIND_AUTO_ATTACK);
  *built.mutable_base() = mode.base();
  *built.mutable_per_level() = mode.per_level();
  built.set_max_enemies(mode.max_enemies());
  built.set_lines(mode.lines());
  return built;
}

// The wound a skill's buff bleeds, as a skill in its own right. It reaches
// what the swing reached, being the mark that swing left, and carries none of
// the parent's tags for the reason AutoModeSkill gives.
Skill BuffPulseSkill(const Skill& skill, const BuffPulse& pulse) {
  Skill built;
  built.set_name(skill.name());
  built.set_kind(SKILL_KIND_AUTO_ATTACK);
  *built.mutable_base() = pulse.base();
  *built.mutable_per_level() = pulse.per_level();
  built.set_max_enemies(skill.max_enemies());
  built.set_lines(pulse.lines());
  return built;
}

// Adds every own-clock half of a skill that has any, beside the swing it
// already is. Nothing for the skills that have none, which is most of them.
void AddAutoModes(const Character& proto, const EquipStats& equipped,
                  EquipType weapon_type, const Skill& skill, int level,
                  const DerivedStats& derived, double speed_factor,
                  const std::vector<CombatType>& types, AttackSet& set) {
  for (const AutoMode& mode : skill.auto_mode()) {
    if (mode.cast_interval_seconds() <= 0.0) {
      continue;
    }
    Skill built = AutoModeSkill(skill, mode);
    // The stage is not read: what this builds is paced by its own interval, and
    // nothing firing on its own clock answers to how fast the weapon swings.
    AttackOption attack =
        AttackFor(proto, equipped, weapon_type, &built, level, types, derived,
                  kUnscaledAttackSpeedStage, speed_factor);
    attack.swing_seconds = 0.0;  // not swung, so never charged
    attack.final_attack_damage.clear();
    attack.final_attack_rolls.clear();  // Final Attack follows a swing
    attack.interval_seconds = mode.cast_interval_seconds() * speed_factor;
    set.auto_attacks.push_back(std::move(attack));
  }
  const BuffPulse& pulse = skill.buff().pulse();
  if (pulse.cast_interval_seconds() <= 0.0) {
    return;
  }
  Skill bleed = BuffPulseSkill(skill, pulse);
  AttackOption wound =
      AttackFor(proto, equipped, weapon_type, &bleed, level, types, derived,
                kUnscaledAttackSpeedStage, speed_factor);
  wound.swing_seconds = 0.0;
  wound.final_attack_damage.clear();
  wound.final_attack_rolls.clear();
  wound.interval_seconds = pulse.cast_interval_seconds() * speed_factor;
  set.auto_attacks.push_back(std::move(wound));
}

// What the rest of the book hands one skill: strikes added to every swing,
// enemies added to its reach, and the clock it fires on.
struct SkillBoosts {
  int lines = 0;
  int max_enemies = 0;
  int attacks_per_cast = 0;
};

// Every such grant in the character's book, summed and keyed by the skill it
// names. Gathered once: the granting skill may be listed after the skill it
// strengthens, and every attack has to be built with the whole of it already
// in.
std::map<std::string, SkillBoosts> BoostsByTarget(const GameState& state,
                                                  int bonus) {
  // The nudge SkillLinesAt takes, for the same reason: a rate written as a
  // decimal lands a hair under the level it is meant to buy.
  constexpr double kEnemyEpsilon = 1e-9;
  std::map<std::string, SkillBoosts> by_target;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    // Learned levels are keyed by display name and the warrior branches share
    // several, so only the character's own book grants anything.
    if (!state.character.HasAdvancement(skill.job_advancement())) {
      continue;
    }
    int learned = EffectiveSkillLevel(state.character, skill, bonus);
    if (learned <= 0) {
      continue;
    }
    for (const SkillBoost& boost : skill.boost()) {
      SkillBoosts& into = by_target[boost.skill_name()];
      into.lines += boost.lines();
      into.max_enemies +=
          boost.max_enemies() +
          static_cast<int>(std::floor(
              boost.max_enemies_per_level() * (learned - 1) + kEnemyEpsilon));
      // The clock replaces rather than sums, so the faster of two stands.
      if (boost.attacks_per_cast() > 0 &&
          (into.attacks_per_cast == 0 ||
           boost.attacks_per_cast() < into.attacks_per_cast)) {
        into.attacks_per_cast = boost.attacks_per_cast();
      }
    }
  }
  return by_target;
}

// `skill` with whatever the book grants it folded in, or `skill` itself when
// nothing does. The line ladder is cashed in at `level` on the way, so the
// strike granted lands on top of the ones the skill bought for itself rather
// than being climbed past a second time.
const Skill& Boosted(const Skill& skill, int level,
                     const std::map<std::string, SkillBoosts>& boosts,
                     Skill& scratch) {
  std::map<std::string, SkillBoosts>::const_iterator it =
      boosts.find(skill.name());
  if (it == boosts.end()) {
    return skill;
  }
  scratch = skill;
  scratch.set_lines(SkillLinesAt(skill, level) + it->second.lines);
  scratch.clear_lines_per_level();
  scratch.set_max_enemies(std::max(1, skill.max_enemies()) +
                          it->second.max_enemies);
  if (it->second.attacks_per_cast > 0) {
    scratch.set_attacks_per_cast(it->second.attacks_per_cast);
  }
  return scratch;
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
  // The form is the same arrow, further upgraded: it gains as it travels the
  // same way, over the further enemies it reaches.
  form.set_pierce_gain_pct(skill.pierce_gain_pct());
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
                          const std::vector<CombatType>& types,
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
                  learned, types, derived, attack_speed, speed_factor));
    swing->swing_seconds = attack.swing_seconds;
    // Final Attack follows the character's swing, and a summon's pulse is not
    // one -- so a form standing in for a pulse must not carry one either.
    if (attack.interval_seconds > 0.0) {
      swing->final_attack_damage.clear();
      swing->final_attack_rolls.clear();
    }
    attack.empowered_every = skill.empowered_form().casts_per_trigger();
    attack.brands_enemies = skill.empowered_form().brands_each_enemy();
    attack.empowered = swing;
  }
}

// Attaches every empowered form, once every attack is built. A second pass
// because the skill carrying the form may be a passive naming its target by
// display name, so the target may not have been reached yet.
void AddEmpoweredForms(const GameState& state, const EquipStats& equipped,
                       EquipType weapon_type, const DerivedStats& derived,
                       int attack_speed, double speed_factor,
                       const std::vector<CombatType>& types, AttackSet& set) {
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
                         attack_speed, speed_factor, types, set.attacks);
    // A form standing in for a pulse is a pulse: no shadow and no mesos, for
    // the reason AddAttacks gives.
    DerivedStats off_clock = derived;
    off_clock.mirror_line_pct = 0.0;
    StripMesoDrops(off_clock);
    AttachEmpoweredForms(state, equipped, weapon_type, skill, learned,
                         off_clock, attack_speed, speed_factor, types,
                         set.auto_attacks);
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
                const std::vector<CombatType>& types, AttackSet& set) {
  const Character& proto = state.character.proto();
  const EquipStats total_stats = TotalEquipStats(state.character, derived);
  // What a skill on its own clock is not: the character's own swing. It gets
  // no shadow copying it and knocks no mesos loose, both for the reason Final
  // Attack is stripped off it in AttackFor -- the character did not swing it.
  DerivedStats off_clock = derived;
  off_clock.mirror_line_pct = 0.0;
  StripMesoDrops(off_clock);
  set.attacks.push_back(AttackFor(proto, total_stats, weapon_type, nullptr, 0,
                                  types, derived, attack_speed, speed_factor));
  int bonus = BonusSkillLevels(state.character, state.skills);
  std::map<std::string, SkillBoosts> boosts = BoostsByTarget(state, bonus);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    int learned = EffectiveSkillLevel(state.character, skill, bonus);
    if (learned <= 0 || !Swingable(state, skill, weapon_type)) {
      continue;
    }
    // Strikes and reach another skill in the book grants this one, folded in
    // before anything is built so the whole chain below sees one skill.
    Skill boosted;
    const Skill& swung = Boosted(skill, learned, boosts, boosted);
    AddAutoModes(proto, total_stats, weapon_type, swung, learned, off_clock,
                 speed_factor, types, set);
    // Everything from here reads `swung`, never `skill`: a boost that changed
    // the clock would otherwise be dropped, the reach and the strikes having
    // already been taken from the copy.
    AttackOption attack =
        AttackFor(proto, total_stats, weapon_type, &swung, learned, types,
                  swung.kind() == SKILL_KIND_AUTO_ATTACK ? off_clock : derived,
                  attack_speed, speed_factor);
    // A cast is not a hit. The damage chain has no multiplier to apply to a
    // skill that deals none, so what it built is the bare poke's damage --
    // which a cast must not land, and which a Final Attack must not follow.
    if (attack.heal_fraction > 0.0) {
      std::fill(attack.damage_per_hit.begin(), attack.damage_per_hit.end(),
                0.0);
      attack.groups.clear();
      attack.lead_damage.clear();
      attack.final_attack_damage.clear();
      attack.final_attack_rolls.clear();
    }
    if (swung.kind() != SKILL_KIND_AUTO_ATTACK) {
      // What this swing counts toward the skills clocked by swings landed.
      // Unset is one, which is what an ordinary swing is worth.
      if (swung.hits_per_attack_count() > 1) {
        attack.count_weight = 1.0 / swung.hits_per_attack_count();
      }
      set.attacks.push_back(std::move(attack));
      continue;
    }
    attack.swing_seconds = 0.0;  // not swung, so never charged
    attack.final_attack_damage.clear();
    attack.final_attack_rolls.clear();  // Final Attack follows a swing
    // Clocked by swings landed rather than by seconds passed.
    if (swung.attacks_per_cast() > 0) {
      attack.attacks_per_cast = swung.attacks_per_cast();
      set.triggered_attacks.push_back(std::move(attack));
      continue;
    }
    // A skill with no clock at all would fire every step, so naming neither is
    // taken as "does not fire" rather than "fires constantly".
    if (swung.cast_interval_seconds() <= 0.0) {
      continue;
    }
    attack.interval_seconds = swung.cast_interval_seconds() * speed_factor;
    set.auto_attacks.push_back(std::move(attack));
  }
}

// The stage the character's swings are paced at: what they start at for their
// job and weapon, plus whatever their passives add, up to the fastest tier we
// model. Asked per attack set, since a buff can be one of the things adding.
int AttackSpeedStageFor(const GameState& state, const EquipPrototype& weapon,
                        const DerivedStats& derived) {
  return std::min(static_cast<int>(ATTACK_SPEED_FASTEST_3),
                  BaseAttackSpeedStage(state.character.proto().job(),
                                       weapon.attack_speed()) +
                      derived.attack_speed_bonus);
}

// Everything the character can attack with, at one particular set of stats --
// theirs alone, or theirs with some buff up.
AttackSet BuildAttackSet(const GameState& state, const DerivedStats& derived,
                         const EquipPrototype& weapon, double speed_factor,
                         const std::vector<CombatType>& types) {
  int attack_speed = AttackSpeedStageFor(state, weapon, derived);
  AttackSet set;
  AddAttacks(state, derived, weapon.equip_type(), attack_speed, speed_factor,
             types, set);
  AddEmpoweredForms(state, TotalEquipStats(state.character, derived),
                    weapon.equip_type(), derived, attack_speed, speed_factor,
                    types, set);
  return set;
}

// How many timed buffs are modelled at once. Every combination of them needs a
// damage table of its own, so the count of tables doubles with each one --
// which is affordable at four and would stop being so before long. A character
// holding more keeps the first four; nothing in the game holds two.
constexpr int kMaxBuffWindows = 4;

// Where `name`'s swing sits among the attacks, or -1 if the character cannot
// swing it. Answered off the unbuffed set, which holds the same attacks in the
// same order as every buffed one.
int AttackNamed(const std::vector<AttackOption>& attacks,
                const std::string& name) {
  for (int i = 0; i < static_cast<int>(attacks.size()); ++i) {
    if (attacks[i].name == name && attacks[i].swing_seconds > 0.0) {
      return i;
    }
  }
  return -1;
}

// What the fight needs to run each buff's clock, at the level it is learned.
// The levers are not here: those are folded into the tables below.
void AddBuffs(const CharacterInstance& character,
              const std::map<std::string, Skill>& skills,
              const std::vector<const Skill*>& buff_skills, double speed_factor,
              CombatParams& params) {
  int bonus = BonusSkillLevels(character, skills);
  for (const Skill* skill : buff_skills) {
    int level = EffectiveSkillLevel(character, *skill, bonus);
    const Buff& buff = skill->buff();
    BuffOption option;
    option.name = skill->name();
    option.duration_seconds =
        (buff.duration_seconds() +
         buff.duration_seconds_per_level() * (level - 1)) *
        speed_factor;
    option.cooldown_seconds = CooldownAt(*skill, level) * speed_factor;
    option.cooldown_reduction_seconds =
        buff.cooldown_reduction_seconds() * speed_factor;
    option.heal_fraction =
        buff.base().heal_pct() + buff.per_level().heal_pct() * (level - 1);
    // A buff hanging off an ATTACK is laid by that swing rather than raised on
    // a wait: what leaves the wound is puncturing something. See
    // BuffOption::laid_by_attack.
    if (skill->kind() == SKILL_KIND_ATTACK) {
      option.laid_by_attack = AttackNamed(params.attacks, skill->name());
    }
    params.buffs.push_back(std::move(option));
  }
}

// Points each bleeding buff's pulse at the buff it belongs to, in the base set
// and in every buffed one alike -- the fight reads whichever set the mask
// names, so a tag on one of them would come and go with the buffs.
//
// Matched by name because a pulse keeps its parent skill's name, and so does
// the buff: one skill, one row in the book, one name.
void TagBuffGatedPulses(const std::vector<const Skill*>& buff_skills,
                        CombatParams& params) {
  for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
    if (i >= static_cast<int>(buff_skills.size()) ||
        buff_skills[i]->buff().pulse().cast_interval_seconds() <= 0.0) {
      continue;
    }
    std::vector<std::vector<AttackOption>*> sets = {&params.auto_attacks};
    for (AttackSet& set : params.buffed) {
      sets.push_back(&set.auto_attacks);
    }
    for (std::vector<AttackOption>* set : sets) {
      for (AttackOption& cast : *set) {
        if (cast.name == params.buffs[i].name) {
          cast.needs_buff = i;
        }
      }
    }
  }
}

// A damage table for every combination of the character's buffs, indexed the
// way CombatParams::Attacks reads them: the mask of which are up, less one.
void AddBuffedSets(const GameState& state,
                   const std::vector<const Skill*>& buff_skills,
                   const EquipPrototype& weapon, double speed_factor,
                   CombatParams& params) {
  int count = static_cast<int>(buff_skills.size());
  for (int mask = 1; mask < (1 << count); ++mask) {
    std::vector<const Skill*> up;
    for (int i = 0; i < count; ++i) {
      if ((mask & (1 << i)) != 0) {
        up.push_back(buff_skills[i]);
      }
    }
    DerivedStats derived =
        DerivedStatsFor(state.character, state.skills, absl::MakeConstSpan(up));
    params.buffed.push_back(
        BuildAttackSet(state, derived, weapon, speed_factor, params.types));
  }
}

}  // namespace

const std::vector<AttackOption>& CombatParams::Attacks(int mask) const {
  if (mask <= 0 || mask > static_cast<int>(buffed.size())) {
    return attacks;
  }
  return buffed[mask - 1].attacks;
}

const std::vector<AttackOption>& CombatParams::AutoAttacks(int mask) const {
  if (mask <= 0 || mask > static_cast<int>(buffed.size())) {
    return auto_attacks;
  }
  return buffed[mask - 1].auto_attacks;
}

const std::vector<AttackOption>& CombatParams::TriggeredAttacks(
    int mask) const {
  if (mask <= 0 || mask > static_cast<int>(buffed.size())) {
    return triggered_attacks;
  }
  return buffed[mask - 1].triggered_attacks;
}

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

  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  // The pace the whole encounter runs at, and the only thing here that asks
  // the character's level directly: the game stretches out as they climb.
  double speed_factor = GameSpeedFactor(state.character.proto().level());
  params.respawn_seconds = kRespawnIntervalSeconds * speed_factor;
  params.hit_seconds = kMobHitIntervalSeconds * speed_factor;
  params.max_player_hp = derived.max_hp;
  params.player_level = state.character.proto().level();
  params.beat_heal_fraction = kBeatHealFraction;
  params.damage_reflect_pct = derived.damage_reflect_pct;
  params.hp_recover_pct = derived.hp_recover_pct;
  params.exp_pct = derived.exp_pct;
  params.meso_pct = derived.meso_pct;
  // The band stretches the interval between pulses, so it thins the rate.
  params.regen_pct_per_second =
      speed_factor > 0.0 ? derived.regen_pct_per_second / speed_factor : 0.0;
  params.revive_cooldown_seconds =
      derived.revive_cooldown_seconds * speed_factor;

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
  // The character as they stand, then one table for every combination of
  // buffs they can have up. What being hit costs them is read off the
  // unbuffed stats alone: nothing yet buffs a pool or a defence.
  AttackSet base =
      BuildAttackSet(state, derived, weapon, speed_factor, params.types);
  params.attacks = std::move(base.attacks);
  params.auto_attacks = std::move(base.auto_attacks);
  params.triggered_attacks = std::move(base.triggered_attacks);
  std::vector<const Skill*> buff_skills =
      BuffSkillsFor(state.character, state.skills);
  if (static_cast<int>(buff_skills.size()) > kMaxBuffWindows) {
    buff_skills.resize(kMaxBuffWindows);
  }
  AddBuffs(state.character, state.skills, buff_skills, speed_factor, params);
  AddBuffedSets(state, buff_skills, weapon, speed_factor, params);
  TagBuffGatedPulses(buff_skills, params);
  params.active = true;
  return params;
}

}  // namespace ms
