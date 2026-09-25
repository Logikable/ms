#include "src/combat/fight.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// HP fraction below which the player spends an attack on healing. Healing
// whenever it helps would leave no time to attack; waiting for the last sliver
// would be too late.
constexpr double kHealBelowFraction = 0.25;

// Tolerance for the attack counter, since a weight of 1/7 isn't exact in
// floating point.
constexpr double kCountEpsilon = 1e-9;

// Whether there's a fight to advance. The basic attack is always first, so
// check its interval.
bool CanFight(const CombatParams& params) {
  return params.active && !params.types.empty() && !params.attacks.empty() &&
         params.attacks.front().swing_seconds > 0.0;
}

// Average per-enemy multiplier for an attack gaining `gain` per enemy pierced
// over `hit` enemies, since the order is random. At Piercing Arrow's numbers,
// the average of (1 + 1.15 + ... + 1.15^5) over six is 1.46.
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
      arrival.max_hp = arrival.hp;
      arrival.boss = params.types[i].mob->boss();
      arrival.id = next_mob_id_++;
      queue_.push_back(std::move(arrival));
    }
  }
  // Shuffle only the new mobs: moving a damaged mob out of the front would undo
  // the damage done to it.
  std::shuffle(queue_.begin() + first_new, queue_.end(), rng_);
}

void CombatSim::AimAtHealthiest(const CombatParams& params) {
  if (!params.focus_healthiest || queue_.size() < 2) {
    return;
  }
  // Stable sort, so parts with equal HP keep their spawn order and a fight
  // plays out the same way every time.
  std::stable_sort(
      queue_.begin(), queue_.end(),
      [](const QueuedMob& a, const QueuedMob& b) { return a.hp > b.hp; });
}

int CombatSim::PerSwingFinalAttackTargets(const AttackOption& attack,
                                          int hit) const {
  if (hit <= 0 || attack.per_swing_final_attack_damage.empty()) {
    return 0;
  }
  return std::min(std::max(1, attack.per_swing_final_attack_enemies),
                  static_cast<int>(queue_.size()));
}

int CombatSim::WideHitTargets(const AttackOption& attack, int hit) const {
  if (hit <= 0 || attack.wide_hit_damage.empty()) {
    return 0;
  }
  return std::min(std::max(1, attack.wide_hit_enemies),
                  static_cast<int>(queue_.size()));
}

// Only increased for DoT Punisher, which summons an orb per burn stack already
// active. Read before any of this cast lands (ApplyDots runs at the end of
// Strike), so the orbs don't count themselves.
int CombatSim::ScatterHits(const AttackOption& attack) const {
  if (attack.scatter_hits <= 0 || attack.scatter_hits_per_dot <= 0.0) {
    return attack.scatter_hits;
  }
  int widened =
      attack.scatter_hits +
      static_cast<int>(attack.scatter_hits_per_dot * BurnStacksAlight());
  return std::min(widened, attack.scatter_max_hits);
}

int CombatSim::Reached(const AttackOption& attack) const {
  int hit = std::min(std::max(1, attack.max_enemies),
                     static_cast<int>(queue_.size()));
  int hits = ScatterHits(attack);
  if (hits > 0) {
    hit = std::min(hit, hits);
  }
  return hit;
}

int CombatSim::ExtraLines(const AttackOption& attack) const {
  if (attack.extra_line == nullptr || attack.lines_per_extra_enemy <= 0) {
    return 0;
  }
  return std::min(attack.max_extra_lines, attack.lines_per_extra_enemy *
                                              std::max(0, swing_enemies_ - 1));
}

// Hits spread out before doubling up: against eleven enemies each takes one
// flame; against a lone boss all eleven land, each repeat reduced by the -55%.
// Leftover hits go to the healthiest, our reading of GMS's rule. Hits past
// scatter_max_hits_per_enemy are lost, not redistributed.
std::vector<double> CombatSim::ScatterShares(const AttackOption& attack,
                                             int hit) const {
  int hits = ScatterHits(attack);
  if (hits <= 0 || hit <= 0) {
    return {};
  }
  std::vector<int> healthiest(hit);
  for (int j = 0; j < hit; ++j) {
    healthiest[j] = j;
  }
  std::sort(healthiest.begin(), healthiest.end(),
            [this](int a, int b) { return queue_[a].hp > queue_[b].hp; });
  std::vector<double> shares(hit, 0.0);
  int each = hits / hit;
  int spare = hits % hit;
  for (int rank = 0; rank < hit; ++rank) {
    int strikes = each + (rank < spare ? 1 : 0);
    if (attack.scatter_max_hits_per_enemy > 0) {
      strikes = std::min(strikes, attack.scatter_max_hits_per_enemy);
    }
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
  // Only the first `want` need to be sorted.
  std::partial_sort(
      reached.begin(), reached.begin() + want, reached.end(),
      [this](int a, int b) { return queue_[a].hp > queue_[b].hp; });
  reached.resize(want);
  return reached;
}

double CombatSim::StrikeDamage(const AttackOption& attack, int hit) const {
  double total = 0.0;
  // Only as many pulses as the fight intends to hold: a full hold would count
  // pulses that hit nothing.
  int pulses = ChannelPulses(attack, hit);
  int extras = ExtraLines(attack);
  std::vector<double> shares = ScatterShares(attack, hit);
  for (int j = 0; j < hit; ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.damage_per_hit.size())) {
      // What releasing early gives up. The dropped pulses are the last ones,
      // which are worth more on a hold that grows.
      double dropped =
          pulses > 0 ? HeldPulseDamage(attack, type, attack.channel.pulses) -
                           HeldPulseDamage(attack, type, pulses)
                     : 0.0;
      double extra =
          extras > 0 && type < static_cast<int>(
                                   attack.extra_line->damage_per_hit.size())
              ? extras * attack.extra_line->damage_per_hit[type]
              : 0.0;
      total += (attack.damage_per_hit[type] - dropped + extra) *
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
  // Rolled once per attack rather than per enemy (the difference between the
  // two types), but hits its own set of enemies.
  for (int j = 0; j < PerSwingFinalAttackTargets(attack, hit); ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.per_swing_final_attack_damage.size())) {
      total += attack.per_swing_final_attack_damage[type];
    }
  }
  // The attack chooser must count the wide part, or an attack whose current is
  // most of its value would look like the orb alone.
  for (int j = 0; j < WideHitTargets(attack, hit); ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.wide_hit_damage.size())) {
      total += attack.wide_hit_damage[type];
    }
  }
  // A proc hits one enemy, so count it once however wide the attack, as a share
  // of that enemy's damage.
  if (hit > 0 &&
      queue_[0].type < static_cast<int>(attack.damage_per_hit.size())) {
    for (const ProcRoll& proc : attack.procs) {
      total +=
          attack.damage_per_hit[queue_[0].type] * proc.chance * proc.damage_pct;
    }
  }
  return total;
}

// Burn damage gained beyond what the monster would take anyway, plus a new
// stack if there's room. Nothing on a full, fresh burn, which makes the chooser
// pick something else until the burn is nearly over.
double CombatSim::BurnCredit(const DotApplication& burn, const QueuedMob& mob,
                             double cadence) const {
  if (mob.type >= static_cast<int>(burn.damage.size())) {
    return 0.0;
  }
  double left = 0.0;
  double stacks = 0.0;
  if (burn.slot >= 0 && burn.slot < static_cast<int>(mob.dots.size())) {
    left = mob.dots[burn.slot].left_seconds;
    stacks = mob.dots[burn.slot].stacks;
  }
  double lit = std::min(burn.duration_seconds, cadence);
  double gained = stacks * (lit - std::min(left, cadence));
  gained += std::min(1.0, burn.max_stacks - stacks) * lit;
  return burn.damage[mob.type] * burn.chance * gained / burn.interval_seconds;
}

// Counted at what reapplying gains, which is little or nothing on a monster
// already burning.
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

// With a five-second cooldown and a one-second attack, a fifth of the side
// strike counts toward each attack; otherwise the attack would be valued as if
// it triggered the strike every time.
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

// A stored attack is used all at once on one press rather than spread across
// attacks while it recharges, unlike a side strike. The fight should prefer the
// attack carrying it only while a charge is available, and value it bare once
// the charge is spent.
double CombatSim::LoadedDamage(const AttackOption& attack) const {
  if (attack.loaded == nullptr || attack.loaded_attack < 0 ||
      attack.loaded_attack >= static_cast<int>(attack_clocks_.size()) ||
      attack_clocks_[attack.loaded_attack].charges_left <= 0) {
    return 0.0;
  }
  const AttackOption& load = *attack.loaded;
  int left = attack_clocks_[attack.loaded_attack].charges_left;
  int hit = Reached(load);
  double worth = (StrikeDamage(load, hit) + BurnDamage(load, hit)) *
                 std::min(load.charges_per_swing, left);
  // A self-recharged charge is worth its share of the time to the next press,
  // like a side strike on a cooldown. Charges from raising the buff count in
  // full, since those go out as fast as the player can press.
  if (load.recharge_seconds > 0.0 && left <= load.recharge_max &&
      attack.swing_seconds > 0.0) {
    worth *= attack.swing_seconds /
             std::max(load.recharge_seconds, attack.swing_seconds);
  }
  return worth;
}

// The barrage lands on its own timer while the player keeps attacking, so it's
// cut short by the next cast of the same skill: a cooldown away if it has one,
// otherwise one attack away.
double CombatSim::BarrageStrikes(const AttackOption& attack) const {
  int strikes = std::max(1, attack.strikes_in_sequence);
  if (strikes == 1 || attack.cast_interval_seconds <= 0.0) {
    return 1.0;
  }
  double again = std::max(SwingSecondsAgainst(attack), attack.cooldown_seconds);
  return std::min<double>(strikes, 1.0 + again / attack.cast_interval_seconds);
}

double CombatSim::SwingDamage(const AttackOption& attack) const {
  // A wound form isn't averaged like an empowered form: it's what this press
  // does if a wound is present, so the rate reflects the current queue.
  if (WoundFull(attack)) {
    return SwingDamage(*attack.wound_form);
  }
  int hit = Reached(attack);
  double total = StrikeDamage(attack, hit) * BarrageStrikes(attack) +
                 BurnDamage(attack, hit);
  // Kept separate because these apply whichever form is used, and the averaging
  // below is between the two forms.
  double side = SideStrikeDamage(attack) + LoadedDamage(attack);
  // An empowered form lands once every N attacks, so the attack is worth the
  // average of the two. The form has no form of its own, so this recurses once.
  if (attack.empowered != nullptr && attack.empowered_every > 0) {
    if (!attack.brands_enemies) {
      total +=
          (SwingDamage(*attack.empowered) - total) / attack.empowered_every;
      return total + side;
    }
    // With marking, each mob hit triggers once every N hits rather than the
    // attack triggering, and takes the full form on top. Averaged over the
    // cycle as above.
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

// Per second, not per attack: a skill that hits 50% harder but takes twice as
// long is worse. Ice attacks also count the stacks and freeze they leave, or
// the chooser would always pick lightning. See FreezeCredit and FrozenCredit.
double CombatSim::SwingRate(const CombatParams& params,
                            const AttackOption& attack) const {
  return (SwingDamage(attack) * StateBoost(attack, FrontMob()) +
          FreezeCredit(params, attack) + FrozenCredit(params, attack) +
          BurnStateCredit(params, attack)) /
         SwingSecondsAgainst(attack);
}

// Only attacks that can actually be used now: a skill two minutes from its next
// cast isn't what the fight will spend Freeze Stacks on.
bool CombatSim::OnOffer(const CombatParams& params, int index) const {
  const std::vector<AttackOption>& options = Attacks(params);
  if (index < 0 || index >= static_cast<int>(options.size())) {
    return false;
  }
  const AttackOption& attack = options[index];
  if (attack.swing_seconds <= 0.0) {
    return false;  // not an attack; auto-firing skills aren't chosen
  }
  if (attack.heal_fraction > 0.0) {
    return false;  // chosen by need, not rate; see HealToCast
  }
  // A stored attack fired by another skill's press isn't its own button: it
  // goes out with that attack, whose value already counts it.
  if (attack.spent_by_attack >= 0) {
    return false;
  }
  // A hold is valued by its pulse over its pulse time regardless of length, so
  // one charge rates the same as a full set and the chooser uses it as soon as
  // a charge is ready.
  return !Recharging(index) && Loaded(params, index) && Charged(params, index);
}

int CombatSim::TopAttack(const CombatParams& params,
                         const std::vector<bool>& held) const {
  int best = -1;
  double best_rate = -1.0;
  const std::vector<AttackOption>& options = Attacks(params);
  for (int i = 0; i < static_cast<int>(options.size()); ++i) {
    const AttackOption& attack = options[i];
    // Skip attacks being saved for a buff window.
    if (i < static_cast<int>(held.size()) && held[i]) {
      continue;
    }
    if (!OnOffer(params, i)) {
      continue;
    }
    double rate = SwingRate(params, attack);
    if (rate > best_rate) {
      best_rate = rate;
      best = i;
    }
  }
  return best;
}

CombatSim::ComingWindow CombatSim::NextWindow(
    const CombatParams& params) const {
  ComingWindow window;
  window.seconds = std::numeric_limits<double>::infinity();
  window.mask = buff_mask_;
  int count = std::min(static_cast<int>(buffs_.size()),
                       static_cast<int>(params.buffs.size()));
  for (int i = 0; i < count; ++i) {
    const BuffOption& buff = params.buffs[i];
    // Shields are raised when needed, not on their cooldown (see ShieldWanted),
    // so waiting for one could stall the fight on a window that never opens.
    if (buffs_[i].left > 0.0 || buff.laid_by_attack >= 0 ||
        buff.charge_lines > 0 || buff.shield_hits > 0 ||
        buff.duration_seconds <= 0.0) {
      continue;
    }
    window.seconds = std::min(window.seconds, buffs_[i].cooldown_left);
  }
  if (!std::isfinite(window.seconds)) {
    return window;
  }
  // The mask at that time: buffs coming up are set, buffs ending are cleared.
  for (int i = 0; i < count; ++i) {
    const BuffOption& buff = params.buffs[i];
    const BuffClock& clock = buffs_[i];
    if (clock.left > 0.0) {
      if (clock.left <= window.seconds) {
        window.mask &= ~(1 << i);
      }
      continue;
    }
    if (buff.laid_by_attack < 0 && buff.charge_lines <= 0 &&
        buff.shield_hits <= 0 && buff.duration_seconds > 0.0 &&
        clock.cooldown_left <= window.seconds) {
      window.mask |= 1 << i;
    }
  }
  return window;
}

bool CombatSim::HoldSaves(const CombatParams& params, int index,
                          const ComingWindow& window) const {
  const std::vector<AttackOption>& options = Attacks(params);
  if (index < 0 || index >= static_cast<int>(options.size())) {
    return false;
  }
  const AttackOption& attack = options[index];
  // An attack that would land inside the window anyway gains nothing by
  // waiting: it lands at the end of its remaining animation, since the attack
  // timer carries over to whatever replaces it. Subtract this step, which the
  // buff timers have counted and the attack hasn't; otherwise a hold releases
  // one step early every time.
  if (window.seconds <
      SwingSecondsAgainst(attack) - attack_phase_ - step_seconds_) {
    return false;
  }
  const ChannelHold& hold = attack.channel;
  if (hold.charge_seconds > 0.0) {
    // Charges cost nothing to hold. Waiting only wastes a charge that would
    // fill past the maximum.
    double banked = index < static_cast<int>(attack_clocks_.size())
                        ? attack_clocks_[index].hold_charges
                        : 0.0;
    return banked + window.seconds / hold.charge_seconds <= hold.max_charges;
  }
  // A cooldown that's back before the window opens is free to use now, since
  // both uses happen. Only a cooldown longer than the wait means choosing when
  // to use it.
  return attack.cooldown_seconds > window.seconds;
}

bool CombatSim::HoldPays(const CombatParams& params, int index, int filler,
                         const ComingWindow& window) const {
  if (filler < 0) {
    return false;  // nothing else to use, and the fight never idles
  }
  // Compare using the masks of buffs that are up, not the ones currently
  // granting: the question is which buffs are active, not which moment of a
  // bursting buff it is.
  const std::vector<AttackOption>& now = params.Attacks(buff_mask_);
  const std::vector<AttackOption>& then = params.Attacks(window.mask);
  if (index >= static_cast<int>(now.size()) ||
      filler >= static_cast<int>(now.size()) ||
      index >= static_cast<int>(then.size()) ||
      filler >= static_cast<int>(then.size())) {
    return false;
  }
  // What one use of this attack gains over the filler in the same time. A
  // difference of rates, so what both share cancels out.
  double seconds = SwingSecondsAgainst(now[index]);
  double press_now =
      (SwingRate(params, now[index]) - SwingRate(params, now[filler])) *
      seconds;
  double press_then =
      (SwingRate(params, then[index]) - SwingRate(params, then[filler])) *
      seconds;
  double gain = press_then - press_now;
  if (gain <= 0.0) {
    return false;  // the window helps the filler as much, so don't wait
  }
  if (now[index].channel.charge_seconds > 0.0) {
    return true;  // charges lose nothing by waiting; HoldSaves prevents
                  // overflow
  }
  // Waiting delays every later use, losing wait/cooldown of a use. Valued at
  // the current use, which errs on the side of not waiting.
  return gain > press_now * window.seconds / now[index].cooldown_seconds;
}

// The best available attack, except a big attack ready just before a buff
// window is saved for it: HoldPays weighs using it inside the window against
// delaying every later use. If an attack is saved, the question moves to the
// runner-up, which is what would actually be used instead.
int CombatSim::BestAttack(const CombatParams& params) const {
  if (queue_.empty()) {
    return -1;  // nothing to hit, so nothing to choose
  }
  std::vector<bool> held;
  int best = TopAttack(params, held);
  ComingWindow window = NextWindow(params);
  // Nothing coming, nothing new in it, or the fight ends before it opens.
  if (best < 0 || window.seconds <= 0.0 || window.mask == buff_mask_ ||
      window.seconds >= SecondsLeft(params)) {
    return best;
  }
  while (best >= 0 && HoldSaves(params, best, window)) {
    held.resize(Attacks(params).size(), false);
    held[best] = true;
    int filler = TopAttack(params, held);
    if (!HoldPays(params, best, filler, window)) {
      return best;
    }
    best = filler;
  }
  return best;
}

// Checks size because the timers grow to fit the params, and an attack queried
// before the first Advance has none yet.
bool CombatSim::Recharging(int index) const {
  return index < static_cast<int>(attack_clocks_.size()) &&
         attack_clocks_[index].cooldown_left > 0.0;
}

// A magazine's attack is unavailable until its buff loads it. The charges
// refill fully each time the buff is raised and disappear when it ends, so zero
// charges means either the buff is down or the charges are spent.
bool CombatSim::Loaded(const CombatParams& params, int index) const {
  const std::vector<AttackOption>& options = Attacks(params);
  if (index >= static_cast<int>(options.size()) ||
      options[index].charges <= 0) {
    return true;
  }
  return index < static_cast<int>(attack_clocks_.size()) &&
         attack_clocks_[index].charges_left > 0;
}

// A charge-based hold is unavailable until a whole charge has filled. Nothing
// else uses charges, so every other attack returns true.
bool CombatSim::Charged(const CombatParams& params, int index) const {
  const std::vector<AttackOption>& options = Attacks(params);
  if (index >= static_cast<int>(options.size()) ||
      options[index].channel.charge_seconds <= 0.0) {
    return true;
  }
  return index < static_cast<int>(attack_clocks_.size()) &&
         attack_clocks_[index].hold_charges >= 1.0;
}

int CombatSim::ChargedPulses(const CombatParams& params, int index) const {
  const std::vector<AttackOption>& options = Attacks(params);
  if (index < 0 || index >= static_cast<int>(options.size())) {
    return 0;
  }
  const ChannelHold& hold = options[index].channel;
  if (hold.charge_seconds <= 0.0 || hold.pulses_per_charge <= 0) {
    return hold.pulses;
  }
  if (index >= static_cast<int>(attack_clocks_.size())) {
    return 0;
  }
  int banked = static_cast<int>(attack_clocks_[index].hold_charges);
  return std::min(hold.pulses, banked * hold.pulses_per_charge);
}

int CombatSim::HealToCast(const CombatParams& params) const {
  // Only mid-fight: an empty map restores HP for free on respawn.
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
  // Index 0 is the basic attack, which is never committed to; see fight.h.
  if (aimed_ > 0 && aimed_ < static_cast<int>(Attacks(params).size()) &&
      Attacks(params)[aimed_].swing_seconds > 0.0 && !queue_.empty()) {
    return aimed_;
  }
  // Checked after the commitment: the heal replaces the next attack rather than
  // interrupting this one.
  int heal = HealToCast(params);
  if (heal >= 0) {
    return heal;
  }
  // Below the heal and above damage: reapplying a lapsed buff is worth more
  // than one more use of the best attack.
  int lay = BuffToLay(params);
  if (lay >= 0) {
    return lay;
  }
  return BestAttack(params);
}

void CombatSim::RunCooldowns(const CombatParams& params, double dt) {
  // Runs even on an empty map, unlike an auto-firing skill's timer: a player
  // waiting for a respawn does have their cooldown back when the mobs appear.
  const std::vector<AttackOption>& options = Attacks(params);
  for (std::size_t i = 0; i < attack_clocks_.size(); ++i) {
    AttackClock& clock = attack_clocks_[i];
    clock.cooldown_left = std::max(0.0, clock.cooldown_left - dt);
    clock.side_cooldown_left = std::max(0.0, clock.side_cooldown_left - dt);
    if (i >= options.size()) {
      continue;
    }
    // Charges fill on an empty map too, for the same reason.
    const ChannelHold& hold = options[i].channel;
    if (hold.charge_seconds > 0.0) {
      clock.hold_charges =
          std::min(static_cast<double>(hold.max_charges),
                   clock.hold_charges + dt / hold.charge_seconds);
    }
    // The timer pauses at max charges, so charges from raising the buff are
    // never topped up, and self-recharging resumes as soon as those are spent.
    const AttackOption& option = options[i];
    if (option.recharge_seconds <= 0.0 ||
        clock.charges_left >= option.recharge_max) {
      clock.load_phase = 0.0;
      continue;
    }
    clock.load_phase += dt;
    while (clock.load_phase >= option.recharge_seconds &&
           clock.charges_left < option.recharge_max) {
      clock.load_phase -= option.recharge_seconds;
      ++clock.charges_left;
    }
  }
}

double CombatSim::Strike(const AttackOption& attack, DamageSource source,
                         int pulses) {
  striking_ = source;
  // One strike hits the front mobs at once, each taking its own type's damage;
  // overkill is wasted.
  int hit = Reached(attack);
  // Picked before anything lands, so the opening hit targets by HP before this
  // attack rather than after the spread.
  std::vector<int> lead = LeadTargets(attack, hit);
  // A piercing attack needs an order, and nothing here has a position, so each
  // attack picks one at random.
  std::vector<int> order = PierceOrder(attack, hit);
  // Also picked up front: which monsters get the extra flames depends on their
  // HP before the attack.
  std::vector<double> shares = ScatterShares(attack, hit);
  // A hold not timed by the attack timer decides its length here.
  int held = attack.channel.pulses > 0
                 ? (pulses >= 0 ? pulses : ChannelPulses(attack, hit))
                 : 0;
  // Before anything lands, so every part of this attack that hits one monster
  // shares one event, which the player sees as one landing.
  ledger_.OpenLandings(queue_.size(), hit, source, attack.credit,
                       std::max(1, held));
  // Computed once for the whole strike: the crowd size belongs to the attack
  // that called the rain, not to each arrow.
  int extra = ExtraLines(attack);
  for (int step = 0; step < hit; ++step) {
    int j = order.empty() ? step : order[step];
    double gain =
        order.empty() ? 1.0 : std::pow(1.0 + attack.pierce_gain_pct, step);
    // The main strike is the first to reach the monster, so it gets the mark
    // bonus; later parts find the mark gone.
    double freeze =
        StateBoost(attack, queue_[j]) * SpendMark(attack, queue_[j]);
    double share = shares.empty() ? 1.0 : shares[j];
    double damage =
        held > 0
            ? ChannelDamage(attack, queue_[j].type, held, LandingAt(j, freeze))
            : DamageToMob(attack, j, LandingAt(j, gain * freeze * share)) *
                  gain;
    // Each rolls separately: the same arrow landing more times, not one
    // stronger arrow.
    for (int line = 0; line < extra; ++line) {
      damage += RolledDamage(*attack.extra_line, queue_[j].type,
                             LandingAt(j, gain * freeze * share)) *
                gain;
    }
    Hurt(queue_[j], damage * freeze * share);
  }
  StrikeRiders(attack, hit, lead);
  double recovered = RollProcs(attack, hit);
  ApplyStates(attack, hit);
  Reap();
  return recovered;
}

void CombatSim::StrikeRiders(const AttackOption& attack, int hit,
                             const std::vector<int>& lead) {
  for (int j : lead) {
    double freeze = StateBoost(attack, queue_[j]);
    double damage =
        attack.lead_damage[queue_[j].type] * Roll(attack.lead_rolls);
    ledger_.RecordRolls(LandingAt(j, freeze),
                        attack.lead_damage[queue_[j].type] * freeze);
    Hurt(queue_[j], damage * freeze);
  }
  // Rolled against every enemy the attack hit.
  riding_ = Rider::kFinalAttack;
  if (!attack.final_attack_damage.empty()) {
    for (int j = 0; j < hit; ++j) {
      double freeze = StateBoost(attack, queue_[j]);
      Hurt(queue_[j], RolledFinalAttack(attack.final_attack_rolls,
                                        attack.final_attack_damage,
                                        queue_[j].type, LandingAt(j, freeze)) *
                          freeze);
    }
  }
  // Rolled once per attack and hitting its own set of enemies: one for
  // Blizzard, ten for Split Shot. The front of the queue works as well as any.
  for (int j = 0; j < PerSwingFinalAttackTargets(attack, hit); ++j) {
    double freeze = StateBoost(attack, queue_[j]);
    Hurt(queue_[j], RolledFinalAttack(attack.per_swing_final_attack_rolls,
                                      attack.per_swing_final_attack_damage,
                                      queue_[j].type, LandingAt(j, freeze)) *
                        freeze);
  }
  riding_ = Rider::kItself;
  // Jupiter Thunder's current arcs to two enemies while the orb hits one. Only
  // if the attack landed: the current arcs off a shock.
  for (int j = 0; j < WideHitTargets(attack, hit); ++j) {
    double freeze = StateBoost(attack, queue_[j]);
    Hurt(queue_[j], RolledGroups(attack.wide_hit_groups, attack.wide_hit_damage,
                                 queue_[j].type, LandingAt(j, freeze)) *
                        freeze);
  }
}

void CombatSim::ApplyStates(const AttackOption& attack, int hit) {
  ApplyDots(attack, hit);
  ApplyFreeze(attack, hit);
  ApplyStun(attack, hit);
  ApplyMark(attack, hit);
  ApplyWound(attack, hit);
  ApplyScar(attack, hit);
}

// Rolled once per attack, as GMS does: an extra helping of the attack's damage
// on one enemy, not a separate hit, rolling its own crit and mastery. It hits
// the front of the queue.
double CombatSim::RollProcs(const AttackOption& attack, int hit) {
  double recovered = 0.0;
  if (hit <= 0) {
    return recovered;
  }
  double boost = StateBoost(attack, queue_[0]);
  for (const ProcRoll& proc : attack.procs) {
    double fired = Chance(proc.chance);
    if (fired <= 0.0) {
      continue;
    }
    Hurt(queue_[0], RolledDamage(attack, queue_[0].type,
                                 LandingAt(0, proc.damage_pct * boost)) *
                        proc.damage_pct * boost * fired);
    recovered += proc.hp_recover_pct * fired;
  }
  return recovered;
}

// Takes a stack count and frozen flag rather than reading them from the
// character and monster, so the chooser can ask what more stacks or a frozen
// enemy would be worth. See FreezeCredit and FrozenCredit.
double CombatSim::BoostForStacks(const AttackOption& attack, int stacks,
                                 int type, bool frozen) const {
  if (stacks <= 0) {
    return 1.0;
  }
  // These multiply rather than add, like all crit and final damage pairs.
  double crit = frozen ? 1.0 + attack.freeze_crit_gain * stacks : 1.0;
  // Stack-based only: GMS requires a frozen enemy for the crit damage but not
  // for the lightning attack's final damage.
  double spent =
      attack.freeze_spends ? 1.0 + attack.freeze_fd_per_stack * stacks : 1.0;
  // Glacial Fury gives attack rather than damage, so it's multiplied in under
  // everything else.
  double matt = 1.0 + attack.freeze_matt_gain * stacks;
  // Shatter differs per mob, since it ignores that monster's defense.
  double shattered =
      frozen && type < static_cast<int>(attack.freeze_ied_gain.size())
          ? 1.0 + attack.freeze_ied_gain[type] * stacks
          : 1.0;
  return crit * spent * matt * shattered;
}

// Three statuses count: frozen, burning and stunned. GMS lists five, but
// nothing here causes the other two; if one is added, it goes in this check.
// See SkillEffect::final_dmg_pct_when_afflicted. In GMS bosses are never
// stunned; they only carry the stun for its bonus.
bool CombatSim::Afflicted(const QueuedMob& mob) const {
  if (mob.frozen_left_seconds > 0.0 ||
      (mob.stunned_left_seconds > 0.0 && !mob.boss)) {
    return true;
  }
  for (const MobDot& burn : mob.dots) {
    if (burn.left_seconds > 0.0 && burn.stacks > 0.0) {
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

// A scar is applied by a line, so an attack scars partway through: the line
// that scars gets nothing, and later lines get the full bonus. Averaged over
// the lines that's 1 - (1 - odds) * (1 - (1 - chance)^n) / (chance * n), which
// gives the full bonus on an already scarred monster.
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

// A monster with two burns counts as two, and eight monsters with one each
// count as eight.
int CombatSim::BurnsAlight() const {
  int alight = 0;
  for (const QueuedMob& mob : queue_) {
    for (const MobDot& burn : mob.dots) {
      if (burn.left_seconds > 0.0 && burn.stacks > 0.0) {
        ++alight;
      }
    }
  }
  return alight;
}

// Base duration plus time added per active burn. Read when raised rather than
// stored, since the burn count changes: Elemental Fury's spirit lasts twice as
// long on a group kept poisoned.
double CombatSim::BuffWindowSeconds(const BuffOption& buff) const {
  if (buff.duration_seconds_per_dot <= 0.0 || buff.dot_count_cap <= 0) {
    return buff.duration_seconds;
  }
  return buff.duration_seconds +
         buff.duration_seconds_per_dot *
             std::min(BurnsAlight(), buff.dot_count_cap);
}

// The same count in stacks, which is what GMS means by a damage-over-time
// stack: a burn with three stacks counts as three.
int CombatSim::BurnStacksAlight() const {
  int alight = 0;
  for (const QueuedMob& mob : queue_) {
    for (const MobDot& burn : mob.dots) {
      if (burn.left_seconds > 0.0) {
        // Measurement can have fractional stacks; GMS counts whole ones.
        alight += static_cast<int>(std::lround(std::max(0.0, burn.stacks)));
      }
    }
  }
  return alight;
}

// Two inputs: whether this monster has a status, and how many burns are on the
// whole group. The group count is what GMS means by "within a certain range",
// so a drain reaches its cap on a map but only what the rotation keeps burning
// on a boss.
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

// Only attacks that benefit get the bonus, which never includes the skill that
// stunned.
double CombatSim::StunBoost(const AttackOption& attack,
                            const QueuedMob& mob) const {
  if (!attack.collects_stun_lift || mob.stunned_left_seconds <= 0.0) {
    return 1.0;
  }
  return 1.0 + mob.stun_lift_pct;
}

double CombatSim::StateBoost(const AttackOption& attack,
                             const QueuedMob& mob) const {
  return FreezeBoost(attack, mob) * ScarBoost(attack, mob) *
         StunBoost(attack, mob) * ConditionBoost(attack, mob);
}

// Nothing has a position, so no monster is nearer than another. An empty
// default when the queue is empty, which reads as an unfrozen type-0 mob.
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

// A hold that grows has two pulse strengths, so this sums two runs.
double CombatSim::HeldPulseDamage(const AttackOption& attack, int type,
                                  int pulses) const {
  const ChannelHold& hold = attack.channel;
  if (type >= static_cast<int>(hold.grown.damage.size())) {
    return pulses * PulseDamage(attack, type);
  }
  int small = std::min(pulses, hold.small_pulses);
  return small * PulseDamage(attack, type) +
         (pulses - small) * hold.grown.damage[type];
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
  // Capped before converting: a monster that never dies needs more pulses than
  // fit in an int.
  auto pulses_for = [&hold](double left, double per_pulse) {
    double need = std::ceil(left / per_pulse);
    return need >= hold.pulses ? hold.pulses : static_cast<int>(need);
  };
  // The hold only needs to bring them within range of the finishing strike;
  // pulses beyond that hit something already dead.
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
    // Not a simple division: the weaker opening pulses come first, and only the
    // HP left after them comes off the stronger pulses.
    double opening = HeldPulseDamage(attack, type, hold.small_pulses) * freeze;
    int need;
    if (hold.grown.damage.empty() || left <= opening) {
      need = pulses_for(left, pulse);
    } else if (type < static_cast<int>(hold.grown.damage.size()) &&
               hold.grown.damage[type] > 0.0) {
      need = hold.small_pulses +
             pulses_for(left - opening, hold.grown.damage[type] * freeze);
    } else {
      need = hold.pulses;
    }
    wanted = std::max(wanted, need);
  }
  return std::clamp(wanted, hold.min_pulses, hold.pulses);
}

double CombatSim::SwingSecondsAgainst(const AttackOption& attack) const {
  if (WoundFull(attack)) {
    return SwingSecondsAgainst(*attack.wound_form);
  }
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
  const ChannelHold& hold = attack.channel;
  bool grows = type < static_cast<int>(hold.grown.damage.size());
  for (int i = 0; i < pulses; ++i) {
    bool grown = grows && i >= hold.small_pulses;
    double pulse = grown ? hold.grown.damage[type] : PulseDamage(attack, type);
    const SwingRolls& rolls =
        grown ? hold.grown.rolls : attack.groups.front().rolls;
    total += pulse * Roll(rolls);
    // Each pulse is its own cast: the hold landing again.
    Landing pulse_landing = landing;
    pulse_landing.cast += i;
    ledger_.RecordRolls(pulse_landing, pulse * landing.scale);
  }
  // Every group after the first is the finishing strike, landed once however
  // long the hold lasted.
  for (std::size_t i = 1; i < attack.groups.size(); ++i) {
    const HitGroup& group = attack.groups[i];
    if (type >= static_cast<int>(group.damage.size())) {
      continue;
    }
    total += group.damage[type] * Roll(group.rolls);
    ledger_.RecordRolls(landing, group.damage[type] * landing.scale);
  }
  return total;
}

double CombatSim::FreezeCredit(const CombatParams& params,
                               const AttackOption& attack) const {
  int room = std::min(FreezeBuilt(attack), FreezeCap(params) - freeze_stacks_);
  if (room <= 0) {
    return 0.0;
  }
  // What the extra stacks gain the next attack: its whole benefit, not just the
  // final damage a lightning attack spends them for. Only attacks actually
  // available count; counting one on cooldown would make ice attacks look worth
  // using for a payoff the chooser can't take. Looks one attack ahead, as far
  // as a greedy chooser sees.
  double best = 0.0;
  int deeper = freeze_stacks_ + room;
  const std::vector<AttackOption>& options = Attacks(params);
  for (int i = 0; i < static_cast<int>(options.size()); ++i) {
    const AttackOption& other = options[i];
    if (!OnOffer(params, i)) {
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

// Freezing changes both the stack bonuses and the has-a-status bonus, so this
// must count both: valued on stacks alone, an ice attack is worthless to a
// character whose only stack consumer is Storm Magic.
//
// Unlike FreezeCredit, this deliberately counts every attack, including those
// on cooldown. It values a status that lasts seconds, not stacks the next press
// spends, and an attack coming off cooldown still finds the ice there. Limiting
// it to available attacks was measured and is worse.
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
  // Only the seconds before this attack comes around again: freeze beyond that
  // would be reapplied anyway.
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

// Seconds the burn in `slot` has left on this monster.
double CombatSim::BurnLeftOn(const QueuedMob& mob, int slot) const {
  if (slot < 0 || slot >= static_cast<int>(mob.dots.size())) {
    return 0.0;
  }
  return mob.dots[slot].stacks > 0.0 ? mob.dots[slot].left_seconds : 0.0;
}

// What one more burning monster is worth per second to the next attack. The
// counterpart of FrozenRate, and likewise counts every attack rather than only
// available ones: the mist is placed to be there when the attack that benefits
// comes up 25 seconds later, and only counting it while that attack is ready
// stops the mist being placed at all. Measured at -0.9% for the branch.
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

// What applying this attack's burns is worth to later attacks: the status and
// burn count the drain effects use, which the burn's own damage doesn't cover.
// Ignite makes Explosion the Fire/Poison mage's best attack, and Explosion
// applies no burns, so a chooser that ignored this would never place the mist.
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

int CombatSim::FreezeBuilt(const AttackOption& attack) const {
  // Based on how many the strike actually hits: a blizzard on one enemy builds
  // more than one spread across ten.
  if (attack.freeze_build_alone > 0 && Reached(attack) == 1) {
    return attack.freeze_build_alone;
  }
  return attack.freeze_build;
}

void CombatSim::CreditFreeze(const CombatParams& params,
                             const AttackOption& attack) {
  int cap = FreezeCap(params);
  if (cap <= 0) {
    return;
  }
  if (attack.freeze_build > 0) {
    freeze_stacks_ = std::min(cap, freeze_stacks_ + FreezeBuilt(attack));
  } else if (attack.freeze_spends) {
    // One stack per line, or the listed rate, minimum one: a strike that spent
    // nothing would get the stack bonus for free every time.
    int lines = std::max(1, attack.lines);
    int spent = std::max(1, lines / std::max(1, attack.freeze_lines_per_spend));
    freeze_stacks_ = std::max(0, freeze_stacks_ - spent);
  }
}

void CombatSim::Hurt(QueuedMob& mob, double damage) {
  // Measured monsters never die: measurement wants the damage rate, and an
  // empty map would measure the respawn timer instead.
  if (!measuring_) {
    mob.hp -= damage;
  }
  view_.damage_this_step += damage;
  damage_dealt_ += damage;
  if (attributing_ >= 0 && attributing_ < static_cast<int>(by_attack_.size())) {
    AttackTally& tally = by_attack_[attributing_];
    tally.damage += damage;
    // Part of the attack's total, not separate from it: the reader wants the
    // attack's total broken down.
    if (riding_ == Rider::kFinalAttack) {
      tally.final_attack_damage += damage;
    } else if (riding_ == Rider::kBurn) {
      tally.burn_damage += damage;
    }
  } else {
    own_clock_damage_ += damage;
    own_clock_by_source_[striking_] += damage;
  }
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
  // Callers read a copy of the mob list made at the end of each step, and this
  // didn't go through a step.
  PublishRoster(params);
}

void CombatSim::Reap() {
  std::vector<QueuedMob> survivors;
  survivors.reserve(queue_.size());
  for (QueuedMob& mob : queue_) {
    if (mob.hp <= 0.0) {
      ++view_.kills_this_step[mob.type];
      ++kills_pending_;
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
      // Rolled per enemy, so a poison only applies to some of the enemies hit.
      // Measurement uses the fraction instead.
      double took = burn.chance < 1.0 ? Chance(burn.chance) : 1.0;
      if (took <= 0.0) {
        continue;
      }
      // Only the duration restarts; the tick timer keeps going, or an attack
      // faster than the interval would refresh the burn before it ever ticked.
      // Stacks are what accumulate.
      MobDot& dot = mob.dots[burn.slot];
      if (dot.left_seconds <= 0.0) {
        dot.phase = 0.0;
        dot.stacks = 0.0;
      }
      dot.stacks = std::min<double>(burn.max_stacks, dot.stacks + took);
      dot.left_seconds =
          took * burn.duration_seconds + (1.0 - took) * dot.left_seconds;
      dot.interval_seconds = burn.interval_seconds;
      dot.damage = burn.damage[mob.type];
      dot.rolls = burn.rolls;
      dot.lit_by = attributing_;
      dot.credit = ledger_.Credit(burn.credit);
    }
  }
}

void CombatSim::ApplyFreeze(const AttackOption& attack, int hit) {
  if (attack.freeze_seconds <= 0.0) {
    return;
  }
  for (int j = 0; j < hit; ++j) {
    // Refreshed, not added to: a monster frozen again is frozen for the full
    // time from now.
    queue_[j].frozen_left_seconds =
        std::max(queue_[j].frozen_left_seconds, attack.freeze_seconds);
  }
}

void CombatSim::ApplyStun(const AttackOption& attack, int hit) {
  if (attack.stun_seconds <= 0.0) {
    return;
  }
  for (int j = 0; j < hit; ++j) {
    // Refreshed like ice. Measurement uses the fraction, like a burn's chance.
    QueuedMob& mob = queue_[j];
    double took = attack.stun_chance < 1.0 ? Chance(attack.stun_chance) : 1.0;
    if (took <= 0.0) {
      continue;
    }
    mob.stunned_left_seconds =
        took * std::max(mob.stunned_left_seconds, attack.stun_seconds) +
        (1.0 - took) * mob.stunned_left_seconds;
    mob.stun_lift_pct = attack.stun_lift_pct;
  }
}

void CombatSim::RunStun(double dt) {
  for (QueuedMob& mob : queue_) {
    mob.stunned_left_seconds = std::max(0.0, mob.stunned_left_seconds - dt);
  }
}

void CombatSim::ApplyMark(const AttackOption& attack, int hit) {
  if (attack.mark_seconds <= 0.0) {
    return;
  }
  for (int j = 0; j < hit; ++j) {
    // Refreshed like the stun: a monster marked again has one mark.
    queue_[j].marked_left_seconds =
        std::max(queue_[j].marked_left_seconds, attack.mark_seconds);
    queue_[j].mark_lift_pct = attack.mark_lift_pct;
  }
}

void CombatSim::RunMark(double dt) {
  for (QueuedMob& mob : queue_) {
    mob.marked_left_seconds = std::max(0.0, mob.marked_left_seconds - dt);
  }
}

void CombatSim::ApplyWound(const AttackOption& attack, int hit) {
  if (attack.wound_stacks <= 0 || hit <= 0 || queue_.empty()) {
    return;
  }
  // GMS picks the target by max HP, not current HP, so a worn-down boss part is
  // still the one wounded.
  int want = 0;
  for (int j = 1; j < hit && j < static_cast<int>(queue_.size()); ++j) {
    if (queue_[j].max_hp > queue_[want].max_hp) {
      want = j;
    }
  }
  // A wound on a different monster removes the old one ("only 1 enemy can
  // receive the wound debuff"); on the same monster it adds stacks.
  if (queue_[want].id != wound_.mob_id) {
    wound_.mob_id = queue_[want].id;
    wound_.stacks = 0;
  }
  wound_.stacks =
      std::min(attack.wound_max_stacks, wound_.stacks + attack.wound_stacks);
  wound_.left_seconds = attack.wound_seconds;
}

void CombatSim::RunWound(double dt) {
  if (wound_.mob_id < 0) {
    return;
  }
  wound_.left_seconds -= dt;
  // Ends when it expires or the monster dies.
  bool standing = false;
  for (const QueuedMob& mob : queue_) {
    if (mob.id == wound_.mob_id) {
      standing = true;
      break;
    }
  }
  if (!standing || wound_.left_seconds <= 0.0) {
    wound_ = WoundState{};
  }
}

bool CombatSim::WoundFull(const AttackOption& attack) const {
  return attack.wound_form != nullptr && attack.wound_max_stacks > 0 &&
         wound_.mob_id >= 0 && wound_.stacks >= attack.wound_max_stacks;
}

// One line consumes the mark and deals that much more, applied as a share of
// the whole attack so the ledger and the monster agree. The share is one line,
// since every line is worth the same on average.
double CombatSim::SpendMark(const AttackOption& attack, QueuedMob& mob) {
  if (!attack.collects_mark_lift || mob.marked_left_seconds <= 0.0) {
    return 1.0;
  }
  mob.marked_left_seconds = 0.0;
  return 1.0 + mob.mark_lift_pct / std::max(1, attack.lines);
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
    // The chance a scar was already there or this attack left one. The timer
    // restarts either way, like freeze.
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
      // Only the time the burn had left, so a burn ending mid-step deals the
      // ticks it was owed and no more.
      double spent = std::min(dt, dot.left_seconds);
      dot.left_seconds -= spent;
      dot.phase += spent;
      attributing_ = dot.lit_by;
      striking_ = {DamageOrigin::kBurn, slot};
      riding_ = Rider::kBurn;
      while (dot.phase >= dot.interval_seconds) {
        dot.phase -= dot.interval_seconds;
        // Each stack ticks for full damage and rolls separately. A fractional
        // stack ticks for its fraction, which only happens when measuring.
        for (double left = dot.stacks; left > 0.0;) {
          double helping = std::min(1.0, left);
          left -= helping;
          Hurt(mob, dot.damage * helping * Roll(dot.rolls));
          // Each tick is its own landing, between attacks.
          ledger_.RecordRolls(
              ledger_.StandAlone(mob.id, {DamageOrigin::kBurn, slot},
                                 dot.credit),
              dot.damage * helping);
        }
        burned = true;
      }
    }
  }
  // Burns kill like attacks do. Skipped when nothing ticked, since scanning the
  // queue costs more than the burn did.
  if (burned) {
    Reap();
  }
  attributing_ = -1;
  riding_ = Rider::kItself;
}

void CombatSim::RunRegen(const CombatParams& params, double dt) {
  regen_phase_.resize(params.regen_pulses.size(), 0.0);
  for (int i = 0; i < static_cast<int>(params.regen_pulses.size()); ++i) {
    const RegenPulse& pulse = params.regen_pulses[i];
    if (pulse.interval_seconds <= 0.0) {
      continue;
    }
    regen_phase_[i] += dt;
    // A step longer than the interval gets every pulse it covered.
    while (regen_phase_[i] >= pulse.interval_seconds) {
      regen_phase_[i] -= pulse.interval_seconds;
      player_hp_ =
          std::min(static_cast<double>(params.max_player_hp),
                   player_hp_ + pulse.hp + pulse.pct * params.max_player_hp);
    }
  }
}

void CombatSim::RunEmergencyHeal(const CombatParams& params, double dt) {
  const EmergencyHeal& heal = params.emergency_heal;
  if (heal.pct <= 0.0 || params.max_player_hp <= 0) {
    return;
  }
  emergency_cooldown_left_ = std::max(0.0, emergency_cooldown_left_ - dt);
  // Triggered by the hit that drops HP under the threshold, never by a killing
  // hit: that's what revives are for.
  if (emergency_left_ <= 0.0 && emergency_cooldown_left_ <= 0.0 &&
      player_hp_ > 0.0 && player_hp_ < heal.threshold * params.max_player_hp) {
    emergency_left_ = heal.seconds;
    emergency_cooldown_left_ = heal.cooldown_seconds;
  }
  if (emergency_left_ <= 0.0) {
    return;
  }
  double poured = std::min(dt, emergency_left_);
  emergency_left_ -= poured;
  player_hp_ = std::min(static_cast<double>(params.max_player_hp),
                        player_hp_ + poured * heal.pct * params.max_player_hp);
}

double CombatSim::Roll(const SwingRolls& rolls) {
  return measuring_ ? 1.0 : RollFactor(rolls, rng_, ledger_.LineSink());
}

double CombatSim::Chance(double chance) {
  if (measuring_) {
    return std::clamp(chance, 0.0, 1.0);
  }
  return std::bernoulli_distribution(chance)(rng_) ? 1.0 : 0.0;
}

double CombatSim::RolledDamage(const AttackOption& attack, int type,
                               const Landing& landing) {
  if (attack.groups.empty()) {
    ledger_.RecordLine(landing, attack.damage_per_hit[type] * landing.scale,
                       false);
    return attack.damage_per_hit[type];
  }
  double total = 0.0;
  for (const HitGroup& group : attack.groups) {
    if (type < static_cast<int>(group.damage.size())) {
      total += group.damage[type] * Roll(group.rolls);
      ledger_.RecordRolls(landing, group.damage[type] * landing.scale);
    }
  }
  return total;
}

// Every group rolls its own mastery and crits. `expected` is used when nothing
// rolls, as with hand-built test attacks.
double CombatSim::RolledGroups(const std::vector<HitGroup>& groups,
                               const std::vector<double>& expected, int type,
                               const Landing& landing) {
  if (groups.empty()) {
    ledger_.RecordLine(landing, expected[type] * landing.scale, false);
    return expected[type];
  }
  double total = 0.0;
  for (const HitGroup& group : groups) {
    if (type < static_cast<int>(group.damage.size())) {
      total += group.damage[type] * Roll(group.rolls);
      ledger_.RecordRolls(landing, group.damage[type] * landing.scale);
    }
  }
  return total;
}

double CombatSim::RolledFinalAttack(const std::vector<FinalAttackRoll>& sources,
                                    const std::vector<double>& expected,
                                    int type, const Landing& landing) {
  if (sources.empty()) {
    ledger_.RecordLine(landing, expected[type] * landing.scale, false);
    return expected[type];
  }
  double total = 0.0;
  for (const FinalAttackRoll& source : sources) {
    if (type >= static_cast<int>(source.damage.size())) {
      continue;
    }
    // The attack's cast, credited to the Final Attack's own name.
    Landing filed = landing;
    filed.credit = ledger_.Credit(source.credit);
    // A chance above 1 means that many guaranteed hits plus a roll for the
    // remainder.
    int certain = static_cast<int>(source.chance);
    for (int roll = 0; roll < source.count; ++roll) {
      double hits = certain + Chance(source.chance - certain);
      if (hits <= 0.0) {
        continue;
      }
      total += source.damage[type] * hits * Roll(source.rolls);
      ledger_.RecordRolls(filed, source.damage[type] * hits * landing.scale);
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
  // Count before checking, like FormToLand: a mark of five triggers on the
  // fifth hit.
  if (++queue_[index].brand < attack.empowered_every) {
    return ordinary;
  }
  queue_[index].brand = 0;
  // Added on top of the hit, not replacing it: a triggered mark is its own
  // event, while an empowered attack replaces the attack.
  return ordinary + RolledDamage(*attack.empowered, type, landing);
}

const AttackOption& CombatSim::FormToLand(int& count,
                                          const AttackOption& attack) {
  // A wound form replaces the press without counting: the wound decides, not an
  // attack count.
  if (WoundFull(attack)) {
    return *attack.wound_form;
  }
  // A marking form never replaces the attack; DamageToMob decides per mob what
  // triggers on top.
  if (attack.empowered == nullptr || attack.empowered_every <= 0 ||
      attack.brands_enemies) {
    return attack;
  }
  // Count before checking: a period of four is three normal attacks and then
  // this one.
  if (++count < attack.empowered_every) {
    return attack;
  }
  count = 0;
  return *attack.empowered;
}

void CombatSim::GoIdle() {
  view_.ClearPicture();
  wound_ = WoundState{};
  initialized_ = false;
  respawning_ = false;
  reach_ = 1;
  player_hp_ = 0.0;
  player_level_ = 0;
  hit_phase_ = 0.0;
  auto_clocks_.clear();
  attack_clocks_.clear();
  regen_phase_.clear();
  owed_casts_.clear();
  damage_dealt_ = 0.0;
  fight_seconds_ = 0.0;
  aimed_ = -1;
}

void CombatSim::BeginMapIfChanged(const CombatParams& params) {
  // The queue's type indices and HP belong to one map; carried over to another,
  // both describe the wrong monsters.
  if (initialized_ && encounter_ == params.encounter) {
    return;
  }
  encounter_ = params.encounter;
  respawn_phase_ = 0.0;
  respawn_interval_ = params.respawn_seconds;
  attack_phase_ = 0.0;
  owed_casts_.clear();
  hit_phase_ = 0.0;
  // A barrage belongs to the fight it was fired in; bolts in the air don't
  // follow the player.
  for (AttackClock& clock : attack_clocks_) {
    clock.strikes_left = 0;
    clock.next_strike_seconds = 0.0;
  }
  next_mob_id_ = 0;
  // The damage rate belongs to the encounter: the last map's damage says
  // nothing about how long this fight will take.
  damage_dealt_ = 0.0;
  fight_seconds_ = 0.0;
  // Timers the character carries (cooldowns, casts, buffs, regen) are kept,
  // since they belong to the character, not the mobs. A boss phase is also a
  // new encounter and must not restart with everything ready.
  aimed_ = -1;
  player_hp_ = params.max_player_hp;
  queue_.clear();
  TopUp(params);
  initialized_ = true;
}

void CombatSim::RespawnBeat(const CombatParams& params, double dt) {
  if (params.respawn_seconds <= 0.0) {
    return;  // nothing more is coming; see CombatParams::respawn_seconds
  }
  respawn_phase_ += dt;
  if (respawn_phase_ < respawn_interval_) {
    return;
  }
  respawn_phase_ -= respawn_interval_;
  // The next wait uses the current params: a totem placed mid-cycle shortens
  // the next respawn, not the one in progress.
  respawn_interval_ = params.respawn_seconds;
  view_.respawned_this_step = true;
  bool was_idle = queue_.empty();
  TopUp(params);
  // Every respawn restores some HP, whether or not the map was cleared. It's
  // the only healing there is.
  player_hp_ =
      std::min(static_cast<double>(params.max_player_hp),
               player_hp_ + params.beat_heal_fraction * params.max_player_hp);
  if (!was_idle) {
    // If mobs arrive mid-fight, the attack being charged continues: restarting
    // it would throw away real progress.
    return;
  }
  // Clearing the map is a bigger break, and restores full HP.
  attack_phase_ = 0.0;
  owed_casts_.clear();
  player_hp_ = params.max_player_hp;
  hit_phase_ = 0.0;
}

// Reductions multiply rather than add, like every other reduction in the game:
// two 50% reductions leave 25%. Uses the buffs active at the start of the step,
// so a smokescreen raised after the hit doesn't reduce it.
double CombatSim::BuffDamageTakenFactor(const CombatParams& params) const {
  // Shields block whole hits, so the only reduction here is the one a shield
  // gives against bosses instead; see BlockHit.
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
  // A hold's damage reduction lasts exactly as long as the hold.
  const std::vector<AttackOption>& options = Attacks(params);
  if (aimed_ >= 0 && aimed_ < static_cast<int>(options.size())) {
    factor *= 1.0 - options[aimed_].channel.damage_taken_pct;
  }
  return std::max(0.0, factor);
}

// Only one shield blocks each hit, however many are up, so two blocks aren't
// spent on one hit. Boss hits are never blocked (GMS exempts attacks that deal
// a percentage of max HP); a shield reduces them instead, in
// BuffDamageTakenFactor.
bool CombatSim::BlockHit(const CombatParams& params) {
  if (queue_.empty() || params.types[queue_.front().type].mob->boss()) {
    return false;
  }
  for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
    if ((buff_mask_ & (1 << i)) == 0 || buffs_[i].blocks_left <= 0) {
      continue;
    }
    if (--buffs_[i].blocks_left == 0) {
      buffs_[i].left = 0.0;  // used up: the shield ends regardless of time left
      buff_mask_ &= ~(1 << i);
    }
    return true;
  }
  return false;
}

void CombatSim::TakeMobHit(const CombatParams& params, double dt) {
  // Only the front mob attacks, and it hits first, so the last one alive still
  // gets its hit in. On an empty map the timer waits rather than saving up a
  // free hit for the next arrival.
  if (queue_.empty() || params.hit_seconds <= 0.0) {
    hit_phase_ = 0.0;
    return;
  }
  hit_phase_ += dt;
  if (hit_phase_ < params.hit_seconds) {
    return;
  }
  hit_phase_ -= params.hit_seconds;
  // A frozen monster can't attack. The timer keeps running, so a long freeze
  // skips several hits rather than saving them up.
  if (queue_.front().frozen_left_seconds > 0.0) {
    return;
  }
  if (BlockHit(params)) {
    return;  // fully blocked: no damage taken, nothing to reflect
  }
  // The scar is a probability rather than a flag, so the hit is the two damage
  // values weighted by it.
  const CombatType& type = params.types[queue_.front().type];
  double odds = queue_.front().scar_odds;
  double taken = (type.damage_to_player * (1.0 - odds) +
                  type.damage_to_player_scarred * odds) *
                 BuffDamageTakenFactor(params);
  player_hp_ = std::max(0.0, player_hp_ - taken);
  view_.died_this_step = player_hp_ <= 0.0 && !Revive(params);
  Reflect(params, taken);
}

bool CombatSim::Revive(const CombatParams& params) {
  if (params.revive_cooldown_seconds <= 0.0 || revive_left_ > 0.0) {
    return false;
  }
  // Full HP, standing where they fell: the mob that hit them is still in front
  // of them.
  player_hp_ = params.max_player_hp;
  revive_left_ = params.revive_cooldown_seconds;
  return true;
}

void CombatSim::Reflect(const CombatParams& params, double damage_taken) {
  // Based on the whole hit, not just the HP a dying player had left.
  if (params.damage_reflect_pct <= 0.0 || queue_.empty()) {
    return;
  }
  QueuedMob& front = queue_.front();
  // Nothing attacked to cause it, so it's credited to index -1, which isn't an
  // entry in any attack list.
  striking_ = {DamageOrigin::kOwnClock, -1};
  Hurt(front, params.damage_reflect_pct * damage_taken);
  if (front.hp > 0.0) {
    return;
  }
  ++view_.kills_this_step[front.type];
  ++kills_pending_;
  queue_.erase(queue_.begin());
}

// Damage tables use the lever mask: what is granting right now. Whether a buff
// is up at all is buff_mask_.
const std::vector<AttackOption>& CombatSim::Attacks(
    const CombatParams& params) const {
  return params.Attacks(lever_mask_);
}

int CombatSim::FreezeCap(const CombatParams& params) const {
  return params.FreezeCap(lever_mask_);
}

const std::vector<AttackOption>& CombatSim::AutoAttacks(
    const CombatParams& params) const {
  return params.AutoAttacks(lever_mask_);
}

const std::vector<AttackOption>& CombatSim::TriggeredAttacks(
    const CombatParams& params) const {
  return params.TriggeredAttacks(lever_mask_);
}

// The one buff held back rather than raised as soon as it's ready, because it
// both heals and shields, which want opposite timing. On a map it waits until
// HP is low enough to be worth healing; on a boss one hit can decide the fight,
// so it goes up at once.
bool CombatSim::ShieldWanted(const CombatParams& params,
                             const BuffOption& buff) const {
  // Nothing hits the player while measuring, so waiting for low HP would wait
  // forever and its bonuses would never apply.
  if (buff.shield_hits <= 0 || params.measuring) {
    return true;
  }
  if (!queue_.empty() && params.types[queue_.front().type].mob->boss()) {
    return true;
  }
  return player_hp_ < kHealBelowFraction * params.max_player_hp;
}

namespace {

// Whether an active buff is granting right now: always, for most buffs; four
// seconds in five for the angel. See duty_seconds.
bool Granting(const BuffOption& buff, double duty_phase) {
  if (buff.duty_seconds <= 0.0 || buff.duty_interval_seconds <= 0.0) {
    return true;
  }
  return std::fmod(duty_phase, buff.duty_interval_seconds) < buff.duty_seconds;
}

// Seconds until a bursting buff next starts or stops granting.
double NextDutyEdge(const BuffOption& buff, double duty_phase) {
  if (buff.duty_seconds <= 0.0 || buff.duty_interval_seconds <= 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  double phase = std::fmod(duty_phase, buff.duty_interval_seconds);
  return phase < buff.duty_seconds ? buff.duty_seconds - phase
                                   : buff.duty_interval_seconds - phase;
}

}  // namespace

void CombatSim::RunBuffs(const CombatParams& params, double dt) {
  int count = static_cast<int>(params.buffs.size());
  // Start each buff at its full charge requirement, or a hit-charged buff would
  // go up before any hits landed.
  if (static_cast<int>(buffs_.size()) != count) {
    buffs_.resize(count);
    for (int i = 0; i < count; ++i) {
      buffs_[i].charge_left = params.buffs[i].charge_lines;
    }
  }
  // Runs before building the mask, so a stack that ended last step doesn't
  // leave a gap in its group: active windows are always first, which keeps it
  // to one damage table per count rather than one per arrangement.
  CompactRolledWindows(params);
  buff_mask_ = 0;
  lever_mask_ = 0;
  for (int i = 0; i < count; ++i) {
    const BuffOption& buff = params.buffs[i];
    BuffClock& clock = buffs_[i];
    clock.left = std::max(0.0, clock.left - dt);
    clock.cooldown_left = std::max(0.0, clock.cooldown_left - dt);
    clock.duty_phase += dt;
    // Raised as soon as it's ready, but only with something to fight: a buff
    // used on an empty map is missing when the mobs arrive. Never recast while
    // active. A buff applied by its own attack waits for that attack; see
    // LayBuffs.
    //
    // Ready means either its cooldown is done or enough hits have landed.
    bool ready = buff.charge_lines > 0 ? clock.charge_left <= 0.0
                                       : clock.cooldown_left <= 0.0;
    if (buff.laid_by_attack < 0 && buff.raise_chance <= 0.0 &&
        clock.left <= 0.0 && ready && !queue_.empty() &&
        buff.duration_seconds > 0.0 && ShieldWanted(params, buff)) {
      // Chosen once and never revisited: a sword planted for two minutes stays
      // planted however the fight goes.
      clock.stance = StanceToRaise(params, buff);
      clock.left = clock.stance < 0
                       ? BuffWindowSeconds(buff)
                       : buff.stances[clock.stance].duration_seconds;
      clock.cooldown_left = buff.cooldown_seconds;
      clock.charge_left = buff.charge_lines;
      clock.blocks_left = buff.shield_hits;
      clock.duty_phase = 0.0;
      // Refill the charges fully; leftover charges from the last raise don't
      // carry over.
      if (buff.magazine_attack >= 0 &&
          buff.magazine_attack < static_cast<int>(attack_clocks_.size())) {
        attack_clocks_[buff.magazine_attack].charges_left =
            Attacks(params)[buff.magazine_attack].charges;
      }
      // Raising it costs its cast time, taken from the attack being charged: a
      // buff is cast instead of attacking, not alongside it. The charge bar
      // draws the cast over the resulting debt.
      if (buff.cast_seconds > 0.0) {
        owed_casts_.push_back({buff.name, attack_phase_, buff.cast_seconds});
      }
      attack_phase_ -= buff.cast_seconds;
      player_hp_ =
          std::min(static_cast<double>(params.max_player_hp),
                   player_hp_ + buff.heal_fraction * params.max_player_hp);
    }
    if (clock.left > 0.0) {
      buff_mask_ |= 1 << i;
      if (Granting(buff, clock.duty_phase)) {
        lever_mask_ |= 1 << i;
      }
      continue;
    }
    // Ended, so any charges it provided go with it.
    if (buff.magazine_attack >= 0 &&
        buff.magazine_attack < static_cast<int>(attack_clocks_.size())) {
      attack_clocks_[buff.magazine_attack].charges_left = 0;
    }
  }
}

double CombatSim::SecondsLeft(const CombatParams& params) const {
  // A map refills on respawn, so there's no end to measure against, and a
  // summon is worth its rate rather than its total. Measurement is deliberately
  // the same, since its monsters never die.
  if (params.respawn_seconds > 0.0 || params.measuring) {
    return std::numeric_limits<double>::infinity();
  }
  double standing = 0.0;
  for (const QueuedMob& mob : queue_) {
    standing += mob.hp;
  }
  // Use the measured rate once enough of the fight has run, and the params'
  // estimate before that: a buff raised on the first step still needs an
  // answer.
  double rate = fight_seconds_ >= 1.0 && damage_dealt_ > 0.0
                    ? damage_dealt_ / fight_seconds_
                    : params.reference_dps;
  if (rate <= 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  return standing / rate;
}

int CombatSim::StanceToRaise(const CombatParams& params,
                             const BuffOption& buff) const {
  if (buff.stances.empty()) {
    return -1;
  }
  double left = SecondsLeft(params);
  // Deliberately uses the unbuffed table: comparing two forms of one skill,
  // every shared multiplier cancels. A buffed table would also mean reading a
  // mask that is still being built, since this runs inside that loop.
  const std::vector<AttackOption>& casts = params.auto_attacks;
  int best = 0;
  double best_damage = -1.0;
  for (int i = 0; i < static_cast<int>(buff.stances.size()); ++i) {
    const StanceOption& form = buff.stances[i];
    if (form.pulse_attack < 0 ||
        form.pulse_attack >= static_cast<int>(casts.size()) ||
        form.pulse_interval_seconds <= 0.0) {
      continue;
    }
    const AttackOption& pulse = casts[form.pulse_attack];
    if (pulse.damage_per_hit.empty()) {
      continue;
    }
    // Damage the form deals before the fight ends: its rate over whichever runs
    // out first, its duration or the encounter. A short, intense form wins any
    // fight that ends before a long, weak one has finished paying out.
    double seconds = std::min(form.duration_seconds, left);
    double damage = pulse.damage_per_hit[0] * pulse.strikes_per_pulse /
                    form.pulse_interval_seconds * seconds;
    if (damage > best_damage) {
      best_damage = damage;
      best = i;
    }
  }
  return best;
}

bool CombatSim::LayBuffs(const CombatParams& params, int swung, bool on_cast) {
  bool laid = false;
  for (int i = 0; i < static_cast<int>(buffs_.size()); ++i) {
    const BuffOption& buff = params.buffs[i];
    if (buff.laid_by_attack != swung || buff.raised_on_cast != on_cast) {
      continue;
    }
    // Trickblade's invulnerability comes only from the heavier form.
    if (buff.needs_wound_form && !WoundFull(Attacks(params)[swung])) {
      continue;
    }
    // Refreshed rather than stacked: a second puncture leaves one wound.
    buffs_[i].left = BuffWindowSeconds(buff);
    buffs_[i].cooldown_left = buff.cooldown_seconds;
    // Refill the charges fully, as RunBuffs does.
    if (buff.magazine_attack >= 0 &&
        buff.magazine_attack < static_cast<int>(attack_clocks_.size())) {
      attack_clocks_[buff.magazine_attack].charges_left =
          Attacks(params)[buff.magazine_attack].charges;
    }
    // The mask is built before any attacks, so a buff raised mid-attack must
    // update it here or the strike won't include it.
    buffs_[i].duty_phase = 0.0;
    buff_mask_ |= 1 << i;
    lever_mask_ |= 1 << i;
    laid = true;
  }
  return laid;
}

void CombatSim::RaiseRolledBuffs(const CombatParams& params, bool afflicted) {
  int count = static_cast<int>(params.buffs.size());
  for (int first = 0; first < count;) {
    const BuffOption& buff = params.buffs[first];
    // A buff's windows are adjacent, and a stacking buff's are identical. One
    // attack raises at most one of them, so iterate over groups rather than
    // windows.
    int end = first + 1;
    while (end < count && params.buffs[end].name == buff.name) {
      ++end;
    }
    if (buff.raise_chance > 0.0 && buff.duration_seconds > 0.0 &&
        (!buff.needs_afflicted_target || afflicted)) {
      RaiseOneWindow(params, first, end);
    }
    first = end;
  }
}

void CombatSim::RaiseOneWindow(const CombatParams& params, int first, int end) {
  const BuffOption& buff = params.buffs[first];
  int free_slot = -1;
  for (int i = first; i < end && free_slot < 0; ++i) {
    if (buffs_[i].left <= 0.0 && buffs_[i].cooldown_left <= 0.0) {
      free_slot = i;
    }
  }
  // Nothing is gained when all stacks are active: each lasts its own full time
  // rather than the newest replacing the oldest.
  if (free_slot < 0 || Chance(buff.raise_chance) <= 0.0) {
    return;
  }
  buffs_[free_slot].left = BuffWindowSeconds(buff);
  buffs_[free_slot].cooldown_left = buff.cooldown_seconds;
  CompactRolledWindows(params);
  // The mask is built before any attacks, so a buff raised mid-attack must
  // update it here, as LayBuffs does.
  for (int i = first; i < end; ++i) {
    if (buffs_[i].left > 0.0) {
      buff_mask_ |= 1 << i;
      lever_mask_ |= 1 << i;
    }
  }
}

void CombatSim::CompactRolledWindows(const CombatParams& params) {
  int count = static_cast<int>(params.buffs.size());
  for (int first = 0; first < count;) {
    int end = first + 1;
    while (end < count && params.buffs[end].name == params.buffs[first].name) {
      ++end;
    }
    // Only for buffs gained by chance in stacks: a staged buff's windows are
    // deliberately staggered, and sorting would lose that.
    if (params.buffs[first].raise_chance > 0.0) {
      std::sort(buffs_.begin() + first, buffs_.begin() + end,
                [](const BuffClock& a, const BuffClock& b) {
                  return a.left > b.left;
                });
    }
    first = end;
  }
}

// Chosen ahead of the best attack, deliberately without comparing: a lapsed
// wound boosts every attack while it lasts, and costs one attack in a hundred.
// A buff worth less than the attack it replaces would be overused here; add a
// rate check when the first skill needs one.
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
      continue;  // still active, so nothing to do
    }
    // Never pursued directly: it comes with an attack the fight makes for
    // damage anyway, and pursuing it would waste Trickblade on 1.8 seconds of
    // shelter.
    if (buff.needs_wound_form) {
      continue;
    }
    // On cooldown, so it can't be applied this time.
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
    // A hit-charged buff only charges while it's down, as in GMS, so its uptime
    // is limited however fast the character attacks.
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

// Heals a fraction of max HP, capped at full.
void CombatSim::RecoverHp(const CombatParams& params, double share) {
  if (share <= 0.0) {
    return;
  }
  player_hp_ = std::min(static_cast<double>(params.max_player_hp),
                        player_hp_ + share * params.max_player_hp);
}

void CombatSim::RunAutoCasts(const CombatParams& params, double dt) {
  // These timers only run with something to hit: waiting on an empty map
  // doesn't give a summon a free cast.
  const std::vector<AttackOption>& casts = AutoAttacks(params);
  auto_clocks_.resize(casts.size());
  for (int i = 0; i < static_cast<int>(casts.size()); ++i) {
    const AttackOption& cast = casts[i];
    AutoClock& clock = auto_clocks_[i];
    if (queue_.empty() || cast.interval_seconds <= 0.0) {
      continue;
    }
    // The timer is paused rather than advanced, so the pulse doesn't fire the
    // instant its buff goes up and again right after. The pulse count resets
    // too, since it's per raise.
    if (cast.needs_buff >= 0 && (buff_mask_ & (1 << cast.needs_buff)) == 0) {
      clock.pulses = 0;
      continue;
    }
    // Dismissed by another skill's summon: it resumes on the same timing it
    // left with.
    if (cast.silenced_by_buff >= 0 &&
        (buff_mask_ & (1 << cast.silenced_by_buff)) != 0) {
      continue;
    }
    // Only the raised form pulses; the other waits.
    if (cast.needs_buff_stance >= 0 &&
        buffs_[cast.needs_buff].stance != cast.needs_buff_stance) {
      clock.pulses = 0;
      continue;
    }
    // Once it has used its pulses for this raise, it stops until the next, so a
    // longer buff duration doesn't add pulses.
    if (cast.max_pulses > 0 && clock.pulses >= cast.max_pulses) {
      continue;
    }
    clock.phase += dt;
    // A step longer than the interval gets every cast it covered.
    while (clock.phase >= cast.interval_seconds) {
      clock.phase -= cast.interval_seconds;
      ++clock.pulses;
      const AttackOption& landed =
          RepeatForm(FormToLand(clock.empowered_count, cast), clock.pulses);
      // Every strike lands in full: three sword strikes 60ms apart happen at
      // one moment here, each hitting its own enemies.
      for (int strike = 0; strike < cast.strikes_per_pulse; ++strike) {
        Strike(landed, {DamageOrigin::kOwnClock, i});
        // Per strike, like the damage: Darkness Aura heals for every attack the
        // aura makes.
        RecoverHp(params, landed.hp_recover_pct);
      }
      // Elquines freezes what it hits. It never spends stacks; ClearSwingRiders
      // ensures that.
      CreditFreeze(params, landed);
      if (cast.max_pulses > 0 && clock.pulses >= cast.max_pulses) {
        // The poison explodes as it ends: one more explosion at the strongest
        // form, landing with the last tick.
        if (cast.final_repeat_strike) {
          Strike(RepeatForm(cast, cast.max_pulses),
                 {DamageOrigin::kOwnClock, i});
        }
        // The scroll bursts as it ends, landing with the last tick for the same
        // reason.
        if (cast.final_strike != nullptr) {
          for (int strike = 0; strike < cast.final_strike->strikes_per_pulse;
               ++strike) {
            Strike(*cast.final_strike, {DamageOrigin::kOwnClock, i});
          }
        }
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
    // Something that only fires while its buff is down doesn't count attacks
    // while the buff is up. The count so far is kept.
    if (cast.silent_while_buff && cast.needs_buff >= 0 &&
        (buff_mask_ & (1 << cast.needs_buff)) != 0) {
      continue;
    }
    trigger_count_[i] += weight;
    // An attack worth more than the whole count fires the skill once per
    // multiple. The epsilon is needed because 1/7 isn't exact: 28 sevenths land
    // just under 4, which would fire one attack late every time.
    while (trigger_count_[i] + kCountEpsilon >= cast.attacks_per_cast) {
      trigger_count_[i] -= cast.attacks_per_cast;
      // Every strike lands in full, like pulses: the afterimage's second of
      // shooting happens at one moment here.
      for (int strike = 0; strike < cast.strikes_per_pulse; ++strike) {
        Strike(cast, {DamageOrigin::kSwingClock, i});
      }
    }
  }
}

void CombatSim::CreditKills(const CombatParams& params) {
  // Taken before anything fires: kills from these casts count toward the next
  // one.
  int defeated = kills_pending_;
  kills_pending_ = 0;
  const std::vector<AttackOption>& casts = TriggeredAttacks(params);
  kill_count_.resize(casts.size(), 0);
  for (int i = 0; i < static_cast<int>(casts.size()); ++i) {
    const AttackOption& cast = casts[i];
    if (cast.kills_per_cast <= 0) {
      continue;
    }
    kill_count_[i] += defeated;
    // A wide attack can kill more than the whole count in one step, and each
    // multiple fires once. The remainder carries over.
    while (kill_count_[i] >= cast.kills_per_cast) {
      kill_count_[i] -= cast.kills_per_cast;
      Strike(cast, {DamageOrigin::kKillClock, i});
    }
  }
}

const AttackOption* CombatSim::AimSwing(const CombatParams& params) {
  int previous = aimed_;
  aimed_ = ChooseAttack(params);
  const AttackOption* attack = aimed_ >= 0 ? &Attacks(params)[aimed_] : nullptr;
  // The charge bar names the form actually being charged, or it would count
  // down the wrong timer.
  view_.attack_name =
      attack != nullptr
          ? (WoundFull(*attack) ? attack->wound_form->name : attack->name)
          : "";
  if (attack != nullptr) {
    // Decided once, when the attack is first aimed: a hold in progress is the
    // player's key held down, and re-deciding it every step would flicker.
    if (aimed_ != previous) {
      held_pulses_ =
          ChannelPulses(*attack, std::min(std::max(1, attack->max_enemies),
                                          static_cast<int>(queue_.size())));
      // The only place charges shorten a hold: the chooser values a hold by its
      // rate, which its length doesn't change.
      if (attack->channel.charge_seconds > 0.0) {
        held_pulses_ = std::min(held_pulses_, ChargedPulses(params, aimed_));
      }
    }
    // A cast hits nobody, so the engaged window keeps the last attack's width
    // and the mob bars don't collapse during a cast.
    if (attack->heal_fraction <= 0.0) {
      reach_ = std::max(1, attack->max_enemies);
    }
    // Cached because the charge bar is drawn after aiming and has no attack to
    // query. If the choice changes mid-charge, the timer changes with it, which
    // is accurate.
    swing_seconds_ = HeldSeconds(*attack);
  }
  return attack;
}

void CombatSim::RunBarrage(const CombatParams& params, double dt) {
  int clocks = static_cast<int>(attack_clocks_.size());
  int options = static_cast<int>(Attacks(params).size());
  for (int i = 0; i < std::min(clocks, options); ++i) {
    if (attack_clocks_[i].strikes_left > 0) {
      RunBarrageOf(params, i, dt);
    }
  }
}

void CombatSim::RunBarrageOf(const CombatParams& params, int index, double dt) {
  // Looked up by index, not pointer: a buff going up between two bolts switches
  // the fight to another attack table, where the index is still valid.
  const AttackOption& attack = Attacks(params)[index];
  AttackClock& clock = attack_clocks_[index];
  clock.next_strike_seconds -= dt;
  // A step longer than the interval gets every strike it covered.
  while (clock.strikes_left > 0 && clock.next_strike_seconds <= 0.0) {
    clock.next_strike_seconds += attack.cast_interval_seconds;
    --clock.strikes_left;
    // A shock that finds no target doesn't use the orb, and GMS refunds its
    // cooldown.
    if (Reached(attack) <= 0) {
      clock.cooldown_left =
          std::max(0.0, clock.cooldown_left - attack.cooldown_refund_seconds);
      continue;
    }
    attributing_ = index;
    RecoverHp(params, Strike(attack, {DamageOrigin::kSwing, 0}));
    attributing_ = -1;
    CreditFreeze(params, attack);
    // For hit-charged buffs. No weight: the cooldown reduction for a landed
    // attack was applied at the cast, and a bolt isn't another attack.
    CreditBuffs(params, 0.0, attack.lines);
  }
}

void CombatSim::RunSwing(const CombatParams& params, double dt) {
  // Aimed at the current queue, so the charge bar names the attack actually
  // coming. Only the basic attack is re-aimed when mobs die.
  const AttackOption* attack = AimSwing(params);
  if (attack == nullptr) {
    return;
  }
  attack_phase_ += dt;
  // A step longer than the attack gets every attack it covered. Without this, a
  // 120ms skill with 150ms frames loses one attack in five and the charge bar
  // stays full, since the phase never drops below one attack.
  while (attack != nullptr && attack->swing_seconds > 0.0 &&
         attack_phase_ >= HeldSeconds(*attack)) {
    attack_phase_ -= HeldSeconds(*attack);
    LandSwing(params, *attack);
    // Landing re-aims: the queue changed and the commitment is over.
    attack = aimed_ >= 0 ? &Attacks(params)[aimed_] : nullptr;
  }
}

void CombatSim::LandSwing(const CombatParams& params,
                          const AttackOption& attack) {
  // Read before the strike, since re-aiming below changes it.
  int swung = aimed_;
  // Everything this attack deals is credited to it, including its side strike
  // and stored attack: they come with it rather than happening separately.
  attributing_ = swung;
  if (swung >= 0 && swung < static_cast<int>(by_attack_.size())) {
    ++by_attack_[swung].swings;
  }
  if (attack.heal_fraction > 0.0) {
    player_hp_ =
        std::min(static_cast<double>(params.max_player_hp),
                 player_hp_ + attack.heal_fraction * params.max_player_hp);
  } else {
    // A buff GMS grants "upon use" goes up before its own attack lands, so the
    // strike includes it and the attack is re-read from the new buff set. All
    // other buffs wait for the landing.
    //
    // `afflicted` is read before the strike, since the mob it checks may die.
    // See Buff::needs_afflicted_target.
    bool afflicted = !queue_.empty() && Afflicted(queue_.front());
    const AttackOption* cast = &attack;
    if (LayBuffs(params, swung, /*on_cast=*/true)) {
      cast = &Attacks(params)[swung];
    }
    const AttackOption& landed =
        FormToLand(attack_clocks_[swung].empowered_count, *cast);
    // A barrage of bolts strikes once per bolt, so dead mobs are cleared
    // between them and a bolt whose targets are dead hits the next ones.
    double proc_recovered =
        Strike(landed, {DamageOrigin::kSwing, 0}, held_pulses_);
    // Per strike, not per attack: each shock spends its own stacks, so they
    // drain across the barrage rather than all at the start.
    CreditFreeze(params, landed);
    // The rest of a spaced-out attack lands on its own timer while the player
    // keeps attacking, each strike hitting whatever is there at the time.
    if (landed.strikes_in_sequence > 1 && landed.cast_interval_seconds > 0.0) {
      attack_clocks_[swung].strikes_left = landed.strikes_in_sequence - 1;
      attack_clocks_[swung].next_strike_seconds = landed.cast_interval_seconds;
    }
    StrikeExtras(*cast, swung);
    RecoverHp(params, SwingRecovery(params, landed, proc_recovered));
    // The attack is over; anything it triggers runs on its own timer.
    attributing_ = -1;
    // After the strike, so triggered skills hit what the attack left alive.
    CreditSwing(params, cast->count_weight);
    // Only the first strike's lines (the rest of a barrage credits its own as
    // they land), but the whole press's weight, since thirty bolts are one key
    // press.
    CreditBuffs(params, cast->count_weight, landed.lines);
    LayBuffs(params, swung, /*on_cast=*/false);
    // After the strike too: a buff an attack rolls for boosts later attacks,
    // never the one that earned it.
    RaiseRolledBuffs(params, afflicted);
  }
  SpendSwingClocks(attack, swung);
  attributing_ = -1;  // nothing left to credit to this attack
  aimed_ = -1;        // the attack landed, so the next one is chosen fresh
  AimSwing(params);
}

void CombatSim::StrikeExtras(const AttackOption& cast, int swung) {
  if (cast.side != nullptr && attack_clocks_[swung].side_cooldown_left <= 0.0) {
    Strike(*cast.side, {DamageOrigin::kSideStrike, swung});
    attack_clocks_[swung].side_cooldown_left = cast.side->cooldown_seconds;
  }
  // Spent here rather than on the stored attack's own timer, since the press is
  // what uses it.
  if (cast.loaded != nullptr && cast.loaded_attack >= 0 &&
      attack_clocks_[cast.loaded_attack].charges_left > 0) {
    // If fewer charges are left than a press uses, it spends what's there:
    // GMS's charms go out in twos to fours.
    int spent = std::min(cast.loaded->charges_per_swing,
                         attack_clocks_[cast.loaded_attack].charges_left);
    for (int i = 0; i < spent; ++i) {
      Strike(*cast.loaded, {DamageOrigin::kLoad, cast.loaded_attack});
    }
    attack_clocks_[cast.loaded_attack].charges_left -= spent;
  }
}

double CombatSim::SwingRecovery(const CombatParams& params,
                                const AttackOption& landed,
                                double proc_recovered) const {
  double recovered =
      params.hp_recover_pct + landed.hp_recover_pct + proc_recovered;
  // The attack's own recovery is per pulse, so releasing early heals less.
  if (landed.channel.pulses > 0) {
    recovered += landed.channel.hp_recover_pct * held_pulses_;
  }
  return recovered;
}

void CombatSim::SpendSwingClocks(const AttackOption& attack, int swung) {
  // GMS gives Trickblade a 14-second cooldown normally and 20 on a wound, so
  // use the form's cooldown when the form was used.
  double wait = WoundFull(attack) ? attack.wound_form->cooldown_seconds
                                  : attack.cooldown_seconds;
  if (wait > 0.0) {
    attack_clocks_[swung].cooldown_left = wait;
  }
  if (attack.charges > 0 && attack_clocks_[swung].charges_left > 0) {
    --attack_clocks_[swung].charges_left;
  }
  // A whole charge for a partial one, as GMS spends a light per second held.
  // Progress toward the next charge is kept, so charges arrive on their own
  // timer rather than with presses.
  const ChannelHold& hold = attack.channel;
  if (hold.charge_seconds > 0.0 && hold.pulses_per_charge > 0) {
    double spent =
        std::ceil(static_cast<double>(held_pulses_) / hold.pulses_per_charge);
    attack_clocks_[swung].hold_charges =
        std::max(0.0, attack_clocks_[swung].hold_charges - spent);
  }
}

void CombatSim::MergeEngagedWindow(const CombatParams& params) {
  // One HP bar per type in the front window, in queue order, each averaging its
  // mobs' remaining HP.
  int window = std::min(reach_, static_cast<int>(queue_.size()));
  for (int j = 0; j < window; ++j) {
    const Mob& mob = *params.types[queue_[j].type].mob;
    double frac = mob.max_hp() > 0
                      ? std::clamp(queue_[j].hp / mob.max_hp(), 0.0, 1.0)
                      : 0.0;
    std::vector<EngagedGroup>::iterator it = std::find_if(
        view_.engaged_groups.begin(), view_.engaged_groups.end(),
        [&mob](const EngagedGroup& g) { return g.name == mob.name(); });
    if (it == view_.engaged_groups.end()) {
      view_.engaged_groups.push_back({mob.name(), mob.level(), 1, frac});
      continue;
    }
    it->hp_fraction = (it->hp_fraction * it->count + frac) / (it->count + 1);
    ++it->count;
  }
}

void CombatSim::PublishRoster(const CombatParams& params) {
  view_.roster.clear();
  for (const QueuedMob& queued : queue_) {
    const Mob& mob = *params.types[queued.type].mob;
    double frac =
        mob.max_hp() > 0 ? std::clamp(queued.hp / mob.max_hp(), 0.0, 1.0) : 0.0;
    view_.roster.push_back({queued.id, queued.type, mob.name(), frac});
  }
}

void CombatSim::PublishPlayer(const CombatParams& params) {
  // Rounded up so a sliver shows as 1 rather than 0.
  view_.player_hp = static_cast<int>(std::ceil(player_hp_));
  view_.player_max_hp = params.max_player_hp;
  view_.player_hp_fraction =
      params.max_player_hp > 0
          ? std::clamp(player_hp_ / params.max_player_hp, 0.0, 1.0)
          : 0.0;
  view_.respawns = params.respawn_seconds > 0.0;
  // Relative to the interval this wait started with, so the bar doesn't jump
  // when a totem is placed mid-cycle.
  view_.respawn_fraction =
      view_.respawns && respawn_interval_ > 0.0
          ? std::clamp(respawn_phase_ / respawn_interval_, 0.0, 1.0)
          : 0.0;
}

void CombatSim::PublishTarget(const CombatParams& params) {
  // A stacking buff counts as one buff however many of its windows are active;
  // they're adjacent and share a name.
  view_.buff_count = 0;
  for (int i = 0; i < static_cast<int>(params.buffs.size()); ++i) {
    bool standing = (buff_mask_ & (1 << i)) != 0;
    bool same_as_last = i > 0 && (buff_mask_ & (1 << (i - 1))) != 0 &&
                        params.buffs[i].name == params.buffs[i - 1].name;
    if (standing && !same_as_last) {
      ++view_.buff_count;
    }
  }
  view_.engaged_groups.clear();
  PublishRoster(params);
  respawning_ = queue_.empty();
  if (queue_.empty()) {
    view_.target_name.clear();
    view_.target_level = 0;
    view_.target_hp_fraction = 0.0;
    view_.attack_fraction = 0.0;
    return;
  }
  const QueuedMob& front = queue_.front();
  const Mob& target = *params.types[front.type].mob;
  view_.target_name = target.name();
  view_.target_level = target.level();
  view_.target_hp_fraction =
      target.max_hp() > 0 ? std::clamp(front.hp / target.max_hp(), 0.0, 1.0)
                          : 0.0;
  view_.attack_fraction =
      swing_seconds_ > 0.0
          ? std::clamp(attack_phase_ / swing_seconds_, 0.0, 1.0)
          : 0.0;
  PublishCast();
  MergeEngagedWindow(params);
}

bool CombatSim::PublishCast() {
  // Newest first: a cast ends when the attack timer climbs back to where it was
  // when raised, and the most recent reaches its mark first.
  while (!owed_casts_.empty() && owed_casts_.back().done_at <= attack_phase_) {
    owed_casts_.pop_back();
  }
  if (owed_casts_.empty()) {
    return false;
  }
  const OwedCast& cast = owed_casts_.back();
  view_.attack_name = cast.name;
  view_.attack_fraction =
      cast.seconds > 0.0
          ? std::clamp(1.0 - (cast.done_at - attack_phase_) / cast.seconds, 0.0,
                       1.0)
          : 1.0;
  return true;
}

void CombatSim::OpenStep(const CombatParams& params) {
  active_ = params.active;
  measuring_ = params.measuring;
  view_.kills_this_step.assign(params.types.size(), 0);
  view_.damage_this_step = 0.0;
  view_.respawned_this_step = false;
  view_.died_this_step = false;
  ledger_.BeginStep(params.record_damage_lines);
}

void CombatSim::GrowForAttacks(const CombatParams& params) {
  const std::vector<AttackOption>& fresh = Attacks(params);
  int had_clocks = static_cast<int>(attack_clocks_.size());
  attack_clocks_.resize(fresh.size());
  for (int i = had_clocks; i < static_cast<int>(attack_clocks_.size()); ++i) {
    attack_clocks_[i].hold_charges = fresh[i].channel.max_charges;
    attack_clocks_[i].charges_left = fresh[i].recharge_max;
  }
  by_attack_.resize(fresh.size());
}

void CombatSim::Advance(const CombatParams& params, double elapsed_seconds) {
  OpenStep(params);
  if (!CanFight(params)) {
    GoIdle();
    return;
  }
  // A large real-time gap is capped at one basic attack, so the fight resumes
  // rather than jumping ahead. The basic attack is used because every character
  // has it, and the next attack isn't known until it's aimed below. Measurement
  // gets the full step it asks for, since nothing stalled.
  double dt = measuring_ ? elapsed_seconds
                         : std::min(elapsed_seconds,
                                    params.attacks.front().swing_seconds);
  step_seconds_ = dt;

  BeginMapIfChanged(params);
  // A level-up raises max HP and fills it, as in GMS. Tracks level rather than
  // max HP, since skill points, scrolls or gear changes also raise max HP and
  // none of those should heal.
  if (params.player_level != player_level_) {
    player_hp_ = params.max_player_hp;
  }
  // If max HP dropped, current HP drops with it.
  player_hp_ = std::min(player_hp_, static_cast<double>(params.max_player_hp));

  // Before any attacks, so the damage rate casts are valued against covers the
  // fight up to now.
  fight_seconds_ += dt;
  // Before the hit that might need it.
  revive_left_ = std::max(0.0, revive_left_ - dt);
  RespawnBeat(params, dt);
  TakeMobHit(params, dt);
  // Before buffs run, so a buff going up this step has its timer.
  GrowForAttacks(params);
  // After the hit, so a buff raised now can heal it, and before any attacks, so
  // this step's attack benefits.
  RunBuffs(params, dt);
  // After the hit and before the attack, so regen applies on the step it was
  // needed.
  RunRegen(params, dt);
  // Alongside regen, for the same reason: it responds to the hit that dropped
  // HP under the threshold.
  RunEmergencyHeal(params, dt);
  // Before any attacks, so summons and the character target from the same
  // order, and the attack is chosen against what it will hit.
  AimAtHealthiest(params);
  // Before any auto-firing skill: a rain that scales with the crowd uses the
  // attack that called it, aimed last step.
  const std::vector<AttackOption>& options = Attacks(params);
  swing_enemies_ = aimed_ >= 0 && aimed_ < static_cast<int>(options.size())
                       ? Reached(options[aimed_])
                       : 0;
  RunAutoCasts(params, dt);
  // After the respawn refilled the mobs, so a skill triggered by the attack
  // that cleared the map still has something to hit.
  CreditKills(params);
  // Before the attack, so a burn applied last step has ticked and a monster
  // that thawed this step is thawed for this step's attack.
  RunDots(dt);
  RunFreeze(dt);
  RunStun(dt);
  RunMark(dt);
  RunWound(dt);
  RunScar(dt);
  RunCooldowns(params, dt);
  // Before the attack, so a bolt in the air hits the mobs present at the start
  // of this step.
  RunBarrage(params, dt);
  RunSwing(params, dt);

  player_level_ = params.player_level;
  // Measurement draws nothing, and building the display costs a string per
  // monster per step.
  if (measuring_) {
    respawning_ = queue_.empty();
    return;
  }
  PublishPlayer(params);
  PublishTarget(params);
}

double CombatSim::SecondsToNextEvent(const CombatParams& params) const {
  double soonest = std::numeric_limits<double>::infinity();
  if (aimed_ >= 0 && swing_seconds_ > 0.0) {
    soonest = swing_seconds_ - attack_phase_;
  }
  for (int i = 0; i < static_cast<int>(buffs_.size()) &&
                  i < static_cast<int>(params.buffs.size());
       ++i) {
    const BuffOption& buff = params.buffs[i];
    const BuffClock& clock = buffs_[i];
    if (clock.left > 0.0) {
      soonest =
          std::min({soonest, clock.left, NextDutyEdge(buff, clock.duty_phase)});
      continue;
    }
    // Buffs waiting on an attack, lines or a roll change at attack boundaries.
    // A ready buff that didn't go up is waiting on something else, so it
    // doesn't limit the step; otherwise the step would shrink to nothing.
    if (buff.laid_by_attack < 0 && buff.charge_lines <= 0 &&
        buff.raise_chance <= 0.0 && buff.duration_seconds > 0.0 &&
        clock.cooldown_left > 0.0) {
      soonest = std::min(soonest, clock.cooldown_left);
    }
  }
  return soonest;
}

}  // namespace ms
