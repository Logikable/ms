#include "src/combat/encounter.h"

#include <algorithm>
#include <map>
#include <set>
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

// Strips everything that rides the character's own swing -- the recovery it
// pays, its Final Attacks, the poison it carries and the strike it sets off.
// Anything on a clock of its own (a summon, a wound, a form standing in for a
// pulse) sets none of them off, and neither does a cast that deals no damage.
//
// A burn the SKILL states is not one of them: Ifrit's flames burn what they
// touch whoever is swinging. Only what the character carries is dropped.
void ClearSwingRiders(AttackOption& attack) {
  attack.hp_recover_pct = 0.0;
  attack.side = nullptr;
  attack.procs.clear();
  // A summon leaves the ice it makes -- Elquines freezes what it touches -- but
  // never spends the pile. GMS says as much of the other one: the lightning orb
  // attacks without consuming freezing stacks.
  attack.freeze_spends = false;
  attack.freeze_fd_per_stack = 0.0;
  attack.final_attack_damage.clear();
  attack.final_attack_rolls.clear();
  attack.single_final_attack_damage.clear();
  attack.single_final_attack_rolls.clear();
  attack.dots.erase(
      std::remove_if(attack.dots.begin(), attack.dots.end(),
                     [](const DotApplication& burn) { return burn.carried; }),
      attack.dots.end());
}

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

// What one burn is worth against every mob type on the map, priced off the
// stat line the swing that lights it was priced off. Its own multiplier and
// its own strikes: a burn is not the swing, it is what the swing left behind.
DotApplication BurnFor(const Dot& dot, const OffenseStats& offense, int level,
                       const std::vector<CombatType>& types,
                       double speed_factor) {
  // A stack ladder walked in thirds or sixths lands a hair under the whole
  // number it climbs to, and a burn that stacks 2.9999 times stacks twice.
  constexpr double kStackEpsilon = 1e-9;
  OffenseStats burn = offense;
  burn.skill_pct =
      dot.base().skill_pct() + dot.per_level().skill_pct() * (level - 1);
  burn.normal_skill_pct = dot.base().normal_skill_pct() +
                          dot.per_level().normal_skill_pct() * (level - 1);
  burn.lines = std::max(1, dot.lines());
  // A shadow copies the swing it was cast beside, not the mark that swing left
  // burning after it.
  burn.mirror_lines = 0;
  DotApplication application;
  for (const CombatType& type : types) {
    application.damage.push_back(ExpectedAttackDamage(burn, *type.mob));
  }
  application.rolls = RollsFor(burn);
  application.interval_seconds = dot.interval_seconds() * speed_factor;
  application.duration_seconds =
      (dot.duration_seconds() +
       dot.duration_seconds_per_level() * (level - 1)) *
      speed_factor;
  double chance = dot.chance() + dot.chance_per_level() * (level - 1);
  // Nothing said is certainty, which is what a burn a swing simply leaves
  // wants. Only a poison the character carries is rolled for.
  application.chance = chance > 0.0 ? std::min(1.0, chance) : 1.0;
  application.max_stacks =
      std::max(1, static_cast<int>(dot.max_stacks() +
                                   dot.max_stacks_per_level() * (level - 1) +
                                   kStackEpsilon));
  return application;
}

// One attack's damage against every mob type on the map. `skill` is null for
// the bare poke, which hits one target for the character's plain 100% swing.
// `equipped` is everything the character wears plus everything their passives
// grant, already summed -- the two are indistinguishable to the damage chain.
// What this swing does with the character's Freeze Stacks. An ice swing leaves
// one per line and a lightning swing spends one per line; both take the
// critical damage a held stack grants, since a frozen enemy is frozen whichever
// element is hitting it.
//
// That critical damage is turned into the share it adds to the swing's MEAN
// damage, which is the only shape the fight can multiply by. Crit rolls per
// line and its bonus is normalised away, so a bigger crit_dmg in the rolls
// would change how the swing varies and not what it averages.
void AddFreezeStacks(const Skill* skill, const DerivedStats& derived,
                     const OffenseStats& offense, AttackOption& attack) {
  if (derived.freeze.cap <= 0 || skill == nullptr) {
    return;
  }
  double rate = std::min(1.0, offense.crit_rate + kBaseCritRate);
  double crit = offense.crit_dmg + kBaseCritDamage;
  attack.freeze_crit_gain =
      rate * derived.freeze.crit_dmg_per_stack / (1.0 + rate * crit);
  if (HasTag(skill, SKILL_TAG_ICE)) {
    attack.freeze_build = attack.lines;
  }
  if (HasTag(skill, SKILL_TAG_LIGHTNING)) {
    attack.freeze_spends = true;
    attack.freeze_fd_per_stack = derived.freeze.final_dmg_pct_per_stack;
  }
}

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
    // An ATTACK's recovery is its own swing's, exactly as its ignored defence
    // is. Read here rather than off the character, who was handed everything
    // but this -- see WithoutSwingLevers.
    if (skill->kind() == SKILL_KIND_ATTACK) {
      attack.hp_recover_pct = skill->base().hp_recover_pct() +
                              skill->per_level().hp_recover_pct() * (level - 1);
    }
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
    attack.lines = SkillLinesAt(*skill, level);
  }
  for (const SwingProc& proc : derived.procs) {
    attack.procs.push_back({proc.chance, proc.damage_pct, proc.hp_recover_pct});
  }
  AddFreezeStacks(skill, derived, offense, attack);
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
    attack.lead_enemies = std::max(1, skill->lead_enemies());
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
  // The burns this swing leaves behind: marks on what it reached rather than
  // part of the strike, so they are priced here and paid out on their own
  // clock. What one is worth is settled now and carried for the whole of its
  // life, which is what makes a burn lit under a buff keep the buffed number.
  //
  // The character's own come first and in their own order, so that every swing
  // writes one poison to one slot. They are priced off the bare stat line for
  // the same reason a Final Attack is -- the poison is on the claw, not in the
  // skill, and takes neither its multiplier nor its ignored defence.
  for (const CharacterDot& carried : derived.dots) {
    attack.dots.push_back(
        BurnFor(carried.dot, follow, carried.level, types, speed_factor));
    attack.dots.back().carried = true;
  }
  if (skill != nullptr && skill->dot().interval_seconds() > 0.0) {
    attack.dots.push_back(
        BurnFor(skill->dot(), offense, level, types, speed_factor));
  }
  attack.final_attack_damage.assign(types.size(), 0.0);
  attack.single_final_attack_damage.assign(types.size(), 0.0);
  // What this swing keeps of the character's chance to shake a coin loose.
  // Cruel Stab alone gives any of it up -- see SkillEffect.meso_drop_cut.
  double meso_kept = 1.0;
  // What the character's own boss damage is, before a source adds to it.
  double carried_boss_pct = follow.boss_pct;
  if (skill != nullptr) {
    meso_kept -= skill->base().meso_drop_cut() +
                 skill->per_level().meso_drop_cut() * (level - 1);
    meso_kept = std::max(0.0, meso_kept);
  }
  for (const FinalAttackSource& source : derived.final_attacks) {
    if (source.required_tag != SKILL_TAG_UNSPECIFIED &&
        !HasTag(skill, source.required_tag)) {
      continue;
    }
    FinalAttackRoll roll;
    // A meso is the one source a swing can shake fewer of loose; nothing cuts
    // a Final Attack, which follows the swing whatever it was.
    roll.chance = source.per_line ? source.chance * meso_kept : source.chance;
    // Boss damage of its own, on top of the character's: Blood Money brands
    // the coins rather than the Shadower. Set every time round, since the last
    // source to carry any would otherwise hand it to the next one.
    follow.boss_pct = carried_boss_pct + source.boss_pct;
    roll.count = source.per_line ? swing_lines : 1;
    follow.skill_pct = source.damage_pct;
    // Its own strikes, not the swing's: a Night Lord's mark throws three stars
    // behind a four-star swing, and each of the three rolls on its own.
    follow.lines = source.lines;
    roll.rolls = RollsFor(follow);
    // A source that strikes one enemy is banked apart: what the swing is worth
    // has to add it once rather than once for every mob in front of the
    // player.
    std::vector<double>& bank = source.single_enemy
                                    ? attack.single_final_attack_damage
                                    : attack.final_attack_damage;
    for (std::size_t i = 0; i < types.size(); ++i) {
      roll.damage.push_back(ExpectedAttackDamage(follow, *types[i].mob));
      bank[i] += roll.damage.back() * roll.chance * roll.count;
    }
    if (source.single_enemy) {
      attack.single_final_attack_rolls.push_back(std::move(roll));
    } else {
      attack.final_attack_rolls.push_back(std::move(roll));
    }
  }
  if (attack.final_attack_rolls.empty()) {
    attack.final_attack_damage.clear();
  }
  if (attack.single_final_attack_rolls.empty()) {
    attack.single_final_attack_damage.clear();
  }
  // The strike this swing sets off on a wait of its own, priced as a swing in
  // its own right: its own reach, its own strikes, its own bargain against an
  // ordinary monster. It is not the character's swing, so nothing rides it.
  if (skill != nullptr && skill->has_side_strike()) {
    const SideStrike& side = skill->side_strike();
    OffenseStats stats = offense;
    stats.skill_pct =
        side.base().skill_pct() + side.per_level().skill_pct() * (level - 1);
    stats.normal_skill_pct = side.base().normal_skill_pct() +
                             side.per_level().normal_skill_pct() * (level - 1);
    stats.lines = std::max(1, side.lines());
    stats.mirror_lines = stats.lines;
    AttackOption strike;
    strike.name = side.label().empty() ? attack.name : side.label();
    strike.max_enemies =
        side.max_enemies() > 0 ? side.max_enemies() : attack.max_enemies;
    strike.cooldown_seconds = side.cooldown_seconds() * speed_factor;
    for (const CombatType& type : types) {
      strike.damage_per_hit.push_back(ExpectedAttackDamage(stats, *type.mob));
    }
    strike.groups.push_back({strike.damage_per_hit, RollsFor(stats)});
    attack.side = std::make_shared<const AttackOption>(std::move(strike));
  }
  return attack;
}

// The mob types this map spawns, each with what one of its hits costs the
// player. Types the mob catalog does not know are skipped.
void AddTypes(const GameState& state, const MapData& map,
              const DefenseStats& defense, CombatParams& params) {
  for (const Spawn& spawn : map.spawns()) {
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
               EquipType weapon_type, const std::set<std::string>& superseded) {
  if (!Castable(skill)) {
    return false;
  }
  // A skill the book has replaced stops offering its swing along with its
  // levers -- Piercing Arrow II states the whole of the Piercing Arrow it
  // takes over, so both being swingable would be one skill offered twice.
  if (superseded.count(skill.name()) > 0) {
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
    ClearSwingRiders(attack);    // what rides a swing needs one
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
  ClearSwingRiders(wound);
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
Skill EmpoweredSkill(const Skill& skill, const EmpoweredForm& upgrade,
                     const std::string& target, SkillKind kind, int reach) {
  Skill form;
  form.set_name("Empowered " + target);
  // The kind of the attack it stands in for, not of the skill granting it --
  // the grant is often a passive, and what stands in for a pulse is a pulse.
  form.set_kind(kind);
  *form.mutable_base() = upgrade.base();
  *form.mutable_per_level() = upgrade.per_level();
  // A form that says nothing about its reach goes as far as the attack it
  // stands in for: Mist Eruption sets off the mist exactly where the mist is.
  form.set_max_enemies(upgrade.max_enemies() > 0 ? upgrade.max_enemies()
                                                 : reach);
  form.set_lines(upgrade.lines());
  *form.mutable_extra_hit() = upgrade.extra_hit();
  // The form is the same arrow, further upgraded: it gains as it travels the
  // same way, over the further enemies it reaches.
  form.set_pierce_gain_pct(skill.pierce_gain_pct());
  return form;
}

// Attaches `skill`'s empowered form to every attack in `into` that it upgrades.
// The form takes the place of the attack it lands for, so it inherits the
// attack's pacing: an animation for a swing, nothing at all for a summon, which
// is paced by the clock the pulse it replaced would have run on.
void AttachEmpoweredForm(const GameState& state, const EquipStats& equipped,
                         EquipType weapon_type, const Skill& skill,
                         const EmpoweredForm& upgrade, int learned,
                         const DerivedStats& derived, int attack_speed,
                         double speed_factor,
                         const std::vector<CombatType>& types, SkillKind kind,
                         std::vector<AttackOption>& into) {
  // The form may name its own target, which is what a skill carrying two of
  // them does. Failing that it takes the one skill this one strengthens, and
  // failing that it upgrades its own attack -- Creeping Toxin detonating the
  // pool it is already spreading.
  std::string target = upgrade.skill_name();
  if (target.empty()) {
    target = skill.boosts_skill_name().empty() ? skill.name()
                                               : skill.boosts_skill_name();
  }
  for (AttackOption& attack : into) {
    if (attack.name != target) {
      continue;
    }
    Skill form =
        EmpoweredSkill(skill, upgrade, attack.name, kind, attack.max_enemies);
    std::shared_ptr<AttackOption> swing = std::make_shared<AttackOption>(
        AttackFor(state.character.proto(), equipped, weapon_type, &form,
                  learned, types, derived, attack_speed, speed_factor));
    swing->swing_seconds = attack.swing_seconds;
    // Final Attack follows the character's swing, and a summon's pulse is not
    // one -- so a form standing in for a pulse must not carry one either.
    if (attack.interval_seconds > 0.0) {
      ClearSwingRiders(*swing);
    }
    attack.empowered_every = upgrade.casts_per_trigger();
    attack.brands_enemies = upgrade.brands_each_enemy();
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
    int learned = EffectiveSkillLevel(state.character, skill, bonus);
    if (learned <= 0) {
      continue;
    }
    // A form standing in for a pulse is a pulse: no shadow and no mesos, for
    // the reason AddAttacks gives.
    DerivedStats off_clock = derived;
    off_clock.mirror_line_pct = 0.0;
    StripMesoDrops(off_clock);
    for (const EmpoweredForm& upgrade : skill.empowered_form()) {
      if (upgrade.casts_per_trigger() <= 0) {
        continue;
      }
      AttachEmpoweredForm(state, equipped, weapon_type, skill, upgrade, learned,
                          derived, attack_speed, speed_factor, types,
                          SKILL_KIND_ATTACK, set.attacks);
      AttachEmpoweredForm(state, equipped, weapon_type, skill, upgrade, learned,
                          off_clock, attack_speed, speed_factor, types,
                          SKILL_KIND_AUTO_ATTACK, set.auto_attacks);
    }
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
  std::set<std::string> superseded =
      SupersededSkillNames(state.character, state.skills, bonus);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    int learned = EffectiveSkillLevel(state.character, skill, bonus);
    if (learned <= 0 || !Swingable(state, skill, weapon_type, superseded)) {
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
      ClearSwingRiders(attack);
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
    ClearSwingRiders(attack);    // what rides a swing needs one
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
// Hands each burn a slot of its own on the monsters it marks, so two never
// write over each other. `shared` is how many of them the character carries
// rather than any one swing. Numbered by attack order, which is the same in
// every buffed set, so a slot the fight is holding means the same thing
// however the buffs come and go.
//
// Every kind of attack is numbered, not only the swings: a summon leaves its
// own burn, and one with no slot is one the fight silently drops.
void NumberDots(AttackSet& set, int shared) {
  int next = shared;
  std::vector<std::vector<AttackOption>*> lists = {
      &set.attacks, &set.auto_attacks, &set.triggered_attacks};
  for (std::vector<AttackOption>* list : lists) {
    for (AttackOption& attack : *list) {
      // A carried burn is the same burn wherever it was applied from, so it
      // keeps the slot its place among the character's gives it. An attack's
      // own gets a slot nothing else writes.
      int carried = 0;
      for (DotApplication& burn : attack.dots) {
        burn.slot = burn.carried ? carried++ : next++;
      }
    }
  }
}

// How many slots a monster needs to carry every burn this character can leave.
// Every list is walked: a summon's burn marks a monster exactly as a swing's
// does.
int DotSlotsNeeded(const CombatParams& params) {
  int slots = 0;
  const std::vector<const std::vector<AttackOption>*> lists = {
      &params.attacks, &params.auto_attacks, &params.triggered_attacks};
  for (const std::vector<AttackOption>* list : lists) {
    for (const AttackOption& attack : *list) {
      for (const DotApplication& burn : attack.dots) {
        slots = std::max(slots, burn.slot + 1);
      }
    }
  }
  return slots;
}

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
  NumberDots(set, static_cast<int>(derived.dots.size()));
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
              double buff_duration_pct, CombatParams& params) {
  int bonus = BonusSkillLevels(character, skills);
  for (const Skill* skill : buff_skills) {
    int level = EffectiveSkillLevel(character, *skill, bonus);
    const Buff& buff = skill->buff();
    BuffOption option;
    option.name = skill->name();
    // Buff Duration lengthens the buff and not the wait below it, which is why
    // a percentage that grants nothing on its own is worth having.
    option.duration_seconds =
        (buff.duration_seconds() +
         buff.duration_seconds_per_level() * (level - 1)) *
        (1.0 + buff_duration_pct) * speed_factor;
    option.cooldown_seconds = CooldownAt(*skill, level) * speed_factor;
    option.damage_taken_pct = buff.base().damage_taken_pct() +
                              buff.per_level().damage_taken_pct() * (level - 1);
    option.cooldown_reduction_seconds =
        buff.cooldown_reduction_seconds() * speed_factor;
    // Lines rather than seconds, so the pacing band leaves it alone: what it
    // measures is how fast the character lands hits, which is already
    // stretched.
    option.charge_lines = buff.charge_lines();
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
  params.item_drop_pct = derived.item_drop_pct;
  // The band stretches the interval between pulses, so it thins the rate.
  params.regen_pct_per_second =
      speed_factor > 0.0 ? derived.regen_pct_per_second / speed_factor : 0.0;
  params.revive_cooldown_seconds =
      derived.revive_cooldown_seconds * speed_factor;
  params.freeze_cap = derived.freeze.cap;

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
  // buffs they can have up. What being hit costs them is read off the unbuffed
  // stats: nothing yet buffs a pool, and the one buff that softens a hit takes
  // its share off the hit itself -- see BuffOption.damage_taken_pct.
  AttackSet base =
      BuildAttackSet(state, derived, weapon, speed_factor, params.types);
  params.attacks = std::move(base.attacks);
  params.auto_attacks = std::move(base.auto_attacks);
  params.triggered_attacks = std::move(base.triggered_attacks);
  params.dot_count = DotSlotsNeeded(params);
  std::vector<const Skill*> buff_skills =
      BuffSkillsFor(state.character, state.skills);
  if (static_cast<int>(buff_skills.size()) > kMaxBuffWindows) {
    buff_skills.resize(kMaxBuffWindows);
  }
  AddBuffs(state.character, state.skills, buff_skills, speed_factor,
           derived.buff_duration_pct, params);
  AddBuffedSets(state, buff_skills, weapon, speed_factor, params);
  TagBuffGatedPulses(buff_skills, params);
  params.active = true;
  return params;
}

}  // namespace ms
