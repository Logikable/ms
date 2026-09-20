#include "src/combat/encounter.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/consumables.h"
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

// How often the engaged mob hits back, before pacing stretches it. The one
// number we chose about damage taken; the rest is the GMS formula. Only the
// one mob swings, or a crowded beginner map would outdo a sparse high-level
// one.
constexpr double kMobHitIntervalSeconds = 1.5;

// The other half of that knob: a map whose mobs take less than this between
// beats can be held indefinitely, so it can be survived by enduring it.
constexpr double kBeatHealFraction = 0.10;

// For a cast that deals no damage, which sets nothing off.
void ClearFinalAttacks(AttackOption& attack) {
  attack.final_attack_damage.clear();
  attack.final_attack_rolls.clear();
  attack.per_swing_final_attack_damage.clear();
  attack.per_swing_final_attack_rolls.clear();
  attack.per_swing_final_attack_enemies = 1;
}

// Drops the Final Attacks only a swing sets off and re-adds the banks off what
// survived. A filter rather than a wipe: Frost Ark's shock is struck by the orb
// as readily as by the bolts -- see Skill.follows_own_clock.
void KeepOwnClockFinalAttacks(AttackOption& attack) {
  std::vector<double>* banks[] = {&attack.final_attack_damage,
                                  &attack.per_swing_final_attack_damage};
  std::vector<FinalAttackRoll>* lists[] = {
      &attack.final_attack_rolls, &attack.per_swing_final_attack_rolls};
  attack.per_swing_final_attack_enemies = 1;
  for (int i = 0; i < 2; ++i) {
    std::vector<FinalAttackRoll> kept;
    for (FinalAttackRoll& roll : *lists[i]) {
      if (roll.follows_own_clock) {
        kept.push_back(std::move(roll));
      }
    }
    *lists[i] = std::move(kept);
    if (lists[i]->empty()) {
      banks[i]->clear();
      continue;
    }
    // The bank is the sum of what each source is worth, so what is left of it
    // is that sum over the rolls that stayed.
    std::fill(banks[i]->begin(), banks[i]->end(), 0.0);
    for (const FinalAttackRoll& roll : *lists[i]) {
      for (std::size_t t = 0; t < banks[i]->size(); ++t) {
        (*banks[i])[t] += roll.damage[t] * roll.chance * roll.count;
      }
    }
  }
  // The widest of the sources that stayed, exactly as AddFinalAttacks took it.
  for (const FinalAttackRoll& roll : attack.per_swing_final_attack_rolls) {
    attack.per_swing_final_attack_enemies =
        std::max(attack.per_swing_final_attack_enemies, roll.max_enemies);
  }
}

// Strips what rides the character's own swing -- its recovery, Final Attacks,
// carried poison and side strike. Anything on a clock of its own sets none of
// them off. A burn the SKILL states stays: Ifrit's flames burn what they touch
// whoever is swinging.
void ClearSwingRiders(AttackOption& attack) {
  attack.hp_recover_pct = 0.0;
  attack.side = nullptr;
  attack.procs.clear();
  // A summon leaves ice but never SPENDS the pile: GMS's lightning orb
  // attacks without consuming freezing stacks.
  attack.freeze_spends = false;
  attack.freeze_fd_per_stack = 0.0;
  KeepOwnClockFinalAttacks(attack);
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

// What throws a meso is the character swinging, so a pulse on its own clock
// carries none however the passive reads.
void StripMesoDrops(DerivedStats& derived) {
  std::vector<FinalAttackSource> kept;
  for (const FinalAttackSource& source : derived.final_attacks) {
    if (!source.per_line) {
      kept.push_back(source);
    }
  }
  derived.final_attacks = std::move(kept);
}

// What one burn is worth per mob type, off the stat line its swing was priced
// off but on its own multiplier and strikes: a burn is what the swing left
// behind, not the swing. `boost` is what the book aims at the burn by name;
// only its multiplier and clock are read, the rest riding in with `offense`.
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

// Ice leaves a stack per line, lightning spends one per line, and both take
// the critical damage a held stack grants: a frozen enemy is frozen whichever
// element hits it. That crit damage becomes the share it adds to the swing's
// MEAN, the only shape the fight can multiply by -- crit rolls per line and
// its bonus is normalised away.
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
    // A stack per line is what the element buys. A skill stating a count of
    // its own replaces that outright, both halves of it -- see FreezeBuild.
    if (skill->has_freeze_build()) {
      attack.freeze_build = skill->freeze_build().crowd();
      attack.freeze_build_alone = skill->freeze_build().alone();
    } else {
      attack.freeze_build = attack.lines;
    }
    // Glacial Fury's magic attack as a share of the swing: damage is linear
    // in the attack behind it, and a share is what the fight can multiply.
    if (offense.attack > 0) {
      attack.freeze_matt_gain =
          derived.freeze.matt_per_stack / static_cast<double>(offense.attack);
    }
  }
  if (HasTag(skill, SKILL_TAG_LIGHTNING)) {
    attack.freeze_spends = true;
    attack.freeze_fd_per_stack = derived.freeze.final_dmg_pct_per_stack;
    attack.freeze_lines_per_spend =
        std::max(1, skill->freeze_lines_per_spend());
  }
}

// One strike of a SwingHit: its own multiplier, lines and critical rate over
// what the character brought. A group of its own, so it rolls separately.
HitGroup SwingHitGroup(const SwingHit& hit, const OffenseStats& offense,
                       int level, const std::vector<CombatType>& types,
                       int& lines) {
  OffenseStats extra = offense;
  SkillEffect lands = EffectAt(hit.base(), hit.per_level(), level);
  extra.skill_pct = lands.skill_pct();
  extra.normal_skill_pct = lands.normal_skill_pct();
  extra.normal_pct += lands.normal_pct();
  extra.crit_rate += lands.crit_rate();
  // Multiplied into what the character and the swing already bring, the way
  // two final damage sources always meet -- see SwingHit.
  extra.final_dmg_pct =
      (1.0 + extra.final_dmg_pct) * (1.0 + lands.final_dmg_pct()) - 1.0;
  extra.lines = std::max(1, hit.lines());
  // The shadow copies it as it copies the rest of the swing. Reset because
  // the line count just changed under it. See SwingHit.skips_mirror.
  bool shadowed = !hit.skips_mirror() && offense.mirror_lines > 0;
  extra.mirror_lines = shadowed ? extra.lines : 0;
  HitGroup group;
  group.rolls = RollsFor(extra);
  for (const CombatType& type : types) {
    group.damage.push_back(ExpectedAttackDamage(extra, *type.mob));
  }
  lines = extra.lines;
  return group;
}

// A second hit the same swing lands, priced on its own and summed in. The
// group it leaves is what makes it roll separately -- see SwingHit.
void AddSwingHit(const SwingHit& hit, const OffenseStats& offense, int level,
                 const std::vector<CombatType>& types, AttackOption& attack) {
  SkillEffect lands = EffectAt(hit.base(), hit.per_level(), level);
  int lines = 0;
  HitGroup group = SwingHitGroup(hit, offense, level, types, lines);
  int casts = SwingHitCasts(hit);
  // Banked apart, as a Final Attack with its own reach is: it lands on
  // enemies the swing never touched.
  if (hit.max_enemies() > 0) {
    if (attack.wide_hit_damage.empty()) {
      attack.wide_hit_damage.assign(types.size(), 0.0);
    }
    for (std::size_t i = 0; i < types.size() && i < group.damage.size(); ++i) {
      attack.wide_hit_damage[i] += group.damage[i] * casts;
    }
    for (int i = 0; i < casts; ++i) {
      attack.wide_hit_groups.push_back(group);
    }
    // The widest of them, as the Final Attack bank takes it: they land on the
    // one swing, so the crowd is the furthest any of them reaches.
    attack.wide_hit_enemies =
        std::max(attack.wide_hit_enemies, hit.max_enemies());
    attack.hp_recover_pct += lands.hp_recover_pct() * lines * casts;
    return;
  }
  for (std::size_t i = 0; i < types.size() && i < group.damage.size(); ++i) {
    attack.damage_per_hit[i] += group.damage[i] * casts;
  }
  // Landed once per strike, as the swing's own is: Sword Illusion sets off
  // five explosions and each of them rolls for itself.
  for (int i = 0; i < casts; ++i) {
    attack.groups.push_back(group);
  }
  // A recovery a HIT states is paid per LINE of it -- GMS's "for every final
  // attack that lands" -- unlike the swing's own, stated against the cast.
  attack.hp_recover_pct += lands.hp_recover_pct() * lines * casts;
}

// What has been priced already is ONE pulse, so the closing strike is added
// beside it and the swing restated as a full hold. A hold that GROWS beats at
// two strengths, so that restatement sums two runs; the grown pulse is kept on
// the hold, out of the groups, which read everywhere as "the first is a pulse,
// the rest are the finish". The pulse clock is not scaled by attack speed, for
// the reason a key-down swing's is not: the rate belongs to the skill.
void AddChannel(const Skill& skill, const OffenseStats& offense, int level,
                const std::vector<CombatType>& types, double speed_factor,
                AttackOption& attack) {
  const Channel& channel = skill.channel();
  if (channel.max_pulses() <= 0 || channel.pulse_interval_ms() <= 0) {
    return;
  }
  std::vector<double> pulse = attack.damage_per_hit;
  // The swing's own recovery is ONE pulse's, exactly as its damage is, so it
  // moves to the hold before the finish adds its own on top.
  double pulse_recover = attack.hp_recover_pct;
  attack.hp_recover_pct = 0.0;
  // A hold that ends by letting go leaves no strike, and pricing an empty one
  // would leave a group floored at a point of damage per enemy.
  if (channel.has_finish()) {
    AddSwingHit(channel.finish(), offense, level, types, attack);
  }
  ChannelHold& hold = attack.channel;
  hold.hp_recover_pct = pulse_recover;
  hold.pulses = channel.max_pulses();
  hold.pulse_seconds = channel.pulse_interval_ms() / 1000.0 * speed_factor;
  hold.finish_seconds = channel.finish_delay_ms() / 1000.0 * speed_factor;
  hold.min_seconds = attack.swing_seconds;
  hold.damage_taken_pct = channel.damage_taken_pct();
  // Game-scaled like every other clock here: the bank fills in the same
  // stretched seconds the pulses fall in.
  hold.charge_seconds = channel.charge_seconds() * speed_factor;
  hold.max_charges = channel.max_charges();
  hold.pulses_per_charge = channel.pulses_per_charge();
  // The pulses fitting inside the floor: the fewest a cast can be let go
  // after. At least one, or the swing would be its finish alone.
  hold.min_pulses =
      std::clamp(static_cast<int>((hold.min_seconds - hold.finish_seconds) /
                                  hold.pulse_seconds),
                 1, hold.pulses);
  int small = hold.pulses;
  if (channel.has_grown()) {
    int lines = 0;
    hold.grown = SwingHitGroup(channel.grown(), offense, level, types, lines);
    hold.small_pulses = std::clamp(channel.small_pulses(), 0, hold.pulses);
    small = hold.small_pulses;
  }
  for (std::size_t i = 0; i < pulse.size() && i < attack.damage_per_hit.size();
       ++i) {
    attack.damage_per_hit[i] += pulse[i] * (small - 1);
    if (i < hold.grown.damage.size()) {
      attack.damage_per_hit[i] += hold.grown.damage[i] * (hold.pulses - small);
    }
  }
  attack.swing_seconds = HoldSeconds(hold, hold.pulses);
}

// The clocks and conditions a skill puts on its own swing. A key-down skill
// fires at its own rate however fast the weapon swings, so it is handed the
// stage the formula is the identity at. The game's pacing still stretches
// every clock here: that is the game running slower than GMS, not the weapon.
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
  // An attack GMS lets the player throw mid-swing plays no cast action, so the
  // animation the file records is not what it costs. See Skill.waives_cast.
  if (skill->waives_cast()) {
    delay_ms = kWaivedCastMs;
  }
  if (skill->fixed_delay() || skill->waives_cast()) {
    stage = kUnscaledAttackSpeedStage;
  }
  attack.swing_seconds = SwingIntervalSeconds(delay_ms, stage) * speed_factor;
  attack.cooldown_seconds =
      ReducedCooldown(CooldownAt(*skill, level),
                      derived.cooldown_reduction_seconds) *
      speed_factor;
  // Game-scaled with the wait it comes off, or a barrage on a cleared map
  // would hand back more of the band than it ever cost.
  attack.cooldown_refund_seconds =
      skill->cooldown_refund_seconds() * speed_factor;
  // Game-scaled, so a freeze covers the same span of the fight it covers in
  // GMS.
  attack.freeze_seconds = skill->freeze_seconds() * speed_factor;
  // Game-scaled like the ice beside it, for the same reason: what a stun
  // covers is the same span of the fight it covers in GMS.
  attack.stun_seconds = skill->stun().duration_seconds() * speed_factor;
  attack.stun_lift_pct = skill->stun().final_dmg_pct();
  // A swing collects a stun's lift where it carries the tag and is not the
  // skill that left it -- GMS excludes Jupiter Thunder from its own shock.
  attack.collects_stun_lift =
      derived.stun_lift.lifted_tag != SKILL_TAG_UNSPECIFIED &&
      HasTag(skill, derived.stun_lift.lifted_tag) &&
      skill->name() != derived.stun_lift.from_skill;
  // The mark beside it. Nothing excludes the skill that left one: the angel
  // carries no element, so the tag keeps it off its own mark anyway.
  attack.mark_seconds = skill->mark().duration_seconds() * speed_factor;
  attack.mark_lift_pct = skill->mark().final_dmg_pct();
  attack.collects_mark_lift =
      derived.mark_lift.lifted_tag != SKILL_TAG_UNSPECIFIED &&
      HasTag(skill, derived.mark_lift.lifted_tag);
  // Both ride anything that lands on the mob, a summon's pulse included: the
  // mob is in that state whatever is hitting it.
  attack.scar_fd = derived.scar.final_dmg_pct;
  attack.fd_when_afflicted = derived.condition.final_dmg_pct_when_afflicted;
  attack.fd_per_dot = derived.condition.final_dmg_pct_per_dot;
  attack.dot_count_cap = derived.condition.dot_count_cap;
  SkillEffect granted = EffectAt(skill->base(), skill->per_level(), level);
  attack.heal_fraction = granted.heal_pct();
  if (skill->kind() != SKILL_KIND_ATTACK) {
    return;
  }
  // The scar and the recovery are the SWING's, kept off anything on a clock
  // of its own: GMS scars with the sword being swung. See WithoutSwingLevers.
  attack.scar_chance = derived.scar.chance;
  attack.scar_seconds = derived.scar.seconds * speed_factor;
  attack.hp_recover_pct = granted.hp_recover_pct();
}

// The harder opening hit some swings land before spreading -- GMS's "strikes
// one, then detonates in place". Same character and weapon on the skill's
// other multiplier; only the target count differs, which is the fight's.
void AddLeadHit(const Skill& skill, const OffenseStats& offense, int level,
                const std::vector<CombatType>& types, AttackOption& attack) {
  if (skill.base().lead_pct() <= 0.0) {
    return;
  }
  OffenseStats lead = offense;
  lead.skill_pct =
      skill.base().lead_pct() + skill.per_level().lead_pct() * (level - 1);
  lead.lines = std::max(1, skill.lead_lines());
  // The shadow copies the opening hit as it copies every other line. Reset
  // because lead.lines just changed under it.
  lead.mirror_lines = offense.mirror_lines > 0 ? lead.lines : 0;
  for (const CombatType& type : types) {
    attack.lead_damage.push_back(ExpectedAttackDamage(lead, *type.mob));
  }
  attack.lead_rolls = RollsFor(lead);
  attack.lead_enemies = std::max(1, skill.lead_enemies());
}

// Marks on what the swing reached rather than part of the strike, so they are
// priced here and paid on their own clock. What one is worth is settled now
// and carried for its whole life, which is why a burn lit under a buff keeps
// the buffed number.
//
// The character's own come first and in their own order, so every swing writes
// one poison to one slot. They are priced off the bare `follow` for the reason
// a Final Attack is: the poison is on the claw, not in the skill.
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
// percent, priced off the bare `follow` and taking neither the skill's
// multiplier nor its lines. A source naming a tag follows only the swings
// carrying it, and each surviving source keeps its own entry, rolling alone.
//
// A source rolling per line rolls `swing_lines` times -- four lines knock four
// mesos loose. The shadow's copies are not the character's lines.
// The follow-on line aimed at one source. Taken from `carried` each time
// round, or the last source carrying any of it would hand it to the next.
OffenseStats FollowFor(const OffenseStats& carried,
                       const FinalAttackSource& source) {
  OffenseStats follow = carried;
  // Boss damage of its own, on top of the character's: Blood Money brands the
  // coins, not the Shadower.
  follow.boss_pct = carried.boss_pct + source.boss_pct;
  follow.damage_pct = carried.damage_pct + source.damage_bonus_pct;
  // Ignored defence MEETS the character's rather than adding, as two sources
  // of it always do.
  follow.ied = CombineIgnoredDefense(carried.ied, source.ied);
  follow.crit_rate = carried.crit_rate + source.crit_rate;
  follow.final_dmg_pct =
      (1.0 + carried.final_dmg_pct) * (1.0 + source.final_dmg_pct) - 1.0;
  follow.skill_pct = source.damage_pct;
  // Points against anything that is not a boss, on the source's own
  // multiplier.
  follow.normal_skill_pct = source.normal_skill_pct;
  // Its own strikes, not the swing's: a Night Lord's mark throws three stars
  // behind a four-star swing, and each of the three rolls on its own.
  follow.lines = source.lines;
  return follow;
}

void AddFinalAttacks(const Skill* skill, const DerivedStats& derived,
                     OffenseStats follow, int level, int swing_lines,
                     const std::vector<CombatType>& types,
                     AttackOption& attack) {
  attack.final_attack_damage.assign(types.size(), 0.0);
  attack.per_swing_final_attack_damage.assign(types.size(), 0.0);
  // What this swing keeps of the two chances: shaking a coin loose, and
  // setting the extra hit off. See SkillEffect.meso_drop_cut and
  // final_attack_chance_cut.
  double meso_kept = 1.0;
  double follow_kept = 1.0;
  if (skill != nullptr) {
    meso_kept -= skill->base().meso_drop_cut() +
                 skill->per_level().meso_drop_cut() * (level - 1);
    meso_kept = std::max(0.0, meso_kept);
    follow_kept -= skill->base().final_attack_chance_cut() +
                   skill->per_level().final_attack_chance_cut() * (level - 1);
    follow_kept = std::max(0.0, follow_kept);
  }
  // The character's own line, before a source adds to any of it.
  const OffenseStats carried = follow;
  for (const FinalAttackSource& source : derived.final_attacks) {
    if (source.required_tag != SKILL_TAG_UNSPECIFIED &&
        !HasTag(skill, source.required_tag)) {
      continue;
    }
    FinalAttackRoll roll;
    // The cut the swing states rides every source, and the coin's own rides
    // the one source that is a coin.
    roll.chance = source.chance * follow_kept;
    if (source.per_line) {
      roll.chance *= meso_kept;
    }
    follow = FollowFor(carried, source);
    roll.count = source.per_line ? swing_lines : 1;
    roll.follows_own_clock = source.follows_own_clock;
    roll.max_enemies = source.max_enemies;
    roll.rolls = RollsFor(follow);
    // A source with a reach of its own is banked apart: what the swing is
    // worth has to add it over that reach rather than over the swing's.
    std::vector<double>& bank = source.max_enemies > 0
                                    ? attack.per_swing_final_attack_damage
                                    : attack.final_attack_damage;
    for (std::size_t i = 0; i < types.size(); ++i) {
      roll.damage.push_back(ExpectedAttackDamage(follow, *types[i].mob));
      bank[i] += roll.damage.back() * roll.chance * roll.count;
    }
    if (source.max_enemies > 0) {
      // The widest of the sources banked together: they roll on the one swing,
      // so the crowd they land on is the furthest any of them reaches.
      attack.per_swing_final_attack_enemies =
          std::max(attack.per_swing_final_attack_enemies, source.max_enemies);
      attack.per_swing_final_attack_rolls.push_back(std::move(roll));
    } else {
      attack.final_attack_rolls.push_back(std::move(roll));
    }
  }
  if (attack.final_attack_rolls.empty()) {
    attack.final_attack_damage.clear();
  }
  if (attack.per_swing_final_attack_rolls.empty()) {
    attack.per_swing_final_attack_damage.clear();
  }
}

// The strike this swing sets off on a wait of its own, priced as a swing in
// its own right. It is not the character's swing, so nothing rides it.
void AddSideStrike(const Character& proto, const EquipStats& equipped,
                   EquipType weapon, const Skill& skill, int level,
                   const std::vector<CombatType>& types,
                   const DerivedStats& derived, double speed_factor,
                   AttackOption& attack) {
  const SideStrike& side = skill.side_strike();
  // Off the character's stat line, twice over. What a boost aimed at this
  // skill by NAME belongs to the swing -- GMS's Showdown - Reinforce leaves
  // the shuriken alone -- and the SKILL is not passed either, so its own
  // levers stop at the swing too. What the strike is worth is what it
  // states.
  PassiveOffense unaimed = PassiveOffenseFor(derived);
  unaimed.skill_bonus.erase(skill.name());
  OffenseStats stats =
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      equipped, weapon, nullptr, level, unaimed);
  SkillEffect thrown = EffectAt(side.base(), side.per_level(), level);
  stats.skill_pct = thrown.skill_pct();
  stats.normal_skill_pct = thrown.normal_skill_pct();
  stats.normal_pct += thrown.normal_pct();
  stats.crit_rate += thrown.crit_rate();
  stats.lines = std::max(1, side.lines());
  stats.mirror_lines = stats.lines;
  AttackOption strike;
  strike.name = side.label().empty() ? attack.name : side.label();
  strike.max_enemies =
      side.max_enemies() > 0 ? side.max_enemies() : attack.max_enemies;
  strike.cooldown_seconds = side.cooldown_seconds() * speed_factor;
  strike.scatter_hits = side.scatter().hits();
  strike.scatter_repeat_kept = 1.0 + side.scatter().repeat_final_dmg_pct();
  strike.scatter_hits_per_dot = side.scatter().hits_per_dot();
  strike.scatter_max_hits = side.scatter().max_hits();
  strike.scatter_max_hits_per_enemy = side.scatter().max_hits_per_enemy();
  std::vector<double> once;
  for (const CombatType& type : types) {
    once.push_back(ExpectedAttackDamage(stats, *type.mob));
  }
  // Landed once per strike, as an extra hit's are: Mighty Mjolnir leaves a
  // shockwave on every enemy the hammer tracks down, and each rolls for itself.
  int casts = std::max(1, side.casts());
  for (double damage : once) {
    strike.damage_per_hit.push_back(damage * casts);
  }
  for (int i = 0; i < casts; ++i) {
    strike.groups.push_back({once, RollsFor(stats)});
  }
  attack.side = std::make_shared<const AttackOption>(std::move(strike));
}

// One attack's damage against every mob type. `skill` is null for the bare
// poke, a plain 100% swing on one target. `equipped` is what the character
// wears plus what their passives grant: the chain cannot tell the two apart.
// Turns the one priced strike into the swing the skill actually throws: the
// pool damage every line pays, how many times it lands, and how it scatters.
void AddCastShape(const Skill* skill, int level, const DerivedStats& derived,
                  const OffenseStats& offense, double speed_factor,
                  AttackOption& attack) {
  if (skill != nullptr) {
    // Damage off the character's own pool lands AFTER the chain, so no
    // multiplier reaches it. Every line pays it, as GMS pays it per attack.
    double pool = (skill->base().max_hp_damage_pct() +
                   skill->per_level().max_hp_damage_pct() * (level - 1)) *
                  derived.max_hp * SkillLinesAt(*skill, level);
    for (double& damage : attack.damage_per_hit) {
      damage += pool;
    }
  }
  // What has been priced is ONE strike. A skill slashing several times lands
  // it again for each, every one rolling its own mastery and criticals.
  int casts = skill != nullptr ? SkillCasts(*skill) : 1;
  // Strikes told apart in time stay ONE here: the fight lands it again per
  // bolt, clearing the dead between. See Skill.cast_interval_ms.
  bool in_sequence = skill != nullptr && skill->cast_interval_ms() > 0;
  HitGroup strike{attack.damage_per_hit, RollsFor(offense)};
  for (int i = 0; i < (in_sequence ? 1 : casts); ++i) {
    attack.groups.push_back(strike);
  }
  if (!in_sequence) {
    for (double& damage : attack.damage_per_hit) {
      damage *= casts;
    }
  }
  if (skill == nullptr) {
    return;
  }
  attack.strikes_in_sequence = in_sequence ? std::max(1, casts) : 1;
  // Game-scaled like every other clock: the pacing band stretches the beat
  // between the bolts exactly as far as it stretches the swing behind them.
  attack.cast_interval_seconds =
      in_sequence ? skill->cast_interval_ms() / 1000.0 * speed_factor : 0.0;
  attack.pierce_gain_pct = skill->pierce_gain_pct();
  attack.lines = SkillLinesAt(*skill, level) * (in_sequence ? 1 : casts);
  // The same swing throughout; how many land where is the fight's business,
  // as the opening hit's target count is.
  attack.scatter_hits = skill->scatter().hits();
  attack.scatter_repeat_kept = 1.0 + skill->scatter().repeat_final_dmg_pct();
  attack.scatter_hits_per_dot = skill->scatter().hits_per_dot();
  attack.scatter_max_hits = skill->scatter().max_hits();
  attack.scatter_max_hits_per_enemy = skill->scatter().max_hits_per_enemy();
}

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
  AddCastShape(skill, level, derived, offense, speed_factor, attack);
  for (const SwingProc& proc : derived.procs) {
    attack.procs.push_back({proc.chance, proc.damage_pct, proc.hp_recover_pct});
  }
  AddFreezeStacks(skill, derived, offense, types, attack);
  if (skill != nullptr) {
    AddLeadHit(*skill, offense, level, types, attack);
    // Two hits at once -- the hammer and the brand it leaves exploding --
    // differing only in multiplier, so each is priced alone and summed.
    for (const SwingHit& hit : skill->extra_hit()) {
      AddSwingHit(hit, offense, level, types, attack);
    }
    // Hits another skill hands this one by name, priced as its own and
    // already read at the granting level -- see SkillBoost::extra_hit.
    std::map<std::string, SkillBonus>::const_iterator aimed =
        derived.skill_bonus.find(skill->name());
    if (aimed != derived.skill_bonus.end()) {
      for (const SwingHit& hit : aimed->second.extra_hit) {
        AddSwingHit(hit, offense, level, types, attack);
      }
      // The wound another skill hands this one by name. Assassinate and Sonic
      // Blow say nothing about it; Trickblade names them -- see Wound.
      attack.wound_stacks = aimed->second.wound_stacks;
      attack.wound_max_stacks = aimed->second.wound_max_stacks;
      attack.wound_seconds = aimed->second.wound_seconds * speed_factor;
    }
    AddChannel(*skill, offense, level, types, speed_factor, attack);
  }
  // The bare stat line everything the swing sets off is priced from: no
  // skill, so no multiplier and no lines, and no shadow -- it mimics the
  // swing, not what the swing set off.
  OffenseStats follow =
      OffenseStatsFor(proto.job(), proto.level(), proto.allocated_stats(),
                      equipped, weapon, nullptr, 0, PassiveOffenseFor(derived));
  follow.mirror_lines = 0;
  AddBurns(skill, derived, offense, follow, level, types, speed_factor, attack);
  AddFinalAttacks(skill, derived, follow, level,
                  skill != nullptr ? attack.lines : 1, types, attack);
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
  // The same defence against a weaker mob. Barriers sum, as always.
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
    type.walk = spawn.walk();
    type.damage_to_player = ExpectedDamageTaken(defense, *type.mob);
    type.damage_to_player_scarred = ExpectedDamageTaken(scarred, *type.mob);
    params.types.push_back(std::move(type));
  }
}

// Whether a swing can be spent on this skill: an attack, or a cast with a
// lever behind it. One with nothing we model would take the slot and do
// nothing.
bool Castable(const Skill& skill) {
  if (DealsDamage(skill.kind())) {
    return true;
  }
  return skill.kind() == SKILL_KIND_ACTIVE && skill.base().heal_pct() > 0.0;
}

// Whether the character has this skill at all. Says nothing about swinging
// it: a passive carrying an own-clock half is not swingable and still fights.
// Castable then decides whether a swing is also on offer.
bool Available(const GameState& state, const Skill& skill,
               const std::set<std::string>& superseded, Activity activity) {
  // A replaced skill stops offering its swing with its levers: Piercing
  // Arrow II states the whole of the one it takes over.
  if (superseded.count(skill.name()) > 0) {
    return false;
  }
  // Learned levels are keyed by display name, which branches share, so ask
  // whose book this is. HoldsSkillFrom, not HasAdvancement, or a V node would
  // never be swingable: a common node's advancement is nobody's.
  if (!state.character.HoldsSkillFrom(skill)) {
    return false;
  }
  // A skill the gear in hand cannot swing is no option. The bare poke always
  // is, so the character is never left with nothing.
  return SkillGearMet(state.character, skill, activity);
}

// The element belongs to the SKILL, not to who swung it: Spirit of Snow's
// blizzard is ice whoever called it down, and leaves the same freeze. The
// parent's other tags come with it and mean nothing to a strike like this.
void CarryElement(const Skill& skill, Skill& built) {
  *built.mutable_tags() = skill.tags();
  built.set_freeze_seconds(skill.freeze_seconds());
  if (skill.has_stun()) {
    *built.mutable_stun() = skill.stun();
  }
  if (skill.has_mark()) {
    *built.mutable_mark() = skill.mark();
  }
  if (skill.has_freeze_build()) {
    *built.mutable_freeze_build() = skill.freeze_build();
  }
  built.set_freeze_lines_per_spend(skill.freeze_lines_per_spend());
}

// An own-clock half as a skill in its own right, so the same damage chain
// builds it. It keeps the parent's name: to the player it is one skill.
Skill AutoModeSkill(const Skill& skill, const AutoMode& mode) {
  Skill built;
  built.set_name(skill.name());
  built.set_kind(SKILL_KIND_AUTO_ATTACK);
  *built.mutable_base() = mode.base();
  *built.mutable_per_level() = mode.per_level();
  built.set_max_enemies(mode.max_enemies());
  built.set_lines(mode.lines());
  CarryElement(skill, built);
  return built;
}

// The wound a skill's buff bleeds, as a skill in its own right. It reaches
// what the swing reached, being the mark that swing left.
Skill BuffPulseSkill(const Skill& skill, const BuffPulse& pulse) {
  Skill built;
  built.set_name(skill.name());
  built.set_kind(SKILL_KIND_AUTO_ATTACK);
  *built.mutable_base() = pulse.base();
  *built.mutable_per_level() = pulse.per_level();
  built.set_max_enemies(pulse.max_enemies() > 0 ? pulse.max_enemies()
                                                : skill.max_enemies());
  built.set_lines(pulse.lines());
  CarryElement(skill, built);
  return built;
}

// How often a pulse fires: its own clock, or the swing of the skill it rides.
double PulseIntervalSeconds(const BuffPulse& pulse,
                            const std::map<std::string, Skill>& skills,
                            int attack_speed, double speed_factor) {
  if (pulse.paced_by_skill_name().empty()) {
    return pulse.cast_interval_seconds() * speed_factor;
  }
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& ridden = entry.second;
    if (ridden.name() != pulse.paced_by_skill_name()) {
      continue;
    }
    int delay_ms = ridden.base_delay_ms() > 0 ? ridden.base_delay_ms()
                                              : kDefaultSwingDelayMs;
    int stage = ridden.fixed_delay() ? kUnscaledAttackSpeedStage : attack_speed;
    return SwingIntervalSeconds(delay_ms, stage) * speed_factor;
  }
  return 0.0;
}

// The bleeding half of one buff or of one form of it: an attack on the buff's
// clock, gated on that buff -- and that form -- standing.
// The turret's parting shot as a skill of its own. It keeps the pulse's
// levers and swaps in its own damage, being the same turret: GMS writes the
// scroll's boss damage once. What the burst states for itself wins, as
// merging one proto3 message over another does.
Skill FinalStrikeSkill(const Skill& bleed, const SwingHit& burst) {
  Skill goes_out = bleed;
  goes_out.mutable_base()->clear_skill_pct();
  goes_out.mutable_per_level()->clear_skill_pct();
  goes_out.mutable_base()->MergeFrom(burst.base());
  goes_out.mutable_per_level()->MergeFrom(burst.per_level());
  goes_out.set_lines(burst.lines());
  if (burst.max_enemies() > 0) {
    goes_out.set_max_enemies(burst.max_enemies());
  }
  return goes_out;
}

void AddBuffPulse(const Character& proto, const EquipStats& equipped,
                  EquipType weapon_type, const Skill& skill,
                  const BuffPulse& pulse, int stance, int level,
                  const DerivedStats& derived,
                  const std::map<std::string, Skill>& skills, int attack_speed,
                  double speed_factor, const std::vector<CombatType>& types,
                  AttackSet& set) {
  double interval =
      PulseIntervalSeconds(pulse, skills, attack_speed, speed_factor);
  if (interval <= 0.0) {
    return;
  }
  // Everything here fires on the buff's clock rather than being swung, so
  // nothing that rides a swing rides any of it.
  auto own_clock = [&](const Skill& built) {
    AttackOption attack =
        AttackFor(proto, equipped, weapon_type, &built, level, types, derived,
                  kUnscaledAttackSpeedStage, speed_factor);
    attack.swing_seconds = 0.0;
    ClearSwingRiders(attack);
    return attack;
  };
  Skill bleed = BuffPulseSkill(skill, pulse);
  AttackOption wound = own_clock(bleed);
  wound.interval_seconds = interval;
  // Put back after ClearSwingRiders took the swing's away: Darkness Aura
  // states its heal against the AURA's attack, so it is paid per pulse.
  wound.hp_recover_pct =
      EffectAt(pulse.base(), pulse.per_level(), level).hp_recover_pct();
  wound.strikes_per_pulse = std::max(1, pulse.casts());
  wound.max_pulses = pulse.max_pulses();
  wound.needs_buff_stance = stance;
  // The ramp an accumulating poison walks: one form per helping, each the
  // whole strike again, so each rolls its own mastery and criticals.
  double step = pulse.skill_pct_per_repeat() +
                pulse.skill_pct_per_repeat_per_level() * (level - 1);
  for (int repeat = 1; repeat <= pulse.max_repeats() && step > 0.0; ++repeat) {
    Skill stronger = bleed;
    stronger.mutable_base()->set_skill_pct(bleed.base().skill_pct() +
                                           step * repeat);
    wound.repeats.push_back(
        std::make_shared<const AttackOption>(own_clock(stronger)));
  }
  wound.final_repeat_strike = pulse.final_repeat_strike();
  // One more of the rain's own lines, built as a strike of exactly one so
  // the fight lands as many as the swing earned, each rolling for itself.
  if (pulse.lines_per_extra_enemy() > 0 && pulse.max_extra_lines() > 0) {
    Skill one = bleed;
    one.set_lines(1);
    wound.extra_line = std::make_shared<const AttackOption>(own_clock(one));
    wound.lines_per_extra_enemy = pulse.lines_per_extra_enemy();
    wound.max_extra_lines = pulse.max_extra_lines();
  }
  // The strike the turret goes out on: the scroll bursts as it leaves. WITH
  // the last tick, not an interval after -- by then its window is down.
  if (pulse.has_final_strike()) {
    const SwingHit& burst = pulse.final_strike();
    AttackOption last = own_clock(FinalStrikeSkill(bleed, burst));
    last.strikes_per_pulse = SwingHitCasts(burst);
    wound.final_strike = std::make_shared<const AttackOption>(std::move(last));
  }
  set.auto_attacks.push_back(std::move(wound));
  // The stars a tick throws whatever the crowd is: their own attack rather
  // than lines on the volley beside them, the volley being worth what the
  // crowd is where these are worth the same on a lone boss.
  if (pulse.fixed_strikes().hits() > 0) {
    Skill fixed = bleed;
    fixed.set_lines(1);
    *fixed.mutable_scatter() = pulse.fixed_strikes();
    AttackOption strikes = own_clock(fixed);
    strikes.interval_seconds = interval;
    strikes.max_pulses = pulse.max_pulses();
    strikes.needs_buff_stance = stance;
    set.auto_attacks.push_back(std::move(strikes));
  }
}

// Adds every own-clock half of a skill that has any, beside the swing it
// already is. Nothing for the skills that have none, which is most of them.
void AddAutoModes(const Character& proto, const EquipStats& equipped,
                  EquipType weapon_type, const Skill& skill, int level,
                  const DerivedStats& derived,
                  const std::map<std::string, Skill>& skills, int attack_speed,
                  double speed_factor, const std::vector<CombatType>& types,
                  AttackSet& set) {
  for (const AutoMode& mode : skill.auto_mode()) {
    if (mode.cast_interval_seconds() <= 0.0 && mode.attacks_per_cast() <= 0) {
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
    attack.strikes_per_pulse = std::max(1, mode.casts());
    attack.silent_while_buff = mode.silent_while_buff_stands();
    // Clocked by the character's swings rather than by seconds passed, which
    // is the list the fight credits a landed swing to.
    if (mode.attacks_per_cast() > 0) {
      attack.attacks_per_cast = mode.attacks_per_cast();
      set.triggered_attacks.push_back(std::move(attack));
      continue;
    }
    attack.interval_seconds = mode.cast_interval_seconds() * speed_factor;
    set.auto_attacks.push_back(std::move(attack));
  }
  AddBuffPulse(proto, equipped, weapon_type, skill, skill.buff().pulse(), -1,
               level, derived, skills, attack_speed, speed_factor, types, set);
  // A buff with forms bleeds once per form. Both sit in the list and only the
  // one the cast raised fires, which is what needs_buff_stance gates.
  for (int i = 0; i < skill.buff().stance_size(); ++i) {
    AddBuffPulse(proto, equipped, weapon_type, skill,
                 skill.buff().stance(i).pulse(), i, level, derived, skills,
                 attack_speed, speed_factor, types, set);
  }
}

// What the rest of the book hands one skill: strikes, reach, its clock, and
// the share off the wait between its casts.
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
  // What the book adds to the BUFF a skill stands as: seconds on its clock,
  // hits on its shell, and the share it takes off a hit it cannot block.
  double buff_duration_seconds = 0.0;
  double shield_hits = 0.0;
  double shield_boss_damage_taken_pct = 0.0;
};

// Every such grant, summed and keyed by the skill it names. Gathered once:
// the granting skill may be listed after the one it strengthens, and every
// attack is built with the whole of it already in.
std::map<std::string, SkillBoosts> BoostsByTarget(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, int bonus) {
  // The nudge SkillLinesAt takes, for the same reason: a rate written as a
  // decimal lands a hair under the level it is meant to buy.
  std::map<std::string, SkillBoosts> by_target;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // Learned levels are keyed by display name, which branches share, so
    // only the character's own book grants anything. Asked of the character
    // rather than the advancement: a V node belongs to no book.
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

// `skill` with what the book grants it folded in. The line ladder is cashed
// in at `level` on the way, so a granted strike lands on top of the ones the
// skill bought rather than being climbed past twice. An empowered form comes
// here under its own name.
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

// An empowered form as a skill in its own right. It takes a NAME of its own,
// unlike an own-clock half: it is a different swing, and must not pick up the
// permanent bonus its parent hands the ordinary one. See SkillBoost::reach.
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

// A wound's heavier form: its own multiplier, reach and strikes, under a name
// of its own so the ledger and the plate can tell the presses apart.
Skill WoundFormSkill(const Skill& skill, const WoundForm& form) {
  Skill built;
  built.set_name(form.label().empty() ? skill.name() : form.label());
  built.set_kind(skill.kind());
  *built.mutable_base() = form.base();
  *built.mutable_per_level() = form.per_level();
  built.set_max_enemies(form.max_enemies());
  built.set_lines(form.lines());
  built.set_casts(form.casts());
  built.set_base_delay_ms(form.base_delay_ms());
  built.set_cooldown_seconds(form.cooldown_seconds());
  *built.mutable_required_equip_type() = skill.required_equip_type();
  return built;
}

// The form the fight lands in place of the ordinary press while a wound
// stands at full depth. Built here rather than in a second pass, unlike an
// empowered form: it always belongs to the skill stating it.
void AttachWoundForm(const Character& proto, const EquipStats& equipped,
                     EquipType weapon_type, const Skill& skill, int learned,
                     const DerivedStats& derived, int attack_speed,
                     double speed_factor, const std::vector<CombatType>& types,
                     AttackOption& attack) {
  const Wound& wound = skill.wound();
  if (wound.max_stacks() <= 0 || !wound.has_form()) {
    return;
  }
  Skill form = WoundFormSkill(skill, wound.form());
  attack.wound_max_stacks = wound.max_stacks();
  attack.wound_form = std::make_shared<AttackOption>(
      AttackFor(proto, equipped, weapon_type, &form, learned, types, derived,
                attack_speed, speed_factor));
}

// Attaches `skill`'s empowered form to every attack it upgrades. The form
// takes the attack's place, so it inherits its pacing: an animation for a
// swing, and nothing for a summon, paced by the clock it replaced.
void AttachEmpoweredForm(const GameState& state, const EquipStats& equipped,
                         EquipType weapon_type, const Skill& skill,
                         const EmpoweredForm& upgrade, int learned,
                         const DerivedStats& derived, int attack_speed,
                         double speed_factor,
                         const std::vector<CombatType>& types, SkillKind kind,
                         const std::map<std::string, SkillBoosts>& boosts,
                         std::vector<AttackOption>& into) {
  // Naming no attack, it upgrades the one its own skill already is.
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

// A second pass, once every attack is built: the skill carrying a form may
// name its target by display name, before that target is reached.
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

// The attack a buff loads, as a skill in its own right. It takes the
// magazine's label for a name: the fight finds the swing by it, and a boost
// aimed at it is filed under it.
Skill MagazineSkill(const Skill& skill, const Magazine& magazine) {
  Skill loaded;
  loaded.set_name(magazine.label());
  loaded.set_kind(SKILL_KIND_ATTACK);
  loaded.set_base_delay_ms(magazine.base_delay_ms());
  loaded.set_max_enemies(magazine.max_enemies());
  loaded.set_lines(magazine.lines());
  loaded.set_casts(magazine.casts());
  *loaded.mutable_scatter() = magazine.scatter();
  *loaded.mutable_base() = magazine.base();
  *loaded.mutable_per_level() = magazine.per_level();
  // The weapons and the group belong to the skill that loads it: one press of
  // the same button, so what sets a Final Attack off is the same mark.
  *loaded.mutable_required_equip_type() = skill.required_equip_type();
  *loaded.mutable_tags() = skill.tags();
  return loaded;
}

// Hangs a load on every swing that spends it, and says whether anybody does.
// `at` is where its charges are counted however many swings press it: one
// bank, not one per button.
bool HangLoad(const Magazine& magazine, AttackOption& load, AttackSet& set) {
  int at = static_cast<int>(set.attacks.size());
  // From 1: the bare poke is no skill and spends nothing -- and charged with
  // a load it would be what the fight reached for through a burst.
  for (int i = 1; i < at; ++i) {
    // Another skill's load is not a swing either.
    if (set.attacks[i].charges > 0) {
      continue;
    }
    if (!magazine.spent_by_every_swing() &&
        set.attacks[i].name != magazine.spent_by_skill_name()) {
      continue;
    }
    load.spent_by_attack = i;
    // It rides that press rather than costing one of its own, so it is worth
    // exactly what the swing carrying it is worth in time.
    load.swing_seconds = set.attacks[i].swing_seconds;
    set.attacks[i].loaded = std::make_shared<AttackOption>(load);
    set.attacks[i].loaded_attack = at;
  }
  return load.spent_by_attack >= 0;
}

// One swing per learned buff that loads one. Its own pass: the skill carrying
// a magazine is a buff, which AddAttacks is done with before it builds one.
void AddMagazines(const GameState& state, const DerivedStats& derived,
                  EquipType weapon_type, int attack_speed, double speed_factor,
                  const std::vector<CombatType>& types, AttackSet& set) {
  const EquipStats total_stats = TotalEquipStats(state.character, derived);
  int bonus = BonusSkillLevels(state.character, state.skills);
  std::map<std::string, SkillBoosts> boosts =
      BoostsByTarget(state.character, state.skills, bonus);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    const Magazine& magazine = skill.buff().magazine();
    int learned = EffectiveSkillLevel(state.character, skill, bonus);
    if (learned <= 0 || magazine.charges() <= 0) {
      continue;
    }
    Skill loaded = MagazineSkill(skill, magazine);
    Skill boosted;
    const Skill& swung = Boosted(loaded, learned, boosts, boosted);
    AttackOption attack =
        AttackFor(state.character.proto(), total_stats, weapon_type, &swung,
                  learned, types, derived, attack_speed, speed_factor);
    attack.charges = magazine.charges();
    attack.charges_per_swing = std::max(1, magazine.charges_per_swing());
    attack.recharge_seconds = magazine.recharge_seconds();
    attack.recharge_max = magazine.recharge_max();
    // A load nothing else spends is a button in its own right. One a swing
    // spends stays on the list, where its charges are counted, but is hung on
    // the presses spending it and taken out of the choice.
    if (magazine.spent_by_skill_name().empty() &&
        !magazine.spent_by_every_swing()) {
      set.attacks.push_back(std::move(attack));
      continue;
    }
    // Nobody holds a skill that would spend it, and an option left on the
    // list is one the fight would choose for itself.
    if (!HangLoad(magazine, attack, set)) {
      continue;
    }
    set.attacks.push_back(std::move(attack));
  }
}

// Every attack the character could swing, the bare poke first. Skills firing
// on their own clock go to auto_attacks instead. Passives apply to whichever
// attack is chosen, so the resolved `derived` is handed to each.
// Strips the damage off a cast. With no multiplier to apply the chain built
// the bare poke's, which a cast must not land -- and a cast that deals no
// damage strikes nothing, so nothing follows it however it is clocked.
void MakeCastHarmless(AttackOption& attack) {
  std::fill(attack.damage_per_hit.begin(), attack.damage_per_hit.end(), 0.0);
  attack.groups.clear();
  attack.lead_damage.clear();
  ClearSwingRiders(attack);
  ClearFinalAttacks(attack);
}

// Files a skill that runs on a clock of its own, under whatever that clock
// counts: swings landed, enemies defeated, or seconds passed.
void FileAutoAttack(const Skill& swung, double speed_factor,
                    AttackOption& attack, AttackSet& set) {
  attack.swing_seconds = 0.0;  // not swung, so never charged
  ClearSwingRiders(attack);    // what rides a swing needs one
  if (swung.attacks_per_cast() > 0 || swung.kills_per_cast() > 0) {
    attack.attacks_per_cast = swung.attacks_per_cast();
    attack.kills_per_cast = swung.kills_per_cast();
    set.triggered_attacks.push_back(std::move(attack));
    return;
  }
  // A skill with no clock at all would fire every step, so naming neither is
  // taken as "does not fire" rather than "fires constantly".
  if (swung.cast_interval_seconds() <= 0.0) {
    return;
  }
  attack.interval_seconds = swung.cast_interval_seconds() * speed_factor;
  set.auto_attacks.push_back(std::move(attack));
}

void AddAttacks(const GameState& state, const DerivedStats& derived,
                EquipType weapon_type, int attack_speed, double speed_factor,
                const std::vector<CombatType>& types, AttackSet& set) {
  const Character& proto = state.character.proto();
  const EquipStats total_stats = TotalEquipStats(state.character, derived);
  // A skill on its own clock is not the character's swing: no shadow copies
  // it and it knocks no mesos loose, as AttackFor strips its Final Attack.
  DerivedStats off_clock = derived;
  off_clock.mirror_line_pct = 0.0;
  StripMesoDrops(off_clock);
  set.attacks.push_back(AttackFor(proto, total_stats, weapon_type, nullptr, 0,
                                  types, derived, attack_speed, speed_factor));
  int bonus = BonusSkillLevels(state.character, state.skills);
  std::map<std::string, SkillBoosts> boosts =
      BoostsByTarget(state.character, state.skills, bonus);
  std::set<std::string> superseded =
      DormantSkillNames(state.character, state.skills, bonus, derived.activity);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    int learned = EffectiveSkillLevel(state.character, skill, bonus);
    if (learned <= 0 ||
        !Available(state, skill, superseded, derived.activity)) {
      continue;
    }
    // Strikes and reach another skill in the book grants this one, folded in
    // before anything is built so the whole chain below sees one skill.
    Skill boosted;
    const Skill& swung = Boosted(skill, learned, boosts, boosted);
    AddAutoModes(proto, total_stats, weapon_type, swung, learned, off_clock,
                 state.skills, attack_speed, speed_factor, types, set);
    // A skill no swing can be spent on is done here; its own-clock halves
    // are already in, which is all an aura contributes.
    if (!Castable(swung)) {
      continue;
    }
    // Everything below reads `swung`, never `skill`, or a boost that moved
    // the clock would be dropped.
    AttackOption attack =
        AttackFor(proto, total_stats, weapon_type, &swung, learned, types,
                  swung.kind() == SKILL_KIND_AUTO_ATTACK ? off_clock : derived,
                  attack_speed, speed_factor);
    if (attack.heal_fraction > 0.0) {
      MakeCastHarmless(attack);
    }
    if (swung.kind() != SKILL_KIND_AUTO_ATTACK) {
      // What this swing counts toward the skills clocked by swings landed.
      // Unset is one, which is what an ordinary swing is worth.
      if (swung.hits_per_attack_count() > 1) {
        attack.count_weight = 1.0 / swung.hits_per_attack_count();
      }
      AttachWoundForm(proto, total_stats, weapon_type, swung, learned, derived,
                      attack_speed, speed_factor, types, attack);
      set.attacks.push_back(std::move(attack));
      continue;
    }
    FileAutoAttack(swung, speed_factor, attack, set);
  }
}

// The stage the character's swings are paced at: their job and weapon's, plus
// what their passives add, held to the soft cap and then past it where
// allowed. Per attack set, a buff being one of the things adding.
int AttackSpeedStageFor(const GameState& state, const EquipPrototype& weapon,
                        const DerivedStats& derived) {
  return AttackSpeedStage(BaseAttackSpeedStage(state.character.proto().job(),
                                               weapon.attack_speed()),
                          derived.attack_speed_bonus,
                          derived.uncapped_attack_speed_bonus);
}

// Hands each burn a slot of its own, so two never write over each other.
// `shared` is how many the CHARACTER carries rather than any one swing.
// Numbered by attack order, the same in every buffed set, so a held slot means
// the same thing however the buffs come and go. Every kind of attack is
// numbered: one with no slot is one the fight silently drops.
void NumberDots(AttackSet& set, int shared) {
  int next = shared;
  std::vector<std::vector<AttackOption>*> lists = {
      &set.attacks, &set.auto_attacks, &set.triggered_attacks};
  for (std::vector<AttackOption>* list : lists) {
    for (AttackOption& attack : *list) {
      // A carried burn is the same burn wherever applied from, so it keeps
      // the slot its place among the character's gives it.
      int carried = 0;
      for (DotApplication& burn : attack.dots) {
        burn.slot = burn.carried ? carried++ : next++;
      }
    }
  }
}

// Slots a monster needs for every burn this character can leave. Every list is
// walked: a summon's burn marks a monster as a swing's does.
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

// Everything the character can attack with, at one particular set of stats --
// theirs alone, or theirs with some buff up.
AttackSet BuildAttackSet(const GameState& state, const DerivedStats& derived,
                         const EquipPrototype& weapon, double speed_factor,
                         const std::vector<CombatType>& types) {
  int attack_speed = AttackSpeedStageFor(state, weapon, derived);
  AttackSet set;
  AddAttacks(state, derived, weapon.equip_type(), attack_speed, speed_factor,
             types, set);
  AddMagazines(state, derived, weapon.equip_type(), attack_speed, speed_factor,
               types, set);
  AddEmpoweredForms(state, TotalEquipStats(state.character, derived),
                    weapon.equip_type(), derived, attack_speed, speed_factor,
                    types, set);
  NumberDots(set, static_cast<int>(derived.dots.size()));
  set.freeze_cap = derived.freeze.cap;
  return set;
}

// Where `name`'s swing sits, or -1. Answered off the unbuffed set, which is
// in the same order as every buffed one.
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

// Buff Duration reaches every buff but a V node's: GMS marks all of them
// notIncBuffDuration, a rule about the whole matrix, which is why it is asked
// of v_node rather than written into each file. The base in constants.h is
// added HERE rather than to the stat line, so the matrix stands outside it
// too.
double BuffDurationFor(const Skill& skill, double buff_duration_pct) {
  return skill.v_node() == V_NODE_KIND_UNSPECIFIED
             ? kBaseBuffDuration + buff_duration_pct
             : 0.0;
}

// One buff's clock and shell once the book has had its say. The seconds a
// hyper adds land BEFORE Buff Duration takes its share: one buff, one length
// however many sources wrote it.
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
  // sheds what it can and takes the rest down with it.
  if (buff.stages() > 1) {
    option.duration_seconds =
        std::min(option.duration_seconds,
                 buff.stage_interval_seconds() * (stage + 1) * speed_factor);
  }
  // The same rule the length above is under, since it is the same length: what
  // the burns add is seconds of window, not a lever of its own.
  option.duration_seconds_per_dot = buff.duration_seconds_per_dot() *
                                    (1.0 + buff_duration_pct) * speed_factor;
  option.dot_count_cap = buff.dot_count_cap();
  // Game-scaled like every other clock here: the angel re-grants on the
  // stretched second, as it strikes on one.
  option.duty_seconds = buff.duty_seconds() * speed_factor;
  option.duty_interval_seconds = buff.duty_interval_seconds() * speed_factor;
  if (buff.has_shield()) {
    option.shield_hits = ShieldHitsAt(buff.shield(), level) + boost.shield_hits;
    option.boss_damage_taken_pct = buff.shield().boss_damage_taken_pct() +
                                   boost.shield_boss_damage_taken_pct;
  }
  return option;
}

// The character's buffs with a shedding one written out per stage. Each entry
// grants one stage's levers on one stage's clock, so the mask indexing the
// damage tables says how many stages still stand.
std::vector<const Skill*> StagedBuffSkills(
    const std::vector<const Skill*>& raised) {
  std::vector<const Skill*> staged;
  for (const Skill* skill : raised) {
    staged.insert(staged.end(), BuffWindowsFor(skill->buff()), skill);
  }
  return staged;
}

// The forms a buff can be raised in, and its own length where it has any: the
// LONGEST of them, so a caller asking how long it runs has an answer. Buff
// Duration is NOT applied -- every buff with forms is a V node, which
// BuffDurationFor exempts anyway.
void AddStances(const Buff& buff, int level, double speed_factor,
                BuffOption& option) {
  for (const Stance& stance : buff.stance()) {
    StanceOption form;
    form.duration_seconds =
        (stance.duration_seconds() +
         stance.duration_seconds_per_level() * (level - 1)) *
        speed_factor;
    form.pulse_interval_seconds =
        stance.pulse().cast_interval_seconds() * speed_factor;
    option.duration_seconds =
        std::max(option.duration_seconds, form.duration_seconds);
    option.stances.push_back(form);
  }
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
  int copy = 0;
  for (const Skill* skill : buff_skills) {
    copy = skill == previous ? copy + 1 : 0;
    previous = skill;
    int level = EffectiveSkillLevel(character, *skill, bonus);
    const Buff& buff = skill->buff();
    // Which shed-stage this window is. A STACK's windows are alike -- each is
    // gathered on its own roll and lives out the whole length -- so only a
    // shedding buff numbers its copies.
    int stage = buff.stages() > 0 ? copy : 0;
    BuffOption option = BuffClockFor(buff, level, boosts[skill->name()],
                                     BuffDurationFor(*skill, buff_duration_pct),
                                     speed_factor, stage);
    option.name = skill->name();
    AddStances(buff, level, speed_factor, option);
    option.cooldown_seconds =
        ReducedCooldown(CooldownAt(*skill, level),
                        derived.cooldown_reduction_seconds) *
        speed_factor;
    // A party takes turns raising a shared buff, so it comes round as often
    // as the party between them can cast it.
    if (buff.party_shared()) {
      option.cooldown_seconds /= PartyHolders(character, party, *skill);
    }
    SkillEffect held = EffectAt(buff.base(), buff.per_level(), level);
    option.damage_taken_pct = held.damage_taken_pct();
    option.cooldown_reduction_seconds =
        buff.cooldown_reduction_seconds() * speed_factor;
    // Lines rather than seconds, so the pacing band leaves it alone: what it
    // counts is already stretched.
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
    // A buff a swing ROLLS for is not pressed at all, so it costs no cast and
    // waits on no clock. One naming an afflicted target with no chance of its
    // own is raised by every swing that finds one.
    option.needs_afflicted_target = buff.needs_afflicted_target();
    if (buff.raise_chance() > 0.0 || buff.raise_chance_per_level() > 0.0) {
      option.raise_chance = std::clamp(
          buff.raise_chance() + buff.raise_chance_per_level() * (level - 1),
          0.0, 1.0);
    } else if (option.needs_afflicted_target) {
      option.raise_chance = 1.0;
    }
    if (option.raise_chance > 0.0) {
      option.cast_seconds = 0.0;
    }
    // The swing this buff loads, found by the magazine's own label -- the name
    // AddMagazines built it under.
    if (buff.magazine().charges() > 0) {
      option.magazine_attack =
          AttackNamed(params.attacks, buff.magazine().label());
    }
    // A buff hanging off an ATTACK is laid by that swing rather than raised
    // on a wait, unless it states a press of its own.
    if (skill->kind() == SKILL_KIND_ATTACK) {
      if (buff.own_cast_delay_ms() > 0) {
        option.cast_seconds = buff.own_cast_delay_ms() / 1000.0 * speed_factor;
      } else {
        option.laid_by_attack = AttackNamed(params.attacks, skill->name());
        option.raised_on_cast = buff.raised_on_cast();
        option.needs_wound_form = buff.needs_wound_form();
        option.cast_seconds = 0.0;
      }
    }
    params.buffs.push_back(std::move(option));
  }
}

// The party's buffs over this character, on their casters' clocks and at
// their levels, appended so one mask covers both. Somebody else's cast lays
// them, so they cost this character no swing.
//
// A caster's Buff Duration and INT reach their half and the party's alike: one
// cloud, one clock, however many stand in it. Worked out here, with the party
// in hand, rather than inside the fold. See BuffUp.
std::vector<BuffUp> AddAllyBuffs(const GameState& state, double speed_factor,
                                 int budget, CombatParams& params) {
  std::vector<BuffUp> raised;
  // Everybody in the zone, the caster included -- there being no positions
  // here, nobody is ever standing out of it. See AllyIntLever.
  int party_size = static_cast<int>(state.party.size()) + 1;
  for (const AllyGrant& grant : AllyBuffsFor(
           state.character, state.skills, absl::MakeConstSpan(state.party))) {
    // The character's own book is served first: a party buff dropped is one
    // less blessing, while an own buff dropped is a hole in their rotation.
    if (static_cast<int>(raised.size()) >= budget) {
      break;
    }
    const Buff& buff = grant.skill->buff();
    // The CASTER's book, not the reader's: one cast stands the same length
    // over everybody under it.
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
    params.buffs.push_back(std::move(option));
    raised.push_back(BuffUp{grant.skill, grant.caster, grant.level,
                            TotalIntFor(*grant.caster, state.skills),
                            party_size});
  }
  return raised;
}

// Whether a buff ticks damage at all, through its own pulse or through one of
// its forms'.
bool Bleeds(const Buff& buff) {
  if (Pulses(buff.pulse())) {
    return true;
  }
  for (const Stance& stance : buff.stance()) {
    if (Pulses(stance.pulse())) {
      return true;
    }
  }
  return false;
}

// Points each bleeding buff's pulse at the buff it belongs to. Run over the
// base set AND every buffed one, or the tag would come and go with the buffs.
// Matched by name, a pulse keeping its parent skill's name.
void TagBuffGatedPulses(const std::vector<BuffOption>& buffs,
                        const std::vector<const Skill*>& buff_skills,
                        std::vector<AttackOption>& casts) {
  for (int i = 0; i < static_cast<int>(buffs.size()); ++i) {
    if (i >= static_cast<int>(buff_skills.size()) ||
        !Bleeds(buff_skills[i]->buff())) {
      continue;
    }
    for (AttackOption& cast : casts) {
      if (cast.name == buffs[i].name) {
        cast.needs_buff = i;
      }
    }
  }
}

// Points each silenced half at the buff that silences it, by the same name
// match: a half firing only while its buff is down must know which buff.
void TagBuffSilencedCasts(const std::vector<BuffOption>& buffs,
                          std::vector<AttackOption>& casts) {
  for (int i = 0; i < static_cast<int>(buffs.size()); ++i) {
    for (AttackOption& cast : casts) {
      if (cast.silent_while_buff && cast.name == buffs[i].name) {
        cast.needs_buff = i;
      }
    }
  }
}

// Points each dismissed summon at the buff dismissing it. The name is the
// SILENCING buff's to state: what it puts out is its own business.
void TagBuffSilencedSummons(const std::vector<BuffOption>& buffs,
                            const std::vector<const Skill*>& buff_skills,
                            std::vector<AttackOption>& casts) {
  for (int i = 0; i < static_cast<int>(buffs.size()); ++i) {
    if (i >= static_cast<int>(buff_skills.size()) ||
        buff_skills[i]->buff().silences_skill_name().empty()) {
      continue;
    }
    for (AttackOption& cast : casts) {
      if (cast.name == buff_skills[i]->buff().silences_skill_name()) {
        cast.silenced_by_buff = i;
      }
    }
  }
}

// Points each form at the pulse it bleeds through, so the fight prices them
// against each other without hunting the list. Over the base set alone: an
// index taken here is good in every buffed one.
void PointStancesAtPulses(const std::vector<AttackOption>& casts,
                          std::vector<BuffOption>& buffs) {
  for (int i = 0; i < static_cast<int>(casts.size()); ++i) {
    const AttackOption& cast = casts[i];
    if (cast.needs_buff < 0 || cast.needs_buff_stance < 0 ||
        cast.needs_buff >= static_cast<int>(buffs.size())) {
      continue;
    }
    std::vector<StanceOption>& stances = buffs[cast.needs_buff].stances;
    if (cast.needs_buff_stance < static_cast<int>(stances.size())) {
      stances[cast.needs_buff_stance].pulse_attack = i;
    }
  }
}

// Expected damage a second with no buff standing: the hardest swing on offer
// plus everything on its own clock, against the first mob type. A rough figure
// by design, standing in only until the fight has a measured rate -- see
// CombatSim::SecondsLeft.
double ReferenceDps(const CombatParams& params) {
  double best_swing = 0.0;
  double own_clocks = 0.0;
  for (const AttackOption& attack : params.attacks) {
    if (attack.swing_seconds <= 0.0 || attack.damage_per_hit.empty()) {
      continue;
    }
    best_swing =
        std::max(best_swing, attack.damage_per_hit[0] / attack.swing_seconds);
  }
  for (const AttackOption& cast : params.auto_attacks) {
    // A pulse waiting on a buff is not firing yet, and counting it would have
    // the fight expect damage nothing is dealing.
    if (cast.interval_seconds <= 0.0 || cast.needs_buff >= 0 ||
        cast.damage_per_hit.empty()) {
      continue;
    }
    own_clocks += cast.damage_per_hit[0] / cast.interval_seconds;
  }
  return best_swing + own_clocks;
}

// A slot per combination of buffs, indexed as CombatParams::Attacks reads
// them. Left empty, and filled by BuildBuffedSet on first ask.
void AddBuffedSets(const GameState& state,
                   const std::vector<const Skill*>& buff_skills,
                   const std::vector<BuffUp>& ally_buffs,
                   const EquipPrototype& weapon, double speed_factor,
                   Activity preset, CombatParams& params) {
  int count = static_cast<int>(buff_skills.size() + ally_buffs.size());
  if (count == 0) {
    return;
  }
  params.buffed_source.state = &state;
  params.buffed_source.weapon = &weapon;
  params.buffed_source.buff_skills = buff_skills;
  params.buffed_source.ally_buffs = ally_buffs;
  params.buffed_source.speed_factor = speed_factor;
  params.buffed_source.preset = preset;
  // Nothing is built here: params.buffs is what bounds the masks, and each
  // one is built the first time the fight stands in it.
}

// Halves how far a swing reaches, rounding up. A boss stands its parts a room
// apart, so a sweep gathering eight monsters off a map gathers nothing like
// eight of those. Rounded up, so a skill still reaches what it was aimed at.
void HalveReach(std::vector<AttackOption>& attacks) {
  for (AttackOption& attack : attacks) {
    attack.max_enemies = (std::max(1, attack.max_enemies) + 1) / 2;
  }
}

// One combination's attack set, off what AddBuffedSets kept. Every pass the
// base set went through runs here too: the windows must be the same shape.
AttackSet BuildBuffedSet(const CombatParams& params, int mask) {
  const BuffedSetSource& source = params.buffed_source;
  int own = static_cast<int>(source.buff_skills.size());
  std::vector<BuffUp> up;
  for (int i = 0; i < own; ++i) {
    if ((mask & (1 << i)) != 0) {
      up.push_back(BuffUp{source.buff_skills[i]});
    }
  }
  // The party's take the bits above the character's own, and carry the caster
  // their half is read at the level of.
  for (int i = 0; i < static_cast<int>(source.ally_buffs.size()); ++i) {
    if ((mask & (1 << (own + i))) != 0) {
      up.push_back(source.ally_buffs[i]);
    }
  }
  DerivedStats derived = DerivedStatsFor(
      source.state->character, source.state->skills, absl::MakeConstSpan(up),
      source.state->party, source.preset);
  AttackSet set = BuildAttackSet(*source.state, derived, *source.weapon,
                                 source.speed_factor, params.types);
  TagBuffGatedPulses(params.buffs, source.buff_skills, set.auto_attacks);
  TagBuffSilencedSummons(params.buffs, source.buff_skills, set.auto_attacks);
  TagBuffSilencedCasts(params.buffs, set.triggered_attacks);
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

const AttackOption& RepeatForm(const AttackOption& attack, int pulses) {
  if (attack.repeats.empty() || pulses <= 1) {
    return attack;
  }
  int step = std::min<int>(pulses - 1, attack.repeats.size());
  return *attack.repeats[step - 1];
}

// The window `mask` names, built on first ask. Null for a mask no combination
// reaches, which the readers below answer with the unbuffed lists.
const AttackSet* CombatParams::Window(int mask) const {
  if (mask <= 0) {
    return nullptr;
  }
  std::map<int, AttackSet>::iterator slot = buffed.find(mask);
  if (slot != buffed.end()) {
    return &slot->second;
  }
  // A params built by hand has no source to build from: what it was handed is
  // all it has, and any other mask reads as no buffs at all.
  if (buffed_source.state == nullptr || mask >= (1 << buffs.size())) {
    return nullptr;
  }
  return &buffed.emplace(mask, BuildBuffedSet(*this, mask)).first->second;
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

// What does not depend on what is in front of the character: their pool, what
// their passives pay, and the clocks the band stretches. The two intervals are
// the caller's -- a boss fight runs off neither.
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
  // The window stretches with the band and the share per second does not, so
  // one firing pours what its skill says however slowly the band runs.
  params.emergency_heal = derived.emergency_heal;
  params.emergency_heal.pct /= speed_factor;
  params.emergency_heal.seconds *= speed_factor;
  params.emergency_heal.cooldown_seconds *= speed_factor;
  params.freeze_cap = derived.freeze.cap;
}

// Every attack the character can swing at the types in `params`: as they
// stand, then one table per combination of buffs. What being hit costs is read
// off the UNBUFFED stats -- nothing buffs a pool, and the buff that softens a
// hit takes its share off the hit itself.
void AddAttacks(const GameState& state, const DerivedStats& derived,
                const EquipPrototype& weapon, double speed_factor,
                Activity preset, CombatParams& params) {
  AttackSet base =
      BuildAttackSet(state, derived, weapon, speed_factor, params.types);
  params.attacks = std::move(base.attacks);
  params.auto_attacks = std::move(base.auto_attacks);
  params.triggered_attacks = std::move(base.triggered_attacks);
  params.dot_count = DotSlotsNeeded(params);
  std::vector<const Skill*> buff_skills =
      StagedBuffSkills(BuffSkillsFor(state.character, state.skills, preset));
  if (static_cast<int>(buff_skills.size()) > kMaxBuffWindows) {
    buff_skills.resize(kMaxBuffWindows);
  }
  AddBuffs(state, buff_skills, speed_factor, derived, params);
  std::vector<BuffUp> ally_buffs = AddAllyBuffs(
      state, speed_factor,
      kMaxBuffWindows - static_cast<int>(buff_skills.size()), params);
  AddBuffedSets(state, buff_skills, ally_buffs, weapon, speed_factor, preset,
                params);
  TagBuffGatedPulses(params.buffs, buff_skills, params.auto_attacks);
  TagBuffSilencedSummons(params.buffs, buff_skills, params.auto_attacks);
  TagBuffSilencedCasts(params.buffs, params.triggered_attacks);
  PointStancesAtPulses(params.auto_attacks, params.buffs);
  params.reference_dps = ReferenceDps(params);
}

// Every list a swing can be picked from: a buffed table reaches as far as the
// unbuffed one. The windows are not built yet, so they are marked.
void HalveBossReach(CombatParams& params) {
  HalveReach(params.attacks);
  HalveReach(params.auto_attacks);
  HalveReach(params.triggered_attacks);
  params.buffed_source.halve_reach = true;
}

// How long the map takes to put its monsters back up, before the pacing band
// stretches it. The Wild Totem halves it, which is the whole of what it does.
double RespawnIntervalFor(const CharacterInstance& character) {
  return character.ConsumableInEffect(CONSUMABLE_TYPE_WILD_TOTEM)
             ? kWildTotemRespawnSeconds
             : kRespawnIntervalSeconds;
}

}  // namespace

const EquipPrototype* EquippedWeapon(const GameState& state,
                                     Activity activity) {
  const WornGear& equipped = state.character.equipped(
      state.character.SlotFor(PresetKind::kEquip, activity));
  WornGear::const_iterator it = equipped.find(EQUIP_SLOT_PRIMARY_WEAPON);
  return it == equipped.end() ? nullptr : &it->second->prototype();
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
  const EquipPrototype* weapon = EquippedWeapon(state, Activity::kFarming);
  if (map_it == state.maps.end() || weapon == nullptr) {
    return params;
  }

  DerivedStats derived =
      DerivedStatsFor(state.character, state.skills, {}, state.party);
  // What the map's Arcane Force requirement does to both sides. Written onto
  // derived, the one struct every builder below carries: the requirement is
  // the map's and neither side alone can answer it.
  ArcaneFactors arcane = ArcaneFactorsFor(state.character.arcane_force(),
                                          map_it->second.arcane_force());
  derived.arcane_damage_factor = arcane.damage_dealt;
  derived.arcane_taken_factor = arcane.damage_taken;
  // The pace the whole encounter runs at, and the only thing here that asks
  // the character's level directly: the game stretches out as they climb.
  double speed_factor = GameSpeedFactor(state.character.proto().level());
  params.respawn_seconds = RespawnIntervalFor(state.character) * speed_factor;
  params.hit_seconds = kMobHitIntervalSeconds * speed_factor;
  AddPacing(state, derived, speed_factor, params);
  AddTypes(state, map_it->second.spawns(), DefenseFor(state, derived),
           derived.scar.enemy_attack_pct, params);
  if (params.types.empty()) {
    return params;
  }
  AddAttacks(state, derived, *weapon, speed_factor, Activity::kFarming, params);
  params.active = true;
  return params;
}

// The rate the boss's drops roll at: the same character read again in the
// gear they set aside for it, and only where the two presets differ.
double DropRollRate(const GameState& state, const DerivedStats& bossing) {
  const CharacterInstance& character = state.character;
  const StatPreset worn =
      character.SlotFor(PresetKind::kEquip, Activity::kBossing);
  if (worn == kDropPreset ||
      character.equipped(worn) == character.equipped(kDropPreset)) {
    return bossing.item_drop_pct;
  }
  return DerivedStatsFor(character, state.skills, {}, state.party,
                         Activity::kBossing, kDropPreset)
      .item_drop_pct;
}

CombatParams ComputeBossParams(const GameState& state,
                               const std::string& boss_key,
                               const BossDifficulty& difficulty, int phase) {
  CombatParams params;
  if (phase < 0 || phase >= difficulty.phases_size()) {
    return params;
  }
  params.encounter = BossEncounterKey(boss_key, difficulty.name(), phase);
  const EquipPrototype* weapon = EquippedWeapon(state, Activity::kBossing);
  if (weapon == nullptr) {
    return params;
  }

  // A boss fight is what the bossing allocation is for.
  DerivedStats derived = DerivedStatsFor(state.character, state.skills, {},
                                         state.party, Activity::kBossing);
  // A boss fight runs in real time whatever the level: the band stretches an
  // idle map so it can be left alone, and a watched fight wants neither the
  // stretch nor a beat. Both intervals stay 0.
  AddPacing(state, derived, 1.0, params);
  AddTypes(state, difficulty.phases(phase).spawns(), DefenseFor(state, derived),
           derived.scar.enemy_attack_pct, params);
  if (params.types.empty()) {
    return params;
  }
  AddAttacks(state, derived, *weapon, 1.0, Activity::kBossing, params);
  params.drop_roll_item_drop_pct = DropRollRate(state, derived);
  HalveBossReach(params);
  // A boss's parts are hit hardest-first: see CombatParams::focus_healthiest.
  params.focus_healthiest = true;
  // The boss screen draws every line as a number, which the map does not.
  params.record_damage_lines = true;
  params.active = true;
  return params;
}

}  // namespace ms
