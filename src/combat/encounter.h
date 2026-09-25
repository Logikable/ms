/* What the player is fighting: the map's mobs, how much damage each takes, and
 * how often attacks and respawns happen.
 *
 * ComputeCombatParams() reads this from a GameState once into a plain
 * CombatParams, and the fight runs only from that (see fight.h), so the two
 * can't disagree. All durations are in game-scaled seconds: real seconds
 * stretched by the character's GameSpeedFactor.
 */
#ifndef MS_SRC_COMBAT_ENCOUNTER_H_
#define MS_SRC_COMBAT_ENCOUNTER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/character/character_stats.h"
#include "src/character/stat_preset.h"
#include "src/combat/damage.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

// One mob type that can be targeted. `mob` is the source of truth for name, HP,
// EXP and drops; GameState owns it and it outlives the step.
struct CombatType {
  const Mob* mob = nullptr;
  int simultaneous = 0;  // how many spawn at once (SpawnCount)
  // Where each mob stands in a boss arena; empty on a map. Stored here rather
  // than read from the phase: a spawn whose mob isn't in the catalog never
  // becomes a type, so indexing into the phase would shift every later type by
  // one.
  std::vector<ArenaSpot> spots;
  // How it moves around the arena. Unset for mobs that stand still.
  ArenaWalk walk;
  // Expected damage of one hit to the player, after their DEF. Every mob of one
  // type hits the same.
  double damage_to_player = 0.0;
  // The same while the mob is scarred, which weakens its attack. Equal to
  // damage_to_player for characters without a scar effect.
  double damage_to_player_scarred = 0.0;
};

// One group of lines within an attack. An attack is one group plus one per
// extra hit the skill adds. Groups differ in line count and crit chance, so
// each rolls separately.
struct HitGroup {
  std::vector<double>
      damage;  // per target type, parallel to CombatParams::types
  SwingRolls rolls;
};

// A held (channeled) attack. The attack's own groups hold the damage: the first
// is one pulse, the rest are the final strike. A hold that grows stores its
// stronger pulse here instead. `pulses` is 0 for normal attacks. See Channel.
struct ChannelHold {
  int pulses = 0;
  // Minimum pulses per cast: as many as fit in min_seconds.
  int min_pulses = 0;
  double pulse_seconds = 0.0;
  double finish_seconds = 0.0;
  double min_seconds = 0.0;
  // Fraction of max HP restored per pulse, so releasing early heals less. The
  // final strike's heal is in AttackOption::hp_recover_pct.
  double hp_recover_pct = 0.0;
  // Fraction of incoming damage blocked while holding.
  double damage_taken_pct = 0.0;
  // Pulses at starting strength before the hold grows. 0 if it never grows.
  int small_pulses = 0;
  // One pulse after the hold has grown; empty if it never grows.
  HitGroup grown;
  // For holds paid for with charges instead of a cooldown: one charge every
  // `charge_seconds`, up to `max_charges`, each buying `pulses_per_charge`
  // pulses. 0 for holds on a cooldown. See Channel.
  double charge_seconds = 0.0;
  int max_charges = 0;
  int pulses_per_charge = 0;
};

// One burn (damage over time) an attack applies: damage per tick for each type,
// its timing, and how it's applied. One per source.
struct DotApplication {
  std::vector<double> damage;  // per target type, one tick's worth
  SwingRolls rolls;
  double interval_seconds = 0.0;
  double duration_seconds = 0.0;
  // Chance to apply on each enemy hit; 1 for burns that always apply.
  double chance = 1.0;
  // How many copies one monster can have at once, each dealing full damage.
  int max_stacks = 1;
  // Index into each mob's burn slots, so two burns don't overwrite each other.
  // Assigned per source and numbered the same in every buff combination, so a
  // slot means the same thing as buffs come and go.
  int slot = -1;
  // Whether the character applies it (rather than the attack): a rogue's claw
  // poison applies on their own attacks only.
  bool carried = false;
  // The skill this burn's damage is credited to in the breakdown: the poison's
  // source skill, or the attack that applied the burn.
  std::string credit;
};

// One Final Attack: a chance, rolled per enemy hit, of one extra hit on that
// enemy. `count` is above 1 only for sources that roll per line, like the meso
// Chief Bandit knocks loose.
struct FinalAttackRoll {
  double chance = 0.0;
  int count = 1;
  std::vector<double> damage;  // per target type, one hit's worth
  SwingRolls rolls;            // how that hit itself varies
  // Whether it still applies to attacks the character doesn't use directly. See
  // Skill.follows_own_clock.
  bool follows_own_clock = false;
  // For sources that roll once per attack, the number of enemies it hits; 0 if
  // it rolls per enemy.
  int max_enemies = 0;
  // The skill its damage is credited to in the breakdown.
  std::string credit;
};

// A chance for an attack to deal extra damage to one enemy, and what it heals
// when it triggers. Rolled once per attack; see the Proc message.
struct ProcRoll {
  double chance = 0.0;
  double damage_pct = 0.0;  // extra damage to that one enemy
  double hp_recover_pct = 0.0;
};

// One thing the character could spend an attack on: a basic attack, an attack
// skill, or a cast that does something else entirely. Which is best depends on
// how many mobs are in range, so the fight chooses each time: a wide skill that
// does less per target wins against a crowd and loses against the last mob.
struct AttackOption {
  std::string name = "Attack";  // shown on the charge bar
  // The skill this is credited to in the breakdown. Usually the name, but a
  // form replacing another skill, or a stored charge, is credited to the skill
  // it stands in for or that stored it.
  std::string credit = "Attack";
  int max_enemies = 1;  // mobs at the front of the queue one attack reaches
  // Hits per enemy per attack. Used by things that count hits rather than
  // attacks, like buffs charged by landed hits and Freeze Stacks.
  int lines = 1;
  // Bonus per enemy already pierced, compounding: the k'th enemy takes (1 +
  // this)^k. 0 for attacks without pierce scaling.
  double pierce_gain_pct = 0.0;
  // Expected damage per target, parallel to CombatParams::types.
  std::vector<double> damage_per_hit;
  // The same damage split into groups that roll, summing to damage_per_hit. If
  // empty, the average is dealt directly, which hand-built test attacks want.
  std::vector<HitGroup> groups;
  // Seconds per attack, from the skill's own delay: slower animations are the
  // price of harder-hitting skills. 0 for attacks on their own timer.
  double swing_seconds = 0.0;
  // Seconds between casts, for attacks on their own timer.
  double interval_seconds = 0.0;
  // Landed attacks between casts, for attacks triggered by attacking. An attack
  // has only one of these three triggers.
  int attacks_per_cast = 0;
  // Kills between casts, for attacks triggered by kills.
  int kills_per_cast = 0;
  // How much one landed attack counts toward attacks_per_cast. Usually 1; less
  // for attacks that land several times a second.
  double count_weight = 1.0;
  // Cooldown after it lands, in seconds.
  double cooldown_seconds = 0.0;
  // Uses granted each time the loading buff is raised, one spent per landing. 0
  // if no buff loads it. See Magazine.
  int charges = 0;
  // For a stored attack with no button of its own: the attack that fires it
  // (e.g. Poison Nova's clouds go off with Mist Eruption). The fight never
  // chooses an option with this set; -1 means it can be chosen normally.
  int spent_by_attack = -1;
  // The stored attack this one fires while a charge is available, and where
  // those charges are counted. Shared for the same reason as `empowered`.
  std::shared_ptr<const AttackOption> loaded;
  int loaded_attack = -1;
  // Charges spent per use, and hits dealt for them; if fewer are left, it
  // spends what remains. 1 for charges spent one at a time.
  int charges_per_swing = 1;
  // Recharge time for a charge when no buff provides them, and the maximum
  // charges. Recharges only while below the maximum.
  double recharge_seconds = 0.0;
  int recharge_max = 0;
  // The opening hit, per target type. It lands on the healthiest mobs in range,
  // since a hit this big wastes the least there.
  std::vector<double> lead_damage;
  // How many enemies it hits. 1 for the rogue version, which hits one and
  // spreads.
  int lead_enemies = 1;
  // The opening hit's own rolls, since its line count differs from the
  // attack's.
  SwingRolls lead_rolls;
  // Hits spread across the enemies in range, one each before any enemy takes a
  // second. 0 for attacks that hit each enemy once. See Skill::scatter.
  int scatter_hits = 0;
  // Damage kept by a repeat hit on an enemy the same cast already hit (0.45
  // means GMS's "Final Damage -55%").
  double scatter_repeat_kept = 1.0;
  // Extra hits added per burn on the targets, capped at scatter_max_hits. 0 for
  // a fixed hit count.
  double scatter_hits_per_dot = 0.0;
  int scatter_max_hits = 0;
  // Maximum hits on any one enemy; extra hits are lost. 0 means no limit. See
  // Scatter.
  int scatter_max_hits_per_enemy = 0;
  // Final Attack damage per type, rolled for each mob hit. Skills on their own
  // timer keep only sources marked as following them.
  std::vector<double> final_attack_damage;
  // The same per source, for rolling. If empty, the average is dealt.
  std::vector<FinalAttackRoll> final_attack_rolls;
  // The same two for sources that roll once per attack and hit their own set of
  // enemies. Kept separate because the damage is added once per attack here,
  // rather than once per enemy.
  std::vector<double> per_swing_final_attack_damage;
  std::vector<FinalAttackRoll> per_swing_final_attack_rolls;
  // Enemies those per-attack sources hit: 1 for Blizzard, ten for Split Shot
  // after an attack that hit one. Never 0 when the lists above aren't empty.
  int per_swing_final_attack_enemies = 1;
  // The part of the attack that hits its own, wider set of enemies: damage per
  // type, its rolls, and how many it hits. See SwingHit.
  std::vector<double> wide_hit_damage;
  std::vector<HitGroup> wide_hit_groups;
  int wide_hit_enemies = 0;
  // Burns this attack applies to the enemies it hits.
  std::vector<DotApplication> dots;
  // Fraction of max HP this option heals instead of dealing damage. It deals no
  // damage, and the fight picks it when HP is low rather than by damage; see
  // CombatSim::HealToCast.
  double heal_fraction = 0.0;
  // Fraction of max HP restored per landed hit, on top of passive recovery.
  // Unlike heal_fraction, the attack still deals its damage.
  double hp_recover_pct = 0.0;
  // The bigger attack that replaces every empowered_every'th attack, and how
  // often. Shared rather than owned, since AttackOption is copied freely and
  // the form never changes.
  std::shared_ptr<const AttackOption> empowered;
  int empowered_every = 0;
  // Whether the count is per enemy rather than per attack. If set, nothing is
  // replaced: each attack marks the mob, and the empowered form lands on top of
  // the normal hit for whichever mobs reach the count.
  bool brands_enemies = false;
  // A second attack this one triggers, with its own cooldown_seconds. Shared
  // for the same reason as `empowered`.
  std::shared_ptr<const AttackOption> side;
  // An extra line of this attack that scales with the crowd the character hits:
  // `lines_per_extra_enemy` per enemy past the first, up to `max_extra_lines`.
  // Extra lines rather than a multiplier, since each rolls its own mastery and
  // crit.
  std::shared_ptr<const AttackOption> extra_line;
  int lines_per_extra_enemy = 0;
  int max_extra_lines = 0;
  // Chances to deal extra damage to one enemy. Removed from anything on its own
  // timer, since GMS only rolls them for the character's own attacks.
  std::vector<ProcRoll> procs;
  // How this attack uses Freeze Stacks: ice attacks add `freeze_build`,
  // lightning attacks spend one per line and gain `freeze_fd_per_stack` final
  // damage for each stack held beforehand.
  int freeze_build = 0;
  // Stacks added instead when it hits exactly one enemy; 0 keeps freeze_build.
  int freeze_build_alone = 0;
  bool freeze_spends = false;
  // Lines dealt per stack spent; 1 for most lightning attacks.
  int freeze_lines_per_spend = 1;
  double freeze_fd_per_stack = 0.0;
  // Crit damage added per held stack through Freezing Crush.
  double freeze_crit_gain = 0.0;
  // Magic attack per stack from Glacial Fury. Only ice attacks get it.
  double freeze_matt_gain = 0.0;
  // Seconds this attack freezes the enemies it hits; 0 if it doesn't freeze
  // (including Frozen Orb). Summons can freeze too: Elquines freezes what it
  // hits.
  double freeze_seconds = 0.0;
  // The stun this attack applies: its duration, and the final damage other
  // attacks get against stunned enemies. See Skill.stun.
  double stun_seconds = 0.0;
  double stun_lift_pct = 0.0;
  // Chance the stun applies to each enemy hit.
  double stun_chance = 1.0;
  // Whether this attack gets the stun bonus from another skill's stun: it has
  // the tag that skill boosts, and isn't that skill.
  bool collects_stun_lift = false;
  // The mark this attack applies, and the bonus for the line that consumes it.
  // See Skill.mark.
  double mark_seconds = 0.0;
  double mark_lift_pct = 0.0;
  // Whether this attack can consume a mark: it has the tag the marking skill
  // names.
  bool collects_mark_lift = false;
  // Seconds taken off this attack's next cooldown for each hit that found no
  // target.
  double cooldown_refund_seconds = 0.0;
  // The wound this attack applies to the healthiest enemy hit: stacks per hit,
  // maximum stacks, and duration. See Wound.
  int wound_stacks = 0;
  int wound_max_stacks = 0;
  double wound_seconds = 0.0;
  // The heavier form used instead while a wound is at max stacks. Shared for
  // the same reason as `empowered`.
  std::shared_ptr<const AttackOption> wound_form;
  // Shatter: ignore defense added per held stack, per mob type. Per type
  // because it depends on each mob's defense, and is worthless where defense is
  // already fully ignored.
  std::vector<double> freeze_ied_gain;
  // Each line has `scar_chance` to scar the mob for `scar_seconds`, and lines
  // on a scarred mob gain `scar_fd`. Only the character's own attacks can scar
  // (summons and Final Attacks can't), but any damage gets the final damage.
  double scar_chance = 0.0;
  double scar_seconds = 0.0;
  double scar_fd = 0.0;
  // Bonuses for the enemy's current status. fd_when_afflicted applies in full
  // to a monster with any status the fight tracks, with no extra for a second.
  // The rest is final damage per burn on the group, up to `dot_count_cap`.
  double fd_when_afflicted = 0.0;
  double fd_per_dot = 0.0;
  int dot_count_cap = 0;
  // Which buff must be active for this to fire, or -1 if it runs on its own.
  // For auras, which only tick while raised. Only for attacks on their own
  // timer; normal attacks are chosen, not fired.
  int needs_buff = -1;
  // Which form of that buff must be active, or -1 for single-form buffs. Both
  // of Burning Soul Blade's swords are listed and only the active one fires.
  // Only used when needs_buff is set.
  int needs_buff_stance = -1;
  // Reverses `needs_buff`: this fires only while that buff is down, like
  // Inhuman Speed's afterimage.
  bool silent_while_buff = false;
  // A buff that dismisses this summon while active, or -1. The opposite of the
  // fields above: another skill's buff stops this one. See
  // Buff::silences_skill_name.
  int silenced_by_buff = -1;
  // Hits per tick, each dealing full damage.
  int strikes_per_pulse = 1;
  // Hits per attack for attacks whose hits are spaced out in time rather than
  // landing together. Each is resolved separately, so dead mobs are cleared
  // between them. See Skill.cast_interval_ms.
  int strikes_in_sequence = 1;
  // Seconds between those hits; 0 if they land together.
  double cast_interval_seconds = 0.0;
  // Ticks per raise of the required buff; it then stops until the buff is
  // raised again. See BuffPulse.max_pulses.
  int max_pulses = 0;
  // Stronger forms this timer steps through as it repeats: the first is used on
  // its second firing, and the last is used from then on. Shared for the same
  // reason as `empowered`. See BuffPulse.skill_pct_per_repeat.
  std::vector<std::shared_ptr<const AttackOption>> repeats;
  // One more hit at the last form when `max_pulses` runs out.
  bool final_repeat_strike = false;
  // The finishing strike this timer ends with, with its own shape; its
  // `strikes_per_pulse` is how many land together. See BuffPulse.
  std::shared_ptr<const AttackOption> final_strike;
  // Hold details if this is a held attack. damage_per_hit above is a full hold,
  // so code comparing attacks assumes the hold runs to the end.
  ChannelHold channel;
};

// One attack's totals over a run, parallel to the list it came from. The last
// two are parts of `damage`, not additions to it: Final Attack and burn damage
// are already included.
struct AttackTally {
  double damage = 0.0;
  int swings = 0;
  double final_attack_damage = 0.0;
  double burn_damage = 0.0;
};

// Seconds a hold of `pulses` takes, at least min_seconds.
double HoldSeconds(const ChannelHold& hold, int pulses);

// The form a timer uses on its `pulses`'th firing: itself first, then one step
// up per repeat, staying at the last.
const AttackOption& RepeatForm(const AttackOption& attack, int pulses);

// Everything the character can attack with under one buff combination. Same
// attacks in the same order in every combination (only damage differs), so a
// saved index stays valid as buffs change.
struct AttackSet {
  std::vector<AttackOption> attacks;
  std::vector<AttackOption> auto_attacks;
  std::vector<AttackOption> triggered_attacks;
  // Max Freeze Stacks under these buffs: Glacial Fury raises it while active.
  int freeze_cap = 0;
};

// What's needed to build the attack set for a buff combination, kept so each
// set is built on first use rather than all up front. Not owned, like
// CombatType::mob.
struct BuffedSetSource {
  const GameState* state = nullptr;
  const EquipPrototype* weapon = nullptr;
  // The character's own buffs in CombatParams::buffs order, so bit i of a mask
  // is skill i.
  std::vector<const Skill*> buff_skills;
  // Party buffs, using the bits after the character's own: bit
  // buff_skills.size() + j is ally_buffs[j]. Sharing one mask means an ally's
  // buff changes an attack exactly like the character's own. Stored as BuffUps
  // because each carries its caster's INT and the party size.
  std::vector<BuffUp> ally_buffs;
  double speed_factor = 1.0;
  Activity preset = Activity::kFarming;
  // Whether to halve reach when building, as boss fights do for every list.
  bool halve_reach = false;
};

// One form a buff can be raised in; the fight picks one at each cast. See
// Buff.stance.
struct StanceOption {
  double duration_seconds = 0.0;
  // Its pulse timer, and the pulse's index in AttackSet::auto_attacks. Stored
  // as an index because a table is built per buff combination and the index is
  // the same in all of them.
  double pulse_interval_seconds = 0.0;
  int pulse_attack = -1;
};

// A timed buff the character raises, with its own cooldown. Its bonuses aren't
// here: they're built into the buffed attack sets, since effects like ignore
// defense can't be applied to an already-computed damage number.
struct BuffOption {
  std::string name;
  // All game-scaled, like every other duration here.
  double duration_seconds = 0.0;
  double cooldown_seconds = 0.0;
  // Seconds each landed attack takes off this buff's cooldown.
  double cooldown_reduction_seconds = 0.0;
  // Fraction of incoming damage blocked while active. Multiplies with the
  // character's other reductions, like all reductions.
  double damage_taken_pct = 0.0;
  // Fraction of max HP healed on cast (1.00 means full).
  double heal_fraction = 0.0;
  // Hits it blocks outright, after which it ends regardless of remaining time.
  // 0 if it isn't a shield. See Shield.
  int shield_hits = 0;
  // Damage reduction against hits the shield can't block (boss hits), used
  // instead of blocking. Only used when shield_hits is set.
  double boss_damage_taken_pct = 0.0;
  // Cast time, taken from the attack being charged. Not affected by attack
  // speed: boosters speed up attacks, not casting.
  double cast_seconds = 0.0;
  // Lines to land before this activates, instead of a cooldown.
  int charge_lines = 0;
  // Chance each landed attack raises this, instead of a timer: 1.0 for buffs
  // raised by every qualifying attack. 0 for buffs on a cooldown or applied by
  // a named attack. See Buff::raise_chance.
  double raise_chance = 0.0;
  // Whether the attack must hit an enemy with a status. Only used when
  // raise_chance is set.
  bool needs_afflicted_target = false;
  // The attack this buff loads, or -1. Its charges refill each time the buff is
  // raised and disappear when it ends. See AttackOption::charges.
  int magazine_attack = -1;
  // The attack that applies this buff, or -1 if it's raised on its own
  // cooldown. A buff attached to an attack can't be separated from it, so
  // raising it costs an attack. Always -1 for party buffs, which an ally casts
  // outside this fight.
  int laid_by_attack = -1;
  // Whether the attack applying it already benefits from it: false for Darkness
  // Aura, true for buffs GMS grants "upon use".
  bool raised_on_cast = false;
  // Whether only the wound form of that attack raises it, as GMS grants
  // Trickblade's invulnerability for the slashes but not the spread.
  bool needs_wound_form = false;
  // Seconds added per burn on the group, up to dot_count_cap, read when raised.
  double duration_seconds_per_dot = 0.0;
  int dot_count_cap = 0;
  // Seconds of each duty_interval_seconds that the buff's bonuses apply. The
  // buff itself stays up (its pulse, shield and charges keep going); only the
  // bonuses switch on and off. See Buff::duty_seconds.
  double duty_seconds = 0.0;
  double duty_interval_seconds = 0.0;
  // Forms it can be raised in, chosen at each cast. If set, duration_seconds is
  // the longest of them, and the actual duration is the chosen form's.
  std::vector<StanceOption> stances;
};

// A snapshot of the current encounter's combat parameters.
struct CombatParams {
  bool active = false;  // false when not farming (no map, weapon or mobs)
  // What these params describe: a map name, or one phase of a boss fight. The
  // fight watches it, so a new phase rebuilds the mobs just like moving to
  // another map.
  std::string encounter;
  // Time between respawns; 0 if the encounter never refills.
  double respawn_seconds = 0.0;
  // Time between mob hits on the player; 0 if nothing hits back.
  double hit_seconds = 0.0;
  int max_player_hp = 0;  // max HP, which a full heal restores
  // Watched to refill HP on level up. max_player_hp can't be used for that,
  // since skill points, scrolls or gear changes also raise it.
  int player_level = 0;
  // Fraction of max HP restored at every respawn, whether or not the map was
  // cleared. This is what lets a player survive a map by outlasting it.
  double beat_heal_fraction = 0.0;
  // Fraction of each hit reflected back to the mob that dealt it.
  double damage_reflect_pct = 0.0;
  // Fraction of max HP restored per landed attack. Costs nothing, so it stacks
  // with the respawn heal, but heals nothing on an empty map.
  double hp_recover_pct = 0.0;
  // Extra EXP per kill, as a fraction of the mob's EXP. Used by
  // AwardCombatRewards, not the fight.
  double exp_pct = 0.0;
  // Bonus to meso per kill, already capped. See MesoBonus.
  double meso_pct = 0.0;
  // Multiplier on meso after the bonus, uncapped.
  double meso_final_mult = 1.0;
  // Bonus to drop chance. Used by AwardCombatRewards; it raises the chance of a
  // drop, not its size.
  double item_drop_pct = 0.0;
  // Drop rate for boss drops: the Drop preset's rate, not the fight's, since
  // there's no chance to swap gear before they drop. Only used in boss fights;
  // map drops use the fight's rate. See kDropPreset.
  double drop_roll_item_drop_pct = 0.0;
  // Whether kills here can drop V Points: only Arcane River and Grandis maps,
  // which are the maps with a force requirement.
  bool pays_v_points = false;
  // The character's HP regen effects, each on its own timer whether or not they
  // are attacking.
  std::vector<RegenPulse> regen_pulses;
  // Seconds between revives: a hit that would kill the player fully heals them
  // instead, and the cooldown restarts.
  double revive_cooldown_seconds = 0.0;
  // The automatic heal when the player is nearly dead. Its window and cooldown
  // are game-scaled like other durations; the heal rate is per GMS second, so a
  // stretched window heals the same total.
  EmergencyHeal emergency_heal;
  // Number of distinct burns the character can apply, and so the burn slots
  // each monster needs.
  int dot_count = 0;
  // Max Freeze Stacks at once; 0 disables the mechanic.
  int freeze_cap = 0;
  // Whether attacks target the healthiest mob rather than the front of the
  // queue. On for bosses, whose parts have different HP and never respawn, so a
  // single-target attack shouldn't be wasted on parts that die anyway. Off on
  // maps, which refill every respawn.
  bool focus_healthiest = false;
  // Whether to record every damage line, for callers that draw damage numbers.
  // The boss screen does; maps and sims don't.
  bool record_damage_lines = false;
  // Whether the fight is measured rather than played: monsters never die, every
  // roll is average, and it runs indefinitely. See MeasureFight.
  bool measuring = false;
  std::vector<CombatType> types;  // in map order
  // Every available attack, basic attack first. Never empty while active.
  std::vector<AttackOption> attacks;
  // Attacks on their own timer that run alongside normal attacks. The fight
  // never chooses them; they just happen.
  std::vector<AttackOption> auto_attacks;
  // The same, but triggered by landed attacks or kills rather than time. Their
  // casts don't count toward any attack count (only the player's own attacks
  // do), but their kills count.
  std::vector<AttackOption> triggered_attacks;
  // Rough expected damage per second from the lists above with no buffs. Only
  // used until the fight has run long enough to measure its own rate. See
  // CombatSim::SecondsLeft.
  double reference_dps = 0.0;
  // Timed buffs the character can raise. The fight runs their timers and looks
  // up the matching attacks.
  std::vector<BuffOption> buffs;
  // One entry per buff combination the fight has actually reached, keyed by the
  // mask of active buffs. The lists above are the set with no buffs.
  //
  // Entries are built on first use from buffed_source, and most never are:
  // combinations double with each buff, but the ones a fight reaches don't. So
  // this is a map, not a full table. It's mutable for that reason, which makes
  // the readers below unsafe to call from two threads on one CombatParams; sims
  // give each worker its own copy.
  mutable std::map<int, AttackSet> buffed;
  // What those entries are built from. Empty for hand-built params, which fill
  // every entry up front.
  BuffedSetSource buffed_source;
  // The attack set for `mask`, built on first use. Null if out of range.
  const AttackSet* Window(int mask) const;
  // The three lists with `mask`'s buffs active. Out of range returns the
  // unbuffed lists, so a fight one step behind attacks unbuffed instead of
  // reading past the end.
  const std::vector<AttackOption>& Attacks(int mask) const;
  const std::vector<AttackOption>& AutoAttacks(int mask) const;
  const std::vector<AttackOption>& TriggeredAttacks(int mask) const;
  // Max Freeze Stacks with `mask`'s buffs active.
  int FreezeCap(int mask) const;
};

// The character's equipped weapon, or null. A fight needs one, so screens that
// offer a fight check this first.
const EquipPrototype* EquippedWeapon(const GameState& state,
                                     Activity activity = Activity::kFarming);

// Reads `state`'s map and character into a CombatParams. `active` is false if
// there's no map, weapon or loaded mobs.
CombatParams ComputeCombatParams(const GameState& state);

// The same for one phase of a boss difficulty. Nothing respawns or hits back,
// and the fight runs in real time rather than stretched by game speed, since
// the player watches bosses rather than idling.
CombatParams ComputeBossParams(const GameState& state,
                               const std::string& boss_key,
                               const BossDifficulty& difficulty, int phase);

// The CombatParams::encounter value for one boss phase. Unique per phase, which
// makes a new phase rebuild the mobs, and distinct from map names.
std::string BossEncounterKey(const std::string& boss,
                             const std::string& difficulty, int phase);

}  // namespace ms

#endif  // MS_SRC_COMBAT_ENCOUNTER_H_
