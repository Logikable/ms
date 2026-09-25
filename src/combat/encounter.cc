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
#include "src/map_force.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"
#include "src/spawn.h"

namespace ms {
namespace {

// How often the front mob hits the player, before game-speed scaling. This is
// the one damage-taken number we chose; the rest is the GMS formula. Only the
// front mob attacks, or a crowded beginner map would be deadlier than a sparse
// high-level one.
constexpr double kMobHitIntervalSeconds = 1.5;

// The other half of that setting: a map whose mobs deal less than this between
// respawns can be held forever, so players can survive it by outlasting it.
constexpr double kBeatHealFraction = 0.10;

// For casts that deal no damage and so trigger nothing.
void ClearFinalAttacks(AttackOption& attack) {
  attack.final_attack_damage.clear();
  attack.final_attack_rolls.clear();
  attack.per_swing_final_attack_damage.clear();
  attack.per_swing_final_attack_rolls.clear();
  attack.per_swing_final_attack_enemies = 1;
}

// Removes Final Attacks that only the character's own attacks trigger, and
// recomputes the totals from what remains. A filter rather than a wipe, because
// some (like Frost Ark's shock) trigger from the orb as well as the bolts. See
// Skill.follows_own_clock.
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
    // Each total is the sum of its sources' values, so recompute it from the
    // rolls that remain.
    std::fill(banks[i]->begin(), banks[i]->end(), 0.0);
    for (const FinalAttackRoll& roll : *lists[i]) {
      for (std::size_t t = 0; t < banks[i]->size(); ++t) {
        (*banks[i])[t] += roll.damage[t] * roll.chance * roll.count;
      }
    }
  }
  // The widest remaining source, as AddFinalAttacks computes it.
  for (const FinalAttackRoll& roll : attack.per_swing_final_attack_rolls) {
    attack.per_swing_final_attack_enemies =
        std::max(attack.per_swing_final_attack_enemies, roll.max_enemies);
  }
}

// Removes what only the character's own attacks trigger: HP recovery, Final
// Attacks, carried poison, and side strikes. Nothing on its own timer triggers
// them. Burns the skill itself lists stay: Ifrit's flames burn whatever they
// touch.
void ClearSwingRiders(AttackOption& attack) {
  attack.hp_recover_pct = 0.0;
  attack.side = nullptr;
  attack.procs.clear();
  // Summons add ice but never spend stacks: in GMS, the lightning orb doesn't
  // consume freezing stacks.
  attack.freeze_spends = false;
  attack.freeze_fd_per_stack = 0.0;
  KeepOwnClockFinalAttacks(attack);
  attack.dots.erase(
      std::remove_if(attack.dots.begin(), attack.dots.end(),
                     [](const DotApplication& burn) { return burn.carried; }),
      attack.dots.end());
}

// Whether `skill` has `tag`. Null (the basic attack) has none.
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

// Only the character's own attacks throw meso, so pulses on their own timer
// never do.
void StripMesoDrops(DerivedStats& derived) {
  std::vector<FinalAttackSource> kept;
  for (const FinalAttackSource& source : derived.final_attacks) {
    if (!source.per_line) {
      kept.push_back(source);
    }
  }
  derived.final_attacks = std::move(kept);
}

// One burn's damage per mob type. It uses the attack's stats but its own
// multiplier and hits: a burn is what the attack leaves behind, not the attack.
// `boost` is the job book's bonus to this burn by name; only its multiplier and
// duration are used here, the rest is already in `offense`.
DotApplication BurnFor(const Dot& dot, const OffenseStats& offense, int level,
                       const std::vector<CombatType>& types,
                       double speed_factor, const SkillBonus* boost) {
  // A stack count that rises by thirds or sixths lands just under the whole
  // number, and 2.9999 stacks would floor to 2.
  constexpr double kStackEpsilon = 1e-9;
  OffenseStats burn = offense;
  SkillEffect burns = EffectAt(dot.base(), dot.per_level(), level);
  burn.skill_pct =
      burns.skill_pct() + (boost != nullptr ? boost->dot_skill_pct : 0.0);
  burn.normal_skill_pct = burns.normal_skill_pct();
  burn.normal_pct += burns.normal_pct();
  burn.lines = std::max(1, dot.lines());
  // Shadows copy the attack, not the burn it leaves.
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
  // No chance given means certain, which is what a burn an attack always leaves
  // needs. Only character-carried poisons roll.
  application.chance = chance > 0.0 ? std::min(1.0, chance) : 1.0;
  application.max_stacks =
      std::max(1, static_cast<int>(dot.max_stacks() +
                                   dot.max_stacks_per_level() * (level - 1) +
                                   kStackEpsilon));
  return application;
}

// Ice adds a stack per line, lightning spends one per line, and both gain the
// crit damage held stacks grant, since a frozen enemy is frozen whatever hits
// it. That crit damage is converted to a share of the attack's average damage,
// the only form the fight can multiply by: crit rolls per line and its bonus is
// normalized out.
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
    // By default an ice skill adds one stack per line. A skill that lists its
    // own count replaces that entirely; see FreezeBuild.
    if (skill->has_freeze_build()) {
      attack.freeze_build = skill->freeze_build().crowd();
      attack.freeze_build_alone = skill->freeze_build().alone();
    } else {
      attack.freeze_build = attack.lines;
    }
    // Glacial Fury's magic attack as a share of the attack's damage, since
    // damage scales linearly with attack and a share is what the fight can
    // multiply by.
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

// One strike of a SwingHit, with its own multiplier, lines and crit chance on
// top of the character's stats. A separate group, so it rolls separately.
HitGroup SwingHitGroup(const SwingHit& hit, const OffenseStats& offense,
                       int level, const std::vector<CombatType>& types,
                       int& lines) {
  OffenseStats extra = offense;
  SkillEffect lands = EffectAt(hit.base(), hit.per_level(), level);
  extra.skill_pct = lands.skill_pct();
  extra.normal_skill_pct = lands.normal_skill_pct();
  extra.normal_pct += lands.normal_pct();
  extra.crit_rate += lands.crit_rate();
  // Multiplied with the character's and attack's final damage, like all final
  // damage sources. See SwingHit.
  extra.final_dmg_pct =
      (1.0 + extra.final_dmg_pct) * (1.0 + lands.final_dmg_pct()) - 1.0;
  extra.lines = std::max(1, hit.lines());
  // The shadow copies it like the rest of the attack. Reset because the line
  // count just changed. See SwingHit.skips_mirror.
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

// An extra hit within the same attack, priced separately and added in. Its own
// group makes it roll separately; see SwingHit.
void AddSwingHit(const SwingHit& hit, const OffenseStats& offense, int level,
                 const std::vector<CombatType>& types, AttackOption& attack) {
  SkillEffect lands = EffectAt(hit.base(), hit.per_level(), level);
  int lines = 0;
  HitGroup group = SwingHitGroup(hit, offense, level, types, lines);
  int casts = SwingHitCasts(hit);
  // Kept separate, like a Final Attack with its own reach, since it hits
  // enemies the attack didn't.
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
    // The widest of them, like the Final Attack total: they belong to one
    // attack, so its reach is the furthest any of them goes.
    attack.wide_hit_enemies =
        std::max(attack.wide_hit_enemies, hit.max_enemies());
    attack.hp_recover_pct += lands.hp_recover_pct() * lines * casts;
    return;
  }
  for (std::size_t i = 0; i < types.size() && i < group.damage.size(); ++i) {
    attack.damage_per_hit[i] += group.damage[i] * casts;
  }
  // One group per strike, like the attack's own: Sword Illusion triggers five
  // explosions and each rolls separately.
  for (int i = 0; i < casts; ++i) {
    attack.groups.push_back(group);
  }
  // HP recovery on a hit is paid per line (GMS's "for every final attack that
  // lands"), unlike the attack's own, which is per cast.
  attack.hp_recover_pct += lands.hp_recover_pct() * lines * casts;
}

// The damage so far is one pulse, so add the finishing strike and restate the
// attack as a full hold. A hold that grows has two pulse strengths, so the
// total sums both; the grown pulse is stored on the hold, not in the groups,
// which are always read as "first is a pulse, the rest are the finish". Attack
// speed doesn't change the pulse rate, since it belongs to the skill.
void AddChannel(const Skill& skill, const OffenseStats& offense, int level,
                const std::vector<CombatType>& types, double speed_factor,
                AttackOption& attack) {
  const Channel& channel = skill.channel();
  if (channel.max_pulses() <= 0 || channel.pulse_interval_ms() <= 0) {
    return;
  }
  std::vector<double> pulse = attack.damage_per_hit;
  // The attack's own HP recovery is one pulse's, like its damage, so move it to
  // the hold before the finish adds its own.
  double pulse_recover = attack.hp_recover_pct;
  attack.hp_recover_pct = 0.0;
  // Some holds end without a strike; pricing an empty one would add a group
  // that deals a floor of 1 damage per enemy.
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
  // Game-scaled like every other timer here: charges refill in the same
  // stretched seconds that pulses use.
  hold.charge_seconds = channel.charge_seconds() * speed_factor;
  hold.max_charges = channel.max_charges();
  hold.pulses_per_charge = channel.pulses_per_charge();
  // The pulses that fit within the minimum hold time: the fewest a cast can be
  // released after. At least one, or the attack would be only the finish.
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

// The timing and conditions a skill puts on its attack. A key-down skill fires
// at its own rate regardless of weapon speed, so it uses the neutral stage.
// Game speed still stretches every timer here, since that's the game running
// slower than GMS, not the weapon.
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
  // Attacks GMS lets the player use mid-attack play no cast animation, so the
  // recorded animation time isn't their cost. See Skill.waives_cast.
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
  // Game-scaled like the cooldown it reduces; otherwise a barrage on a cleared
  // map would refund more time than the cooldown was.
  attack.cooldown_refund_seconds =
      skill->cooldown_refund_seconds() * speed_factor;
  // Game-scaled, so a freeze covers the same share of the fight as in GMS.
  attack.freeze_seconds = skill->freeze_seconds() * speed_factor;
  // Game-scaled like freeze, for the same reason.
  attack.stun_seconds = skill->stun().duration_seconds() * speed_factor;
  attack.stun_lift_pct = skill->stun().final_dmg_pct();
  if (skill->stun().chance() > 0.0) {
    attack.stun_chance = skill->stun().chance();
  }
  // An attack gets the stun bonus if it has the tag and isn't the skill that
  // stunned: GMS excludes Jupiter Thunder from its own shock.
  attack.collects_stun_lift =
      derived.stun_lift.lifted_tag != SKILL_TAG_UNSPECIFIED &&
      HasTag(skill, derived.stun_lift.lifted_tag) &&
      skill->name() != derived.stun_lift.from_skill;
  // Marks work the same way. The marking skill isn't excluded, but the angel
  // has no element, so the tag already keeps it off its own mark.
  attack.mark_seconds = skill->mark().duration_seconds() * speed_factor;
  attack.mark_lift_pct = skill->mark().final_dmg_pct();
  attack.collects_mark_lift =
      derived.mark_lift.lifted_tag != SKILL_TAG_UNSPECIFIED &&
      HasTag(skill, derived.mark_lift.lifted_tag);
  // These apply to anything that hits the mob, summon pulses included, since
  // the mob's status doesn't depend on what hits it.
  attack.scar_fd = derived.scar.final_dmg_pct;
  attack.fd_when_afflicted = derived.condition.final_dmg_pct_when_afflicted;
  attack.fd_per_dot = derived.condition.final_dmg_pct_per_dot;
  attack.dot_count_cap = derived.condition.dot_count_cap;
  SkillEffect granted = EffectAt(skill->base(), skill->per_level(), level);
  attack.heal_fraction = granted.heal_pct();
  if (skill->kind() != SKILL_KIND_ATTACK) {
    return;
  }
  // Scarring and HP recovery come from the character's own attacks only, not
  // anything on its own timer: in GMS, the sword being swung scars. See
  // WithoutSwingLevers.
  attack.scar_chance = derived.scar.chance;
  attack.scar_seconds = derived.scar.seconds * speed_factor;
  attack.hp_recover_pct = granted.hp_recover_pct();
}

// The stronger opening hit some attacks land before spreading (GMS's "strikes
// one, then detonates in place"). Same character and weapon with the skill's
// other multiplier; only the target count differs, which the fight handles.
void AddLeadHit(const Skill& skill, const OffenseStats& offense, int level,
                const std::vector<CombatType>& types, AttackOption& attack) {
  if (skill.base().lead_pct() <= 0.0) {
    return;
  }
  OffenseStats lead = offense;
  lead.skill_pct =
      skill.base().lead_pct() + skill.per_level().lead_pct() * (level - 1);
  lead.lines = std::max(1, skill.lead_lines());
  // The shadow copies the opening hit like every other line. Reset because
  // lead.lines just changed.
  lead.mirror_lines = offense.mirror_lines > 0 ? lead.lines : 0;
  for (const CombatType& type : types) {
    attack.lead_damage.push_back(ExpectedAttackDamage(lead, *type.mob));
  }
  attack.lead_rolls = RollsFor(lead);
  attack.lead_enemies = std::max(1, skill.lead_enemies());
}

// Burns mark what the attack hit rather than being part of the hit, so they are
// priced here and deal damage on their own timer. Their damage is fixed when
// applied, so a burn applied under a buff keeps the buffed value.
//
// The character's own burns come first, in their own order, so every attack
// writes the same poison to the same slot. They use the plain `follow` stats,
// like Final Attacks: the poison is on the claw, not in the skill.
void AddBurns(const Skill* skill, const DerivedStats& derived,
              const OffenseStats& offense, const OffenseStats& follow,
              int level, const std::vector<CombatType>& types,
              double speed_factor, AttackOption& attack) {
  for (const CharacterDot& carried : derived.dots) {
    attack.dots.push_back(BurnFor(carried.dot, follow, carried.level, types,
                                  speed_factor, nullptr));
    attack.dots.back().carried = true;
    attack.dots.back().credit = carried.skill_name;
  }
  if (skill == nullptr || skill->dot().interval_seconds() <= 0.0) {
    return;
  }
  // Look up by the attack's own name rather than its parent's, so a form with
  // its own burn gets bonuses filed under its name.
  std::map<std::string, SkillBonus>::const_iterator boost =
      derived.skill_bonus.find(skill->name());
  attack.dots.push_back(
      BurnFor(skill->dot(), offense, level, types, speed_factor,
              boost != derived.skill_bonus.end() ? &boost->second : nullptr));
  attack.dots.back().credit = skill->name();
}

// The stats for one Final Attack source. Built fresh from `carried` each time
// so one source's bonuses don't leak into the next.
OffenseStats FollowFor(const OffenseStats& carried,
                       const FinalAttackSource& source) {
  OffenseStats follow = carried;
  // The source's own boss damage, on top of the character's: Blood Money boosts
  // the coins, not the Shadower.
  follow.boss_pct = carried.boss_pct + source.boss_pct;
  follow.damage_pct = carried.damage_pct + source.damage_bonus_pct;
  // Ignore defense combines multiplicatively with the character's, as always.
  follow.ied = CombineIgnoredDefense(carried.ied, source.ied);
  follow.crit_rate = carried.crit_rate + source.crit_rate;
  follow.final_dmg_pct =
      (1.0 + carried.final_dmg_pct) * (1.0 + source.final_dmg_pct) - 1.0;
  follow.skill_pct = source.damage_pct;
  // Skill damage against non-bosses, on the source's own multiplier.
  follow.normal_skill_pct = source.normal_skill_pct;
  // Its own hit count, not the attack's: a Night Lord's mark throws three stars
  // after a four-star attack, and each of the three rolls separately.
  follow.lines = source.lines;
  return follow;
}

// Final Attacks come from the attack, not the skill: a plain hit at its own
// percent, priced from the plain `follow` stats without the skill's
// multiplier or lines. A source that names a tag follows only attacks with
// that tag, and each source keeps its own entry and rolls separately.
//
// A per-line source rolls `swing_lines` times: four lines knock four mesos
// loose. Shadow copies don't count as the character's lines.
void AddFinalAttacks(const Skill* skill, const DerivedStats& derived,
                     OffenseStats follow, int level, int swing_lines,
                     const std::vector<CombatType>& types,
                     AttackOption& attack) {
  attack.final_attack_damage.assign(types.size(), 0.0);
  attack.per_swing_final_attack_damage.assign(types.size(), 0.0);
  // How much of each chance this attack keeps: knocking meso loose, and
  // triggering the extra hit. See SkillEffect.meso_drop_cut and
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
  // The character's stats before any source adds to them.
  const OffenseStats carried = follow;
  for (const FinalAttackSource& source : derived.final_attacks) {
    if (source.required_tag != SKILL_TAG_UNSPECIFIED &&
        !HasTag(skill, source.required_tag)) {
      continue;
    }
    FinalAttackRoll roll;
    // The attack's reduction applies to every source; the meso reduction
    // applies only to the meso source.
    roll.chance = source.chance * follow_kept;
    if (source.per_line) {
      roll.chance *= meso_kept;
    }
    follow = FollowFor(carried, source);
    roll.count = source.per_line ? swing_lines : 1;
    roll.follows_own_clock = source.follows_own_clock;
    roll.max_enemies = source.max_enemies;
    roll.credit = source.credit;
    roll.rolls = RollsFor(follow);
    // A source with its own reach is totaled separately, since its value is
    // added over its own reach rather than the attack's.
    std::vector<double>& bank = source.max_enemies > 0
                                    ? attack.per_swing_final_attack_damage
                                    : attack.final_attack_damage;
    for (std::size_t i = 0; i < types.size(); ++i) {
      roll.damage.push_back(ExpectedAttackDamage(follow, *types[i].mob));
      bank[i] += roll.damage.back() * roll.chance * roll.count;
    }
    if (source.max_enemies > 0) {
      // The widest of the per-attack sources: they roll on the same attack, so
      // their reach is the furthest any of them goes.
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

// The strike this attack triggers on its own cooldown, priced as a separate
// attack. It isn't the character's attack, so nothing is triggered by it.
void AddSideStrike(const Character& proto, const EquipStats& equipped,
                   EquipType weapon, const Skill& skill, int level,
                   const std::vector<CombatType>& types,
                   const DerivedStats& derived, double speed_factor,
                   AttackOption& attack) {
  const SideStrike& side = skill.side_strike();
  // Uses the character's stats without skill-specific bonuses. Bonuses aimed at
  // this skill by name belong to the attack (GMS's Showdown - Reinforce doesn't
  // boost the shuriken), and the skill itself isn't passed, so its own effects
  // stay on the attack too. The strike deals only what it lists.
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
  strike.credit = attack.credit;
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
  // One group per strike, like extra hits: Mighty Mjolnir leaves a shockwave on
  // every enemy the hammer hits, and each rolls separately.
  int casts = std::max(1, side.casts());
  for (double damage : once) {
    strike.damage_per_hit.push_back(damage * casts);
  }
  for (int i = 0; i < casts; ++i) {
    strike.groups.push_back({once, RollsFor(stats)});
  }
  attack.side = std::make_shared<const AttackOption>(std::move(strike));
}

// Turns the single priced strike into the attack the skill actually makes: HP-
// based damage added to every line, how many times it lands, and how it
// scatters.
void AddCastShape(const Skill* skill, int level, const DerivedStats& derived,
                  const OffenseStats& offense, double speed_factor,
                  AttackOption& attack) {
  if (skill != nullptr) {
    // Damage based on the character's max HP is added after the formula, so no
    // multiplier affects it. Every line gets it, as GMS applies it per attack.
    double pool = (skill->base().max_hp_damage_pct() +
                   skill->per_level().max_hp_damage_pct() * (level - 1)) *
                  derived.max_hp * SkillLinesAt(*skill, level);
    for (double& damage : attack.damage_per_hit) {
      damage += pool;
    }
  }
  // The damage so far is one strike. A skill that slashes several times lands
  // it again for each, each rolling its own mastery and crits.
  int casts = skill != nullptr ? SkillCasts(*skill) : 1;
  // Strikes spaced out in time stay as one here; the fight lands it again per
  // bolt, clearing dead mobs in between. See Skill.cast_interval_ms.
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
  // Game-scaled like every other timer: game speed stretches the gap between
  // bolts as much as the attack itself.
  attack.cast_interval_seconds =
      in_sequence ? skill->cast_interval_ms() / 1000.0 * speed_factor : 0.0;
  attack.pierce_gain_pct = skill->pierce_gain_pct();
  attack.lines = SkillLinesAt(*skill, level) * (in_sequence ? 1 : casts);
  // The attack's shape stays fixed; how many hits land where is up to the
  // fight, like the opening hit's target count.
  attack.scatter_hits = skill->scatter().hits();
  attack.scatter_repeat_kept = 1.0 + skill->scatter().repeat_final_dmg_pct();
  attack.scatter_hits_per_dot = skill->scatter().hits_per_dot();
  attack.scatter_max_hits = skill->scatter().max_hits();
  attack.scatter_max_hits_per_enemy = skill->scatter().max_hits_per_enemy();
}

// One attack's damage against every mob type. `skill` is null for the basic
// attack, a plain 100% hit on one target. `equipped` is the character's gear
// plus what passives grant; the formula can't tell them apart.
AttackOption AttackFor(const Character& proto, const EquipStats& equipped,
                       EquipType weapon, const Skill* skill, int level,
                       const std::vector<CombatType>& types,
                       const DerivedStats& derived, int attack_speed,
                       double speed_factor) {
  AttackOption attack;
  AddSwingClocks(skill, level, derived, attack_speed, speed_factor, attack);
  attack.credit = attack.name;
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
    // Two hits at once (like the hammer and its brand exploding), differing
    // only in multiplier, so each is priced separately and summed.
    for (const SwingHit& hit : skill->extra_hit()) {
      AddSwingHit(hit, offense, level, types, attack);
    }
    // Hits another skill adds to this one by name, priced separately and
    // already evaluated at the granting skill's level. See
    // SkillBoost::extra_hit.
    std::map<std::string, SkillBonus>::const_iterator aimed =
        derived.skill_bonus.find(skill->name());
    if (aimed != derived.skill_bonus.end()) {
      for (const SwingHit& hit : aimed->second.extra_hit) {
        AddSwingHit(hit, offense, level, types, attack);
      }
      // A wound another skill adds to this one by name. Assassinate and Sonic
      // Blow don't mention it; Trickblade names them. See Wound.
      attack.wound_stacks = aimed->second.wound_stacks;
      attack.wound_max_stacks = aimed->second.wound_max_stacks;
      attack.wound_seconds = aimed->second.wound_seconds * speed_factor;
    }
    AddChannel(*skill, offense, level, types, speed_factor, attack);
  }
  // The plain stats everything triggered by the attack is priced from: no
  // skill, so no multiplier or lines, and no shadow, which copies the attack
  // but not what it triggers.
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

// Adds the mob types `spawns` puts in front of the player, each with the damage
// one of its hits deals. Types not in the mob catalog are skipped.
void AddTypes(const GameState& state,
              const google::protobuf::RepeatedPtrField<Spawn>& spawns,
              const DefenseStats& defense, double scar_enemy_attack_pct,
              CombatParams& params) {
  // The same defense against a scarred (weakened) mob. Weakening effects add
  // up.
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

// Whether an attack can be spent on this skill: it deals damage, or it's a cast
// with an effect we model. Anything else would use up the attack and do
// nothing.
bool Castable(const Skill& skill) {
  if (DealsDamage(skill.kind())) {
    return true;
  }
  return skill.kind() == SKILL_KIND_ACTIVE && skill.base().heal_pct() > 0.0;
}

// Whether the character has this skill at all. This doesn't mean it can be used
// as an attack: a passive with an auto-firing part still contributes to the
// fight. Castable decides whether it's also offered as an attack.
bool Available(const GameState& state, const Skill& skill,
               const std::set<std::string>& superseded, Activity activity) {
  // A replaced skill is no longer offered: Piercing Arrow II fully replaces the
  // one it upgrades.
  if (superseded.count(skill.name()) > 0) {
    return false;
  }
  // Learned levels are keyed by display name, which branches share, so check
  // which job's book this is. Use HoldsSkillFrom, not HasAdvancement, or V
  // nodes would never be usable, since common nodes belong to no advancement.
  if (!state.character.HoldsSkillFrom(skill, activity)) {
    return false;
  }
  // Skip skills the current weapon can't use. The basic attack is always
  // available, so the character is never left with nothing.
  return SkillGearMet(state.character, skill, activity);
}

// The element belongs to the skill, not the caster: Spirit of Snow's blizzard
// is ice whoever summoned it, and freezes the same way. The parent's other tags
// are copied too, but don't matter for a strike like this.
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

// An auto-firing part of a skill as a skill of its own, so the same damage code
// builds it. It keeps the parent's name, since to the player it is one skill.
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

// The damage a skill's buff deals over time, as a skill of its own. It hits the
// same enemies as the attack, since it's the mark the attack left.
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

// How often a pulse fires: its own interval, or the attack speed of the skill
// it's attached to.
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

// The turret's final shot as a skill of its own. It keeps the pulse's bonuses
// and uses its own damage, since it's the same turret (GMS lists the scroll's
// boss damage once). Values the burst lists override, like merging one proto3
// message into another.
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

// The damage-dealing part of a buff, or of one of its forms: an attack on the
// buff's timer that fires only while that buff (and form) is active.
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
  // Everything here fires on the buff's timer rather than being an attack, so
  // nothing that triggers on attacks applies.
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
  // Restore the HP recovery ClearSwingRiders removed: Darkness Aura's heal is
  // on the aura's own attack, so it's paid per pulse.
  wound.hp_recover_pct =
      EffectAt(pulse.base(), pulse.per_level(), level).hp_recover_pct();
  wound.strikes_per_pulse = std::max(1, pulse.casts());
  wound.max_pulses = pulse.max_pulses();
  wound.needs_buff_stance = stance;
  // The escalating forms of a stacking poison: one per stack, each the full
  // strike, so each rolls its own mastery and crits.
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
  // One extra line of the rain, built as a single-line strike so the fight can
  // land as many as the attack earned, each rolling separately.
  if (pulse.lines_per_extra_enemy() > 0 && pulse.max_extra_lines() > 0) {
    Skill one = bleed;
    one.set_lines(1);
    wound.extra_line = std::make_shared<const AttackOption>(own_clock(one));
    wound.lines_per_extra_enemy = pulse.lines_per_extra_enemy();
    wound.max_extra_lines = pulse.max_extra_lines();
  }
  // The turret's final strike as the scroll bursts. It lands with the last
  // tick, not an interval later, since by then the buff has ended.
  if (pulse.has_final_strike()) {
    const SwingHit& burst = pulse.final_strike();
    AttackOption last = own_clock(FinalStrikeSkill(bleed, burst));
    last.strikes_per_pulse = SwingHitCasts(burst);
    wound.final_strike = std::make_shared<const AttackOption>(std::move(last));
  }
  set.auto_attacks.push_back(std::move(wound));
  // Stars each tick throws regardless of crowd size. A separate attack rather
  // than extra lines on the volley, since the volley scales with the crowd
  // while these are worth the same against a lone boss.
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

// Adds any auto-firing parts of a skill alongside its normal attack. Most
// skills have none.
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
    // Attack speed doesn't apply: this fires on its own interval, which weapon
    // speed doesn't affect.
    AttackOption attack =
        AttackFor(proto, equipped, weapon_type, &built, level, types, derived,
                  kUnscaledAttackSpeedStage, speed_factor);
    attack.swing_seconds = 0.0;  // not an attack, so never charged
    ClearSwingRiders(attack);    // swing-triggered effects don't apply
    attack.strikes_per_pulse = std::max(1, mode.casts());
    attack.silent_while_buff = mode.silent_while_buff_stands();
    // Triggered by the character's attacks rather than by time, so it goes in
    // the list the fight credits landed attacks to.
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
  // A buff with forms has one pulse per form. All are listed, and only the form
  // the cast raised fires, which needs_buff_stance controls.
  for (int i = 0; i < skill.buff().stance_size(); ++i) {
    AddBuffPulse(proto, equipped, weapon_type, skill,
                 skill.buff().stance(i).pulse(), i, level, derived, skills,
                 attack_speed, speed_factor, types, set);
  }
}

// What the rest of the job book gives one skill: extra lines, reach, trigger
// rate, and cooldown reduction.
struct SkillBoosts {
  int lines = 0;
  // Extra lines on each of its additional hits (the opening hit and every extra
  // hit), which are priced separately from its main lines.
  int extra_hit_lines = 0;
  int max_enemies = 0;
  int attacks_per_cast = 0;
  // The fraction of the cooldown left, so two reductions combine
  // multiplicatively like ignore defense. 1.0 means no reduction.
  double cooldown_left = 1.0;
  // What the book adds to the skill's buff: duration, shield hits, and damage
  // reduction against hits the shield can't block.
  double buff_duration_seconds = 0.0;
  double shield_hits = 0.0;
  double shield_boss_damage_taken_pct = 0.0;
};

// Every such bonus, summed and keyed by the skill it targets. Collected up
// front because the granting skill may come after the one it boosts, and every
// attack is built with all bonuses already applied.
std::map<std::string, SkillBoosts> BoostsByTarget(
    const CharacterInstance& character,
    const std::map<std::string, Skill>& skills, int bonus,
    Activity activity = Activity::kFarming) {
  std::map<std::string, SkillBoosts> by_target;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // Learned levels are keyed by display name, which branches share, so only
    // the character's own book grants anything. Asks the character, not the
    // advancement, since V nodes belong to no book.
    if (!character.HoldsSkillFrom(skill, activity)) {
      continue;
    }
    int learned = EffectiveSkillLevel(character, skill, bonus, activity);
    if (learned <= 0) {
      continue;
    }
    for (const SkillBoost& boost : skill.boost()) {
      // Skip bonuses the granting skill hasn't reached yet, like the extra
      // enemy a boost node's Lv20 tier adds.
      if (learned < boost.min_level()) {
        continue;
      }
      int enemies = boost.max_enemies() +
                    WholeValue(boost.max_enemies_per_level() * (learned - 1));
      // Empowered forms use their own name, so bonuses that reach them are
      // filed there. See BoostTargetNames.
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
        // Trigger rates replace rather than add, so the faster one wins.
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

// `skill` with the job book's bonuses applied. Its per-level line count is
// resolved at `level` first, so granted lines are added on top rather than
// scaled again. Empowered forms come through here under their own name.
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
  // Additional hits each gain the extra lines too. The opening hit only counts
  // if the skill has one, since lead_lines means nothing without it.
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
  // Scale the whole per-level cooldown so the reduction is the same share at
  // every level; CooldownAt then reads the scaled copy normally.
  if (it->second.cooldown_left < 1.0) {
    scratch.set_cooldown_seconds(skill.cooldown_seconds() *
                                 it->second.cooldown_left);
    scratch.set_cooldown_seconds_per_level(skill.cooldown_seconds_per_level() *
                                           it->second.cooldown_left);
  }
  return scratch;
}

// Credits a form or stored attack to the skill it belongs to, along with the
// burns it applies. Burns the character carries keep their own skill.
void CreditTo(const std::string& skill, AttackOption& attack) {
  attack.credit = skill;
  for (DotApplication& dot : attack.dots) {
    if (!dot.carried) {
      dot.credit = skill;
    }
  }
}

// An empowered form as a skill of its own. Unlike an auto-firing part, it gets
// its own name: it's a different attack and must not get the permanent bonus
// its parent gives the normal one. See SkillBoost::reach.
Skill EmpoweredSkill(const Skill& skill, const EmpoweredForm& upgrade,
                     const std::string& target, SkillKind kind, int reach) {
  Skill form;
  form.set_name(EmpoweredSkillName(target));
  // Uses the kind of the attack it replaces, not the granting skill's: the
  // grant is often a passive, and a form replacing a pulse is itself a pulse.
  form.set_kind(kind);
  *form.mutable_base() = upgrade.base();
  *form.mutable_per_level() = upgrade.per_level();
  // A form with no reach of its own reaches as far as the attack it replaces:
  // Mist Eruption triggers the mist exactly where the mist is.
  form.set_max_enemies(upgrade.max_enemies() > 0 ? upgrade.max_enemies()
                                                 : reach);
  form.set_lines(upgrade.lines());
  *form.mutable_extra_hit() = upgrade.extra_hit();
  // The form is the same arrow, upgraded, so it gets the same pierce bonus.
  form.set_pierce_gain_pct(skill.pierce_gain_pct());
  return form;
}

// A wound's heavier form: its own multiplier, reach and hits, with its own name
// so the damage log and display can tell the attacks apart.
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

// Attaches the form the fight uses instead of the normal attack while a wound
// is at max stacks. Built here directly, unlike empowered forms, since it
// always belongs to the skill that lists it.
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
  AttackOption heavier = AttackFor(proto, equipped, weapon_type, &form, learned,
                                   types, derived, attack_speed, speed_factor);
  CreditTo(skill.name(), heavier);
  attack.wound_form = std::make_shared<AttackOption>(std::move(heavier));
}

// Attaches `skill`'s empowered form to every attack it upgrades. The form
// replaces the attack, so it keeps its timing: an animation for an attack, and
// none for a summon, which keeps the timer it replaced.
void AttachEmpoweredForm(const GameState& state, const EquipStats& equipped,
                         EquipType weapon_type, const Skill& skill,
                         const EmpoweredForm& upgrade, int learned,
                         const DerivedStats& derived, int attack_speed,
                         double speed_factor,
                         const std::vector<CombatType>& types, SkillKind kind,
                         const std::map<std::string, SkillBoosts>& boosts,
                         std::vector<AttackOption>& into) {
  // If no target is named, it upgrades its own skill's attack.
  const std::string& target =
      upgrade.skill_name().empty() ? skill.name() : upgrade.skill_name();
  for (AttackOption& attack : into) {
    if (attack.name != target) {
      continue;
    }
    Skill form =
        EmpoweredSkill(skill, upgrade, attack.name, kind, attack.max_enemies);
    // Apply the book's bonuses filed under the form's own name.
    Skill boosted;
    const Skill& swung = Boosted(form, learned, boosts, boosted);
    std::shared_ptr<AttackOption> swing = std::make_shared<AttackOption>(
        AttackFor(state.character.proto(), equipped, weapon_type, &swung,
                  learned, types, derived, attack_speed, speed_factor));
    swing->swing_seconds = attack.swing_seconds;
    CreditTo(attack.credit, *swing);
    // Final Attacks follow the character's attacks, and a summon pulse isn't
    // one, so a form replacing a pulse can't have them either.
    if (attack.interval_seconds > 0.0) {
      ClearSwingRiders(*swing);
    }
    attack.empowered_every = upgrade.casts_per_trigger();
    attack.brands_enemies = upgrade.brands_each_enemy();
    attack.empowered = swing;
  }
}

// A second pass after every attack is built, since a skill's form may target
// another skill by name before that skill is built.
void AddEmpoweredForms(const GameState& state, const EquipStats& equipped,
                       EquipType weapon_type, const DerivedStats& derived,
                       int attack_speed, double speed_factor,
                       const std::vector<CombatType>& types, AttackSet& set) {
  int bonus = BonusSkillLevels(state.character, state.skills);
  std::map<std::string, SkillBoosts> boosts =
      BoostsByTarget(state.character, state.skills, bonus, derived.activity);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    int learned =
        EffectiveSkillLevel(state.character, skill, bonus, derived.activity);
    if (learned <= 0) {
      continue;
    }
    // A form replacing a pulse is a pulse: no shadow and no meso, for the
    // reason given in AddAttacks.
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

// The attack a buff loads, as a skill of its own. Named after the magazine's
// label, which the fight uses to find it and bonuses are filed under.
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
  // Weapon requirements and tags come from the skill that loads it: it's one
  // press of the same button, so the same things trigger Final Attacks.
  *loaded.mutable_required_equip_type() = skill.required_equip_type();
  *loaded.mutable_tags() = skill.tags();
  return loaded;
}

// Attaches a stored attack to every attack that fires it, and returns whether
// any does. `at` is where its charges are counted: one shared pool, however
// many attacks use it.
bool HangLoad(const Magazine& magazine, AttackOption& load, AttackSet& set) {
  int at = static_cast<int>(set.attacks.size());
  // Start at 1: the basic attack isn't a skill and doesn't fire stored attacks.
  // If it did, the fight would pick it just to burn through them.
  for (int i = 1; i < at; ++i) {
    // Another skill's stored attack isn't a regular attack either.
    if (set.attacks[i].charges > 0) {
      continue;
    }
    if (!magazine.spent_by_every_swing() &&
        set.attacks[i].name != magazine.spent_by_skill_name()) {
      continue;
    }
    load.spent_by_attack = i;
    // It fires as part of that attack rather than costing its own, so it takes
    // exactly the attack's time.
    load.swing_seconds = set.attacks[i].swing_seconds;
    set.attacks[i].loaded = std::make_shared<AttackOption>(load);
    set.attacks[i].loaded_attack = at;
  }
  return load.spent_by_attack >= 0;
}

// Adds one attack for each learned buff that loads one. A separate pass because
// the skill with the magazine is a buff, which AddAttacks has already skipped.
void AddMagazines(const GameState& state, const DerivedStats& derived,
                  EquipType weapon_type, int attack_speed, double speed_factor,
                  const std::vector<CombatType>& types, AttackSet& set) {
  const EquipStats total_stats = TotalEquipStats(state.character, derived);
  int bonus = BonusSkillLevels(state.character, state.skills);
  std::map<std::string, SkillBoosts> boosts =
      BoostsByTarget(state.character, state.skills, bonus, derived.activity);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    const Magazine& magazine = skill.buff().magazine();
    int learned =
        EffectiveSkillLevel(state.character, skill, bonus, derived.activity);
    if (learned <= 0 || magazine.charges() <= 0) {
      continue;
    }
    Skill loaded = MagazineSkill(skill, magazine);
    Skill boosted;
    const Skill& swung = Boosted(loaded, learned, boosts, boosted);
    AttackOption attack =
        AttackFor(state.character.proto(), total_stats, weapon_type, &swung,
                  learned, types, derived, attack_speed, speed_factor);
    CreditTo(skill.name(), attack);
    attack.charges = magazine.charges();
    attack.charges_per_swing = std::max(1, magazine.charges_per_swing());
    attack.recharge_seconds = magazine.recharge_seconds();
    attack.recharge_max = magazine.recharge_max();
    // A stored attack nothing else fires is its own button. One that another
    // attack fires stays in the list, where its charges are counted, but is
    // attached to the attacks that fire it and never chosen directly.
    if (magazine.spent_by_skill_name().empty() &&
        !magazine.spent_by_every_swing()) {
      set.attacks.push_back(std::move(attack));
      continue;
    }
    // Nobody has a skill that fires it, and leaving it in the list would let
    // the fight choose it directly.
    if (!HangLoad(magazine, attack, set)) {
      continue;
    }
    set.attacks.push_back(std::move(attack));
  }
}

// Removes a cast's damage. With no multiplier, the formula priced it as a basic
// attack, which a cast must not deal; and a cast that deals no damage hits
// nothing, so nothing triggers from it.
void MakeCastHarmless(AttackOption& attack) {
  std::fill(attack.damage_per_hit.begin(), attack.damage_per_hit.end(), 0.0);
  attack.groups.clear();
  attack.lead_damage.clear();
  ClearSwingRiders(attack);
  ClearFinalAttacks(attack);
}

// Files a skill that runs on its own timer under what the timer counts: landed
// attacks, kills, or seconds.
void FileAutoAttack(const Skill& swung, double speed_factor,
                    AttackOption& attack, AttackSet& set) {
  attack.swing_seconds = 0.0;  // not an attack, so never charged
  ClearSwingRiders(attack);    // swing-triggered effects don't apply
  if (swung.attacks_per_cast() > 0 || swung.kills_per_cast() > 0) {
    attack.attacks_per_cast = swung.attacks_per_cast();
    attack.kills_per_cast = swung.kills_per_cast();
    set.triggered_attacks.push_back(std::move(attack));
    return;
  }
  // A skill with no interval would fire every step, so naming neither is read
  // as "never fires" rather than "fires constantly".
  if (swung.cast_interval_seconds() <= 0.0) {
    return;
  }
  attack.interval_seconds = swung.cast_interval_seconds() * speed_factor;
  set.auto_attacks.push_back(std::move(attack));
}

// Every attack the character could use, basic attack first. Skills on their
// own timer go to auto_attacks instead. Passives apply to whichever attack is
// chosen, so each gets the full `derived`.
void AddAttacks(const GameState& state, const DerivedStats& derived,
                EquipType weapon_type, int attack_speed, double speed_factor,
                const std::vector<CombatType>& types, AttackSet& set) {
  const Character& proto = state.character.proto();
  const EquipStats total_stats = TotalEquipStats(state.character, derived);
  // A skill on its own timer isn't the character's attack: no shadow copies it
  // and it throws no meso, just as AttackFor removes its Final Attacks.
  DerivedStats off_clock = derived;
  off_clock.mirror_line_pct = 0.0;
  StripMesoDrops(off_clock);
  set.attacks.push_back(AttackFor(proto, total_stats, weapon_type, nullptr, 0,
                                  types, derived, attack_speed, speed_factor));
  int bonus = BonusSkillLevels(state.character, state.skills);
  std::map<std::string, SkillBoosts> boosts =
      BoostsByTarget(state.character, state.skills, bonus, derived.activity);
  std::set<std::string> superseded =
      DormantSkillNames(state.character, state.skills, bonus, derived.activity);
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    int learned =
        EffectiveSkillLevel(state.character, skill, bonus, derived.activity);
    if (learned <= 0 ||
        !Available(state, skill, superseded, derived.activity)) {
      continue;
    }
    // Apply lines and reach other skills grant this one before building
    // anything, so everything below sees one skill.
    Skill boosted;
    const Skill& swung = Boosted(skill, learned, boosts, boosted);
    AddAutoModes(proto, total_stats, weapon_type, swung, learned, off_clock,
                 state.skills, attack_speed, speed_factor, types, set);
    // Skills that can't be used as an attack stop here. Their auto-firing parts
    // were already added, which is all an aura does.
    if (!Castable(swung)) {
      continue;
    }
    // Everything below uses `swung`, not `skill`, or a bonus that changed the
    // trigger would be lost.
    AttackOption attack =
        AttackFor(proto, total_stats, weapon_type, &swung, learned, types,
                  swung.kind() == SKILL_KIND_AUTO_ATTACK ? off_clock : derived,
                  attack_speed, speed_factor);
    if (attack.heal_fraction > 0.0) {
      MakeCastHarmless(attack);
    }
    if (swung.kind() != SKILL_KIND_AUTO_ATTACK) {
      // How much this attack counts toward skills triggered by landed attacks.
      // Unset means 1, a normal attack.
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

// The character's attack speed stage: job and weapon, plus passives, capped at
// the soft cap and then beyond it where allowed. Computed per attack set, since
// buffs can add to it.
int AttackSpeedStageFor(const GameState& state, const EquipPrototype& weapon,
                        const DerivedStats& derived) {
  return AttackSpeedStage(BaseAttackSpeedStage(state.character.proto().job(),
                                               weapon.attack_speed()),
                          derived.attack_speed_bonus,
                          derived.uncapped_attack_speed_bonus);
}

// Gives each burn its own slot so two never overwrite each other. `shared` is
// how many the character carries, rather than any one attack. Numbered in
// attack order, the same in every buff combination, so a slot always means the
// same burn. Every kind of attack is numbered; one without a slot would be
// silently dropped by the fight.
void NumberDots(AttackSet& set, int shared) {
  int next = shared;
  std::vector<std::vector<AttackOption>*> lists = {
      &set.attacks, &set.auto_attacks, &set.triggered_attacks};
  for (std::vector<AttackOption>* list : lists) {
    for (AttackOption& attack : *list) {
      // A carried burn is the same burn wherever it's applied from, so it keeps
      // its slot among the character's burns.
      int carried = 0;
      for (DotApplication& burn : attack.dots) {
        burn.slot = burn.carried ? carried++ : next++;
      }
    }
  }
}

// Burn slots each monster needs for every burn this character can apply. Checks
// every list, since summons can apply burns too.
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

// Everything the character can attack with under one set of stats: their own,
// or with some buffs active.
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

// The index of `name`'s attack, or -1. Uses the unbuffed set, which has the
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

// How many party members, including the character, have `skill`. Party-shared
// buffs divide their cooldown by this; see Buff::party_shared.
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

// Buff Duration applies to every buff except V nodes: GMS marks all of those
// notIncBuffDuration, so we check v_node rather than marking each file. The
// base from constants.h is added here rather than to the stats, so V nodes skip
// it too.
double BuffDurationFor(const Skill& skill, double buff_duration_pct) {
  return skill.v_node() == V_NODE_KIND_UNSPECIFIED
             ? kBaseBuffDuration + buff_duration_pct
             : 0.0;
}

// One buff's duration and shield after job book bonuses. Seconds from Hyper
// skills are added before Buff Duration scales the total, since it's one
// duration no matter how many sources add to it.
BuffOption BuffClockFor(const Buff& buff, int level, const SkillBoosts& boost,
                        double buff_duration_pct, double speed_factor,
                        int stage) {
  BuffOption option;
  // Buff Duration lengthens the buff, not its cooldown, which is why it's worth
  // having even though it grants nothing itself.
  option.duration_seconds = (buff.duration_seconds() +
                             buff.duration_seconds_per_level() * (level - 1) +
                             boost.buff_duration_seconds) *
                            (1.0 + buff_duration_pct) * speed_factor;
  // One stage of a buff that fades in stages: the first ends one stage interval
  // in, the last lasts the full duration. Clamped, so a buff shorter than its
  // stages ends the rest with it.
  if (buff.stages() > 1) {
    option.duration_seconds =
        std::min(option.duration_seconds,
                 buff.stage_interval_seconds() * (stage + 1) * speed_factor);
  }
  // Same scaling as the duration above, since it's the same duration: burns add
  // seconds, not a separate effect.
  option.duration_seconds_per_dot = buff.duration_seconds_per_dot() *
                                    (1.0 + buff_duration_pct) * speed_factor;
  option.dot_count_cap = buff.dot_count_cap();
  // Game-scaled like every other timer here: the angel re-grants on stretched
  // seconds, just as it attacks on them.
  option.duty_seconds = buff.duty_seconds() * speed_factor;
  option.duty_interval_seconds = buff.duty_interval_seconds() * speed_factor;
  if (buff.has_shield()) {
    option.shield_hits = ShieldHitsAt(buff.shield(), level) + boost.shield_hits;
    option.boss_damage_taken_pct = buff.shield().boss_damage_taken_pct() +
                                   boost.shield_boss_damage_taken_pct;
  }
  return option;
}

// The character's buffs, with each stage of a staged buff listed separately.
// Each entry grants one stage's effects on one stage's timer, so the damage
// table's mask shows how many stages are still up.
std::vector<const Skill*> StagedBuffSkills(
    const std::vector<const Skill*>& raised) {
  std::vector<const Skill*> staged;
  for (const Skill* skill : raised) {
    staged.insert(staged.end(), BuffWindowsFor(skill->buff()), skill);
  }
  return staged;
}

// Adds the forms a buff can be raised in, and sets its duration to the longest
// of them so callers asking how long it lasts get an answer. Buff Duration
// isn't applied: every buff with forms is a V node, which BuffDurationFor
// exempts anyway.
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

// How the buff is raised and what it costs the fight: its own key press, a
// chance on each attack, or applied by the attack it's attached to.
void SetHowRaised(const Skill& skill, int level, int stage, double speed_factor,
                  const std::vector<AttackOption>& attacks,
                  BuffOption& option) {
  const Buff& buff = skill.buff();
  // The cost of raising it. A buff applied by an attack is paid for by that
  // attack, so it costs nothing here; see BuffOption::cast_seconds.
  option.cast_seconds = skill.base_delay_ms() / 1000.0 * speed_factor;
  // All stages of a staged buff go up in one cast, so only the first is
  // charged.
  if (stage > 0) {
    option.cast_seconds = 0.0;
  }
  // A buff raised by a chance on attacks is never pressed, so it has no cast
  // time or cooldown. One that requires a target with a status but lists no
  // chance is raised by every qualifying attack.
  option.needs_afflicted_target = buff.needs_afflicted_target();
  if (buff.raise_chance() > 0.0 || buff.raise_chance_per_level() > 0.0) {
    option.raise_chance = std::clamp(
        buff.raise_chance() + buff.raise_chance_per_level() * (level - 1), 0.0,
        1.0);
  } else if (option.needs_afflicted_target) {
    option.raise_chance = 1.0;
  }
  if (option.raise_chance > 0.0) {
    option.cast_seconds = 0.0;
  }
  // A buff attached to an attack is applied by that attack rather than raised
  // on a cooldown, unless it lists its own cast time.
  if (skill.kind() == SKILL_KIND_ATTACK) {
    if (buff.own_cast_delay_ms() > 0) {
      option.cast_seconds = buff.own_cast_delay_ms() / 1000.0 * speed_factor;
    } else {
      option.laid_by_attack = AttackNamed(attacks, skill.name());
      option.raised_on_cast = buff.raised_on_cast();
      option.needs_wound_form = buff.needs_wound_form();
      option.cast_seconds = 0.0;
    }
  }
}

// Adds what the fight needs to run each buff's timer at its learned level. The
// buffs' effects aren't here; they're built into the damage tables.
void AddBuffs(const GameState& state,
              const std::vector<const Skill*>& buff_skills, double speed_factor,
              const DerivedStats& derived, CombatParams& params) {
  const double buff_duration_pct = derived.buff_duration_pct;
  const CharacterInstance& character = state.character;
  const std::map<std::string, Skill>& skills = state.skills;
  absl::Span<const CharacterInstance> party = absl::MakeConstSpan(state.party);
  int bonus = BonusSkillLevels(character, skills);
  std::map<std::string, SkillBoosts> boosts =
      BoostsByTarget(character, skills, bonus, derived.activity);
  const Skill* previous = nullptr;
  int copy = 0;
  for (const Skill* skill : buff_skills) {
    copy = skill == previous ? copy + 1 : 0;
    previous = skill;
    int level = EffectiveSkillLevel(character, *skill, bonus, derived.activity);
    const Buff& buff = skill->buff();
    // Which stage this entry is. Stacking buffs' entries are identical (each is
    // gained by its own roll and lasts the full duration), so only staged buffs
    // number their copies.
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
    // Party members take turns casting a shared buff, so it comes back as often
    // as the party can cast it between them.
    if (buff.party_shared()) {
      option.cooldown_seconds /= PartyHolders(character, party, *skill);
    }
    SkillEffect held = EffectAt(buff.base(), buff.per_level(), level);
    option.damage_taken_pct = held.damage_taken_pct();
    option.cooldown_reduction_seconds =
        buff.cooldown_reduction_seconds() * speed_factor;
    // Counted in lines rather than seconds, so game speed doesn't apply: the
    // lines are already stretched.
    option.charge_lines = buff.charge_lines();
    option.heal_fraction = held.heal_pct();
    SetHowRaised(*skill, level, stage, speed_factor, params.attacks, option);
    // The attack this buff loads, found by the magazine's label, which is the
    // name AddMagazines built it under.
    if (buff.magazine().charges() > 0) {
      option.magazine_attack =
          AttackNamed(params.attacks, buff.magazine().label());
    }
    params.buffs.push_back(std::move(option));
  }
}

// Adds party members' buffs on this character, using their casters' timers and
// levels, after the character's own so one mask covers both. Another player
// casts them, so they cost this character no attack.
//
// A caster's Buff Duration and INT apply equally to their own portion and the
// party's: one cast, one duration. Worked out here, where the party is
// available. See BuffUp.
std::vector<BuffUp> AddAllyBuffs(const GameState& state, double speed_factor,
                                 int budget, CombatParams& params) {
  std::vector<BuffUp> raised;
  // Everyone in the area, caster included. There are no positions here, so
  // nobody is ever out of range. See AllyIntLever.
  int party_size = static_cast<int>(state.party.size()) + 1;
  for (const AllyGrant& grant : AllyBuffsFor(
           state.character, state.skills, absl::MakeConstSpan(state.party))) {
    // The character's own buffs take priority: dropping a party buff loses one
    // bonus, while dropping their own leaves a hole in their rotation.
    if (static_cast<int>(raised.size()) >= budget) {
      break;
    }
    const Buff& buff = grant.skill->buff();
    // Use the caster's book, not the reader's: one cast lasts the same for
    // everyone.
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
    // A shield that isn't a party shield protects only its caster.
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

// Whether a buff deals damage at all, through its own pulse or one of its
// forms'.
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

// Links each damage-dealing buff's pulse to its buff. Runs on the base set and
// every buffed set, or the link would appear and disappear with the buffs.
// Matched by name, since a pulse keeps its parent skill's name.
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

// Links each silenced auto-attack to the buff that silences it, also by name:
// something that fires only while its buff is down needs to know which buff.
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

// Links each dismissible summon to the buff that dismisses it. The silencing
// buff names what it dismisses.
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

// Links each buff form to its pulse, so the fight can compare them without
// searching. Only on the base set; the indices are the same in every buffed
// set.
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

// Expected damage per second with no buffs: the strongest attack plus
// everything on its own timer, against the first mob type. Intentionally rough;
// only used until the fight has measured its own rate. See
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
    // A pulse waiting on a buff isn't firing yet; counting it would make the
    // fight expect damage that isn't happening.
    if (cast.interval_seconds <= 0.0 || cast.needs_buff >= 0 ||
        cast.damage_per_hit.empty()) {
      continue;
    }
    own_clocks += cast.damage_per_hit[0] / cast.interval_seconds;
  }
  return best_swing + own_clocks;
}

// Records what's needed to build each buff combination's attack set, indexed as
// CombatParams::Attacks reads them. BuildBuffedSet builds them on first use.
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
  // Nothing is built here: params.buffs limits the masks, and each set is built
  // the first time the fight reaches it.
}

// Halves how many enemies an attack reaches, rounding up. Boss parts stand far
// apart, so an attack that gathers eight mobs on a map reaches far fewer boss
// parts. Rounded up so a skill still hits what it's aimed at.
void HalveReach(std::vector<AttackOption>& attacks) {
  for (AttackOption& attack : attacks) {
    attack.max_enemies = (std::max(1, attack.max_enemies) + 1) / 2;
  }
}

// Builds one buff combination's attack set from what AddBuffedSets stored. Runs
// every pass the base set went through, so all sets have the same shape.
AttackSet BuildBuffedSet(const CombatParams& params, int mask) {
  const BuffedSetSource& source = params.buffed_source;
  int own = static_cast<int>(source.buff_skills.size());
  std::vector<BuffUp> up;
  for (int i = 0; i < own; ++i) {
    if ((mask & (1 << i)) != 0) {
      up.push_back(BuffUp{source.buff_skills[i]});
    }
  }
  // Party buffs use the bits after the character's own, and carry the caster
  // whose level their portion is read at.
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

// The attack set for `mask`, built on first use. Null for masks no combination
// reaches, which the readers below treat as unbuffed.
const AttackSet* CombatParams::Window(int mask) const {
  if (mask <= 0) {
    return nullptr;
  }
  std::map<int, AttackSet>::iterator slot = buffed.find(mask);
  if (slot != buffed.end()) {
    return &slot->second;
  }
  // Hand-built params have no source to build from, so any other mask reads as
  // no buffs.
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

// The character's defensive stats, which are the same whichever mob hits them.
DefenseStats DefenseFor(const GameState& state, const DerivedStats& derived) {
  DefenseStats defense;
  defense.level = state.character.proto().level();
  defense.def = derived.def;
  defense.damage_taken_pct = derived.damage_taken_pct;
  defense.dodge_chance = derived.dodge_chance;
  defense.enemy_attack_pct = derived.enemy_attack_pct;
  defense.enemy_attack_reaches_boss = derived.enemy_attack_reaches_boss;
  defense.force_taken = derived.force_taken_factor;
  return defense;
}

// Sets everything that doesn't depend on the enemies: HP, passive bonuses, and
// timers that game speed stretches. The caller sets the respawn and hit
// intervals, since boss fights use neither.
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
  // Game speed stretches the time between pulses like every other timer. The
  // amount each heals is unchanged.
  params.regen_pulses.clear();
  for (const RegenPulse& pulse : derived.regen_pulses) {
    params.regen_pulses.push_back(
        {pulse.pct, pulse.hp, pulse.interval_seconds * speed_factor});
  }
  params.revive_cooldown_seconds =
      derived.revive_cooldown_seconds * speed_factor;
  // The window stretches with game speed but the heal per second doesn't, so
  // one activation heals what the skill says regardless of game speed.
  params.emergency_heal = derived.emergency_heal;
  params.emergency_heal.pct /= speed_factor;
  params.emergency_heal.seconds *= speed_factor;
  params.emergency_heal.cooldown_seconds *= speed_factor;
  params.freeze_cap = derived.freeze.cap;
}

// Every attack the character can use against `params`' mob types: unbuffed,
// then one table per buff combination. Damage taken uses the unbuffed stats: no
// buff raises max HP, and damage-reducing buffs apply to the hit itself.
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

// Halves reach on every attack list. Buffed tables must match the unbuffed one,
// but they aren't built yet, so they're flagged to halve when built.
void HalveBossReach(CombatParams& params) {
  HalveReach(params.attacks);
  HalveReach(params.auto_attacks);
  HalveReach(params.triggered_attacks);
  params.buffed_source.halve_reach = true;
}

// Time for the map to respawn its monsters, before game speed. The Wild Totem
// halves it; that's all it does.
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
  // The effect of the map's force requirement on both sides. Stored on derived,
  // which every builder below uses, since the requirement belongs to the map
  // and neither side alone can compute it.
  ForceFactors force = MapForceFor(map_it->second, state.character).factors;
  derived.force_damage_factor = force.damage_dealt;
  derived.force_taken_factor = force.damage_taken;
  // V Points drop in Arcane River and Grandis, which are the maps with a force
  // requirement.
  params.pays_v_points = AsksForForce(map_it->second);
  // The game speed for the whole encounter, and the only place here that uses
  // the character's level directly: the game slows down as they level.
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

// The drop rate for boss drops: the character's stats recomputed with the gear
// in their Drop preset, only if it differs from what they're wearing.
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

  // Boss fights use the bossing stat allocation.
  DerivedStats derived = DerivedStatsFor(state.character, state.skills, {},
                                         state.party, Activity::kBossing);
  // Boss fights run in real time at every level. Game speed stretches idle maps
  // so the player can leave them running; a watched fight needs neither that
  // nor respawns. Both intervals stay 0.
  AddPacing(state, derived, 1.0, params);
  AddTypes(state, difficulty.phases(phase).spawns(), DefenseFor(state, derived),
           derived.scar.enemy_attack_pct, params);
  if (params.types.empty()) {
    return params;
  }
  AddAttacks(state, derived, *weapon, 1.0, Activity::kBossing, params);
  params.drop_roll_item_drop_pct = DropRollRate(state, derived);
  HalveBossReach(params);
  // Attack the healthiest boss part first; see CombatParams::focus_healthiest.
  params.focus_healthiest = true;
  // The boss screen draws every damage line as a number; maps don't.
  params.record_damage_lines = true;
  params.active = true;
  return params;
}

}  // namespace ms
