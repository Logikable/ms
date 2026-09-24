/* What the player is fighting: the map's mobs, how hard each is hit, and how
 * fast the swings and respawns come.
 *
 * ComputeCombatParams() reads that off a GameState once into a plain
 * CombatParams, and the fight steps from those alone (see fight.h), so it
 * cannot quietly disagree with the encounter it is playing out. Every
 * duration is in game-scaled seconds -- real ones stretched by the
 * character's GameSpeedFactor.
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

// One targetable mob type. `mob` is the source of truth for name, HP, EXP and
// drops; GameState owns it and it outlives the step.
struct CombatType {
  const Mob* mob = nullptr;
  int simultaneous = 0;  // how many spawn at once: SpawnCount
  // Where each stands in a boss arena; empty on a map. Carried rather than
  // read back off the phase: a spawn whose mob the catalog lacks never
  // becomes a type, so an index would slide every later part one cell over.
  std::vector<ArenaSpot> spots;
  // How one wanders its arena. Unset for everything that stands still.
  ArenaWalk walk;
  // Expected damage one hit does to the player, already through their DEF.
  // Per type, every member of one hitting alike.
  double damage_to_player = 0.0;
  // The same once SCARRED, which weakens its attack. Equal to the line above
  // for every character who scars nothing.
  double damage_to_player_scarred = 0.0;
};

// One block of lines inside a swing. A swing is one of these plus one per
// extra hit the skill lands; the parts differ in line count and crit rate, so
// each rolls on its own.
struct HitGroup {
  std::vector<double> damage;  // per target type, parallel to CombatParams
  SwingRolls rolls;
};

// A swing that is HELD. The damage is in the swing's own groups -- the first
// is one pulse, the rest the strike it ends on -- since a hold is that pulse
// over and over; a hold that GROWS carries its second pulse here instead.
// `pulses` is 0 for every attack that is simply swung. See Channel.
struct ChannelHold {
  int pulses = 0;
  // The fewest a cast is committed to: what fits inside min_seconds.
  int min_pulses = 0;
  double pulse_seconds = 0.0;
  double finish_seconds = 0.0;
  double min_seconds = 0.0;
  // Share of the pool ONE pulse puts back, so letting go early recovers
  // less. The closing strike pays its own, in AttackOption::hp_recover_pct.
  double hp_recover_pct = 0.0;
  // Share of every hit the player takes that the hold cancels while it runs.
  double damage_taken_pct = 0.0;
  // Pulses beaten at the opening strength before it grows. 0 for a hold that
  // beats the same throughout.
  int small_pulses = 0;
  // One pulse once the hold has grown; empty for a hold that never does.
  HitGroup grown;
  // A hold bought out of a bank rather than a cooldown: one charge every
  // `charge_seconds`, `max_charges` at once, `pulses_per_charge` bought by
  // each. 0 for a hold paced by a cooldown. See Channel.
  double charge_seconds = 0.0;
  int max_charges = 0;
  int pulses_per_charge = 0;
};

// One burn a swing leaves: a tick's worth per type, the clock it burns on and
// how it takes hold. One per source that marks what the swing hits.
struct DotApplication {
  std::vector<double> damage;  // per target type, one tick's worth
  SwingRolls rolls;
  double interval_seconds = 0.0;
  double duration_seconds = 0.0;
  // Chance it takes hold on each enemy reached; 1 for a burn simply left.
  double chance = 1.0;
  // Helpings one monster can carry at once, each ticking for the whole damage.
  int max_stacks = 1;
  // An index into the slots every mob carries, so two burns do not overwrite
  // each other. Assigned per SOURCE, and numbered alike in every buffed set,
  // so a held slot means the same thing however the buffs come and go.
  int slot = -1;
  // Whether the CHARACTER carries it rather than the attack stating it: the
  // poison on a rogue's claw rides their own swings and nothing else.
  bool carried = false;
  // The skill a damage breakdown files its ticks under: the one carrying the
  // poison, or the swing that left the burn.
  std::string credit;
};

// One Final Attack: a chance, rolled per enemy the swing reached, of one more
// hit on that enemy. `count` is above 1 only for a source riding the LINES --
// the meso a Chief Bandit knocks loose is rolled once per line.
struct FinalAttackRoll {
  double chance = 0.0;
  int count = 1;
  std::vector<double> damage;  // per target type, one hit's worth
  SwingRolls rolls;            // how that hit itself varies
  // Whether it survives on an attack the character does not swing. See
  // Skill.follows_own_clock.
  bool follows_own_clock = false;
  // Enemies reached where it rolls once for the whole swing; 0 for one that
  // follows the swing onto each enemy.
  int max_enemies = 0;
  // The skill a damage breakdown files its hits under.
  std::string credit;
};

// One chance a swing has to land harder on one enemy, and what firing it
// hands back. Rolled once for the whole swing -- see the Proc message.
struct ProcRoll {
  double chance = 0.0;
  double damage_pct = 0.0;  // share added to what that one enemy takes
  double hp_recover_pct = 0.0;
};

// One thing the character could spend a swing on: the bare poke, a learned
// attack skill, or a cast that does something else with the swing entirely.
// Which one is best depends on how many mobs are actually in front of the
// player, so the choice is made per swing by the fight rather than fixed here
// -- a wide skill that does less per target wins on a crowd and loses on the
// last mob standing.
struct AttackOption {
  std::string name = "Attack";  // shown on the charge bar
  // The skill a damage breakdown files this under. The name, except on a form
  // taking another skill's place and a load: those belong to the skill they
  // stand in for, and to the one that loaded them.
  std::string credit = "Attack";
  int max_enemies = 1;  // front-of-queue mobs one swing reaches
  // Strikes one swing of it lands on one enemy. Read by the things that count
  // hits rather than swings -- a buff charged by landing them, and the Freeze
  // Stacks an elemental swing leaves or spends.
  int lines = 1;
  // What the swing gains per enemy already gone through, compounding: the
  // k'th reached takes (1 + this)^k. 0 for a swing that hits alike.
  double pierce_gain_pct = 0.0;
  // Expected damage per target, parallel to CombatParams::types.
  std::vector<double> damage_per_hit;
  // The same swing in the blocks that roll, summing to damage_per_hit. Empty
  // lands the average itself, which an attack built by hand wants.
  std::vector<HitGroup> groups;
  // Seconds one swing takes. Per attack, the delay belonging to the skill: a
  // slower animation is what a harder-hitting skill pays. 0 for an attack on
  // its own clock.
  double swing_seconds = 0.0;
  // Seconds between casts, for an attack on its own clock.
  double interval_seconds = 0.0;
  // Landed swings between casts, for an attack clocked by attacking. An
  // attack carries one of the three clocks, never two.
  int attacks_per_cast = 0;
  // Enemies defeated between casts, for an attack clocked by the dying.
  int kills_per_cast = 0;
  // What one landed swing counts toward the field above. 1 ordinarily, less
  // for a swing landing several times a second.
  double count_weight = 1.0;
  // Seconds it cannot be swung for after it lands.
  double cooldown_seconds = 0.0;
  // Swings a raising of the loading buff pays for, spent one per landing. 0
  // for an attack no buff loads. See Magazine.
  int charges = 0;
  // The swing whose press spends this one, for a load that is no button of
  // its own: Poison Nova's clouds go off on Mist Eruption. An option carrying
  // an index here is never CHOSEN; -1 is every attack the fight may pick.
  int spent_by_attack = -1;
  // The load this swing sets off while a charge stands, and where that
  // charge is counted. Shared for the reason `empowered` is.
  std::shared_ptr<const AttackOption> loaded;
  int loaded_attack = -1;
  // Charges one press spends, and strikes landed for them; a press finding
  // fewer left spends what is there. 1 for a load spent one at a time.
  int charges_per_swing = 1;
  // The clock a charge comes back on with no buff behind it, and the bank it
  // fills to. It runs only while the bank is under that, so a load in hand is
  // never topped up.
  double recharge_seconds = 0.0;
  int recharge_max = 0;
  // The opening hit, per target type. It lands on the HEALTHIEST mobs the
  // swing reached, a hit this big being worth least where it overkills.
  std::vector<double> lead_damage;
  // How many of them it lands on. 1 for the Rogue's shape, which strikes one
  // and spreads.
  int lead_enemies = 1;
  // Its own, since it lands on its own line count rather than the swing's.
  SwingRolls lead_rolls;
  // Strikes scattered over the enemies reached, spreading before they double
  // up. 0 for a swing landing on each once. See Skill::scatter.
  int scatter_hits = 0;
  // What a repeat strike keeps on an enemy the same cast already reached
  // (0.45 == GMS's "Final Damage -55%").
  double scatter_repeat_kept = 1.0;
  // Strikes thrown on top of scatter_hits per burn stack alight, capped at
  // scatter_max_hits. 0 for a swing whose count is fixed.
  double scatter_hits_per_dot = 0.0;
  int scatter_max_hits = 0;
  // Strikes any one enemy may take; what does not fit is LOST. 0 lets them
  // pile as deep as the count allows. See Scatter.
  int scatter_max_hits_per_enemy = 0;
  // Final Attack damage per type, landing on every mob the swing reached and
  // rolled for each. A skill on its own clock keeps only the sources that say
  // they follow one.
  std::vector<double> final_attack_damage;
  // The same per source, as what actually rolls. Empty lands the average.
  std::vector<FinalAttackRoll> final_attack_rolls;
  // The pair above for the sources rolling ONCE for the whole swing, onto a
  // crowd of their own. Kept apart because the damage is added in a different
  // place: once per swing here, once per enemy there.
  std::vector<double> per_swing_final_attack_damage;
  std::vector<FinalAttackRoll> per_swing_final_attack_rolls;
  // Enemies that bank lands on: 1 for Blizzard, ten for Split Shot behind a
  // swing that reached one. Never 0 where the bank holds anything.
  int per_swing_final_attack_enemies = 1;
  // The half of the swing reaching its own crowd rather than the swing's:
  // its damage per type, what rolls it, and how many it finds. See SwingHit.
  std::vector<double> wide_hit_damage;
  std::vector<HitGroup> wide_hit_groups;
  int wide_hit_enemies = 0;
  // The burns this swing leaves on the enemies it reaches.
  std::vector<DotApplication> dots;
  // Share of the pool this option puts back INSTEAD of dealing damage. One
  // carrying it deals none, and the fight picks it by need rather than by
  // rate -- see CombatSim::HealToCast.
  double heal_fraction = 0.0;
  // Share of the pool a landed strike puts back, on top of what the
  // character's passives recover. Costs no swing, unlike heal_fraction: the
  // damage still goes out.
  double hp_recover_pct = 0.0;
  // The bigger swing taking the PLACE of every empowered_every'th swing, and
  // how often that is. Shared rather than owned: an AttackOption is copied
  // freely and the form never changes.
  std::shared_ptr<const AttackOption> empowered;
  int empowered_every = 0;
  // Whether the count runs per ENEMY rather than per swing. Set, nothing is
  // replaced: the swing marks each mob, and the form lands on top of the
  // ordinary strike for whichever came due.
  bool brands_enemies = false;
  // The second attack this swing sets off, on the wait its own
  // cooldown_seconds states. Shared for the reason `empowered` is.
  std::shared_ptr<const AttackOption> side;
  // One more line of this same strike, for a rain that grows with the crowd
  // the character's swing reaches: `lines_per_extra_enemy` per enemy past the
  // first, capped at `max_extra_lines`. A LINE rather than a multiplier
  // because each rolls its own mastery and crit.
  std::shared_ptr<const AttackOption> extra_line;
  int lines_per_extra_enemy = 0;
  int max_extra_lines = 0;
  // Chances to land harder on one enemy reached. Stripped from anything on a
  // clock of its own: what GMS rolls is the character attacking.
  std::vector<ProcRoll> procs;
  // What this swing does with the Freeze Stacks: ice leaves `freeze_build`,
  // lightning spends one per line and takes `freeze_fd_per_stack` of final
  // damage for each it went in holding.
  int freeze_build = 0;
  // Stacks left instead when it reaches exactly ONE enemy; 0 keeps the count
  // above however few it finds.
  int freeze_build_alone = 0;
  bool freeze_spends = false;
  // Lines landed per stack spent; 1 for most lightning swings.
  int freeze_lines_per_spend = 1;
  double freeze_fd_per_stack = 0.0;
  // What one HELD stack adds through Freezing Crush's critical damage.
  // Linear in the stacks: what a stack adds is crit damage, not damage.
  double freeze_crit_gain = 0.0;
  // The magic attack Glacial Fury pays per stack, which only ICE collects.
  double freeze_matt_gain = 0.0;
  // Seconds this swing leaves the enemies it reached frozen; 0 for a swing
  // that freezes nothing, Frozen Orb included. A summon carries it like any
  // other swing -- Elquines freezes what it touches.
  double freeze_seconds = 0.0;
  // The stun this swing leaves: the seconds it stands and the final damage
  // it hands the swings that collect. See Skill.stun.
  double stun_seconds = 0.0;
  double stun_lift_pct = 0.0;
  // Odds the stun takes hold on each enemy reached.
  double stun_chance = 1.0;
  // Whether THIS swing collects a stun somebody else left: it carries the
  // tag the stunning skill lifts, and is not that skill.
  bool collects_stun_lift = false;
  // The mark this swing leaves, and what the line spending it takes. See
  // Skill.mark.
  double mark_seconds = 0.0;
  double mark_lift_pct = 0.0;
  // Whether THIS swing can spend a mark: it carries the tag the marking
  // skill names.
  bool collects_mark_lift = false;
  // Seconds off this swing's next cast per strike that found nothing.
  double cooldown_refund_seconds = 0.0;
  // The wound this swing leaves on the healthiest enemy reached: how deep,
  // how deep one can go, and how long it stands. See Wound.
  int wound_stacks = 0;
  int wound_max_stacks = 0;
  double wound_seconds = 0.0;
  // The heavier form landed INSTEAD while a wound stands at full depth.
  // Shared for the reason `empowered` is.
  std::shared_ptr<const AttackOption> wound_form;
  // Shatter's: what one held stack adds against each mob type. Per type
  // because the defence ignored is that mob's own, and worth nothing where it
  // is already cancelled.
  std::vector<double> freeze_ied_gain;
  // Each line has `scar_chance` of scarring the mob for `scar_seconds`, and
  // a line landing on a scarred mob takes `scar_fd`. The chance and the
  // seconds belong to the character's own swings alone -- a summon and a
  // Final Attack scar nothing -- where the final damage rides anything.
  double scar_chance = 0.0;
  double scar_seconds = 0.0;
  double scar_fd = 0.0;
  // What the condition the enemy is ALREADY in adds. The first is taken
  // whole on a monster under any status the fight keeps, with nothing extra
  // for a second; the rest are final damage per burn alight on the group, up
  // to `dot_count_cap`.
  double fd_when_afflicted = 0.0;
  double fd_per_dot = 0.0;
  int dot_count_cap = 0;
  // Which buff must stand for this to fire, or -1 for a clock running on its
  // own: what ticks is the aura, so it ticks only where one was raised.
  // Off-clock attacks only -- a swing is chosen rather than fired.
  int needs_buff = -1;
  // Which FORM of that buff must stand, or -1 for a buff with one form: both
  // Burning Soul Blade's swords sit in the list and only the raised one
  // fires. Read only where needs_buff is set.
  int needs_buff_stance = -1;
  // Whether `needs_buff` reads the other way round: this fires only while
  // that buff is DOWN, as Inhuman Speed's afterimage does.
  bool silent_while_buff = false;
  // A buff that puts THIS summon out while it stands, or -1. The opposite
  // sense to the pair above: another skill's buff dismisses this summon,
  // rather than this one waiting on its own. See Buff::silences_skill_name.
  int silenced_by_buff = -1;
  // Strikes one due tick fires, each landing in full on its own.
  int strikes_per_pulse = 1;
  // The same for a SWING whose strikes are told apart in time rather than
  // folded into one landing. Each is struck on its own, so the dead are
  // cleared between them. See Skill.cast_interval_ms.
  int strikes_in_sequence = 1;
  // Seconds between those strikes; 0 says they fall together.
  double cast_interval_seconds = 0.0;
  // Ticks one raising of the gating buff is worth, after which this falls
  // silent until the buff comes round. See BuffPulse.max_pulses.
  int max_pulses = 0;
  // The stronger forms this clock walks through as it repeats: the first is
  // what its second firing lands, the last is where it pins. Shared for the
  // reason `empowered` is. See BuffPulse.skill_pct_per_repeat.
  std::vector<std::shared_ptr<const AttackOption>> repeats;
  // One more strike at the last of those forms as `max_pulses` runs out.
  bool final_repeat_strike = false;
  // The strike this clock goes out on, of a shape all its own -- its
  // `strikes_per_pulse` is how many land together. See BuffPulse.
  std::shared_ptr<const AttackOption> final_strike;
  // The hold this swing is. damage_per_hit above is a FULL hold, so an
  // attack weighed without asking is weighed at holding it to the end.
  ChannelHold channel;
};

// What one attack came to over a run, parallel to the list it was built from.
// One tally rather than a vector apiece: the four move together, and the last
// two are HALVES of the first rather than additions to it -- a Final Attack
// and a burn are already inside `damage`.
struct AttackTally {
  double damage = 0.0;
  int swings = 0;
  double final_attack_damage = 0.0;
  double burn_damage = 0.0;
};

// Seconds a hold of `pulses` takes, never shorter than min_seconds.
double HoldSeconds(const ChannelHold& hold, int pulses);

// The form a clock lands on its `pulses`'th firing: itself first, then one
// step up the ramp per repeat, pinned at the last.
const AttackOption& RepeatForm(const AttackOption& attack, int pulses);

// Everything the character can attack with under one set of buffs. The same
// attacks in the same order in every set -- only the damage differs -- so a
// held index stays good however the buffs come and go.
struct AttackSet {
  std::vector<AttackOption> attacks;
  std::vector<AttackOption> auto_attacks;
  std::vector<AttackOption> triggered_attacks;
  // Freeze Stacks holdable under these buffs: Glacial Fury deepens the pile
  // only while it stands.
  int freeze_cap = 0;
};

// What one combination of buffs needs to have its attack set built, kept so a
// window is built the first time the fight asks rather than all up front.
// Borrowed, not owned, as CombatType::mob is.
struct BuffedSetSource {
  const GameState* state = nullptr;
  const EquipPrototype* weapon = nullptr;
  // The character's own buffs, in CombatParams::buffs order, so bit i of a
  // mask is skill i.
  std::vector<const Skill*> buff_skills;
  // The party's, taking the bits above those: bit buff_skills.size() + j is
  // ally_buffs[j]. One mask with the character's own, an ally's blessing
  // changing a swing exactly as their own buff does. Held as BuffUps because
  // each carries its caster's INT and the party's size, worked out once.
  std::vector<BuffUp> ally_buffs;
  double speed_factor = 1.0;
  Activity preset = Activity::kFarming;
  // Whether a window's reach is halved on the way out, as a boss fight does
  // to every list.
  bool halve_reach = false;
};

// One form a buff can be raised in, priced against the fight at each cast.
// See Buff.stance.
struct StanceOption {
  double duration_seconds = 0.0;
  // Its pulse's clock, and where that pulse sits in AttackSet::auto_attacks.
  // Read by index rather than copied: a table is built per buff window and
  // the option keeps its index in every one.
  double pulse_interval_seconds = 0.0;
  int pulse_attack = -1;
};

// A buff the character puts up for a while, on a wait of its own. What it
// GRANTS is not here: it is folded into the buffed attack sets, a lever like
// ignored defence being unappliable to a damage number already worked out.
struct BuffOption {
  std::string name;
  // All game-scaled, like every other duration here.
  double duration_seconds = 0.0;
  double cooldown_seconds = 0.0;
  // Seconds a landed swing takes off the wait for the next cast.
  double cooldown_reduction_seconds = 0.0;
  // Share of every hit this buff cancels while it stands. Multiplies with
  // what the character already cancels, as every reduction does.
  double damage_taken_pct = 0.0;
  // Share of the pool the cast puts back at once (1.00 == all of it).
  double heal_fraction = 0.0;
  // Hits it cancels outright, after which it falls whatever is left of its
  // clock. 0 for a buff that is not a shell. See Shield.
  int shield_hits = 0;
  // Share off a hit the shell cannot block -- a boss's -- taken instead of
  // blocking it. Read only where shield_hits is set.
  double boss_damage_taken_pct = 0.0;
  // Seconds raising this costs, taken off the swing being charged. NOT
  // scaled by attack speed: a booster hurries a swing, not an arm-raise.
  double cast_seconds = 0.0;
  // Lines to land before this goes up, instead of a wait in seconds.
  int charge_lines = 0;
  // The chance one LANDED SWING raises this, instead of a clock coming round
  // -- 1.0 for one raised by every swing that meets its condition. 0 for a
  // buff raised on a wait or laid by a named attack, which is every other
  // one. See Buff::raise_chance.
  double raise_chance = 0.0;
  // Whether that swing has to land on an afflicted enemy. Read only where
  // raise_chance is set.
  bool needs_afflicted_target = false;
  // The swing this buff LOADS, or -1. Its charges are handed back whole each
  // raising and gone the moment it lapses. See AttackOption::charges.
  int magazine_attack = -1;
  // The swing that lays this buff, or -1 for one raised on its own wait: a
  // buff hanging off an ATTACK is inseparable from the swing delivering it,
  // so the fight spends a swing to put it up. Always -1 for a party buff,
  // which an ally's cast lays where this fight cannot see it.
  int laid_by_attack = -1;
  // Whether the swing laying it is already under it: false for Darkness
  // Aura, true for a buff GMS grants "upon use".
  bool raised_on_cast = false;
  // Whether only the WOUND FORM of that swing raises it, as GMS grants
  // Trickblade's invulnerability for the slashes and not the spread.
  bool needs_wound_form = false;
  // Seconds added to the window per burn alight on the group, up to
  // dot_count_cap, read at the raise rather than baked in.
  double duration_seconds_per_dot = 0.0;
  int dot_count_cap = 0;
  // Seconds this buff GRANTS of every duty_interval_seconds it stands. The
  // buff never flickers -- its pulse, shell and magazine run through the gap
  // -- only what it hands the character. See Buff::duty_seconds.
  double duty_seconds = 0.0;
  double duty_interval_seconds = 0.0;
  // The forms it can be raised in, chosen between at each cast. Where filled,
  // duration_seconds above is the LONGEST of them; what stands is the chosen
  // stance's own length.
  std::vector<StanceOption> stances;
};

// A snapshot of the current encounter's combat parameters.
struct CombatParams {
  bool active = false;  // false when not farming (no map/weapon/mobs)
  // What these params describe: a map's name, or one phase of a boss fight.
  // The fight watches it, so a phase turning over rebuilds the roster exactly
  // as walking to another map does.
  std::string encounter;
  // Time between full-roster respawn beats; 0 for an encounter that never
  // refills.
  double respawn_seconds = 0.0;
  // Time between mob hits on the player; 0 where nothing hits back.
  double hit_seconds = 0.0;
  int max_player_hp = 0;  // what a full heal fills the player back to
  // Watched for the level-up fill, which cannot be read off max_player_hp: a
  // skill point, a scroll or a swapped hat widens the pool too.
  int player_level = 0;
  // Share of the pool returned on every beat, cleared map or not. What lets
  // a map be survived by outlasting it.
  double beat_heal_fraction = 0.0;
  // Share of every hit that goes back into the mob that landed it.
  double damage_reflect_pct = 0.0;
  // Share of the pool a landed swing puts back. Costs no swing, so it stacks
  // with the beat heal, and pays nothing on an empty map.
  double hp_recover_pct = 0.0;
  // Extra EXP per kill, as a share of the mob's worth. Read by
  // AwardCombatRewards rather than by the fight.
  double exp_pct = 0.0;
  // Share added to the meso a kill yields, already capped -- see MesoBonus.
  double meso_pct = 0.0;
  // What multiplies that meso afterwards, uncapped.
  double meso_final_mult = 1.0;
  // Share added to how often a kill drops anything. Read by
  // AwardCombatRewards: it raises the CHANCE of a drop, not its size.
  double item_drop_pct = 0.0;
  // The rate a boss's drops roll at: the DROP preset's, not the fight's,
  // there being no moment to change gear before they fall. Unread outside a
  // boss fight, a map's drops being the fight's own. See kDropPreset.
  double drop_roll_item_drop_pct = 0.0;
  // Whether a kill here can pay a V Point. Arcane River monsters are the only
  // ones that do, so this is the map's Arcane Force requirement asked as a
  // yes or no.
  bool pays_v_points = false;
  // The fountains the character carries, each on its own clock whether or not
  // they are swinging.
  std::vector<RegenPulse> regen_pulses;
  // Seconds between revivals: the hit that would have killed the player
  // fills the pool instead, and the wait starts over.
  double revive_cooldown_seconds = 0.0;
  // The heal that fires on its own when the player is nearly dead. Its window
  // and its wait are game-scaled, like every other duration here; the share
  // is per GMS second, so a stretched window pours the same total.
  EmergencyHeal emergency_heal;
  // Distinct burns the character can leave, and so the slots a monster
  // needs.
  int dot_count = 0;
  // Freeze Stacks holdable at once; 0 switches the mechanism off.
  int freeze_cap = 0;
  // Whether a swing picks the healthiest of the roster rather than the front
  // of the queue. On for a boss, whose parts differ in HP and none of which
  // respawns: a narrow swing spends itself on what would outlast the fight.
  // Off on a map, refilled on the beat.
  bool focus_healthiest = false;
  // Whether to record every line landed, for a caller drawing the damage as
  // numbers. The boss screen asks; the map and the sims do not.
  bool record_damage_lines = false;
  // Whether the fight is MEASURED rather than played: the monsters never
  // fall, every roll lands its mean, and the horizon is infinite. See
  // MeasureFight.
  bool measuring = false;
  std::vector<CombatType> types;  // in map order
  // Every attack available, the bare poke first. Never empty while active.
  std::vector<AttackOption> attacks;
  // Attacks on their own clock beside whatever is being swung. Not
  // candidates for the swing: they simply also happen.
  std::vector<AttackOption> auto_attacks;
  // The same, clocked by swings landed or enemies defeated rather than by
  // seconds. A cast of one counts toward no swing count -- what is paid for
  // is the player's own attacking -- but what it kills does count.
  std::vector<AttackOption> triggered_attacks;
  // Expected damage a second, off the lists above with no buff standing. A
  // rough figure, used only until enough fight has run to measure a rate of
  // its own. See CombatSim::SecondsLeft.
  double reference_dps = 0.0;
  // The timed buffs this character can put up. The fight runs their clocks
  // and asks for the matching attacks.
  std::vector<BuffOption> buffs;
  // One entry per combination of those buffs the fight has actually stood in,
  // keyed by the mask of which are up -- the lists above are the set for
  // none.
  //
  // An entry is built the first time the fight asks, off buffed_source, and
  // most never are: the combinations DOUBLE per buff while the ones a fight
  // reaches do not, which is why this is a map and not a table of every
  // mask. Mutable for that reason, which makes the readers below unsafe on
  // one CombatParams from two threads. A sim gives each worker its own.
  mutable std::map<int, AttackSet> buffed;
  // What those slots are built from. Empty for a hand-built params, where
  // every slot is filled up front.
  BuffedSetSource buffed_source;
  // The window `mask` names, built on first ask. Null out of range.
  const AttackSet* Window(int mask) const;
  // The three lists as they stand with `mask`'s buffs up. Out of range reads
  // as none, so a fight a step behind what was learned swings unbuffed rather
  // than off the end.
  const std::vector<AttackOption>& Attacks(int mask) const;
  const std::vector<AttackOption>& AutoAttacks(int mask) const;
  const std::vector<AttackOption>& TriggeredAttacks(int mask) const;
  // The Freeze Stack cap with `mask`'s buffs up.
  int FreezeCap(int mask) const;
};

// The weapon the character is holding, or null. A fight needs one, so a
// screen offering a fight asks this first.
const EquipPrototype* EquippedWeapon(const GameState& state,
                                     Activity activity = Activity::kFarming);

// Reads `state`'s map and character into a CombatParams. active is false with
// no map, no equipped weapon or no loaded mobs.
CombatParams ComputeCombatParams(const GameState& state);

// The same for one phase of a boss difficulty. Nothing respawns or hits back,
// and the fight runs in real time rather than at the pacing band's stretch: a
// boss is watched, not left alone.
CombatParams ComputeBossParams(const GameState& state,
                               const std::string& boss_key,
                               const BossDifficulty& difficulty, int phase);

// What CombatParams::encounter holds for one boss phase. Distinct per phase,
// which is what rebuilds the roster when one turns over, and from map names.
std::string BossEncounterKey(const std::string& boss,
                             const std::string& difficulty, int phase);

}  // namespace ms

#endif  // MS_SRC_COMBAT_ENCOUNTER_H_
