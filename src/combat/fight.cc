#include "src/combat/fight.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// How low the player has to fall before they will spend a swing healing
// instead of attacking. The whole of the decision: a cast that cost a swing
// every time it was merely useful would never let the character attack, and
// one saved for the last sliver would come too late to matter.
constexpr double kHealBelowFraction = 0.25;

// Slack for the swing counter below, far smaller than any weight a swing
// carries. A weight of a seventh cannot be written exactly.
constexpr double kCountEpsilon = 1e-9;

// Whether there is a fight to advance at all. The bare poke is always the
// first attack, so its interval is the one to ask about: every character has
// it, whatever they have learned or are holding.
bool CanFight(const CombatParams& params) {
  return params.active && !params.types.empty() && !params.attacks.empty() &&
         params.attacks.front().swing_seconds > 0.0;
}

// What one swing of an attack gaining `gain` a step is worth per enemy, over
// `hit` of them: the escalation averaged, since the order is drawn fresh. A
// sixth of (1 + 1.15 + ... + 1.15^5) is 1.46, at Piercing Arrow's own numbers.
double PierceMean(double gain, int hit) {
  if (gain <= 0.0 || hit <= 1) {
    return 1.0;
  }
  return (std::pow(1.0 + gain, hit) - 1.0) / (gain * hit);
}

}  // namespace

void CombatSim::TopUp(const CombatParams& params) {
  std::vector<int> standing(params.types.size(), 0);
  for (const QueuedMob& mob : queue_) {
    ++standing[mob.type];
  }
  int first_new = static_cast<int>(queue_.size());
  for (int i = 0; i < static_cast<int>(params.types.size()); ++i) {
    for (int k = standing[i]; k < params.types[i].simultaneous; ++k) {
      QueuedMob arrival;
      arrival.type = i;
      arrival.hp = params.types[i].mob->max_hp();
      arrival.id = next_mob_id_++;
      queue_.push_back(std::move(arrival));
    }
  }
  // Interleave the newcomers so a swing does not face one whole type at a
  // time. Only they are shuffled: the mobs already in the queue are being
  // fought, and moving a wounded one out of the front window would hand back
  // the damage done to it.
  std::shuffle(queue_.begin() + first_new, queue_.end(), rng_);
}

// How many of the queue one swing of `attack` lands on. A scattered swing is
// held to the strikes it throws as well as to its reach: eleven flames cannot
// burn a twelfth enemy, and one that took no strike takes no mark either.
int CombatSim::Reached(const AttackOption& attack) const {
  int hit = std::min(std::max(1, attack.max_enemies),
                     static_cast<int>(queue_.size()));
  if (attack.scatter_hits > 0) {
    hit = std::min(hit, attack.scatter_hits);
  }
  return hit;
}

// What each of the `hit` enemies takes of one scattered swing, indexed by their
// place in the queue. The strikes spread before they double up, so on eleven
// enemies every one of them takes a whole flame and on a lone boss all eleven
// land -- the second and every later one worth what the -55% leaves it.
//
// The leftovers go to the healthiest, which is GMS's rule read through what
// this game has: the flames go for the boss first, and a boss is the monster
// with the HP.
std::vector<double> CombatSim::ScatterShares(const AttackOption& attack,
                                             int hit) const {
  if (attack.scatter_hits <= 0 || hit <= 0) {
    return {};
  }
  std::vector<int> healthiest(hit);
  for (int j = 0; j < hit; ++j) {
    healthiest[j] = j;
  }
  std::sort(healthiest.begin(), healthiest.end(),
            [this](int a, int b) { return queue_[a].hp > queue_[b].hp; });
  std::vector<double> shares(hit, 0.0);
  int each = attack.scatter_hits / hit;
  int spare = attack.scatter_hits % hit;
  for (int rank = 0; rank < hit; ++rank) {
    int strikes = each + (rank < spare ? 1 : 0);
    shares[healthiest[rank]] = 1.0 + (strikes - 1) * attack.scatter_repeat_kept;
  }
  return shares;
}

std::vector<int> CombatSim::PierceOrder(const AttackOption& attack, int hit) {
  if (attack.pierce_gain_pct <= 0.0 || hit <= 1) {
    return {};
  }
  std::vector<int> order(hit);
  for (int j = 0; j < hit; ++j) {
    order[j] = j;
  }
  std::shuffle(order.begin(), order.end(), rng_);
  return order;
}

std::vector<int> CombatSim::LeadTargets(const AttackOption& attack,
                                        int hit) const {
  if (attack.lead_damage.empty() || hit <= 0) {
    return {};
  }
  std::vector<int> reached(hit);
  for (int j = 0; j < hit; ++j) {
    reached[j] = j;
  }
  int want = std::min(std::max(1, attack.lead_enemies), hit);
  // Only the front `want` need be in order, and the queue is short either way.
  std::partial_sort(
      reached.begin(), reached.begin() + want, reached.end(),
      [this](int a, int b) { return queue_[a].hp > queue_[b].hp; });
  reached.resize(want);
  return reached;
}

// What one swing of `attack` would land on the queue as it stands: its own
// damage to each mob it reaches, the opening hit on one of them, and the Final
// Attack that follows it onto every one of them.
double CombatSim::StrikeDamage(const AttackOption& attack, int hit) const {
  double total = 0.0;
  // A hold is worth its pulses and its finish, and the pulses are only as many
  // as the fight means to hold for -- weighing it at a full hold would price
  // pulses that will land on nothing.
  int pulses = ChannelPulses(attack, hit);
  double dropped = pulses > 0 ? attack.channel.pulses - pulses : 0;
  std::vector<double> shares = ScatterShares(attack, hit);
  for (int j = 0; j < hit; ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.damage_per_hit.size())) {
      total +=
          (attack.damage_per_hit[type] - dropped * PulseDamage(attack, type)) *
          (shares.empty() ? 1.0 : shares[j]);
    }
  }
  total *= PierceMean(attack.pierce_gain_pct, hit);
  for (int lead : LeadTargets(attack, hit)) {
    if (queue_[lead].type < static_cast<int>(attack.lead_damage.size())) {
      total += attack.lead_damage[queue_[lead].type];
    }
  }
  for (int j = 0; j < hit; ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.final_attack_damage.size())) {
      total += attack.final_attack_damage[type];
    }
  }
  // Added once for the whole swing, not once per enemy: that is the whole
  // difference between the two banks.
  if (hit > 0 &&
      queue_[0].type <
          static_cast<int>(attack.single_final_attack_damage.size())) {
    total += attack.single_final_attack_damage[queue_[0].type];
  }
  // A chance that lands on one enemy, so it is charged once however many the
  // swing reached -- and it is a share of what that one was taking anyway.
  if (hit > 0 &&
      queue_[0].type < static_cast<int>(attack.damage_per_hit.size())) {
    for (const ProcRoll& proc : attack.procs) {
      total +=
          attack.damage_per_hit[queue_[0].type] * proc.chance * proc.damage_pct;
    }
  }
  return total;
}

// What relighting a burn on one monster buys, over the seconds before the
// swing carrying it could come round again: the burning it gains on top of
// what the monster had coming anyway, plus a helping where the pile has room
// for another. Nothing at all on a monster already carrying a full, fresh
// pile, which is what sends the chooser elsewhere until the burn nears its end.
double CombatSim::BurnCredit(const DotApplication& burn, const QueuedMob& mob,
                             double cadence) const {
  if (mob.type >= static_cast<int>(burn.damage.size())) {
    return 0.0;
  }
  double left = 0.0;
  int stacks = 0;
  if (burn.slot >= 0 && burn.slot < static_cast<int>(mob.dots.size())) {
    left = mob.dots[burn.slot].left_seconds;
    stacks = mob.dots[burn.slot].stacks;
  }
  double lit = std::min(burn.duration_seconds, cadence);
  double gained = stacks * (lit - std::min(left, cadence));
  if (stacks < burn.max_stacks) {
    gained += lit;
  }
  return burn.damage[mob.type] * burn.chance * gained / burn.interval_seconds;
}

// A burn is charged at what relighting it actually buys rather than in full,
// and on a monster already burning that is little or nothing. How long the
// monster lives thins it further, and the chooser cannot know that before it
// swings.
double CombatSim::BurnDamage(const AttackOption& attack, int hit) const {
  double total = 0.0;
  double cadence = std::max(attack.swing_seconds, attack.cooldown_seconds);
  for (const DotApplication& burn : attack.dots) {
    if (burn.interval_seconds <= 0.0) {
      continue;
    }
    for (int j = 0; j < hit; ++j) {
      total += BurnCredit(burn, queue_[j], cadence);
    }
  }
  return total;
}

// At a wait of five seconds and a swing of one, a fifth of the strike rides
// each swing. The chooser has to say so, or a swing would be weighed as though
// it set the strike off every time.
double CombatSim::SideStrikeDamage(const AttackOption& attack) const {
  if (attack.side == nullptr) {
    return 0.0;
  }
  double every = std::max(attack.side->cooldown_seconds, attack.swing_seconds);
  if (every <= 0.0) {
    return 0.0;
  }
  return SwingDamage(*attack.side) * attack.swing_seconds / every;
}

double CombatSim::SwingDamage(const AttackOption& attack) const {
  int hit = Reached(attack);
  double total = StrikeDamage(attack, hit) + BurnDamage(attack, hit);
  // The side strike is held aside rather than added, because it rides the
  // swing whichever form that swing took -- the averaging below is between the
  // two forms, and this is outside it.
  double side = SideStrikeDamage(attack);
  // A swing with an empowered form lands it once in every N, so what the
  // attack is worth per swing is the average of the two. The rate has to say
  // so, or the attack would be weighed on the weaker of the two things it
  // does. The form has no form of its own, so this recurs exactly once.
  if (attack.empowered != nullptr && attack.empowered_every > 0) {
    if (!attack.brands_enemies) {
      total +=
          (SwingDamage(*attack.empowered) - total) / attack.empowered_every;
      return total + side;
    }
    // Marking instead, every mob the swing reaches comes due once in every N
    // rather than the swing doing so, and takes the whole form on top of its
    // ordinary strike. Averaged over the cycle like the case above, and for
    // the same reason: what an attack is worth is what it does over the run of
    // swings, not what this one swing happens to land on.
    for (int j = 0; j < hit; ++j) {
      int type = queue_[j].type;
      if (type < static_cast<int>(attack.empowered->damage_per_hit.size())) {
        total +=
            attack.empowered->damage_per_hit[type] / attack.empowered_every;
      }
    }
  }
  return total + side;
}

int CombatSim::BestAttack(const CombatParams& params) const {
  if (queue_.empty()) {
    return -1;  // nothing to hit, so nothing to choose between
  }
  int best = -1;
  double best_rate = -1.0;
  const std::vector<AttackOption>& options = Attacks(params);
  for (int i = 0; i < static_cast<int>(options.size()); ++i) {
    const AttackOption& attack = options[i];
    if (attack.swing_seconds <= 0.0) {
      continue;  // not a swing; a skill on its own clock is not chosen between
    }
    if (attack.heal_fraction > 0.0) {
      continue;  // a cast is chosen by need, not by rate -- see HealToCast
    }
    // Still recharging, so it is not among the swings on offer this time --
    // which is the whole point of a cooldown on something this good.
    if (Recharging(i)) {
      continue;
    }
    // Per second, not per swing: a skill that hits half again as hard but takes
    // twice as long is worse, and only the rate says so.
    //
    // An ice swing is also paid for the pile it leaves and for the freeze it
    // lays, or the chooser would take the harder lightning swing every time
    // and neither would ever exist -- see FreezeCredit and FrozenCredit.
    double rate = (SwingDamage(attack) * StateBoost(attack, FrontMob()) +
                   FreezeCredit(params, attack) + FrozenCredit(params, attack) +
                   BurnStateCredit(params, attack)) /
                  SwingSecondsAgainst(attack);
    if (rate > best_rate) {
      best_rate = rate;
      best = i;
    }
  }
  return best;
}

// Whether the attack at `index` is still winding back up. Guarded on size
// because the clocks are grown to fit the params, and a swing asked about
// before the first Advance has none yet.
bool CombatSim::Recharging(int index) const {
  return index < static_cast<int>(attack_clocks_.size()) &&
         attack_clocks_[index].cooldown_left > 0.0;
}

int CombatSim::HealToCast(const CombatParams& params) const {
  // Only mid-fight. With the map cleared the beat hands HP back for free, so
  // spending a swing on it would buy nothing.
  if (queue_.empty() || params.max_player_hp <= 0) {
    return -1;
  }
  if (player_hp_ >= kHealBelowFraction * params.max_player_hp) {
    return -1;
  }
  const std::vector<AttackOption>& options = Attacks(params);
  for (int i = 0; i < static_cast<int>(options.size()); ++i) {
    const AttackOption& attack = options[i];
    if (attack.heal_fraction <= 0.0 || attack.swing_seconds <= 0.0) {
      continue;
    }
    if (Recharging(i)) {
      continue;
    }
    return i;
  }
  return -1;
}

int CombatSim::ChooseAttack(const CombatParams& params) const {
  // Index 0 is the bare poke, which is never held to -- see fight.h.
  if (aimed_ > 0 && aimed_ < static_cast<int>(Attacks(params).size()) &&
      Attacks(params)[aimed_].swing_seconds > 0.0 && !queue_.empty()) {
    return aimed_;
  }
  // Checked after the commitment, so a swing already winding up lands first:
  // the cast replaces the NEXT attack, it does not interrupt this one.
  int heal = HealToCast(params);
  if (heal >= 0) {
    return heal;
  }
  // Below the heal, since staying alive comes before hitting harder, and above
  // the damage: a lapsed wound is worth more than one more of the best swing.
  int lay = BuffToLay(params);
  if (lay >= 0) {
    return lay;
  }
  return BestAttack(params);
}

void CombatSim::RunCooldowns(const CombatParams& params, double dt) {
  // Unlike an auto-cast's clock, this runs on an empty map too: a player
  // waiting out a respawn really does have their cooldown back when the mobs
  // land, where a summon with nothing to hit has simply not fired.
  attack_clocks_.resize(Attacks(params).size());
  for (AttackClock& clock : attack_clocks_) {
    clock.cooldown_left = std::max(0.0, clock.cooldown_left - dt);
    clock.side_cooldown_left = std::max(0.0, clock.side_cooldown_left - dt);
  }
}

double CombatSim::Strike(const AttackOption& attack, DamageSource source,
                         int pulses) {
  // One strike hits the front mobs at once; each takes its own type's damage.
  // Overkill on any of them is wasted. Dead mobs leave the queue and the ones
  // behind slide into the window next time.
  int hit = Reached(attack);
  // The pile does not move until the swing is over -- CreditFreeze runs after
  // Strike -- so what a stack is worth can be read enemy by enemy below, which
  // is what Shatter's share of the defence needs.
  //
  // Picked before anything lands, so the opening hit chooses by the HP the
  // mobs went into the swing with rather than what the spread left them on.
  std::vector<int> lead = LeadTargets(attack, hit);
  // An arrow that gains as it travels needs an order to travel along, and
  // nothing here has a position -- so the swing draws one. Every other swing
  // hits the queue as it stands, which is the same thing for damage that does
  // not escalate.
  std::vector<int> order = PierceOrder(attack, hit);
  // Picked with the opening hit's targets and for the same reason: what the
  // flames doubled up on is decided by the HP the monsters went in with.
  std::vector<double> shares = ScatterShares(attack, hit);
  // Before anything lands, so every way this swing reaches one monster files
  // its lines under the one event -- the strike, the opening hit and whatever
  // follows them are one landing to the player watching.
  OpenLandings(hit, source);
  // A hold that was not timed by the swing clock decides here instead, which
  // is what an attack on a clock of its own would want.
  int held = attack.channel.pulses > 0
                 ? (pulses >= 0 ? pulses : ChannelPulses(attack, hit))
                 : 0;
  for (int step = 0; step < hit; ++step) {
    int j = order.empty() ? step : order[step];
    double gain =
        order.empty() ? 1.0 : std::pow(1.0 + attack.pierce_gain_pct, step);
    double freeze = StateBoost(attack, queue_[j]);
    double share = shares.empty() ? 1.0 : shares[j];
    double damage =
        held > 0
            ? ChannelDamage(attack, queue_[j].type, held, LandingAt(j, freeze))
            : DamageToMob(attack, j, LandingAt(j, gain * freeze * share)) *
                  gain;
    Hurt(queue_[j], damage * freeze * share);
  }
  for (int j : lead) {
    double freeze = StateBoost(attack, queue_[j]);
    double damage = attack.lead_damage[queue_[j].type] *
                    RollFactor(attack.lead_rolls, rng_, LineSink());
    RecordRolls(LandingAt(j, freeze),
                attack.lead_damage[queue_[j].type] * freeze);
    Hurt(queue_[j], damage * freeze);
  }
  // A Final Attack rolls separately against every enemy the swing reached, so
  // in expectation each of them takes it.
  if (!attack.final_attack_damage.empty()) {
    for (int j = 0; j < hit; ++j) {
      double freeze = StateBoost(attack, queue_[j]);
      Hurt(queue_[j], RolledFinalAttack(attack.final_attack_rolls,
                                        attack.final_attack_damage,
                                        queue_[j].type, LandingAt(j, freeze)) *
                          freeze);
    }
  }
  // Blizzard's rolls once for the swing and falls on one enemy, whatever the
  // swing reached. The first in the queue is as good as any: nothing here has
  // a position, so no enemy is nearer than another.
  if (hit > 0 && !attack.single_final_attack_damage.empty()) {
    double freeze = StateBoost(attack, queue_[0]);
    Hurt(queue_[0], RolledFinalAttack(attack.single_final_attack_rolls,
                                      attack.single_final_attack_damage,
                                      queue_[0].type, LandingAt(0, freeze)) *
                        freeze);
  }
  double recovered = RollProcs(attack, hit);
  // Marked before the dead are cleared, so the indices the swing reached are
  // still the ones the mark is written to.
  ApplyDots(attack, hit);
  ApplyFreeze(attack, hit);
  ApplyScar(attack, hit);
  Reap();
  return recovered;
}

// A chance rolled once for the whole swing, as GMS rolls it once per attack.
// What it adds is a share of what one enemy was already taking, so it is a
// second helping of the swing rather than a hit of its own -- and it rolls its
// own crit and mastery, being a separate landing.
//
// It falls on the front of the queue. Nothing here has a position, so no enemy
// is nearer than another; the same reason Blizzard's single strike picks it.
double CombatSim::RollProcs(const AttackOption& attack, int hit) {
  double recovered = 0.0;
  if (hit <= 0) {
    return recovered;
  }
  double boost = StateBoost(attack, queue_[0]);
  for (const ProcRoll& proc : attack.procs) {
    std::bernoulli_distribution fires(proc.chance);
    if (!fires(rng_)) {
      continue;
    }
    Hurt(queue_[0], RolledDamage(attack, queue_[0].type,
                                 LandingAt(0, proc.damage_pct * boost)) *
                        proc.damage_pct * boost);
    recovered += proc.hp_recover_pct;
  }
  return recovered;
}

// What a pile that deep multiplies this swing by against a mob of `type` in
// the state `frozen` says. Taken as a count and a flag rather than off the
// character and the monster, so the chooser can ask what a DEEPER pile or a
// FROZEN enemy would be worth -- see FreezeCredit and FrozenCredit.
double CombatSim::BoostForStacks(const AttackOption& attack, int stacks,
                                 int type, bool frozen) const {
  if (stacks <= 0) {
    return 1.0;
  }
  // The two multiply rather than sum: critical damage is folded into the swing
  // and final damage is the last thing applied to it, which is where every
  // other pair of the two meets.
  double crit = frozen ? 1.0 + attack.freeze_crit_gain * stacks : 1.0;
  // The stacks are spent whichever state the enemy ends in, so this one is the
  // pile's alone -- GMS gates the critical damage on a frozen enemy and says
  // nothing of the kind about the final damage the lightning swing takes.
  double spent =
      attack.freeze_spends ? 1.0 + attack.freeze_fd_per_stack * stacks : 1.0;
  // Glacial Fury's magic attack is another factor again: it is attack rather
  // than damage, and lands under everything the swing already multiplies.
  double matt = 1.0 + attack.freeze_matt_gain * stacks;
  // Shatter's is the one factor that differs mob by mob: what ignoring a
  // little more defence buys is that monster's own.
  double shattered =
      frozen && type < static_cast<int>(attack.freeze_ied_gain.size())
          ? 1.0 + attack.freeze_ied_gain[type] * stacks
          : 1.0;
  return crit * spent * matt * shattered;
}

// Whether the monster is under any status the fight keeps on it. Two are: the
// ice a swing left and a burn. GMS asks for a list of five, and the other
// three are inflicted by nothing here -- when one of them arrives it joins the
// test and no lever moves. See SkillEffect::final_dmg_pct_when_afflicted.
bool CombatSim::Afflicted(const QueuedMob& mob) const {
  if (mob.frozen_left_seconds > 0.0) {
    return true;
  }
  for (const MobDot& burn : mob.dots) {
    if (burn.left_seconds > 0.0 && burn.stacks > 0) {
      return true;
    }
  }
  return false;
}

double CombatSim::FreezeBoost(const AttackOption& attack,
                              const QueuedMob& mob) const {
  return BoostForStacks(attack, freeze_stacks_, mob.type,
                        mob.frozen_left_seconds > 0.0);
}

// A scar is left by a LINE, so a swing that lands four of them scars partway
// through itself: the line that cuts collects nothing and the ones after it
// collect everything. What share of the swing that is, is the odds the scar
// was already there as each line landed, averaged over the lines -- which
// comes to 1 - (1 - odds) * (1 - (1 - chance)^n) / (chance * n).
//
// A monster carrying the scar already pays the whole of it, which is what the
// odds standing at 1 says.
double CombatSim::ScarBoost(const AttackOption& attack,
                            const QueuedMob& mob) const {
  if (attack.scar_fd <= 0.0) {
    return 1.0;
  }
  double chance = attack.scar_chance;
  double share = mob.scar_odds;
  if (chance > 0.0) {
    int lines = std::max(1, attack.lines);
    double unscarred = std::pow(1.0 - chance, static_cast<double>(lines));
    share = 1.0 - (1.0 - share) * (1.0 - unscarred) / (chance * lines);
  }
  return 1.0 + attack.scar_fd * share;
}

// Burns standing across the whole group: a monster carrying two of them is
// two, and eight carrying one apiece are eight.
int CombatSim::BurnsAlight() const {
  int alight = 0;
  for (const QueuedMob& mob : queue_) {
    for (const MobDot& burn : mob.dots) {
      if (burn.left_seconds > 0.0 && burn.stacks > 0) {
        ++alight;
      }
    }
  }
  return alight;
}

// What the state the enemies are already in multiplies this swing by. The two
// readings answer different questions: whether THIS monster is afflicted, and
// how many burns stand on the WHOLE group.
//
// The count is the group's because that is what GMS means by "within a certain
// range", so a drain is worth its cap on a map and only what the rotation
// keeps alight on a boss.
//
// No credit goes with either: a swing that lights a burn already earns the
// burn, and the drains ride the F/P's DoT swings the chooser was picking
// anyway -- unlike the freeze, which nothing but the ice would have bought.
// See FrozenCredit.
double CombatSim::ConditionBoostFor(const AttackOption& attack, bool afflicted,
                                    int alight) const {
  double gate = afflicted ? 1.0 + attack.fd_when_afflicted : 1.0;
  if (attack.fd_per_dot <= 0.0 || attack.dot_count_cap <= 0) {
    return gate;
  }
  return gate *
         (1.0 + attack.fd_per_dot * std::min(alight, attack.dot_count_cap));
}

double CombatSim::ConditionBoost(const AttackOption& attack,
                                 const QueuedMob& mob) const {
  return ConditionBoostFor(attack, Afflicted(mob), BurnsAlight());
}

double CombatSim::StateBoost(const AttackOption& attack,
                             const QueuedMob& mob) const {
  return FreezeBoost(attack, mob) * ScarBoost(attack, mob) *
         ConditionBoost(attack, mob);
}

// The monster a reader with no particular enemy in mind takes -- nothing here
// has a position, so none is nearer than another. A bare one where nothing is
// standing, which reads as an unfrozen mob of the first type.
const CombatSim::QueuedMob& CombatSim::FrontMob() const {
  static const QueuedMob kNone;
  return queue_.empty() ? kNone : queue_.front();
}

double CombatSim::PulseDamage(const AttackOption& attack, int type) const {
  if (attack.groups.empty() ||
      type >= static_cast<int>(attack.groups.front().damage.size())) {
    return 0.0;
  }
  return attack.groups.front().damage[type];
}

double CombatSim::FinishDamage(const AttackOption& attack, int type) const {
  double total = 0.0;
  for (std::size_t i = 1; i < attack.groups.size(); ++i) {
    const HitGroup& group = attack.groups[i];
    if (type < static_cast<int>(group.damage.size())) {
      total += group.damage[type];
    }
  }
  return total;
}

int CombatSim::ChannelPulses(const AttackOption& attack, int hit) const {
  const ChannelHold& hold = attack.channel;
  if (hold.pulses <= 0) {
    return 0;
  }
  // What the strike at the end will land anyway. The hold only has to bring
  // them within its reach: pulses past that fall on something already dead.
  int wanted = hold.min_pulses;
  for (int j = 0; j < hit && j < static_cast<int>(queue_.size()); ++j) {
    int type = queue_[j].type;
    double freeze = StateBoost(attack, queue_[j]);
    double pulse = PulseDamage(attack, type) * freeze;
    if (pulse <= 0.0) {
      continue;
    }
    double left = queue_[j].hp - FinishDamage(attack, type) * freeze;
    if (left <= 0.0) {
      continue;
    }
    wanted = std::max(wanted, static_cast<int>(std::ceil(left / pulse)));
  }
  return std::clamp(wanted, hold.min_pulses, hold.pulses);
}

double CombatSim::SwingSecondsAgainst(const AttackOption& attack) const {
  if (attack.channel.pulses <= 0) {
    return attack.swing_seconds;
  }
  int hit = Reached(attack);
  return HoldSeconds(attack.channel, ChannelPulses(attack, hit));
}

double CombatSim::HeldSeconds(const AttackOption& attack) const {
  if (attack.channel.pulses <= 0) {
    return attack.swing_seconds;
  }
  return HoldSeconds(attack.channel, held_pulses_);
}

double CombatSim::ChannelDamage(const AttackOption& attack, int type,
                                int pulses, const Landing& landing) {
  double total = 0.0;
  double pulse = PulseDamage(attack, type);
  for (int i = 0; i < pulses; ++i) {
    total += pulse * RollFactor(attack.groups.front().rolls, rng_, LineSink());
    RecordRolls(landing, pulse * landing.scale);
  }
  // Everything past the first group is the strike the hold ends on, landed
  // once however long the hold ran.
  for (std::size_t i = 1; i < attack.groups.size(); ++i) {
    const HitGroup& group = attack.groups[i];
    if (type >= static_cast<int>(group.damage.size())) {
      continue;
    }
    total += group.damage[type] * RollFactor(group.rolls, rng_, LineSink());
    RecordRolls(landing, group.damage[type] * landing.scale);
  }
  return total;
}

double CombatSim::FreezeCredit(const CombatParams& params,
                               const AttackOption& attack) const {
  int room = std::min(attack.freeze_build, FreezeCap(params) - freeze_stacks_);
  if (room <= 0) {
    return 0.0;
  }
  // What the deeper pile is worth to the swing that comes next -- the whole of
  // what a stack buys, not only the final damage a lightning swing spends it
  // for. The best swing on offer, since that is the one the chooser will reach
  // for once the stacks are down, whichever element it carries.
  //
  // One swing of lookahead, which is as far as a greedy chooser sees. A deep
  // pile pays out over several swings and this credits it once.
  double best = 0.0;
  int deeper = freeze_stacks_ + room;
  for (const AttackOption& other : Attacks(params)) {
    if (other.swing_seconds <= 0.0) {
      continue;
    }
    const QueuedMob& front = FrontMob();
    bool frozen = front.frozen_left_seconds > 0.0;
    double gain = (BoostForStacks(other, deeper, front.type, frozen) -
                   BoostForStacks(other, freeze_stacks_, front.type, frozen)) *
                  ConditionBoost(other, front);
    best = std::max(best, SwingDamage(other) * gain);
  }
  return best;
}

// Freezing moves two things at once: the pile's own factors, which only pay on
// a frozen monster, and the affliction gate. The credit has to ask for the
// pair -- priced on the pile alone, an ice swing looks worth nothing at all to
// a character whose only reader is Storm Magic, and the chooser never casts
// it.
double CombatSim::FrozenRate(const CombatParams& params,
                             const QueuedMob& mob) const {
  double best = 0.0;
  bool afflicted = Afflicted(mob);
  int alight = BurnsAlight();
  for (const AttackOption& other : Attacks(params)) {
    if (other.swing_seconds <= 0.0 ||
        mob.type >= static_cast<int>(other.damage_per_hit.size())) {
      continue;
    }
    double gain = BoostForStacks(other, freeze_stacks_, mob.type, true) *
                      ConditionBoostFor(other, true, alight) -
                  BoostForStacks(other, freeze_stacks_, mob.type, false) *
                      ConditionBoostFor(other, afflicted, alight);
    best = std::max(
        best, other.damage_per_hit[mob.type] * gain / other.swing_seconds);
  }
  return best;
}

double CombatSim::FrozenCredit(const CombatParams& params,
                               const AttackOption& attack) const {
  if (attack.freeze_seconds <= 0.0) {
    return 0.0;
  }
  // Only the seconds before this swing could come round again are worth
  // anything: freeze past that will have been laid down a second time.
  double cadence = std::max(attack.swing_seconds, attack.cooldown_seconds);
  double lays = std::min(attack.freeze_seconds, cadence);
  int hit = Reached(attack);
  double credit = 0.0;
  for (int j = 0; j < hit; ++j) {
    double gained = lays - std::min(queue_[j].frozen_left_seconds, cadence);
    if (gained > 0.0) {
      credit += gained * FrozenRate(params, queue_[j]);
    }
  }
  return credit;
}

// Seconds the burn in `slot` still has on this monster, 0 for one carrying
// none.
double CombatSim::BurnLeftOn(const QueuedMob& mob, int slot) const {
  if (slot < 0 || slot >= static_cast<int>(mob.dots.size())) {
    return 0.0;
  }
  return mob.dots[slot].stacks > 0 ? mob.dots[slot].left_seconds : 0.0;
}

// What one more burning monster is worth per second to whatever is swung next,
// through the gate and through the count. The mirror of FrozenRate.
double CombatSim::BurningRate(const CombatParams& params, const QueuedMob& mob,
                              int alight) const {
  double best = 0.0;
  bool afflicted = Afflicted(mob);
  for (const AttackOption& other : Attacks(params)) {
    if (other.swing_seconds <= 0.0 ||
        mob.type >= static_cast<int>(other.damage_per_hit.size())) {
      continue;
    }
    double gain = ConditionBoostFor(other, true, alight + 1) -
                  ConditionBoostFor(other, afflicted, alight);
    best = std::max(
        best, other.damage_per_hit[mob.type] * gain / other.swing_seconds);
  }
  return best;
}

// What lighting this swing's burns is worth to everything swung AFTER it. A
// burn afflicts the monster it lands on and deepens the count the drains read,
// and neither is paid for by the burn's own ticks -- BurnCredit prices those
// alone. The mirror of FrozenCredit, and needed for the same reason: Ignite
// makes Explosion the F/P Mage's best swing and Explosion burns nothing, so a
// chooser blind to this never lays the mist that turns the drains on.
double CombatSim::BurnStateCredit(const CombatParams& params,
                                  const AttackOption& attack) const {
  if (attack.dots.empty()) {
    return 0.0;
  }
  double cadence = std::max(attack.swing_seconds, attack.cooldown_seconds);
  int hit = Reached(attack);
  int alight = BurnsAlight();
  double credit = 0.0;
  for (int j = 0; j < hit; ++j) {
    for (const DotApplication& burn : attack.dots) {
      if (burn.interval_seconds <= 0.0) {
        continue;
      }
      double gained = std::min(burn.duration_seconds, cadence) -
                      std::min(BurnLeftOn(queue_[j], burn.slot), cadence);
      if (gained > 0.0) {
        credit += gained * burn.chance * BurningRate(params, queue_[j], alight);
      }
    }
  }
  return credit;
}

void CombatSim::CreditFreeze(const CombatParams& params,
                             const AttackOption& attack) {
  int cap = FreezeCap(params);
  if (cap <= 0) {
    return;
  }
  if (attack.freeze_build > 0) {
    freeze_stacks_ = std::min(cap, freeze_stacks_ + attack.freeze_build);
  } else if (attack.freeze_spends) {
    freeze_stacks_ = std::max(0, freeze_stacks_ - std::max(1, attack.lines));
  }
}

void CombatSim::OpenLandings(int hit, DamageSource source) {
  if (!record_lines_) {
    return;
  }
  landing_source_ = source;
  landing_event_.assign(queue_.size(), 0);
  for (int j = 0; j < hit && j < static_cast<int>(queue_.size()); ++j) {
    landing_event_[j] = ++next_damage_event_;
  }
}

Landing CombatSim::LandingAt(int index, double scale) const {
  if (!record_lines_) {
    return {0, 0, {}, scale};
  }
  int event = index < static_cast<int>(landing_event_.size())
                  ? landing_event_[index]
                  : 0;
  return {queue_[index].id, event, landing_source_, scale};
}

void CombatSim::RecordLine(const Landing& landing, double damage, bool crit) {
  if (!record_lines_) {
    return;
  }
  damage_lines_this_step_.push_back(
      {landing.mob_id, landing.event, landing.source, damage, crit});
}

void CombatSim::RecordRolls(const Landing& landing, double damage) {
  if (!record_lines_) {
    return;
  }
  for (const LineRoll& roll : line_rolls_) {
    RecordLine(landing, damage * roll.share, roll.crit);
  }
}

std::vector<LineRoll>* CombatSim::LineSink() {
  return record_lines_ ? &line_rolls_ : nullptr;
}

void CombatSim::Hurt(QueuedMob& mob, double damage) {
  mob.hp -= damage;
  damage_this_step_ += damage;
}

void CombatSim::ClampRoster(const CombatParams& params,
                            const std::map<int, double>& hp_by_id) {
  for (QueuedMob& mob : queue_) {
    std::map<int, double>::const_iterator said = hp_by_id.find(mob.id);
    if (said == hp_by_id.end() ||
        mob.type >= static_cast<int>(params.types.size())) {
      continue;
    }
    mob.hp =
        std::min(mob.hp, said->second * params.types[mob.type].mob->max_hp());
  }
  Reap();
  // The roster a caller reads is a copy, taken when the step ended. Nothing
  // here went through a step, so it is taken again.
  PublishRoster(params);
}

void CombatSim::Reap() {
  std::vector<QueuedMob> survivors;
  survivors.reserve(queue_.size());
  for (QueuedMob& mob : queue_) {
    if (mob.hp <= 0.0) {
      ++kills_this_step_[mob.type];
    } else {
      survivors.push_back(std::move(mob));
    }
  }
  queue_ = std::move(survivors);
}

void CombatSim::ApplyDots(const AttackOption& attack, int hit) {
  for (const DotApplication& burn : attack.dots) {
    if (burn.slot < 0 || burn.interval_seconds <= 0.0) {
      continue;
    }
    for (int j = 0; j < hit; ++j) {
      QueuedMob& mob = queue_[j];
      if (static_cast<int>(mob.dots.size()) <= burn.slot) {
        mob.dots.resize(burn.slot + 1);
      }
      if (mob.type >= static_cast<int>(burn.damage.size())) {
        continue;
      }
      // Rolled per enemy, so a poison takes hold on some of what the swing
      // reached and not the rest.
      std::bernoulli_distribution takes(burn.chance);
      if (burn.chance < 1.0 && !takes(rng_)) {
        continue;
      }
      // The damage is written over rather than added to, and only the duration
      // starts again -- the tick clock is left where it is, or a swing faster
      // than the interval would refresh the burn out of ever ticking at all.
      // What piles up is the helpings, up to what the burn allows.
      MobDot& dot = mob.dots[burn.slot];
      if (dot.left_seconds <= 0.0) {
        dot.phase = 0.0;
        dot.stacks = 0;
      }
      dot.stacks = std::min(burn.max_stacks, dot.stacks + 1);
      dot.left_seconds = burn.duration_seconds;
      dot.interval_seconds = burn.interval_seconds;
      dot.damage = burn.damage[mob.type];
      dot.rolls = burn.rolls;
    }
  }
}

void CombatSim::ApplyFreeze(const AttackOption& attack, int hit) {
  if (attack.freeze_seconds <= 0.0) {
    return;
  }
  for (int j = 0; j < hit; ++j) {
    // Written over rather than added to: a monster frozen again is frozen for
    // the full time from now, not for what was left plus the whole of it.
    queue_[j].frozen_left_seconds =
        std::max(queue_[j].frozen_left_seconds, attack.freeze_seconds);
  }
}

void CombatSim::RunFreeze(double dt) {
  for (QueuedMob& mob : queue_) {
    mob.frozen_left_seconds = std::max(0.0, mob.frozen_left_seconds - dt);
  }
}

void CombatSim::ApplyScar(const AttackOption& attack, int hit) {
  if (attack.scar_chance <= 0.0 || attack.scar_seconds <= 0.0) {
    return;
  }
  double unscarred =
      std::pow(1.0 - attack.scar_chance, std::max(1, attack.lines));
  for (int j = 0; j < hit; ++j) {
    // The odds a scar stands are the odds one was already there or this swing
    // left one; the clock starts again either way, as the freeze's does.
    queue_[j].scar_odds = 1.0 - (1.0 - queue_[j].scar_odds) * unscarred;
    queue_[j].scarred_left_seconds =
        std::max(queue_[j].scarred_left_seconds, attack.scar_seconds);
  }
}

void CombatSim::RunScar(double dt) {
  for (QueuedMob& mob : queue_) {
    mob.scarred_left_seconds = std::max(0.0, mob.scarred_left_seconds - dt);
    if (mob.scarred_left_seconds <= 0.0) {
      mob.scar_odds = 0.0;
    }
  }
}

void CombatSim::RunDots(double dt) {
  bool burned = false;
  for (QueuedMob& mob : queue_) {
    for (int slot = 0; slot < static_cast<int>(mob.dots.size()); ++slot) {
      MobDot& dot = mob.dots[slot];
      if (dot.left_seconds <= 0.0 || dot.interval_seconds <= 0.0) {
        continue;
      }
      // Only the seconds the burn still had are spent, so one running out
      // partway through a step lands the ticks it was owed and no more.
      double spent = std::min(dt, dot.left_seconds);
      dot.left_seconds -= spent;
      dot.phase += spent;
      while (dot.phase >= dot.interval_seconds) {
        dot.phase -= dot.interval_seconds;
        // Every helping ticks for the whole damage, and each rolls its own.
        for (int i = 0; i < dot.stacks; ++i) {
          Hurt(mob, dot.damage * RollFactor(dot.rolls, rng_, LineSink()));
          // A tick is its own landing: it falls on its own clock, between the
          // swings rather than with one.
          RecordRolls(
              {mob.id, ++next_damage_event_, {DamageOrigin::kBurn, slot}, 1.0},
              dot.damage);
        }
        burned = true;
      }
    }
  }
  // A burn kills the same way a swing does, and the kill is counted the same
  // way. Skipped where nothing ticked, since walking the queue costs more than
  // the burn did.
  if (burned) {
    Reap();
  }
}

void CombatSim::RunRegen(const CombatParams& params, double dt) {
  regen_phase_.resize(params.regen_pulses.size(), 0.0);
  for (int i = 0; i < static_cast<int>(params.regen_pulses.size()); ++i) {
    const RegenPulse& pulse = params.regen_pulses[i];
    if (pulse.interval_seconds <= 0.0) {
      continue;
    }
    regen_phase_[i] += dt;
    // A while rather than an if: a step wider than the interval owes every
    // pulse it covered, the way a burn ticks for each one it outlasted.
    while (regen_phase_[i] >= pulse.interval_seconds) {
      regen_phase_[i] -= pulse.interval_seconds;
      player_hp_ =
          std::min(static_cast<double>(params.max_player_hp),
                   player_hp_ + pulse.hp + pulse.pct * params.max_player_hp);
    }
  }
}

double CombatSim::RolledDamage(const AttackOption& attack, int type,
                               const Landing& landing) {
  if (attack.groups.empty()) {
    RecordLine(landing, attack.damage_per_hit[type] * landing.scale, false);
    return attack.damage_per_hit[type];
  }
  double total = 0.0;
  for (const HitGroup& group : attack.groups) {
    if (type < static_cast<int>(group.damage.size())) {
      total += group.damage[type] * RollFactor(group.rolls, rng_, LineSink());
      RecordRolls(landing, group.damage[type] * landing.scale);
    }
  }
  return total;
}

double CombatSim::RolledFinalAttack(const std::vector<FinalAttackRoll>& sources,
                                    const std::vector<double>& expected,
                                    int type, const Landing& landing) {
  if (sources.empty()) {
    RecordLine(landing, expected[type] * landing.scale, false);
    return expected[type];
  }
  double total = 0.0;
  for (const FinalAttackRoll& source : sources) {
    if (type >= static_cast<int>(source.damage.size())) {
      continue;
    }
    // A chance past certainty is that many hits guaranteed and a roll for
    // what is left over. Nothing grants one yet, but summing two sources into
    // one entry is exactly what this design stopped doing, so the shape has
    // to hold if one ever does.
    int certain = static_cast<int>(source.chance);
    std::bernoulli_distribution lands(source.chance - certain);
    for (int roll = 0; roll < source.count; ++roll) {
      int hits = certain + (lands(rng_) ? 1 : 0);
      for (int hit = 0; hit < hits; ++hit) {
        total +=
            source.damage[type] * RollFactor(source.rolls, rng_, LineSink());
        RecordRolls(landing, source.damage[type] * landing.scale);
      }
    }
  }
  return total;
}

double CombatSim::DamageToMob(const AttackOption& attack, int index,
                              const Landing& landing) {
  int type = queue_[index].type;
  double ordinary = RolledDamage(attack, type, landing);
  if (!attack.brands_enemies || attack.empowered == nullptr ||
      attack.empowered_every <= 0) {
    return ordinary;
  }
  // Counted before the test, exactly as FormToLand counts swings: a mark of
  // five goes off on the fifth strike, not the sixth.
  if (++queue_[index].brand < attack.empowered_every) {
    return ordinary;
  }
  queue_[index].brand = 0;
  // On top of the strike that set it off, not instead of it: a mark going off
  // is its own event, where an empowered swing IS the swing.
  return ordinary + RolledDamage(*attack.empowered, type, landing);
}

const AttackOption& CombatSim::FormToLand(int& count,
                                          const AttackOption& attack) {
  // A form that marks enemies never stands in for the swing: the swing lands
  // as itself, and DamageToBranded decides mob by mob what goes off on top.
  if (attack.empowered == nullptr || attack.empowered_every <= 0 ||
      attack.brands_enemies) {
    return attack;
  }
  // Counted before the test, so a period of four is three ordinary landings
  // and then this one -- not this one first and three after.
  if (++count < attack.empowered_every) {
    return attack;
  }
  count = 0;
  return *attack.empowered;
}

int CombatSim::player_hp() const {
  return static_cast<int>(std::ceil(player_hp_));
}

void CombatSim::GoIdle() {
  initialized_ = false;
  respawning_ = false;
  roster_.clear();
  target_name_.clear();
  target_level_ = 0;
  target_hp_fraction_ = 0.0;
  attack_fraction_ = 0.0;
  attack_name_.clear();
  reach_ = 1;
  player_hp_ = 0.0;
  player_hp_fraction_ = 0.0;
  player_max_hp_ = 0;
  player_level_ = 0;
  hit_phase_ = 0.0;
  respawn_fraction_ = 0.0;
  respawns_ = false;
  auto_clocks_.clear();
  attack_clocks_.clear();
  regen_phase_.clear();
  aimed_ = -1;
}

void CombatSim::BeginMapIfChanged(const CombatParams& params) {
  // The queue holds indices into the map's types, and its HP values are that
  // map's mobs'. Carried to another map, both would describe the wrong
  // monsters.
  if (initialized_ && encounter_ == params.encounter) {
    return;
  }
  encounter_ = params.encounter;
  respawn_phase_ = 0.0;
  attack_phase_ = 0.0;
  hit_phase_ = 0.0;
  next_mob_id_ = 0;
  auto_clocks_.assign(params.auto_attacks.size(), AutoClock());
  attack_clocks_.assign(params.attacks.size(), AttackClock());
  // The buff and fountain clocks are deliberately left alone: they belong to
  // the character rather than to the map, and walking somewhere else neither
  // takes a buff away nor hands back a pulse early.
  // Another map's attacks were another map's indices, and nothing here is
  // part-way through a swing at it any more.
  aimed_ = -1;
  player_hp_ = params.max_player_hp;
  queue_.clear();
  TopUp(params);
  initialized_ = true;
}

void CombatSim::RespawnBeat(const CombatParams& params, double dt) {
  if (params.respawn_seconds <= 0.0) {
    return;  // nothing more is coming: see CombatParams::respawn_seconds
  }
  respawn_phase_ += dt;
  if (respawn_phase_ < params.respawn_seconds) {
    return;
  }
  respawn_phase_ -= params.respawn_seconds;
  respawned_this_step_ = true;
  bool was_idle = queue_.empty();
  TopUp(params);
  // Every beat hands back a slice of the pool, cleared map or not. It is the
  // only healing there is: hold out for a beat taking less than the slice and
  // the fight can be won by outlasting it.
  player_hp_ =
      std::min(static_cast<double>(params.max_player_hp),
               player_hp_ + params.beat_heal_fraction * params.max_player_hp);
  if (!was_idle) {
    // Mobs arriving mid-fight leave a wound-up swing wound up: restarting it
    // would throw away real progress, not just the bar being watched.
    return;
  }
  // Clearing the map is the bigger breather, and worth the whole pool.
  attack_phase_ = 0.0;
  player_hp_ = params.max_player_hp;
  hit_phase_ = 0.0;
}

// What is left of an incoming hit once the buffs standing have taken their
// share. They multiply rather than sum, the way every other reduction in the
// game does: two halves leave a quarter of the hit, not none of it.
//
// The buffs standing are the ones the step opened with: a smokescreen dropped
// after the blow landed does not take that blow back. See Advance.
double CombatSim::BuffDamageTakenFactor(const CombatParams& params) const {
  // A shell blocks whole hits, so what it takes off a hit is only ever the
  // share it hands a boss instead -- see BlockHit.
  bool boss = !queue_.empty() && params.types[queue_.front().type].mob->boss();
  double factor = 1.0;
  for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
    if ((buff_mask_ & (1 << i)) != 0) {
      factor *= 1.0 - params.buffs[i].damage_taken_pct;
      if (boss && buffs_[i].blocks_left > 0) {
        factor *= 1.0 - params.buffs[i].boss_damage_taken_pct;
      }
    }
  }
  // A party's buff is one more reduction and multiplies like the rest: a
  // Shadower's smokescreen over a Shadower's own is not twice the shelter.
  for (int i = 0; i < static_cast<int>(ally_buffs_.size()); ++i) {
    if (ally_buffs_[i].left > 0.0) {
      factor *= 1.0 - params.ally_buffs[i].damage_taken_pct;
      if (boss && ally_buffs_[i].blocks_left > 0) {
        factor *= 1.0 - params.ally_buffs[i].boss_damage_taken_pct;
      }
    }
  }
  // The shelter a HOLD is, which lasts exactly as long as the hold: the swing
  // being charged is the key being held down.
  const std::vector<AttackOption>& options = Attacks(params);
  if (aimed_ >= 0 && aimed_ < static_cast<int>(options.size())) {
    factor *= 1.0 - options[aimed_].channel.damage_taken_pct;
  }
  return std::max(0.0, factor);
}

// A hit a shell swallows whole. Only one shell pays for it however many are
// standing: two would spend two blocks on one hit and the player would be no
// better off for it.
//
// A boss's hit is never blocked -- GMS exempts the attacks that cost a share
// of the pool, and `Mob.boss` is what says which those are here. What a shell
// does about one of those is take its share off, in BuffDamageTakenFactor.
bool CombatSim::BlockHit(const CombatParams& params) {
  if (queue_.empty() || params.types[queue_.front().type].mob->boss()) {
    return false;
  }
  for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
    if ((buff_mask_ & (1 << i)) == 0 || buffs_[i].blocks_left <= 0) {
      continue;
    }
    if (--buffs_[i].blocks_left == 0) {
      buffs_[i].left = 0.0;  // spent: the shell falls, clock or no clock
      buff_mask_ &= ~(1 << i);
    }
    return true;
  }
  for (int i = 0; i < static_cast<int>(ally_buffs_.size()); ++i) {
    if (ally_buffs_[i].left <= 0.0 || ally_buffs_[i].blocks_left <= 0) {
      continue;
    }
    if (--ally_buffs_[i].blocks_left == 0) {
      ally_buffs_[i].left = 0.0;
    }
    return true;
  }
  return false;
}

void CombatSim::TakeMobHit(const CombatParams& params, double dt) {
  // Only the mob at the front hits back, however many are on the map -- see
  // fight.h. It swings first, so the last one standing still lands its hit on
  // the way out. An empty map has nothing to be hit by, and its clock waits
  // rather than banking a free hit for whatever arrives next.
  if (queue_.empty() || params.hit_seconds <= 0.0) {
    hit_phase_ = 0.0;
    return;
  }
  hit_phase_ += dt;
  if (hit_phase_ < params.hit_seconds) {
    return;
  }
  hit_phase_ -= params.hit_seconds;
  // A frozen monster is stopped where it stands and never gets its swing off.
  // The clock runs on regardless, so a long freeze eats several beats instead
  // of banking them for the thaw.
  if (queue_.front().frozen_left_seconds > 0.0) {
    return;
  }
  if (BlockHit(params)) {
    return;  // cancelled whole: nothing to lose, and nothing to reflect
  }
  // A scarred monster swings weaker, and the scar is odds rather than a flag
  // -- so what it lands is the two damages weighed by how likely the scar is.
  const CombatType& type = params.types[queue_.front().type];
  double odds = queue_.front().scar_odds;
  double taken = (type.damage_to_player * (1.0 - odds) +
                  type.damage_to_player_scarred * odds) *
                 BuffDamageTakenFactor(params);
  player_hp_ = std::max(0.0, player_hp_ - taken);
  died_this_step_ = player_hp_ <= 0.0 && !Revive(params);
  Reflect(params, taken);
}

bool CombatSim::Revive(const CombatParams& params) {
  if (params.revive_cooldown_seconds <= 0.0 || revive_left_ > 0.0) {
    return false;
  }
  // The whole pool back, standing where they fell: what the pact buys is the
  // trip home, and the mob that landed the hit is still in front of them.
  player_hp_ = params.max_player_hp;
  revive_left_ = params.revive_cooldown_seconds;
  return true;
}

void CombatSim::Reflect(const CombatParams& params, double damage_taken) {
  // Off the whole hit, not the sliver of it a dying player had left to lose:
  // what comes back is a share of what was thrown, not of what it emptied.
  if (params.damage_reflect_pct <= 0.0 || queue_.empty()) {
    return;
  }
  QueuedMob& front = queue_.front();
  Hurt(front, params.damage_reflect_pct * damage_taken);
  if (front.hp > 0.0) {
    return;
  }
  ++kills_this_step_[front.type];
  queue_.erase(queue_.begin());
}

const std::vector<AttackOption>& CombatSim::Attacks(
    const CombatParams& params) const {
  return params.Attacks(buff_mask_);
}

int CombatSim::FreezeCap(const CombatParams& params) const {
  return params.FreezeCap(buff_mask_);
}

const std::vector<AttackOption>& CombatSim::AutoAttacks(
    const CombatParams& params) const {
  return params.AutoAttacks(buff_mask_);
}

const std::vector<AttackOption>& CombatSim::TriggeredAttacks(
    const CombatParams& params) const {
  return params.TriggeredAttacks(buff_mask_);
}

// Whether a shell is worth raising now. It is the one buff held back rather
// than raised the moment it comes round, because it is a heal and a shelter
// at once and the two want opposite timing: the heal is wasted on a full pool,
// and a shelter raised after the blow has landed shelters nothing.
//
// On a map the blows are small and steady, so it waits for the pool to be low
// enough to be worth filling -- the same line a cast heal is chosen on. On a
// boss one blow is the whole fight, so it goes up as soon as it comes round.
//
// Every other buff is raised on its clock as it always has been: one granting
// damage is worth having up whatever the pool is at.
bool CombatSim::ShieldWanted(const CombatParams& params,
                             const BuffOption& buff) const {
  if (buff.shield_hits <= 0) {
    return true;
  }
  if (!queue_.empty() && params.types[queue_.front().type].mob->boss()) {
    return true;
  }
  return player_hp_ < kHealBelowFraction * params.max_player_hp;
}

void CombatSim::RunBuffs(const CombatParams& params, double dt) {
  int count = static_cast<int>(params.buffs.size());
  // Seeded with each buff's full charge rather than with nothing, or one
  // charged by hits would go up before a single hit had landed.
  if (static_cast<int>(buffs_.size()) != count) {
    buffs_.resize(count);
    for (int i = 0; i < count; ++i) {
      buffs_[i].charge_left = params.buffs[i].charge_lines;
    }
  }
  buff_mask_ = 0;
  for (int i = 0; i < count; ++i) {
    const BuffOption& buff = params.buffs[i];
    BuffClock& clock = buffs_[i];
    clock.left = std::max(0.0, clock.left - dt);
    clock.cooldown_left = std::max(0.0, clock.cooldown_left - dt);
    // Put up the moment it comes round, and only with something to fight: one
    // spent on an empty map is one the player does not have when the mobs
    // land. Nothing is recast while it is still standing -- a player timing
    // these would not throw the tail of one away.
    //
    // A buff its own swing lays is not raised here at all: it waits for that
    // swing to land. See LayBuffs.
    // What it is waiting on: a wait in seconds, or a count of landed hits.
    bool ready = buff.charge_lines > 0 ? clock.charge_left <= 0.0
                                       : clock.cooldown_left <= 0.0;
    if (buff.laid_by_attack < 0 && clock.left <= 0.0 && ready &&
        !queue_.empty() && buff.duration_seconds > 0.0 &&
        ShieldWanted(params, buff)) {
      clock.left = buff.duration_seconds;
      clock.cooldown_left = buff.cooldown_seconds;
      clock.charge_left = buff.charge_lines;
      clock.blocks_left = buff.shield_hits;
      // Raising it costs the character its animation, taken off the swing they
      // were charging: a buff is cast instead of attacking, not alongside it.
      attack_phase_ -= buff.cast_seconds;
      player_hp_ =
          std::min(static_cast<double>(params.max_player_hp),
                   player_hp_ + buff.heal_fraction * params.max_player_hp);
    }
    if (clock.left > 0.0) {
      buff_mask_ |= 1 << i;
    }
  }
}

void CombatSim::RunAllyBuffs(const CombatParams& params, double dt) {
  int count = static_cast<int>(params.ally_buffs.size());
  ally_buffs_.resize(count);
  for (int i = 0; i < count; ++i) {
    const BuffOption& buff = params.ally_buffs[i];
    BuffClock& clock = ally_buffs_[i];
    clock.left = std::max(0.0, clock.left - dt);
    clock.cooldown_left = std::max(0.0, clock.cooldown_left - dt);
    // The same rule the character's own buffs go up under: the moment it comes
    // round, and only with something to fight. What the ally is doing between
    // casts is not modelled -- they are in the same fight, so they raise it
    // when it is worth raising.
    if (clock.left <= 0.0 && clock.cooldown_left <= 0.0 && !queue_.empty() &&
        buff.duration_seconds > 0.0 && ShieldWanted(params, buff)) {
      clock.left = buff.duration_seconds;
      clock.cooldown_left = buff.cooldown_seconds;
      clock.blocks_left = buff.shield_hits;
      // An ally's cast reaches this character too: what it puts back is a
      // share of THEIR pool, the caster's own being no business of this fight.
      player_hp_ =
          std::min(static_cast<double>(params.max_player_hp),
                   player_hp_ + buff.heal_fraction * params.max_player_hp);
    }
  }
}

void CombatSim::LayBuffs(const CombatParams& params, int swung) {
  for (int i = 0; i < static_cast<int>(buffs_.size()); ++i) {
    const BuffOption& buff = params.buffs[i];
    if (buff.laid_by_attack != swung) {
      continue;
    }
    // Refreshed rather than stacked, and its wait started from the swing that
    // laid it: what a second puncture leaves is one wound, not two.
    buffs_[i].left = buff.duration_seconds;
    buffs_[i].cooldown_left = buff.cooldown_seconds;
  }
}

// The swing that lays a buff nobody is holding, chosen ahead of the hardest
// swing on offer. Deliberately not a comparison: a wound that has lapsed lifts
// every swing after it for as long as it stands, and the one swing it costs is
// one in a hundred, so asking whether it pays would be arithmetic with only
// one answer.
//
// It is a rule about every swing-laid buff, not just this one. A buff worth
// less than the swing it displaces would be over-cast here -- the guard for
// that is a rate check, and it belongs with the first skill that needs one
// rather than with an imagined one.
int CombatSim::BuffToLay(const CombatParams& params) const {
  if (queue_.empty()) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
    const BuffOption& buff = params.buffs[i];
    if (buff.laid_by_attack < 0 || buff.duration_seconds <= 0.0) {
      continue;
    }
    if (i < static_cast<int>(buffs_.size()) && buffs_[i].left > 0.0) {
      continue;  // still standing, so there is nothing to go and do
    }
    // The swing itself may be recharging, in which case there is no laying it
    // this time and the fight swings for damage instead.
    if (buff.laid_by_attack < static_cast<int>(attack_clocks_.size()) &&
        attack_clocks_[buff.laid_by_attack].cooldown_left > 0.0) {
      continue;
    }
    return buff.laid_by_attack;
  }
  return -1;
}

void CombatSim::CreditBuffs(const CombatParams& params, double weight,
                            int lines) {
  for (int i = 0; i < static_cast<int>(buffs_.size()); ++i) {
    BuffClock& clock = buffs_[i];
    // A buff counting hits is charged by what the swing landed, and only while
    // it is down: GMS stops counting for as long as the window stands, so what
    // its uptime is worth is bounded however fast the character fires.
    if (params.buffs[i].charge_lines > 0) {
      if (clock.left <= 0.0) {
        clock.charge_left = std::max(0.0, clock.charge_left - lines);
      }
      continue;
    }
    clock.cooldown_left =
        std::max(0.0, clock.cooldown_left -
                          params.buffs[i].cooldown_reduction_seconds * weight);
  }
}

void CombatSim::RunAutoCasts(const CombatParams& params, double dt) {
  // Their clocks run only while there is something to hit: a summon has
  // nothing to do on an empty map, and waiting there earns it no free cast.
  const std::vector<AttackOption>& casts = AutoAttacks(params);
  auto_clocks_.resize(casts.size());
  for (int i = 0; i < static_cast<int>(casts.size()); ++i) {
    const AttackOption& cast = casts[i];
    AutoClock& clock = auto_clocks_[i];
    if (queue_.empty() || cast.interval_seconds <= 0.0) {
      continue;
    }
    // A pulse that is really a wound waits for one to have been left. Its
    // phase is left alone rather than wound on, so it does not come due the
    // instant the wound lands and then again a moment later. What it has
    // already spent of the window goes back with it: the count is per raising.
    if (cast.needs_buff >= 0 && (buff_mask_ & (1 << cast.needs_buff)) == 0) {
      clock.pulses = 0;
      continue;
    }
    // One that has spent its window falls silent for the rest of it, phase and
    // all -- lengthening the buff behind it buys nothing.
    if (cast.max_pulses > 0 && clock.pulses >= cast.max_pulses) {
      continue;
    }
    clock.phase += dt;
    // As in RunSwing and RunDots: a step wider than the interval owes every
    // cast it covered.
    while (clock.phase >= cast.interval_seconds) {
      clock.phase -= cast.interval_seconds;
      ++clock.pulses;
      const AttackOption& landed = FormToLand(clock.empowered_count, cast);
      // Every strike of the tick lands in full: three sword strikes 60ms apart
      // are one moment here, and each is its own attack on its own enemies.
      for (int strike = 0; strike < cast.strikes_per_pulse; ++strike) {
        Strike(landed, {DamageOrigin::kOwnClock, i});
      }
      // A summon leaves the ice it makes: Elquines freezes what it touches. It
      // never spends the pile -- ClearSwingRiders sees to that.
      CreditFreeze(params, landed);
      if (cast.max_pulses > 0 && clock.pulses >= cast.max_pulses) {
        break;
      }
    }
  }
}

void CombatSim::CreditSwing(const CombatParams& params, double weight) {
  const std::vector<AttackOption>& casts = TriggeredAttacks(params);
  trigger_count_.resize(casts.size(), 0.0);
  for (int i = 0; i < static_cast<int>(casts.size()); ++i) {
    const AttackOption& cast = casts[i];
    if (cast.attacks_per_cast <= 0) {
      continue;
    }
    trigger_count_[i] += weight;
    // A while rather than an if: nothing stops a swing being worth more than
    // the whole count, and one that is should fire the skill for each of them.
    // Nudged, because a weight of a seventh cannot be written exactly: 28 of
    // them land a hair under the 4 they are meant to come to, and the volley
    // would fire one swing late every time.
    while (trigger_count_[i] + kCountEpsilon >= cast.attacks_per_cast) {
      trigger_count_[i] -= cast.attacks_per_cast;
      Strike(cast, {DamageOrigin::kSwingClock, i});
    }
  }
}

const AttackOption* CombatSim::AimSwing(const CombatParams& params) {
  int previous = aimed_;
  aimed_ = ChooseAttack(params);
  const AttackOption* attack = aimed_ >= 0 ? &Attacks(params)[aimed_] : nullptr;
  attack_name_ = attack != nullptr ? attack->name : "";
  if (attack != nullptr) {
    // How long to hold is settled once, when the swing is first aimed. A hold
    // already running is the player's key held down: the queue moving under it
    // does not re-time it, and re-deciding every step would let it flicker.
    if (aimed_ != previous) {
      held_pulses_ =
          ChannelPulses(*attack, std::min(std::max(1, attack->max_enemies),
                                          static_cast<int>(queue_.size())));
    }
    // A cast reaches nobody, so it leaves the window on whatever the last
    // swing set: the mob bars must not collapse for the length of the cast.
    if (attack->heal_fraction <= 0.0) {
      reach_ = std::max(1, attack->max_enemies);
    }
    // Cached because the charge bar is drawn after the swing is aimed and has
    // no attack of its own to ask. A pick that changes mid-charge changes the
    // clock under it, which is the honest reading: the swing being charged is
    // the one that will land.
    swing_seconds_ = HeldSeconds(*attack);
  }
  return attack;
}

void CombatSim::RunSwing(const CombatParams& params, double dt) {
  // Aimed against the queue as it stands, so the charge bar names the swing
  // that is really coming. Only the poke is re-aimed as mobs die out from
  // under it; a skill winding up is committed to.
  const AttackOption* attack = AimSwing(params);
  if (attack == nullptr) {
    return;
  }
  attack_phase_ += dt;
  // A while rather than an if: a step wider than the swing owes every swing it
  // covered, the way a burn ticks for each interval it outlasted. A 120ms
  // key-down skill under a 150ms frame otherwise loses one swing in five and
  // leaves the charge bar pinned full, since the phase never falls back under
  // one swing.
  while (attack != nullptr && attack->swing_seconds > 0.0 &&
         attack_phase_ >= HeldSeconds(*attack)) {
    attack_phase_ -= HeldSeconds(*attack);
    LandSwing(params, *attack);
    // Aimed afresh by the landing, because the queue just moved and the
    // commitment is discharged.
    attack = aimed_ >= 0 ? &Attacks(params)[aimed_] : nullptr;
  }
}

void CombatSim::LandSwing(const CombatParams& params,
                          const AttackOption& attack) {
  // Read before the strike, because aiming again below moves it.
  int swung = aimed_;
  if (attack.heal_fraction > 0.0) {
    player_hp_ =
        std::min(static_cast<double>(params.max_player_hp),
                 player_hp_ + attack.heal_fraction * params.max_player_hp);
  } else {
    const AttackOption& landed =
        FormToLand(attack_clocks_[swung].empowered_count, attack);
    double proc_recovered =
        Strike(landed, {DamageOrigin::kSwing, 0}, held_pulses_);
    CreditFreeze(params, landed);
    // The strike this swing sets off beside itself, where its own wait has
    // run out. Read off the aimed attack rather than off what landed: the
    // strike belongs to the skill, not to the form standing in for it this
    // time. It goes out after the swing, so it lands on what the swing left.
    if (attack.side != nullptr &&
        attack_clocks_[swung].side_cooldown_left <= 0.0) {
      Strike(*attack.side, {DamageOrigin::kSideStrike, swung});
      attack_clocks_[swung].side_cooldown_left = attack.side->cooldown_seconds;
    }
    // Recovery rides the hit, so a cast does not earn it and neither does a
    // swing at nothing. What landed pays it rather than what was aimed, and
    // the swing's own is added to the character's: Angel Ray heals as it
    // lands, on top of whatever any passive recovers.
    double recovered =
        params.hp_recover_pct + landed.hp_recover_pct + proc_recovered;
    player_hp_ = std::min(static_cast<double>(params.max_player_hp),
                          player_hp_ + recovered * params.max_player_hp);
    // Credited after the strike, so the volley lands on what the swing left
    // standing rather than on mobs it was about to kill anyway. A healing cast
    // credits nothing: it is not an attack.
    CreditSwing(params, attack.count_weight);
    // Attacking is what brings a buff round sooner, so the same swing that
    // credits the volleys credits the buffs. A cast credits neither.
    CreditBuffs(params, attack.count_weight, landed.lines);
    LayBuffs(params, swung);
  }
  if (attack.cooldown_seconds > 0.0) {
    attack_clocks_[swung].cooldown_left = attack.cooldown_seconds;
  }
  aimed_ = -1;  // the swing landed, so the next one is chosen afresh
  AimSwing(params);
}

void CombatSim::MergeEngagedWindow(const CombatParams& params) {
  // One HP bar per type in the front window (the mobs the next swing hits),
  // in queue order, each bar averaging its members' remaining HP.
  int window = std::min(reach_, static_cast<int>(queue_.size()));
  for (int j = 0; j < window; ++j) {
    const Mob& mob = *params.types[queue_[j].type].mob;
    double frac = mob.max_hp() > 0
                      ? std::clamp(queue_[j].hp / mob.max_hp(), 0.0, 1.0)
                      : 0.0;
    std::vector<EngagedGroup>::iterator it = std::find_if(
        engaged_groups_.begin(), engaged_groups_.end(),
        [&mob](const EngagedGroup& g) { return g.name == mob.name(); });
    if (it == engaged_groups_.end()) {
      engaged_groups_.push_back({mob.name(), mob.level(), 1, frac});
      continue;
    }
    it->hp_fraction = (it->hp_fraction * it->count + frac) / (it->count + 1);
    ++it->count;
  }
}

void CombatSim::PublishRoster(const CombatParams& params) {
  roster_.clear();
  for (const QueuedMob& queued : queue_) {
    const Mob& mob = *params.types[queued.type].mob;
    double frac =
        mob.max_hp() > 0 ? std::clamp(queued.hp / mob.max_hp(), 0.0, 1.0) : 0.0;
    roster_.push_back({queued.id, queued.type, mob.name(), frac});
  }
}

void CombatSim::PublishTarget(const CombatParams& params) {
  engaged_groups_.clear();
  PublishRoster(params);
  respawning_ = queue_.empty();
  if (queue_.empty()) {
    target_name_.clear();
    target_level_ = 0;
    target_hp_fraction_ = 0.0;
    attack_fraction_ = 0.0;
    return;
  }
  const QueuedMob& front = queue_.front();
  const Mob& target = *params.types[front.type].mob;
  target_name_ = target.name();
  target_level_ = target.level();
  target_hp_fraction_ = target.max_hp() > 0
                            ? std::clamp(front.hp / target.max_hp(), 0.0, 1.0)
                            : 0.0;
  attack_fraction_ = swing_seconds_ > 0.0
                         ? std::clamp(attack_phase_ / swing_seconds_, 0.0, 1.0)
                         : 0.0;
  MergeEngagedWindow(params);
}

void CombatSim::Advance(const CombatParams& params, double elapsed_seconds) {
  active_ = params.active;
  kills_this_step_.assign(params.types.size(), 0);
  damage_this_step_ = 0.0;
  respawned_this_step_ = false;
  died_this_step_ = false;
  record_lines_ = params.record_damage_lines;
  damage_lines_this_step_.clear();
  if (!CanFight(params)) {
    GoIdle();
    return;
  }
  // Clamp a large real-time gap (a pause, say) to one swing, so the fight
  // resumes rather than jumping. Measured against the bare poke, which every
  // character has: which skill is coming is not known until the swing is aimed,
  // several steps below this.
  double dt = std::min(elapsed_seconds, params.attacks.front().swing_seconds);

  BeginMapIfChanged(params);
  // A level-up widens the pool and fills it, as GMS does. player_level_ is
  // still last step's, so this catches the moment it moves. It watches the
  // level and not the pool because everything else that widens the pool -- a
  // skill point, a scroll, a swapped hat -- is not a reason to be healed.
  if (params.player_level != player_level_) {
    player_hp_ = params.max_player_hp;
  }
  // A pool that shrank -- an unequipped hat -- takes the player down with it,
  // rather than leaving them holding HP their stats do not give them.
  player_hp_ = std::min(player_hp_, static_cast<double>(params.max_player_hp));

  // Before the hit that may need it, so a wait that runs out this step is one
  // the player has the benefit of.
  revive_left_ = std::max(0.0, revive_left_ - dt);
  RespawnBeat(params, dt);
  TakeMobHit(params, dt);
  // After the hit, so a buff going up now answers it with its heal, and
  // before everything that attacks, so this step swings with it.
  RunBuffs(params, dt);
  RunAllyBuffs(params, dt);
  // After the hit and before the swing, so a fountain is worth something on
  // the step it was needed rather than only on the next one.
  RunRegen(params, dt);
  RunAutoCasts(params, dt);
  // After the summons and before the swing, so a burn lit last step has landed
  // its ticks before this step's swing decides what is worth hitting. The
  // thaw runs with them, and for the same reason: a monster that came out of
  // the ice this step is one this step's swing must see out of it.
  RunDots(dt);
  RunFreeze(dt);
  RunScar(dt);
  RunCooldowns(params, dt);
  RunSwing(params, dt);

  player_max_hp_ = params.max_player_hp;
  player_level_ = params.player_level;
  player_hp_fraction_ =
      params.max_player_hp > 0
          ? std::clamp(player_hp_ / params.max_player_hp, 0.0, 1.0)
          : 0.0;
  respawns_ = params.respawn_seconds > 0.0;
  respawn_fraction_ =
      respawns_ ? std::clamp(respawn_phase_ / params.respawn_seconds, 0.0, 1.0)
                : 0.0;
  PublishTarget(params);
}

}  // namespace ms
