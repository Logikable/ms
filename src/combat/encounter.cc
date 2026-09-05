#include "src/combat/encounter.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/hyper_stats.h"
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
#include "src/spawn.h"

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
//
// `boost` is what the rest of the book hands this burn by name, or nullptr for
// a burn no boost can reach. The levers it grants the swing are already in
// `offense` and ride in with it -- only the multiplier and the clock are the
// burn's own, so only those two are read here.
DotApplication BurnFor(const Dot& dot, const OffenseStats& offense, int level,
                       const std::vector<CombatType>& types,
                       double speed_factor, const SkillBonus* boost) {
  // A stack ladder walked in thirds or sixths lands a hair under the whole
  // number it climbs to, and a burn that stacks 2.9999 times stacks twice.
  constexpr double kStackEpsilon = 1e-9;
  OffenseStats burn = offense;
  SkillEffect burns = EffectAt(dot.base(), dot.per_level(), level);
  burn.skill_pct =
      burns.skill_pct() + (boost != nullptr ? boost->dot_skill_pct : 0.0);
  burn.normal_skill_pct = burns.normal_skill_pct();
  burn.normal_pct += burns.normal_pct();
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
      (dot.duration_seconds() + dot.duration_seconds_per_level() * (level - 1) +
       (boost != nullptr ? boost->dot_duration_seconds : 0.0)) *
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

// What this swing does with the character's Freeze Stacks. An ice swing leaves
// one per line and a lightning swing spends one per line; both take the
// critical damage a held stack grants and Storm Magic's final damage, since a
// frozen enemy is frozen whichever element is hitting it.
//
// That critical damage is turned into the share it adds to the swing's MEAN
// damage, which is the only shape the fight can multiply by. Crit rolls per
// line and its bonus is normalised away, so a bigger crit_dmg in the rolls
// would change how the swing varies and not what it averages.
void AddFreezeStacks(const Skill* skill, const DerivedStats& derived,
                     const OffenseStats& offense,
                     const std::vector<CombatType>& types,
                     AttackOption& attack) {
  if (derived.freeze.cap <= 0 || skill == nullptr) {
    return;
  }
  double rate = std::min(1.0, offense.crit_rate + kBaseCritRate);
  double crit = offense.crit_dmg + kBaseCritDamage;
  attack.freeze_crit_gain =
      rate * derived.freeze.crit_dmg_per_stack / (1.0 + rate * crit);
  if (derived.freeze.ied_pct_per_stack > 0.0) {
    for (const CombatType& type : types) {
      attack.freeze_ied_gain.push_back(DefenseShare(*type.mob, offense.ied) *
                                       derived.freeze.ied_pct_per_stack);
    }
  }
  if (HasTag(skill, SKILL_TAG_ICE)) {
    attack.freeze_build = attack.lines;
    // Glacial Fury's magic attack, as the share of this swing one held stack
    // adds. Damage is linear in the attack behind it, so the two are the same
    // thing said twice -- and it is a share here because that is what the
    // fight can multiply a damage table by.
    if (offense.attack > 0) {
      attack.freeze_matt_gain =
          derived.freeze.matt_per_stack / static_cast<double>(offense.attack);
    }
  }
  if (HasTag(skill, SKILL_TAG_LIGHTNING)) {
    attack.freeze_spends = true;
    attack.freeze_fd_per_stack = derived.freeze.final_dmg_pct_per_stack;
  }
}

// A second hit the same swing lands, priced on its own and summed into the
// swing: its own multiplier, its own lines, its own critical rate on top of
// what the character brought. The group it leaves behind is what makes it roll
// separately -- see SwingHit.
void AddSwingHit(const SwingHit& hit, const OffenseStats& offense, int level,
                 const std::vector<CombatType>& types, AttackOption& attack) {
  OffenseStats extra = offense;
  SkillEffect lands = EffectAt(hit.base(), hit.per_level(), level);
  extra.skill_pct = lands.skill_pct();
  extra.normal_skill_pct = lands.normal_skill_pct();
  extra.normal_pct += lands.normal_pct();
  extra.crit_rate += lands.crit_rate();
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

// The hold a held swing is. What has been priced already is ONE pulse, so the
// strike the hold ends on is added beside it and the swing's damage is then
// restated as a full hold: every pulse of it, and the one finish.
//
// The floor is the animation's own, which is what base_delay_ms already became
// -- the shortest the player can let go. The pulse clock is not scaled by
// attack speed for the reason a key-down swing's is not: the rate belongs to
// the skill.
void AddChannel(const Skill& skill, const OffenseStats& offense, int level,
                const std::vector<CombatType>& types, double speed_factor,
                AttackOption& attack) {
  const Channel& channel = skill.channel();
  if (channel.max_pulses() <= 0 || channel.pulse_interval_ms() <= 0) {
    return;
  }
  std::vector<double> pulse = attack.damage_per_hit;
  AddSwingHit(channel.finish(), offense, level, types, attack);
  ChannelHold& hold = attack.channel;
  hold.pulses = channel.max_pulses();
  hold.pulse_seconds = channel.pulse_interval_ms() / 1000.0 * speed_factor;
  hold.finish_seconds = channel.finish_delay_ms() / 1000.0 * speed_factor;
  hold.min_seconds = attack.swing_seconds;
  hold.damage_taken_pct = channel.damage_taken_pct();
  // The pulses that fit inside the floor, which is the fewest a cast can be
  // let go after. At least one: a hold that landed no pulse at all would be a
  // swing that does nothing but its finish.
  hold.min_pulses =
      std::clamp(static_cast<int>((hold.min_seconds - hold.finish_seconds) /
                                  hold.pulse_seconds),
                 1, hold.pulses);
  for (std::size_t i = 0; i < pulse.size() && i < attack.damage_per_hit.size();
       ++i) {
    attack.damage_per_hit[i] += pulse[i] * (hold.pulses - 1);
  }
  attack.swing_seconds = HoldSeconds(hold, hold.pulses);
}

// The clocks and conditions a skill puts on its own swing: how often it can be
// swung, how long its cooldown and the ice it leaves last, and what the
// character's own scarring and affliction damage are worth to it.
//
// A key-down skill fires at its own rate however fast the weapon swings, so it
// is handed the stage the formula is the identity at rather than the
// character's. The game's own pacing still stretches every clock here -- that
// is about the game running slower than GMS, not about the weapon.
void AddSwingClocks(const Skill* skill, int level, const DerivedStats& derived,
                    int attack_speed, double speed_factor,
                    AttackOption& attack) {
  int delay_ms = kDefaultSwingDelayMs;
  int stage = attack_speed;
  if (skill == nullptr) {
    attack.swing_seconds = SwingIntervalSeconds(delay_ms, stage) * speed_factor;
    return;
  }
  attack.name = skill->name();
  attack.max_enemies = std::max(1, skill->max_enemies());
  if (skill->base_delay_ms() > 0) {
    delay_ms = skill->base_delay_ms();
  }
  if (skill->fixed_delay()) {
    stage = kUnscaledAttackSpeedStage;
  }
  attack.swing_seconds = SwingIntervalSeconds(delay_ms, stage) * speed_factor;
  attack.cooldown_seconds =
      ReducedCooldown(CooldownAt(*skill, level),
                      derived.cooldown_reduction_seconds) *
      speed_factor;
  // Game-scaled like every other duration: the pacing band stretches the ice
  // exactly as far as it stretches the summon clock relaying it, so what a
  // freeze covers is the same span of the fight it covers in GMS.
  attack.freeze_seconds = skill->freeze_seconds() * speed_factor;
  // Chance Attack's damage against a scarred monster, and what the enemy's own
  // condition is worth. Both ride anything that lands on the mob -- a summon's
  // pulse included -- since the mob is in that state whatever is hitting it.
  attack.scar_fd = derived.scar.final_dmg_pct;
  attack.fd_when_afflicted = derived.condition.final_dmg_pct_when_afflicted;
  attack.fd_per_dot = derived.condition.final_dmg_pct_per_dot;
  attack.dot_count_cap = derived.condition.dot_count_cap;
  SkillEffect granted = EffectAt(skill->base(), skill->per_level(), level);
  attack.heal_fraction = granted.heal_pct();
  if (skill->kind() != SKILL_KIND_ATTACK) {
    return;
  }
  // The scar the character's own swings leave, and the recovery they pay: both
  // are the swing's own, kept off anything on a clock of its own -- GMS scars
  // with the sword being swung. Read here rather than off the character, who
  // was handed everything but this -- see WithoutSwingLevers.
  attack.scar_chance = derived.scar.chance;
  attack.scar_seconds = derived.scar.seconds * speed_factor;
  attack.hp_recover_pct = granted.hp_recover_pct();
}

// The harder opening hit some swings land on a single enemy before spreading --
// GMS's "strikes one, then detonates in place". Same character, same weapon,
// the skill's other multiplier: only the target count differs, and that is the
// fight's business rather than the damage chain's.
void AddLeadHit(const Skill& skill, const OffenseStats& offense, int level,
                const std::vector<CombatType>& types, AttackOption& attack) {
  if (skill.base().lead_pct() <= 0.0) {
    return;
  }
  OffenseStats lead = offense;
  lead.skill_pct =
      skill.base().lead_pct() + skill.per_level().lead_pct() * (level - 1);
  lead.lines = std::max(1, skill.lead_lines());
  // The shadow copies the opening hit as it copies every other line of the
  // swing -- it is the same swing, landed on one enemy instead of all of them.
  // Reset here because lead.lines just changed under it.
  lead.mirror_lines = lead.lines;
  for (const CombatType& type : types) {
    attack.lead_damage.push_back(ExpectedAttackDamage(lead, *type.mob));
  }
  attack.lead_rolls = RollsFor(lead);
  attack.lead_enemies = std::max(1, skill.lead_enemies());
}

// The burns this swing leaves behind: marks on what it reached rather than part
// of the strike, so they are priced here and paid out on their own clock. What
// one is worth is settled now and carried for the whole of its life, which is
// what makes a burn lit under a buff keep the buffed number.
//
// The character's own come first and in their own order, so that every swing
// writes one poison to one slot. They are priced off the bare stat line
// `follow` for the same reason a Final Attack is -- the poison is on the claw,
// not in the skill, and takes neither its multiplier nor its ignored defence.
// A boost names a skill, so the carried ones are handed nothing.
void AddBurns(const Skill* skill, const DerivedStats& derived,
              const OffenseStats& offense, const OffenseStats& follow,
              int level, const std::vector<CombatType>& types,
              double speed_factor, AttackOption& attack) {
  for (const CharacterDot& carried : derived.dots) {
    attack.dots.push_back(BurnFor(carried.dot, follow, carried.level, types,
                                  speed_factor, nullptr));
    attack.dots.back().carried = true;
  }
  if (skill == nullptr || skill->dot().interval_seconds() <= 0.0) {
    return;
  }
  // By the name being swung rather than the parent's, so a form that ever
  // states a burn of its own reads what was filed under its own name.
  std::map<std::string, SkillBonus>::const_iterator boost =
      derived.skill_bonus.find(skill->name());
  attack.dots.push_back(
      BurnFor(skill->dot(), offense, level, types, speed_factor,
              boost != derived.skill_bonus.end() ? &boost->second : nullptr));
}

// Final Attack rides the swing, not the skill: a plain hit worth its own
// percent, so it is priced off the bare stat line `follow` and takes neither
// the skill's multiplier nor its lines. An attack on its own clock strips it
// back off -- see ComputeCombatParams.
//
// A source naming a tag follows only the swings carrying it, which is how a
// fire mage's ignores everything they cast that is not fire. Every source that
// survives keeps its own entry, since each rolls on its own.
//
// A source rolling per line rolls `swing_lines` times: four lines knock four
// mesos loose where a Final Attack rolls once. The shadow's copies are not the
// character's lines and do not count.
void AddFinalAttacks(const Skill* skill, const DerivedStats& derived,
                     OffenseStats follow, int level, int swing_lines,
                     const std::vector<CombatType>& types,
                     AttackOption& attack) {
  attack.final_attack_damage.assign(types.size(), 0.0);
  attack.single_final_attack_damage.assign(types.size(), 0.0);
  // What this swing keeps of the character's chance to shake a coin loose.
  // Cruel Stab alone gives any of it up -- see SkillEffect.meso_drop_cut.
  double meso_kept = 1.0;
  if (skill != nullptr) {
    meso_kept -= skill->base().meso_drop_cut() +
                 skill->per_level().meso_drop_cut() * (level - 1);
    meso_kept = std::max(0.0, meso_kept);
  }
  // What the character's own boss damage, plain damage and ignored defence are,
  // before a source adds to any of them.
  const double carried_boss_pct = follow.boss_pct;
  const double carried_damage_pct = follow.damage_pct;
  const double carried_ied = follow.ied;
  const double carried_crit_rate = follow.crit_rate;
  const double carried_final_dmg_pct = follow.final_dmg_pct;
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
    // source to carry any would otherwise hand it to the next one. Plain
    // damage rides the same rule -- that is a Hyper Skill's Reinforce aimed at
    // the passive the Final Attack belongs to.
    follow.boss_pct = carried_boss_pct + source.boss_pct;
    follow.damage_pct = carried_damage_pct + source.damage_bonus_pct;
    // Ignored defence meets the character's rather than adding to it, the way
    // two sources of it always do -- Meso Explosion - Guardbreak. Critical
    // rate and final damage meet it the way each of them always does.
    follow.ied = CombineIgnoredDefense(carried_ied, source.ied);
    follow.crit_rate = carried_crit_rate + source.crit_rate;
    follow.final_dmg_pct =
        (1.0 + carried_final_dmg_pct) * (1.0 + source.final_dmg_pct) - 1.0;
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
}

// The strike this swing sets off on a wait of its own, priced as a swing in its
// own right: its own reach, its own strikes, its own bargain against an
// ordinary monster. It is not the character's swing, so nothing rides it.
void AddSideStrike(const Character& proto, const EquipStats& equipped,
                   EquipType weapon, const Skill& skill, int level,
                   const std::vector<CombatType>& types,
                   const DerivedStats& derived, double speed_factor,
                   AttackOption& attack) {
  const SideStrike& side = skill.side_strike();
  // Off the character's own stat line rather than the swing's, because what
  // the book aimed at this skill by NAME belongs to the swing: GMS's
  // Showdown - Reinforce says in so many words that it leaves the shuriken
  // alone. Everything else the skill states is still its own.
  PassiveOffense unaimed = PassiveOffenseFor(derived);
  unaimed.skill_bonus.erase(skill.name());
  OffenseStats stats =
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      equipped, weapon, &skill, level, unaimed);
  SkillEffect thrown = EffectAt(side.base(), side.per_level(), level);
  stats.skill_pct = thrown.skill_pct();
  stats.normal_skill_pct = thrown.normal_skill_pct();
  stats.normal_pct += thrown.normal_pct();
  stats.lines = std::max(1, side.lines());
  stats.mirror_lines = stats.lines;
  AttackOption strike;
  strike.name = side.label().empty() ? attack.name : side.label();
  strike.max_enemies =
      side.max_enemies() > 0 ? side.max_enemies() : attack.max_enemies;
  strike.cooldown_seconds = side.cooldown_seconds() * speed_factor;
  strike.scatter_hits = side.scatter().hits();
  strike.scatter_repeat_kept = 1.0 + side.scatter().repeat_final_dmg_pct();
  for (const CombatType& type : types) {
    strike.damage_per_hit.push_back(ExpectedAttackDamage(stats, *type.mob));
  }
  strike.groups.push_back({strike.damage_per_hit, RollsFor(stats)});
  attack.side = std::make_shared<const AttackOption>(std::move(strike));
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
  AddSwingClocks(skill, level, derived, attack_speed, speed_factor, attack);
  OffenseStats offense = OffenseStatsFor(
      proto.job(), proto.level(), proto.allocated_stats(), equipped, weapon,
      skill, level, PassiveOffenseFor(derived));
  for (const CombatType& type : types) {
    attack.damage_per_hit.push_back(ExpectedAttackDamage(offense, *type.mob));
  }
  if (skill != nullptr) {
    // Damage off the character's own pool, which lands after the chain rather
    // than through it: added once the multipliers are already in, so none of
    // them reaches it. Every line pays it, as GMS pays it per attack.
    double pool = (skill->base().max_hp_damage_pct() +
                   skill->per_level().max_hp_damage_pct() * (level - 1)) *
                  derived.max_hp * SkillLinesAt(*skill, level);
    for (double& damage : attack.damage_per_hit) {
      damage += pool;
    }
  }
  attack.groups.push_back({attack.damage_per_hit, RollsFor(offense)});
  if (skill != nullptr) {
    attack.pierce_gain_pct = skill->pierce_gain_pct();
    attack.lines = SkillLinesAt(*skill, level);
    // A scattered swing is the same swing throughout -- what differs is how
    // many of it land where, which is the fight's business rather than the
    // damage chain's, exactly as the opening hit's target count is.
    attack.scatter_hits = skill->scatter().hits();
    attack.scatter_repeat_kept = 1.0 + skill->scatter().repeat_final_dmg_pct();
  }
  for (const SwingProc& proc : derived.procs) {
    attack.procs.push_back({proc.chance, proc.damage_pct, proc.hp_recover_pct});
  }
  AddFreezeStacks(skill, derived, offense, types, attack);
  if (skill != nullptr) {
    AddLeadHit(*skill, offense, level, types, attack);
    // A swing that lands two hits at once: the hammer, and the brand it leaves
    // exploding. Same character, same weapon, same reach -- what differs is the
    // multiplier and what it adds against an ordinary monster, so each half is
    // priced on its own and the two are summed into the one swing.
    for (const SwingHit& hit : skill->extra_hit()) {
      AddSwingHit(hit, offense, level, types, attack);
    }
    AddChannel(*skill, offense, level, types, speed_factor, attack);
  }
  // The bare stat line everything the swing sets off is priced from: no skill,
  // so neither its multiplier nor its lines. The shadow mimics the swing, and
  // this is what the swing set off rather than the swing.
  OffenseStats follow =
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      equipped, weapon, nullptr, 0, PassiveOffenseFor(derived));
  follow.mirror_lines = 0;
  AddBurns(skill, derived, offense, follow, level, types, speed_factor, attack);
  AddFinalAttacks(skill, derived, follow, level,
                  skill != nullptr ? SkillLinesAt(*skill, level) : 1, types,
                  attack);
  if (skill != nullptr && skill->has_side_strike()) {
    AddSideStrike(proto, equipped, weapon, *skill, level, types, derived,
                  speed_factor, attack);
  }
  return attack;
}

// The mob types `spawns` puts in front of the player, each with what one of
// its hits costs them. Types the mob catalog does not know are skipped.
void AddTypes(const GameState& state,
              const google::protobuf::RepeatedPtrField<Spawn>& spawns,
              const DefenseStats& defense, double scar_enemy_attack_pct,
              CombatParams& params) {
  // A scarred monster is one whose attack the character has already cut
  // further, so it is the same defence against a weaker mob. Barriers sum, as
  // they always do.
  DefenseStats scarred = defense;
  scarred.enemy_attack_pct =
      std::min(1.0, scarred.enemy_attack_pct + scar_enemy_attack_pct);
  for (const Spawn& spawn : spawns) {
    std::map<std::string, Mob>::const_iterator mob_it =
        state.mobs.find(spawn.mob());
    if (mob_it == state.mobs.end()) {
      continue;
    }
    CombatType type;
    type.mob = &mob_it->second;
    type.simultaneous = SpawnCount(spawn);
    type.spots.assign(spawn.spots().begin(), spawn.spots().end());
    type.move_interval_seconds = spawn.move_interval_seconds();
    type.damage_to_player = ExpectedDamageTaken(defense, *type.mob);
    type.damage_to_player_scarred = ExpectedDamageTaken(scarred, *type.mob);
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

// Whether a learned skill is one this character has at all right now. Says
// nothing about swinging it: a passive carrying an own-clock half is not
// swingable and still fights, so this is the gate the fight asks first, and
// Castable then decides whether a swing is also on offer.
bool Available(const GameState& state, const Skill& skill,
               const std::set<std::string>& superseded) {
  // A skill the book has replaced stops offering its swing along with its
  // levers -- Piercing Arrow II states the whole of the Piercing Arrow it
  // takes over, so both being swingable would be one skill offered twice.
  if (superseded.count(skill.name()) > 0) {
    return false;
  }
  // Another branch's book can share a skill's display name, and learned levels
  // are keyed by that name -- so ask whose book this is before reading a level
  // off it. HoldsSkillFrom rather than HasAdvancement, or a V node would never
  // be swingable: a common node's advancement is nobody's.
  if (!state.character.HoldsSkillFrom(skill)) {
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
  built.set_max_enemies(pulse.max_enemies() > 0 ? pulse.max_enemies()
                                                : skill.max_enemies());
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
  wound.strikes_per_pulse = std::max(1, pulse.casts());
  wound.max_pulses = pulse.max_pulses();
  set.auto_attacks.push_back(std::move(wound));
}

// What the rest of the book hands one skill: strikes added to every swing,
// enemies added to its reach, the clock it fires on, and the share off the
// wait between its casts.
struct SkillBoosts {
  int lines = 0;
  // Strikes on each of its second hits rather than on the swing itself -- the
  // opening hit and every extra hit, which are priced apart from its lines.
  int extra_hit_lines = 0;
  int max_enemies = 0;
  int attacks_per_cast = 0;
  // What is LEFT of the wait, so two cuts combine in reverse the way two
  // sources of ignored defence do. 1.0 is a skill nothing hurries.
  double cooldown_left = 1.0;
  // What the book adds to the BUFF the named skill stands as: seconds on its
  // clock, hits on its shell, and the share that shell takes off a hit it
  // cannot block. Holy Magic Shell's three hypers and nothing else.
  double buff_duration_seconds = 0.0;
  double shield_hits = 0.0;
  double shield_boss_damage_taken_pct = 0.0;
};

// Every such grant in the character's book, summed and keyed by the skill it
// names. Gathered once: the granting skill may be listed after the skill it
// strengthens, and every attack has to be built with the whole of it already
// in.
std::map<std::string, SkillBoosts> BoostsByTarget(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, int bonus) {
  // The nudge SkillLinesAt takes, for the same reason: a rate written as a
  // decimal lands a hair under the level it is meant to buy.
  std::map<std::string, SkillBoosts> by_target;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // Learned levels are keyed by display name and the warrior branches share
    // several, so only the character's own book grants anything -- and a V
    // Matrix node belongs to no book at all, which is why this asks the
    // character rather than the advancement.
    if (!character.HoldsSkillFrom(skill)) {
      continue;
    }
    int learned = EffectiveSkillLevel(character, skill, bonus);
    if (learned <= 0) {
      continue;
    }
    for (const SkillBoost& boost : skill.boost()) {
      // A gift the granting skill has not grown into yet -- the enemy a boost
      // node's Lv20 tier adds, before the node reaches 20.
      if (learned < boost.min_level()) {
        continue;
      }
      int enemies = boost.max_enemies() +
                    WholeValue(boost.max_enemies_per_level() * (learned - 1));
      // The form swings under a name of its own, so a grant reaching it is
      // filed there -- see BoostTargetNames.
      for (const std::string& name : BoostTargetNames(boost)) {
        SkillBoosts& into = by_target[name];
        into.lines += boost.lines();
        into.extra_hit_lines += boost.extra_hit_lines();
        into.max_enemies += enemies;
        into.cooldown_left *= 1.0 - boost.cooldown_pct();
        into.buff_duration_seconds += boost.buff_duration_seconds();
        into.shield_hits += boost.shield_hits();
        into.shield_boss_damage_taken_pct +=
            boost.shield_boss_damage_taken_pct();
        // The clock replaces rather than sums, so the faster of two stands.
        if (boost.attacks_per_cast() > 0 &&
            (into.attacks_per_cast == 0 ||
             boost.attacks_per_cast() < into.attacks_per_cast)) {
          into.attacks_per_cast = boost.attacks_per_cast();
        }
      }
    }
  }
  return by_target;
}

// `skill` with whatever the book grants it folded in, or `skill` itself when
// nothing does. The line ladder is cashed in at `level` on the way, so the
// strike granted lands on top of the ones the skill bought for itself rather
// than being climbed past a second time. An empowered form is handed here
// under its own name, so what it lands beside itself gains with it.
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
  // The hits landed beside the swing, each gaining its own strike: the opening
  // hit only where there is one, since lead_lines says nothing without it.
  if (it->second.extra_hit_lines > 0) {
    if (skill.base().lead_pct() > 0.0) {
      scratch.set_lead_lines(std::max(1, skill.lead_lines()) +
                             it->second.extra_hit_lines);
    }
    for (SwingHit& hit : *scratch.mutable_extra_hit()) {
      hit.set_lines(std::max(1, hit.lines()) + it->second.extra_hit_lines);
    }
  }
  if (it->second.attacks_per_cast > 0) {
    scratch.set_attacks_per_cast(it->second.attacks_per_cast);
  }
  // The whole ladder, so the wait is cut by the same share however far the
  // skill is taught -- and CooldownAt reads the copy without knowing.
  if (it->second.cooldown_left < 1.0) {
    scratch.set_cooldown_seconds(skill.cooldown_seconds() *
                                 it->second.cooldown_left);
    scratch.set_cooldown_seconds_per_level(skill.cooldown_seconds_per_level() *
                                           it->second.cooldown_left);
  }
  return scratch;
}

// A skill's empowered form, as a skill in its own right, so the same damage
// chain builds it. It takes a name of its own -- unlike an own-clock half,
// this really is a different swing, and it must not pick up the permanent
// bonus its parent hands the ordinary version. What a boost hands it on
// purpose is filed under this name -- see SkillBoost::reach.
Skill EmpoweredSkill(const Skill& skill, const EmpoweredForm& upgrade,
                     const std::string& target, SkillKind kind, int reach) {
  Skill form;
  form.set_name(EmpoweredSkillName(target));
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
                         const std::map<std::string, SkillBoosts>& boosts,
                         std::vector<AttackOption>& into) {
  // A form names the attack it stands in for; naming none, it upgrades the
  // attack its own skill already is -- Creeping Toxin detonating the pool it
  // is already spreading.
  const std::string& target =
      upgrade.skill_name().empty() ? skill.name() : upgrade.skill_name();
  for (AttackOption& attack : into) {
    if (attack.name != target) {
      continue;
    }
    Skill form =
        EmpoweredSkill(skill, upgrade, attack.name, kind, attack.max_enemies);
    // What the book grants the form under its own name, which is what a boost
    // following the skill into it was filed under.
    Skill boosted;
    const Skill& swung = Boosted(form, learned, boosts, boosted);
    std::shared_ptr<AttackOption> swing = std::make_shared<AttackOption>(
        AttackFor(state.character.proto(), equipped, weapon_type, &swung,
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
  std::map<std::string, SkillBoosts> boosts =
      BoostsByTarget(state.character, state.skills, bonus);
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
                          SKILL_KIND_ATTACK, boosts, set.attacks);
      AttachEmpoweredForm(state, equipped, weapon_type, skill, upgrade, learned,
                          off_clock, attack_speed, speed_factor, types,
                          SKILL_KIND_AUTO_ATTACK, boosts, set.auto_attacks);
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
  std::map<std::string, SkillBoosts> boosts =
      BoostsByTarget(state.character, state.skills, bonus);
  std::set<std::string> superseded =
      DormantSkillNames(state.character, state.skills, bonus);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    int learned = EffectiveSkillLevel(state.character, skill, bonus);
    if (learned <= 0 || !Available(state, skill, superseded)) {
      continue;
    }
    // Strikes and reach another skill in the book grants this one, folded in
    // before anything is built so the whole chain below sees one skill.
    Skill boosted;
    const Skill& swung = Boosted(skill, learned, boosts, boosted);
    AddAutoModes(proto, total_stats, weapon_type, swung, learned, off_clock,
                 speed_factor, types, set);
    // A skill the fight cannot spend a swing on is done here. Its own-clock
    // halves are already in, which is the whole of what a passive like Weapon
    // Aura contributes -- an aura is not something the character swings.
    if (!Castable(swung)) {
      continue;
    }
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
    // Clocked by something counted rather than by seconds passed: swings
    // landed, or enemies defeated.
    if (swung.attacks_per_cast() > 0 || swung.kills_per_cast() > 0) {
      attack.attacks_per_cast = swung.attacks_per_cast();
      attack.kills_per_cast = swung.kills_per_cast();
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
// job and weapon, plus whatever their passives add, held to the soft cap --
// and then whatever is allowed past it. Asked per attack set, since a buff can
// be one of the things adding.
int AttackSpeedStageFor(const GameState& state, const EquipPrototype& weapon,
                        const DerivedStats& derived) {
  return AttackSpeedStage(BaseAttackSpeedStage(state.character.proto().job(),
                                               weapon.attack_speed()),
                          derived.attack_speed_bonus,
                          derived.uncapped_attack_speed_bonus);
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
  set.freeze_cap = derived.freeze.cap;
  return set;
}

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

// How many of the party raise `skill`, the character among them. What a
// party-shared buff divides its wait by -- see Buff::party_shared.
int PartyHolders(const CharacterInstance& character,
                 absl::Span<const CharacterInstance> party,
                 const Skill& skill) {
  int holders = 1;
  for (const CharacterInstance& ally : party) {
    if (ally.skill_level(skill) > 0) {
      ++holders;
    }
  }
  return holders;
}

// Buff Duration reaches every buff but a V node's. GMS marks all of them
// notIncBuffDuration, so the matrix stands outside the lever entirely -- a
// rule about the whole matrix rather than a quirk of any node, which is why
// it is asked of v_node rather than written into each file.
double BuffDurationFor(const Skill& skill, double buff_duration_pct) {
  return skill.v_node() == V_NODE_KIND_UNSPECIFIED ? buff_duration_pct : 0.0;
}

// What one buff's clock and shell come to once the book has had its say: the
// seconds a hyper adds land before Buff Duration takes its share, one buff
// being one length however many sources wrote it.
BuffOption BuffClockFor(const Buff& buff, int level, const SkillBoosts& boost,
                        double buff_duration_pct, double speed_factor,
                        int stage) {
  BuffOption option;
  // Buff Duration lengthens the buff and not the wait below it, which is why
  // a percentage that grants nothing on its own is worth having.
  option.duration_seconds = (buff.duration_seconds() +
                             buff.duration_seconds_per_level() * (level - 1) +
                             boost.buff_duration_seconds) *
                            (1.0 + buff_duration_pct) * speed_factor;
  // One stage of a shedding buff: the first falls a stage-interval in, the
  // last stands the whole length. Clamped, so a buff shorter than its stages
  // sheds what it has time to and takes the rest down with it.
  if (buff.stages() > 1) {
    option.duration_seconds =
        std::min(option.duration_seconds,
                 buff.stage_interval_seconds() * (stage + 1) * speed_factor);
  }
  if (buff.has_shield()) {
    option.shield_hits = ShieldHitsAt(buff.shield(), level) + boost.shield_hits;
    option.boss_damage_taken_pct = buff.shield().boss_damage_taken_pct() +
                                   boost.shield_boss_damage_taken_pct;
  }
  return option;
}

// The buff list the fight runs, which is the character's with a shedding buff
// written out one entry per stage. Each entry grants one stage's levers and
// carries one stage's clock, so the mask that indexes the damage tables says
// how many stages are still standing.
std::vector<const Skill*> StagedBuffSkills(
    const std::vector<const Skill*>& raised) {
  std::vector<const Skill*> staged;
  for (const Skill* skill : raised) {
    staged.insert(staged.end(), std::max(1, skill->buff().stages()), skill);
  }
  return staged;
}

// What the fight needs to run each buff's clock, at the level it is learned.
// The levers are not here: those are folded into the tables below.
void AddBuffs(const GameState& state,
              const std::vector<const Skill*>& buff_skills, double speed_factor,
              const DerivedStats& derived, CombatParams& params) {
  const double buff_duration_pct = derived.buff_duration_pct;
  const CharacterInstance& character = state.character;
  const std::map<std::string, Skill>& skills = state.skills;
  absl::Span<const CharacterInstance> party = absl::MakeConstSpan(state.party);
  int bonus = BonusSkillLevels(character, skills);
  std::map<std::string, SkillBoosts> boosts =
      BoostsByTarget(character, skills, bonus);
  const Skill* previous = nullptr;
  int stage = 0;
  for (const Skill* skill : buff_skills) {
    stage = skill == previous ? stage + 1 : 0;
    previous = skill;
    int level = EffectiveSkillLevel(character, *skill, bonus);
    const Buff& buff = skill->buff();
    BuffOption option = BuffClockFor(buff, level, boosts[skill->name()],
                                     BuffDurationFor(*skill, buff_duration_pct),
                                     speed_factor, stage);
    option.name = skill->name();
    option.cooldown_seconds =
        ReducedCooldown(CooldownAt(*skill, level),
                        derived.cooldown_reduction_seconds) *
        speed_factor;
    // A party takes turns raising a shared buff, so it comes round on this
    // character as often as the party between them can cast it. Their own wait
    // is untouched: what shortens is the gap they spend without it.
    if (buff.party_shared()) {
      option.cooldown_seconds /= PartyHolders(character, party, *skill);
    }
    SkillEffect held = EffectAt(buff.base(), buff.per_level(), level);
    option.damage_taken_pct = held.damage_taken_pct();
    option.cooldown_reduction_seconds =
        buff.cooldown_reduction_seconds() * speed_factor;
    // Lines rather than seconds, so the pacing band leaves it alone: what it
    // measures is how fast the character lands hits, which is already
    // stretched.
    option.charge_lines = buff.charge_lines();
    option.heal_fraction = held.heal_pct();
    // What raising it costs. A buff a swing lays is paid for by that swing, so
    // it is charged nothing here -- see BuffOption::cast_seconds.
    option.cast_seconds = skill->base_delay_ms() / 1000.0 * speed_factor;
    // The stages of one shedding buff go up on the one cast, so only the first
    // of them is charged for it.
    if (stage > 0) {
      option.cast_seconds = 0.0;
    }
    // A buff hanging off an ATTACK is laid by that swing rather than raised on
    // a wait: what leaves the wound is puncturing something. See
    // BuffOption::laid_by_attack.
    if (skill->kind() == SKILL_KIND_ATTACK) {
      option.laid_by_attack = AttackNamed(params.attacks, skill->name());
      option.cast_seconds = 0.0;
    }
    params.buffs.push_back(std::move(option));
  }
}

// The buffs the rest of the party puts up over this character, on their
// casters' clocks and at their casters' levels. What they grant never reaches
// a damage table: all a party buff can hand over is a share off what a hit
// costs, and the fight takes that off the hit itself.
//
// A caster's Buff Duration lengthens their half of the cast and the party's
// alike: one cloud, one clock, however many are standing in it.
void AddAllyBuffs(const GameState& state, double speed_factor,
                  CombatParams& params) {
  for (const AllyGrant& grant : AllyBuffsFor(
           state.character, state.skills, absl::MakeConstSpan(state.party))) {
    const Buff& buff = grant.skill->buff();
    // The CASTER's book throughout, not the reader's: one cast stands the same
    // length and blocks the same hits over everybody under it. See
    // BuffDurationPctFor.
    double buff_duration_pct = BuffDurationPctFor(*grant.caster, state.skills);
    std::map<std::string, SkillBoosts> boosts =
        BoostsByTarget(*grant.caster, state.skills,
                       BonusSkillLevels(*grant.caster, state.skills));
    BuffOption option = BuffClockFor(
        buff, grant.level, boosts[grant.skill->name()],
        BuffDurationFor(*grant.skill, buff_duration_pct), speed_factor, 0);
    option.name = grant.skill->name();
    option.cooldown_seconds =
        CooldownAt(*grant.skill, grant.level) * speed_factor;
    // A shell that is not the party's shelters its caster and nobody else.
    if (!buff.shield().party()) {
      option.shield_hits = 0.0;
    }
    SkillEffect shared =
        EffectAt(buff.ally_base(), buff.ally_per_level(), grant.level);
    option.damage_taken_pct = shared.damage_taken_pct();
    option.heal_fraction = shared.heal_pct();
    params.ally_buffs.push_back(std::move(option));
  }
}

// Points each bleeding buff's pulse at the buff it belongs to. Run over the
// base set and over every buffed one as it is built -- the fight reads
// whichever set the mask names, so a tag on one of them alone would come and
// go with the buffs.
//
// Matched by name because a pulse keeps its parent skill's name, and so does
// the buff: one skill, one row in the book, one name.
void TagBuffGatedPulses(const std::vector<BuffOption>& buffs,
                        const std::vector<const Skill*>& buff_skills,
                        std::vector<AttackOption>& casts) {
  for (int i = 0; i < static_cast<int>(buffs.size()); ++i) {
    if (i >= static_cast<int>(buff_skills.size()) ||
        buff_skills[i]->buff().pulse().cast_interval_seconds() <= 0.0) {
      continue;
    }
    for (AttackOption& cast : casts) {
      if (cast.name == buffs[i].name) {
        cast.needs_buff = i;
      }
    }
  }
}

// A slot for every combination of the character's buffs, indexed the way
// CombatParams::Attacks reads them: the mask of which are up, less one. Left
// empty, and filled by BuildBuffedSet the first time one is asked for.
void AddBuffedSets(const GameState& state,
                   const std::vector<const Skill*>& buff_skills,
                   const EquipPrototype& weapon, double speed_factor,
                   StatPreset preset, CombatParams& params) {
  if (buff_skills.empty()) {
    return;
  }
  params.buffed_source.state = &state;
  params.buffed_source.weapon = &weapon;
  params.buffed_source.buff_skills = buff_skills;
  params.buffed_source.speed_factor = speed_factor;
  params.buffed_source.preset = preset;
  params.buffed.assign((1 << buff_skills.size()) - 1, std::nullopt);
}

// Halves how far one swing reaches, rounding up. A boss stands its parts a
// room apart -- Zakum's arms down two columns, the dragon around his own
// wings, Pink Bean's statues across the whole arena -- so a sweep that gathers
// eight monsters off a map is not gathering eight of those. Rounded up, so a
// skill still reaches the part it was aimed at.
void HalveReach(std::vector<AttackOption>& attacks) {
  for (AttackOption& attack : attacks) {
    attack.max_enemies = (std::max(1, attack.max_enemies) + 1) / 2;
  }
}

// One combination's attack set, built off what AddBuffedSets kept. Every pass
// the base set was put through is run here too: a window the fight picks a
// swing from has to be the same shape as the one it picked from a moment ago.
AttackSet BuildBuffedSet(const CombatParams& params, int mask) {
  const BuffedSetSource& source = params.buffed_source;
  std::vector<const Skill*> up;
  for (int i = 0; i < static_cast<int>(source.buff_skills.size()); ++i) {
    if ((mask & (1 << i)) != 0) {
      up.push_back(source.buff_skills[i]);
    }
  }
  DerivedStats derived = DerivedStatsFor(
      source.state->character, source.state->skills, absl::MakeConstSpan(up),
      source.state->party, source.preset);
  AttackSet set = BuildAttackSet(*source.state, derived, *source.weapon,
                                 source.speed_factor, params.types);
  TagBuffGatedPulses(params.buffs, source.buff_skills, set.auto_attacks);
  if (source.halve_reach) {
    HalveReach(set.attacks);
    HalveReach(set.auto_attacks);
    HalveReach(set.triggered_attacks);
  }
  return set;
}

}  // namespace

double HoldSeconds(const ChannelHold& hold, int pulses) {
  return std::max(hold.min_seconds,
                  pulses * hold.pulse_seconds + hold.finish_seconds);
}

// The window `mask` names, built now if this is the first time it was asked
// for. Null for a mask no combination of buffs reaches, which the four readers
// below answer with the unbuffed lists.
const AttackSet* CombatParams::Window(int mask) const {
  if (mask <= 0 || mask > static_cast<int>(buffed.size())) {
    return nullptr;
  }
  std::optional<AttackSet>& slot = buffed[mask - 1];
  if (!slot.has_value()) {
    slot = BuildBuffedSet(*this, mask);
  }
  return &slot.value();
}

const std::vector<AttackOption>& CombatParams::Attacks(int mask) const {
  const AttackSet* set = Window(mask);
  return set == nullptr ? attacks : set->attacks;
}

const std::vector<AttackOption>& CombatParams::AutoAttacks(int mask) const {
  const AttackSet* set = Window(mask);
  return set == nullptr ? auto_attacks : set->auto_attacks;
}

const std::vector<AttackOption>& CombatParams::TriggeredAttacks(
    int mask) const {
  const AttackSet* set = Window(mask);
  return set == nullptr ? triggered_attacks : set->triggered_attacks;
}

int CombatParams::FreezeCap(int mask) const {
  const AttackSet* set = Window(mask);
  return set == nullptr ? freeze_cap : set->freeze_cap;
}

namespace {

// What the character brings to being hit, which is the same whichever mob is
// hitting them.
DefenseStats DefenseFor(const GameState& state, const DerivedStats& derived) {
  DefenseStats defense;
  defense.level = state.character.proto().level();
  defense.def = derived.def;
  defense.damage_taken_pct = derived.damage_taken_pct;
  defense.dodge_chance = derived.dodge_chance;
  defense.enemy_attack_pct = derived.enemy_attack_pct;
  defense.enemy_attack_reaches_boss = derived.enemy_attack_reaches_boss;
  defense.arcane_taken = derived.arcane_taken_factor;
  return defense;
}

// Everything about the character that does not depend on what is in front of
// them: their pool, what their passives pay, and the clocks the band stretches.
// The two intervals are left to the caller -- a boss fight runs off neither.
void AddPacing(const GameState& state, const DerivedStats& derived,
               double speed_factor, CombatParams& params) {
  params.max_player_hp = derived.max_hp;
  params.player_level = state.character.proto().level();
  params.beat_heal_fraction = kBeatHealFraction;
  params.damage_reflect_pct = derived.damage_reflect_pct;
  params.hp_recover_pct = derived.hp_recover_pct;
  params.exp_pct = derived.exp_pct;
  params.meso_pct = MesoBonus(derived);
  params.meso_final_mult = derived.meso_final_mult;
  params.item_drop_pct = derived.item_drop_pct;
  // The band stretches the interval between pulses, the way it stretches every
  // other clock in the fight. The helping each one pours is untouched.
  params.regen_pulses.clear();
  for (const RegenPulse& pulse : derived.regen_pulses) {
    params.regen_pulses.push_back(
        {pulse.pct, pulse.hp, pulse.interval_seconds * speed_factor});
  }
  params.revive_cooldown_seconds =
      derived.revive_cooldown_seconds * speed_factor;
  params.freeze_cap = derived.freeze.cap;
}

// Every attack the character can swing at the types already in `params`: as
// they stand, and then one table per combination of buffs they can put up.
// What being hit costs them is read off the unbuffed stats -- nothing yet
// buffs a pool, and the one buff that softens a hit takes its share off the
// hit itself; see BuffOption.damage_taken_pct.
void AddAttacks(const GameState& state, const DerivedStats& derived,
                const EquipPrototype& weapon, double speed_factor,
                StatPreset preset, CombatParams& params) {
  AttackSet base =
      BuildAttackSet(state, derived, weapon, speed_factor, params.types);
  params.attacks = std::move(base.attacks);
  params.auto_attacks = std::move(base.auto_attacks);
  params.triggered_attacks = std::move(base.triggered_attacks);
  params.dot_count = DotSlotsNeeded(params);
  std::vector<const Skill*> buff_skills =
      StagedBuffSkills(BuffSkillsFor(state.character, state.skills));
  if (static_cast<int>(buff_skills.size()) > kMaxBuffWindows) {
    buff_skills.resize(kMaxBuffWindows);
  }
  AddBuffs(state, buff_skills, speed_factor, derived, params);
  AddAllyBuffs(state, speed_factor, params);
  AddBuffedSets(state, buff_skills, weapon, speed_factor, preset, params);
  TagBuffGatedPulses(params.buffs, buff_skills, params.auto_attacks);
}

// Every list a swing can be picked from, the buffed windows included: a table
// built for one combination of buffs reaches as far as the unbuffed one does.
// The windows are not built yet, so they are marked rather than walked.
void HalveBossReach(CombatParams& params) {
  HalveReach(params.attacks);
  HalveReach(params.auto_attacks);
  HalveReach(params.triggered_attacks);
  params.buffed_source.halve_reach = true;
}

}  // namespace

const EquipPrototype* EquippedWeapon(const GameState& state) {
  const std::map<EquipSlot, EquipInstance>& equipped =
      state.character.equipped();
  std::map<EquipSlot, EquipInstance>::const_iterator it =
      equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  return it == equipped.end() ? nullptr : &it->second.prototype();
}

std::string BossEncounterKey(const std::string& boss,
                             const std::string& difficulty, int phase) {
  return "boss:" + boss + ":" + difficulty + ":" + std::to_string(phase);
}

CombatParams ComputeCombatParams(const GameState& state) {
  CombatParams params;
  params.encounter = state.current_map;
  std::map<std::string, MapData>::const_iterator map_it =
      state.maps.find(state.current_map);
  const EquipPrototype* weapon = EquippedWeapon(state);
  if (map_it == state.maps.end() || weapon == nullptr) {
    return params;
  }

  DerivedStats derived =
      DerivedStatsFor(state.character, state.skills, {}, state.party);
  // What the map's Arcane Force requirement does to both sides of the fight.
  // Written onto derived because that is the one struct every damage builder
  // below already carries -- the requirement is the map's, not the
  // character's, and neither of them alone can answer it.
  ArcaneFactors arcane = ArcaneFactorsFor(state.character.arcane_force(),
                                          map_it->second.arcane_force());
  derived.arcane_damage_factor = arcane.damage_dealt;
  derived.arcane_taken_factor = arcane.damage_taken;
  // The pace the whole encounter runs at, and the only thing here that asks
  // the character's level directly: the game stretches out as they climb.
  double speed_factor = GameSpeedFactor(state.character.proto().level());
  params.respawn_seconds = kRespawnIntervalSeconds * speed_factor;
  params.hit_seconds = kMobHitIntervalSeconds * speed_factor;
  AddPacing(state, derived, speed_factor, params);
  AddTypes(state, map_it->second.spawns(), DefenseFor(state, derived),
           derived.scar.enemy_attack_pct, params);
  if (params.types.empty()) {
    return params;
  }
  AddAttacks(state, derived, *weapon, speed_factor, StatPreset::kFarming,
             params);
  params.active = true;
  return params;
}

CombatParams ComputeBossParams(const GameState& state,
                               const std::string& boss_key,
                               const BossDifficulty& difficulty, int phase) {
  CombatParams params;
  if (phase < 0 || phase >= difficulty.phases_size()) {
    return params;
  }
  params.encounter = BossEncounterKey(boss_key, difficulty.name(), phase);
  const EquipPrototype* weapon = EquippedWeapon(state);
  if (weapon == nullptr) {
    return params;
  }

  // A boss fight is what the bossing allocation is for.
  DerivedStats derived = DerivedStatsFor(state.character, state.skills, {},
                                         state.party, StatPreset::kBossing);
  // A boss fight runs in real time whatever the character's level: the pacing
  // band stretches an idle map out so it can be left alone, and a fight the
  // player is sitting and watching wants neither the stretch nor a beat.
  // Both intervals stay at 0 -- nothing respawns, and nothing hits back yet.
  AddPacing(state, derived, 1.0, params);
  AddTypes(state, difficulty.phases(phase).spawns(), DefenseFor(state, derived),
           derived.scar.enemy_attack_pct, params);
  if (params.types.empty()) {
    return params;
  }
  AddAttacks(state, derived, *weapon, 1.0, StatPreset::kBossing, params);
  HalveBossReach(params);
  // A boss's parts are hit hardest-first: see CombatParams::focus_healthiest.
  params.focus_healthiest = true;
  // The boss screen draws every line as a number, which the map does not.
  params.record_damage_lines = true;
  params.active = true;
  return params;
}

}  // namespace ms
