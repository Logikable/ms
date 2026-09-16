#include "src/combat/fight.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "src/combat/encounter.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// How low the player falls before spending a swing on healing. A cast made
// whenever it was merely useful would never let the character attack; one
// saved for the last sliver would come too late.
constexpr double kHealBelowFraction = 0.25;

// Slack for the swing counter: a weight of a seventh has no exact double.
constexpr double kCountEpsilon = 1e-9;

// Whether there is a fight to advance. The bare poke is always the first
// attack, so its interval is the one to ask about.
bool CanFight(const CombatParams& params) {
  return params.active && !params.types.empty() && !params.attacks.empty() &&
         params.attacks.front().swing_seconds > 0.0;
}

// What a swing gaining `gain` a step is worth per enemy over `hit` of them:
// the escalation averaged, the order being drawn fresh. A sixth of
// (1 + 1.15 + ... + 1.15^5) is 1.46, at Piercing Arrow's numbers.
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
      arrival.id = next_mob_id_++;
      queue_.push_back(std::move(arrival));
    }
  }
  // Interleave the newcomers only: moving a wounded mob out of the front
  // window would hand back the damage done to it.
  std::shuffle(queue_.begin() + first_new, queue_.end(), rng_);
}

void CombatSim::AimAtHealthiest(const CombatParams& params) {
  if (!params.focus_healthiest || queue_.size() < 2) {
    return;
  }
  // Stable, so parts standing on the same HP keep the order they spawned in
  // and a fight plays out the same way twice.
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

// Widened only for DoT Punisher, which summons an orb per burn stack already
// standing. Read before anything of this cast lands -- ApplyDots runs at the
// end of Strike -- so the orbs never widen themselves.
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

// The strikes spread before they double up: on eleven enemies each takes one
// flame, on a lone boss all eleven land and every repeat is worth what the
// -55% leaves it. Leftovers go to the healthiest, GMS's rule read through
// what this game has. Strikes past scatter_max_hits_per_enemy are LOST rather
// than moved along.
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
  // Only the front `want` need be in order.
  std::partial_sort(
      reached.begin(), reached.begin() + want, reached.end(),
      [this](int a, int b) { return queue_[a].hp > queue_[b].hp; });
  reached.resize(want);
  return reached;
}

double CombatSim::StrikeDamage(const AttackOption& attack, int hit) const {
  double total = 0.0;
  // Only as many pulses as the fight means to hold for: a full hold would
  // price pulses that land on nothing.
  int pulses = ChannelPulses(attack, hit);
  int extras = ExtraLines(attack);
  std::vector<double> shares = ScatterShares(attack, hit);
  for (int j = 0; j < hit; ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.damage_per_hit.size())) {
      // What letting go early gives up. The pulses dropped are the LAST of
      // them, worth more than the rest on a hold that grows.
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
  // Rolled once for the whole swing, not per enemy -- the difference between
  // the two banks -- but landing on a crowd of its own.
  for (int j = 0; j < PerSwingFinalAttackTargets(attack, hit); ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.per_swing_final_attack_damage.size())) {
      total += attack.per_swing_final_attack_damage[type];
    }
  }
  // The chooser has to see the wide half, or a swing whose current is most
  // of its worth reads as the orb alone.
  for (int j = 0; j < WideHitTargets(attack, hit); ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.wide_hit_damage.size())) {
      total += attack.wide_hit_damage[type];
    }
  }
  // A proc lands on one enemy, so it is charged once however wide the swing,
  // as a share of what that enemy was taking anyway.
  if (hit > 0 &&
      queue_[0].type < static_cast<int>(attack.damage_per_hit.size())) {
    for (const ProcRoll& proc : attack.procs) {
      total +=
          attack.damage_per_hit[queue_[0].type] * proc.chance * proc.damage_pct;
    }
  }
  return total;
}

// The burning gained over what the monster had coming anyway, plus a helping
// where the pile has room. Nothing on a full, fresh pile, which is what sends
// the chooser elsewhere until the burn nears its end.
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

// Charged at what relighting buys, which on a monster already burning is
// little or nothing.
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
// each swing -- or the swing would be weighed as though it set the strike off
// every time.
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

// A load is spent whole on one press rather than spread over the swings that
// go out while it waits, which is what tells it from a side strike: the fight
// should reach for the swing carrying it exactly while the charge stands, and
// weigh that swing bare once it is gone.
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
  // A charge the load prepared for itself is worth its share of the wait for
  // the next press, as a side strike on a cooldown is. Charges a raising of
  // the buff hands over are left whole -- those go out as fast as the player
  // can press.
  if (load.recharge_seconds > 0.0 && left <= load.recharge_max &&
      attack.swing_seconds > 0.0) {
    worth *= attack.swing_seconds /
             std::max(load.recharge_seconds, attack.swing_seconds);
  }
  return worth;
}

// The wall lands on its own beat while the player goes on swinging, so what
// cuts it short is the next cast of the same skill: a cooldown away where it
// has one, a press away where it does not.
double CombatSim::BarrageStrikes(const AttackOption& attack) const {
  int strikes = std::max(1, attack.strikes_in_sequence);
  if (strikes == 1 || attack.cast_interval_seconds <= 0.0) {
    return 1.0;
  }
  double again = std::max(SwingSecondsAgainst(attack), attack.cooldown_seconds);
  return std::min<double>(strikes, 1.0 + again / attack.cast_interval_seconds);
}

double CombatSim::SwingDamage(const AttackOption& attack) const {
  // A wound's form is not averaged as an empowered one is: it is what this
  // press lands if a wound stands, so the rate reads the queue as it is.
  if (WoundFull(attack)) {
    return SwingDamage(*attack.wound_form);
  }
  int hit = Reached(attack);
  double total = StrikeDamage(attack, hit) * BarrageStrikes(attack) +
                 BurnDamage(attack, hit);
  // Held aside because these ride the swing whichever form it took, and the
  // averaging below is between the two forms.
  double side = SideStrikeDamage(attack) + LoadedDamage(attack);
  // An empowered form lands once in every N, so the attack is worth the
  // average of the two. The form has none of its own, so this recurs once.
  if (attack.empowered != nullptr && attack.empowered_every > 0) {
    if (!attack.brands_enemies) {
      total +=
          (SwingDamage(*attack.empowered) - total) / attack.empowered_every;
      return total + side;
    }
    // Marking instead: each mob reached comes due once in every N rather
    // than the swing doing so, and takes the whole form on top. Averaged over
    // the cycle for the same reason as above.
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

// Per second, not per swing: a skill hitting half again as hard but taking
// twice as long is worse. An ice swing is also paid for the pile it leaves
// and the freeze it lays, or the chooser would take the lightning swing every
// time -- see FreezeCredit and FrozenCredit.
double CombatSim::SwingRate(const CombatParams& params,
                            const AttackOption& attack) const {
  return (SwingDamage(attack) * StateBoost(attack, FrontMob()) +
          FreezeCredit(params, attack) + FrozenCredit(params, attack) +
          BurnStateCredit(params, attack)) /
         SwingSecondsAgainst(attack);
}

// The only thing a lookahead may reach for: a skill two minutes from its next
// cast is not what the fight will spend a pile of freeze stacks on.
bool CombatSim::OnOffer(const CombatParams& params, int index) const {
  const std::vector<AttackOption>& options = Attacks(params);
  if (index < 0 || index >= static_cast<int>(options.size())) {
    return false;
  }
  const AttackOption& attack = options[index];
  if (attack.swing_seconds <= 0.0) {
    return false;  // not a swing; a skill on its own clock is not chosen
                   // between
  }
  if (attack.heal_fraction > 0.0) {
    return false;  // a cast is chosen by need, not by rate -- see HealToCast
  }
  // A load another skill's press sets off is no button of its own: it goes out
  // with that swing, and its damage is already counted there.
  if (attack.spent_by_attack >= 0) {
    return false;
  }
  // A hold is judged on its pulse over its pulse clock whatever its length,
  // so one charge prices the same as a full bank and the chooser takes the
  // hold the moment a charge lands.
  return !Recharging(index) && Loaded(params, index) && Charged(params, index);
}

int CombatSim::TopAttack(const CombatParams& params,
                         const std::vector<bool>& held) const {
  int best = -1;
  double best_rate = -1.0;
  const std::vector<AttackOption>& options = Attacks(params);
  for (int i = 0; i < static_cast<int>(options.size()); ++i) {
    const AttackOption& attack = options[i];
    // Saved for a window, so what goes out now comes from the rest.
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
    // A shell is raised by need, not by its clock (see ShieldWanted), so
    // waiting for one would stall the fight on a window that may never open.
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
  // The mask as it would stand then: what comes up set, what lapses cleared.
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
  // A swing landing inside the window anyway saves nothing: it lands at the
  // end of what is LEFT of its animation, the phase carrying over to whatever
  // replaces it. Less this step, which the buff clocks have taken and the
  // swing has not -- without it a hold lets go one step early every time.
  if (window.seconds <
      SwingSecondsAgainst(attack) - attack_phase_ - step_seconds_) {
    return false;
  }
  const ChannelHold& hold = attack.channel;
  if (hold.charge_seconds > 0.0) {
    // A bank costs nothing to sit on. The only thing waiting throws away is
    // a charge that fills past the top of it.
    double banked = index < static_cast<int>(attack_clocks_.size())
                        ? attack_clocks_[index].hold_charges
                        : 0.0;
    return banked + window.seconds / hold.charge_seconds <= hold.max_charges;
  }
  // A cooldown back before the window opens is free to spend now: both
  // presses are had. Only one outlasting the wait is a press being placed.
  return attack.cooldown_seconds > window.seconds;
}

bool CombatSim::HoldPays(const CombatParams& params, int index, int filler,
                         const ComingWindow& window) const {
  if (filler < 0) {
    return false;  // nothing else to swing, and the fight never idles
  }
  // Both sides priced off the STANDING masks, not the granting ones: what is
  // weighed is which buffs stand, not which instant of a bursting one it is.
  const std::vector<AttackOption>& now = params.Attacks(buff_mask_);
  const std::vector<AttackOption>& then = params.Attacks(window.mask);
  if (index >= static_cast<int>(now.size()) ||
      filler >= static_cast<int>(now.size()) ||
      index >= static_cast<int>(then.size()) ||
      filler >= static_cast<int>(then.size())) {
    return false;
  }
  // What one slot of this attack buys over the filler holding the same
  // seconds. A difference of rates, so what the pair share falls out.
  double seconds = SwingSecondsAgainst(now[index]);
  double press_now =
      (SwingRate(params, now[index]) - SwingRate(params, now[filler])) *
      seconds;
  double press_then =
      (SwingRate(params, then[index]) - SwingRate(params, then[filler])) *
      seconds;
  double gain = press_then - press_now;
  if (gain <= 0.0) {
    return false;  // the window lifts the filler as much, so there is no wait
                   // worth taking
  }
  if (now[index].channel.charge_seconds > 0.0) {
    return true;  // a banked hold loses nothing by waiting; HoldSaves already
                  // kept it from overflowing
  }
  // Waiting pushes the train of presses back, losing a wait/cooldown share
  // of a press. Priced at today's press, the conservative side.
  return gain > press_now * window.seconds / now[index].cooldown_seconds;
}

// The hardest swing on offer, except that a big move ready just before a buff
// window is saved for it: HoldPays weighs the press landing inside the window
// against every later press pushed back by the wait. A move set aside sends
// the question to the runner-up, which is what would really go out instead.
int CombatSim::BestAttack(const CombatParams& params) const {
  if (queue_.empty()) {
    return -1;  // nothing to hit, so nothing to choose between
  }
  std::vector<bool> held;
  int best = TopAttack(params, held);
  ComingWindow window = NextWindow(params);
  // Nothing on its way, nothing new in it, or a fight over before it opens.
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

// Guarded on size: the clocks grow to fit the params, and a swing asked about
// before the first Advance has none yet.
bool CombatSim::Recharging(int index) const {
  return index < static_cast<int>(attack_clocks_.size()) &&
         attack_clocks_[index].cooldown_left > 0.0;
}

// A magazine's swing is off the list until its buff loads it. The charges are
// handed back whole at each raising and gone with it, so an empty count means
// both "the buff is down" and "the cartridges are spent".
bool CombatSim::Loaded(const CombatParams& params, int index) const {
  const std::vector<AttackOption>& options = Attacks(params);
  if (index >= static_cast<int>(options.size()) ||
      options[index].charges <= 0) {
    return true;
  }
  return index < static_cast<int>(attack_clocks_.size()) &&
         attack_clocks_[index].charges_left > 0;
}

// A hold bought out of a bank is off the list until a whole charge has filled.
// Nothing else keeps one, so every other attack answers true.
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
  // Only mid-fight: a cleared map hands HP back free on the beat.
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
  // After the commitment: the cast replaces the NEXT attack rather than
  // interrupting this one.
  int heal = HealToCast(params);
  if (heal >= 0) {
    return heal;
  }
  // Below the heal and above the damage: a lapsed buff is worth more than
  // one more of the best swing.
  int lay = BuffToLay(params);
  if (lay >= 0) {
    return lay;
  }
  return BestAttack(params);
}

void CombatSim::RunCooldowns(const CombatParams& params, double dt) {
  // Runs on an empty map, unlike an auto-cast's clock: a player waiting out
  // a respawn really does have their cooldown back when the mobs land.
  const std::vector<AttackOption>& options = Attacks(params);
  for (std::size_t i = 0; i < attack_clocks_.size(); ++i) {
    AttackClock& clock = attack_clocks_[i];
    clock.cooldown_left = std::max(0.0, clock.cooldown_left - dt);
    clock.side_cooldown_left = std::max(0.0, clock.side_cooldown_left - dt);
    if (i >= options.size()) {
      continue;
    }
    // The bank fills on an empty map too, for the same reason.
    const ChannelHold& hold = options[i].channel;
    if (hold.charge_seconds > 0.0) {
      clock.hold_charges =
          std::min(static_cast<double>(hold.max_charges),
                   clock.hold_charges + dt / hold.charge_seconds);
    }
    // The clock is held while the bank is at its cap, so a raising of the
    // buff is never topped up and the passive half comes back as soon as the
    // burst's charges are gone.
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
  // One strike hits the front mobs at once, each taking its own type's
  // damage; overkill is wasted.
  int hit = Reached(attack);
  // Picked before anything lands, so the opening hit chooses by the HP the
  // mobs went into the swing with rather than what the spread left them on.
  std::vector<int> lead = LeadTargets(attack, hit);
  // An arrow that gains as it travels needs an order, and nothing here has a
  // position, so the swing draws one.
  std::vector<int> order = PierceOrder(attack, hit);
  // Picked for the same reason: what the flames double up on is decided by
  // the HP the monsters went in with.
  std::vector<double> shares = ScatterShares(attack, hit);
  // Before anything lands, so every way this swing reaches one monster files
  // under the one event: that is one landing to the player watching.
  ledger_.OpenLandings(queue_.size(), hit, source);
  // A hold not timed by the swing clock decides here instead.
  int held = attack.channel.pulses > 0
                 ? (pulses >= 0 ? pulses : ChannelPulses(attack, hit))
                 : 0;
  // Settled once for the whole strike: the crowd belongs to the swing that
  // called the rain down, not to who stands under each arrow.
  int extra = ExtraLines(attack);
  for (int step = 0; step < hit; ++step) {
    int j = order.empty() ? step : order[step];
    double gain =
        order.empty() ? 1.0 : std::pow(1.0 + attack.pierce_gain_pct, step);
    // The strike proper is the first landing to reach the monster, so it
    // takes the lift; every later bank finds the mark gone.
    double freeze =
        StateBoost(attack, queue_[j]) * SpendMark(attack, queue_[j]);
    double share = shares.empty() ? 1.0 : shares[j];
    double damage =
        held > 0
            ? ChannelDamage(attack, queue_[j].type, held, LandingAt(j, freeze))
            : DamageToMob(attack, j, LandingAt(j, gain * freeze * share)) *
                  gain;
    // Each rolls on its own: the same arrow falling more times, not one
    // arrow worth more.
    for (int line = 0; line < extra; ++line) {
      damage += RolledDamage(*attack.extra_line, queue_[j].type,
                             LandingAt(j, gain * freeze * share)) *
                gain;
    }
    Hurt(queue_[j], damage * freeze * share);
  }
  for (int j : lead) {
    double freeze = StateBoost(attack, queue_[j]);
    double damage =
        attack.lead_damage[queue_[j].type] * Roll(attack.lead_rolls);
    ledger_.RecordRolls(LandingAt(j, freeze),
                        attack.lead_damage[queue_[j].type] * freeze);
    Hurt(queue_[j], damage * freeze);
  }
  // Rolled against every enemy the swing reached.
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
  // Rolled once for the swing and falling on its own crowd: Blizzard's one,
  // Split Shot's ten. The front of the queue is as good a crowd as any.
  for (int j = 0; j < PerSwingFinalAttackTargets(attack, hit); ++j) {
    double freeze = StateBoost(attack, queue_[j]);
    Hurt(queue_[j], RolledFinalAttack(attack.per_swing_final_attack_rolls,
                                      attack.per_swing_final_attack_damage,
                                      queue_[j].type, LandingAt(j, freeze)) *
                        freeze);
  }
  riding_ = Rider::kItself;
  // Jupiter Thunder's current arcs onto two where the orb rides one. Held to
  // the swing landing at all: a current arcs off a shock, not off nothing.
  for (int j = 0; j < WideHitTargets(attack, hit); ++j) {
    double freeze = StateBoost(attack, queue_[j]);
    Hurt(queue_[j], RolledGroups(attack.wide_hit_groups, attack.wide_hit_damage,
                                 queue_[j].type, LandingAt(j, freeze)) *
                        freeze);
  }
  double recovered = RollProcs(attack, hit);
  // Before the dead are cleared, so the indices reached are still the ones
  // the marks are written to.
  ApplyDots(attack, hit);
  ApplyFreeze(attack, hit);
  ApplyStun(attack, hit);
  ApplyMark(attack, hit);
  ApplyWound(attack, hit);
  ApplyScar(attack, hit);
  Reap();
  return recovered;
}

// Rolled once for the whole swing, as GMS rolls it per attack: a second
// helping of the swing on one enemy rather than a hit of its own, rolling its
// own crit and mastery. It falls on the front of the queue.
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

// Taken as a count and a flag rather than read off the character and the
// monster, so the chooser can ask what a DEEPER pile or a FROZEN enemy would
// be worth -- see FreezeCredit and FrozenCredit.
double CombatSim::BoostForStacks(const AttackOption& attack, int stacks,
                                 int type, bool frozen) const {
  if (stacks <= 0) {
    return 1.0;
  }
  // These multiply rather than sum, as every other pair of critical and final
  // damage does.
  double crit = frozen ? 1.0 + attack.freeze_crit_gain * stacks : 1.0;
  // The pile's alone: GMS gates the critical damage on a frozen enemy and
  // says nothing of the kind about the lightning swing's final damage.
  double spent =
      attack.freeze_spends ? 1.0 + attack.freeze_fd_per_stack * stacks : 1.0;
  // Glacial Fury's is attack rather than damage, so it lands under
  // everything the swing already multiplies.
  double matt = 1.0 + attack.freeze_matt_gain * stacks;
  // Shatter's differs mob by mob: the defence ignored is that monster's.
  double shattered =
      frozen && type < static_cast<int>(attack.freeze_ied_gain.size())
          ? 1.0 + attack.freeze_ied_gain[type] * stacks
          : 1.0;
  return crit * spent * matt * shattered;
}

// Three statuses count: ice, a burn and a stun. GMS lists five, and the other
// two are inflicted by nothing here -- when one arrives it joins the test and
// no lever moves. See SkillEffect::final_dmg_pct_when_afflicted.
bool CombatSim::Afflicted(const QueuedMob& mob) const {
  if (mob.frozen_left_seconds > 0.0 || mob.stunned_left_seconds > 0.0) {
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

// A scar is left by a LINE, so a swing scars partway through itself: the line
// that cuts collects nothing, the ones after it collect everything. Averaged
// over the lines that is 1 - (1 - odds) * (1 - (1 - chance)^n) / (chance * n),
// which pays the whole scar on a monster already carrying one.
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

// A monster carrying two burns is two, and eight carrying one apiece are
// eight.
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

// Its own length plus what the burns alight add. Read at the raise rather
// than baked onto the option, the count moving with the fight: Elemental
// Fury's spirit stays twice as long over a group kept poisoned.
double CombatSim::BuffWindowSeconds(const BuffOption& buff) const {
  if (buff.duration_seconds_per_dot <= 0.0 || buff.dot_count_cap <= 0) {
    return buff.duration_seconds;
  }
  return buff.duration_seconds +
         buff.duration_seconds_per_dot *
             std::min(BurnsAlight(), buff.dot_count_cap);
}

// The same count in STACKS, which is what GMS means by a damage-over-time
// stack: a burn piled three deep is three. The two part only over Toxic
// Venom's, the one burn that stacks.
int CombatSim::BurnStacksAlight() const {
  int alight = 0;
  for (const QueuedMob& mob : queue_) {
    for (const MobDot& burn : mob.dots) {
      if (burn.left_seconds > 0.0) {
        // A measurement carries part of a helping; GMS counts whole ones.
        alight += static_cast<int>(std::lround(std::max(0.0, burn.stacks)));
      }
    }
  }
  return alight;
}

// Two questions: whether THIS monster is afflicted, and how many burns stand
// on the WHOLE group. The count is the group's because that is what GMS means
// by "within a certain range", so a drain is worth its cap on a map and only
// what the rotation keeps alight on a boss.
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

// Only a swing that collects takes the lift, which is never the one that left
// the stun.
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

// Nothing here has a position, so no monster is nearer than another. A bare
// one where nothing stands, which reads as an unfrozen mob of type 0.
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

// A hold that grows beats at two strengths, so this is a sum of two runs.
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
  // Capped before the cast, not after: a monster that never falls asks for
  // more pulses than fit in an int.
  auto pulses_for = [&hold](double left, double per_pulse) {
    double need = std::ceil(left / per_pulse);
    return need >= hold.pulses ? hold.pulses : static_cast<int>(need);
  };
  // The hold only has to bring them within the closing strike's reach:
  // pulses past that fall on something already dead.
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
    // Not a division: the opening run is spent first, and only what still
    // stands comes off the stronger pulses.
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
    ledger_.RecordRolls(landing, pulse * landing.scale);
  }
  // Past the first group is the closing strike, landed once however long the
  // hold ran.
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
  // What the deeper pile buys the swing that comes next -- all of it, not
  // only the final damage a lightning swing spends it for. The best swing
  // REALLY on offer: reading one still on its cooldown makes an ice swing
  // look worth laying for a payout the chooser cannot take. One swing of
  // lookahead, which is as far as a greedy chooser sees.
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

// Freezing moves the pile's own factors and the affliction gate at once, and
// the credit must ask for the pair: priced on the pile alone, an ice swing is
// worth nothing to a character whose only reader is Storm Magic.
//
// Every swing is read, a recharging one included, DELIBERATELY -- unlike
// FreezeCredit, which asks OnOffer. This prices a condition standing on a
// monster for seconds, not a pile the next press spends, and a reader on a
// cooldown finds the ice still there. Limiting it to what is on offer was
// measured and is a loss.
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
  // Only the seconds before this swing comes round again: freeze past that
  // would be laid down a second time.
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

// What one more burning monster is worth per second to whatever is swung
// next. The mirror of FrozenRate, including that it reads EVERY swing rather
// than the ones on offer: the mist is laid to be standing when its reader
// comes up 25 seconds later, and crediting it only while that reader is ready
// stops it being laid at all. Measured at -0.9% for the branch.
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

// What lighting this swing's burns is worth to everything swung AFTER it: the
// affliction and the count the drains read, neither paid for by the burn's own
// ticks. Ignite makes Explosion the F/P Mage's best swing and Explosion burns
// nothing, so a chooser blind to this never lays the mist at all.
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
  // Read off what the strike reaches, not what it could: a blizzard falling
  // on one enemy builds more than the same one spread over ten.
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
    // A stack per line, or a stated rate floored at one: a strike spending
    // nothing would take the pile's final damage free every time.
    int lines = std::max(1, attack.lines);
    int spent = std::max(1, lines / std::max(1, attack.freeze_lines_per_spend));
    freeze_stacks_ = std::max(0, freeze_stacks_ - spent);
  }
}

void CombatSim::Hurt(QueuedMob& mob, double damage) {
  // A measurement's monsters never fall: it asks the rate, and an emptied
  // roster would measure the respawn beat instead.
  if (!measuring_) {
    mob.hp -= damage;
  }
  view_.damage_this_step += damage;
  damage_dealt_ += damage;
  if (attributing_ >= 0 &&
      attributing_ < static_cast<int>(damage_by_attack_.size())) {
    damage_by_attack_[attributing_] += damage;
    // Within that total, not out of it: the reader wants the swing's figure
    // split, not short.
    if (riding_ == Rider::kFinalAttack) {
      final_attack_damage_by_attack_[attributing_] += damage;
    } else if (riding_ == Rider::kBurn) {
      burn_damage_by_attack_[attributing_] += damage;
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
  // The roster a caller reads is a copy taken when the step ended, and
  // nothing here went through a step.
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
      // Rolled per enemy, so a poison takes on some of what the swing
      // reached. A measurement takes the share instead.
      double took = burn.chance < 1.0 ? Chance(burn.chance) : 1.0;
      if (took <= 0.0) {
        continue;
      }
      // Only the duration starts again; the tick clock stays where it is, or
      // a swing faster than the interval would refresh the burn out of ever
      // ticking. What piles up is the helpings.
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
    }
  }
}

void CombatSim::ApplyFreeze(const AttackOption& attack, int hit) {
  if (attack.freeze_seconds <= 0.0) {
    return;
  }
  for (int j = 0; j < hit; ++j) {
    // Written over, not added to: a monster frozen again is frozen for the
    // full time from now.
    queue_[j].frozen_left_seconds =
        std::max(queue_[j].frozen_left_seconds, attack.freeze_seconds);
  }
}

void CombatSim::ApplyStun(const AttackOption& attack, int hit) {
  if (attack.stun_seconds <= 0.0) {
    return;
  }
  for (int j = 0; j < hit; ++j) {
    // Written over, as the ice is.
    queue_[j].stunned_left_seconds =
        std::max(queue_[j].stunned_left_seconds, attack.stun_seconds);
    queue_[j].stun_lift_pct = attack.stun_lift_pct;
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
    // Written over, as the stun is: a monster marked again carries one
    // mark.
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
  // GMS names the target by MAX HP, not by what is left, so a boss part worn
  // down is still the one wounded.
  int want = 0;
  for (int j = 1; j < hit && j < static_cast<int>(queue_.size()); ++j) {
    if (queue_[j].max_hp > queue_[want].max_hp) {
      want = j;
    }
  }
  // A wound landing elsewhere takes the old one off -- "only 1 enemy can
  // receive the wound debuff" -- and on the same monster it deepens.
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
  // Gone when it lapses and gone with the monster.
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

// One line spends the mark and lands that much harder, taken as a share of
// the whole swing so the ledger and the monster agree. The share IS the line:
// every line is worth the same in expectation.
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
    // The odds one was already there or this swing left one. The clock
    // starts again either way, as the freeze's does.
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
      // Only the seconds the burn had left, so one running out partway
      // through a step lands the ticks it was owed and no more.
      double spent = std::min(dt, dot.left_seconds);
      dot.left_seconds -= spent;
      dot.phase += spent;
      attributing_ = dot.lit_by;
      striking_ = {DamageOrigin::kBurn, slot};
      riding_ = Rider::kBurn;
      while (dot.phase >= dot.interval_seconds) {
        dot.phase -= dot.interval_seconds;
        // Every helping ticks for the whole damage and rolls its own. Part
        // of one ticks for part, which only a measurement carries.
        for (double left = dot.stacks; left > 0.0;) {
          double helping = std::min(1.0, left);
          left -= helping;
          Hurt(mob, dot.damage * helping * Roll(dot.rolls));
          // A tick is its own landing, falling between the swings.
          ledger_.RecordRolls(
              {mob.id, ledger_.NextEvent(), {DamageOrigin::kBurn, slot}, 1.0},
              dot.damage * helping);
        }
        burned = true;
      }
    }
  }
  // A burn kills as a swing does. Skipped where nothing ticked: walking the
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
    // A step wider than the interval owes every pulse it covered.
    while (regen_phase_[i] >= pulse.interval_seconds) {
      regen_phase_[i] -= pulse.interval_seconds;
      player_hp_ =
          std::min(static_cast<double>(params.max_player_hp),
                   player_hp_ + pulse.hp + pulse.pct * params.max_player_hp);
    }
  }
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

// Every group rolls its own mastery and criticals. `expected` is landed where
// nothing rolls, which is what an attack built by hand leaves behind.
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
    // A chance past certainty is that many hits guaranteed plus a roll for
    // the remainder. Nothing grants one yet.
    int certain = static_cast<int>(source.chance);
    for (int roll = 0; roll < source.count; ++roll) {
      double hits = certain + Chance(source.chance - certain);
      if (hits <= 0.0) {
        continue;
      }
      total += source.damage[type] * hits * Roll(source.rolls);
      ledger_.RecordRolls(landing, source.damage[type] * hits * landing.scale);
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
  // Counted before the test, as FormToLand counts swings: a mark of five
  // goes off on the fifth strike.
  if (++queue_[index].brand < attack.empowered_every) {
    return ordinary;
  }
  queue_[index].brand = 0;
  // On top of the strike, not instead of it: a mark going off is its own
  // event, where an empowered swing IS the swing.
  return ordinary + RolledDamage(*attack.empowered, type, landing);
}

const AttackOption& CombatSim::FormToLand(int& count,
                                          const AttackOption& attack) {
  // A wound's form stands in for the press and is not counted: the wound
  // decides it, not a run of swings.
  if (WoundFull(attack)) {
    return *attack.wound_form;
  }
  // A marking form never stands in for the swing: DamageToMob decides mob by
  // mob what goes off on top.
  if (attack.empowered == nullptr || attack.empowered_every <= 0 ||
      attack.brands_enemies) {
    return attack;
  }
  // Counted before the test: a period of four is three ordinary landings and
  // then this one.
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
  // The queue's type indices and HP belong to one map; carried to another,
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
  // A barrage belongs to the fight it was loosed in; bolts in the air do not
  // follow the player.
  for (AttackClock& clock : attack_clocks_) {
    clock.strikes_left = 0;
    clock.next_strike_seconds = 0.0;
  }
  next_mob_id_ = 0;
  // The rate belongs to the encounter: the last map's damage says nothing
  // about how long this fight has left.
  damage_dealt_ = 0.0;
  fight_seconds_ = 0.0;
  // Every clock the character carries -- cooldowns, casts, buffs, fountains
  // -- is left alone: they belong to the character, not to the mobs. A boss
  // phase is a new encounter too, and must not reopen with everything ready.
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
  if (respawn_phase_ < respawn_interval_) {
    return;
  }
  respawn_phase_ -= respawn_interval_;
  // The next wait takes whatever the params say now: a totem planted
  // mid-cycle shortens the beat after this one, not the one already ticking.
  respawn_interval_ = params.respawn_seconds;
  view_.respawned_this_step = true;
  bool was_idle = queue_.empty();
  TopUp(params);
  // Every beat hands back a slice of the pool, cleared map or not. It is the
  // only healing there is.
  player_hp_ =
      std::min(static_cast<double>(params.max_player_hp),
               player_hp_ + params.beat_heal_fraction * params.max_player_hp);
  if (!was_idle) {
    // Mobs arriving mid-fight leave a wound-up swing alone: restarting it
    // throws away real progress, not just the bar.
    return;
  }
  // Clearing the map is the bigger breather, and worth the whole pool.
  attack_phase_ = 0.0;
  owed_casts_.clear();
  player_hp_ = params.max_player_hp;
  hit_phase_ = 0.0;
}

// Reductions multiply rather than sum, as every other one in the game does:
// two halves leave a quarter. The buffs read are the ones the step opened
// with, so a smokescreen dropped after the blow does not take it back.
double CombatSim::BuffDamageTakenFactor(const CombatParams& params) const {
  // A shell blocks whole hits, so all it takes off here is the share it
  // hands a boss instead -- see BlockHit.
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
  // A hold's shelter lasts exactly as long as the hold.
  const std::vector<AttackOption>& options = Attacks(params);
  if (aimed_ >= 0 && aimed_ < static_cast<int>(options.size())) {
    factor *= 1.0 - options[aimed_].channel.damage_taken_pct;
  }
  return std::max(0.0, factor);
}

// Only one shell pays for a hit however many stand, or two blocks go on one
// hit for nothing. A boss's hit is never blocked -- GMS exempts the attacks
// costing a share of the pool -- and a shell takes its share off instead, in
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
      buffs_[i].left = 0.0;  // spent: the shell falls, clock or no clock
      buff_mask_ &= ~(1 << i);
    }
    return true;
  }
  return false;
}

void CombatSim::TakeMobHit(const CombatParams& params, double dt) {
  // Only the front mob hits back, and it swings first, so the last one
  // standing still lands its hit on the way out. An empty map's clock waits
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
  // A frozen monster never gets its swing off. The clock runs on regardless,
  // so a long freeze eats several beats rather than banking them.
  if (queue_.front().frozen_left_seconds > 0.0) {
    return;
  }
  if (BlockHit(params)) {
    return;  // cancelled whole: nothing to lose, and nothing to reflect
  }
  // The scar is odds rather than a flag, so the hit is the two damages
  // weighed by how likely it is.
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
  // The whole pool back, standing where they fell: the mob that landed the
  // hit is still in front of them.
  player_hp_ = params.max_player_hp;
  revive_left_ = params.revive_cooldown_seconds;
  return true;
}

void CombatSim::Reflect(const CombatParams& params, double damage_taken) {
  // Off the whole hit, not the sliver a dying player had left to lose.
  if (params.damage_reflect_pct <= 0.0 || queue_.empty()) {
    return;
  }
  QueuedMob& front = queue_.front();
  // Nothing struck to earn it, so it files under index -1, which names no
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

// The damage tables are picked with the LEVER mask -- what is granted this
// instant. Whether a buff STANDS is a different question, asked of
// buff_mask_.
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

// The one buff held back rather than raised the moment it comes round: it is
// a heal and a shelter at once, and the two want opposite timing. On a map it
// waits for a pool low enough to be worth filling; on a boss one blow is the
// whole fight, so it goes up at once.
bool CombatSim::ShieldWanted(const CombatParams& params,
                             const BuffOption& buff) const {
  // Nothing hits the player in a measurement, so one held back for a low
  // pool would be held back forever and its levers would go missing.
  if (buff.shield_hits <= 0 || params.measuring) {
    return true;
  }
  if (!queue_.empty() && params.types[queue_.front().type].mob->boss()) {
    return true;
  }
  return player_hp_ < kHealBelowFraction * params.max_player_hp;
}

namespace {

// Whether a standing buff is granting this instant: always, for one granting
// steadily; four seconds in five for the angel. See duty_seconds.
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
  // Seeded with each buff's full charge, or one charged by hits would go up
  // before a hit had landed.
  if (static_cast<int>(buffs_.size()) != count) {
    buffs_.resize(count);
    for (int i = 0; i < count; ++i) {
      buffs_[i].charge_left = params.buffs[i].charge_lines;
    }
  }
  buff_mask_ = 0;
  lever_mask_ = 0;
  for (int i = 0; i < count; ++i) {
    const BuffOption& buff = params.buffs[i];
    BuffClock& clock = buffs_[i];
    clock.left = std::max(0.0, clock.left - dt);
    clock.cooldown_left = std::max(0.0, clock.cooldown_left - dt);
    clock.duty_phase += dt;
    // Raised the moment it comes round, and only with something to fight:
    // one spent on an empty map is one the player lacks when the mobs land.
    // Never recast over itself. A buff its own swing lays waits for that
    // swing -- see LayBuffs.
    //
    // What it waits on: seconds, or a count of landed hits.
    bool ready = buff.charge_lines > 0 ? clock.charge_left <= 0.0
                                       : clock.cooldown_left <= 0.0;
    if (buff.laid_by_attack < 0 && clock.left <= 0.0 && ready &&
        !queue_.empty() && buff.duration_seconds > 0.0 &&
        ShieldWanted(params, buff)) {
      // Settled here and never revisited: a sword planted for two minutes
      // stays planted, however the fight turns.
      clock.stance = StanceToRaise(params, buff);
      clock.left = clock.stance < 0
                       ? BuffWindowSeconds(buff)
                       : buff.stances[clock.stance].duration_seconds;
      clock.cooldown_left = buff.cooldown_seconds;
      clock.charge_left = buff.charge_lines;
      clock.blocks_left = buff.shield_hits;
      clock.duty_phase = 0.0;
      // A fresh load, whole: what was left of the last one is not carried.
      if (buff.magazine_attack >= 0 &&
          buff.magazine_attack < static_cast<int>(attack_clocks_.size())) {
        attack_clocks_[buff.magazine_attack].charges_left =
            Attacks(params)[buff.magazine_attack].charges;
      }
      // Raising it costs its animation, taken off the swing being charged: a
      // buff is cast instead of attacking, not alongside it. The clock left
      // in debt is what the charge bar draws the cast over.
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
    // Lapsed, so whatever it still had loaded goes with it.
    if (buff.magazine_attack >= 0 &&
        buff.magazine_attack < static_cast<int>(attack_clocks_.size())) {
      attack_clocks_[buff.magazine_attack].charges_left = 0;
    }
  }
}

double CombatSim::SecondsLeft(const CombatParams& params) const {
  // A map refills on the beat, so there is no end to measure against and a
  // summon is worth its rate rather than its total. A measurement asks the
  // same question on purpose: its monsters never fall.
  if (params.respawn_seconds > 0.0 || params.measuring) {
    return std::numeric_limits<double>::infinity();
  }
  double standing = 0.0;
  for (const QueuedMob& mob : queue_) {
    standing += mob.hp;
  }
  // Measured once there is enough fight to measure, the params' estimate
  // before that: a buff raised on the opening step still needs a horizon.
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
  // Priced off the unbuffed table on purpose: comparing two forms of one
  // skill, every multiplier they share cancels. A buffed table would also
  // mean reading a mask still being built, this running inside that loop.
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
    // What the form delivers before the fight ends: its rate over whichever
    // runs out first, its clock or the encounter. A short, dense form wins
    // every fight that ends before a long, thin one has finished paying.
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
    // Trickblade's invulnerability is the heavier press's alone.
    if (buff.needs_wound_form && !WoundFull(Attacks(params)[swung])) {
      continue;
    }
    // Refreshed rather than stacked: a second puncture leaves one wound.
    buffs_[i].left = BuffWindowSeconds(buff);
    buffs_[i].cooldown_left = buff.cooldown_seconds;
    // A fresh load, whole, as RunBuffs hands one out.
    if (buff.magazine_attack >= 0 &&
        buff.magazine_attack < static_cast<int>(attack_clocks_.size())) {
      attack_clocks_[buff.magazine_attack].charges_left =
          Attacks(params)[buff.magazine_attack].charges;
    }
    // The mask is built before anything swings, so one raised mid-swing must
    // say so itself or the strike is priced without it.
    buffs_[i].duty_phase = 0.0;
    buff_mask_ |= 1 << i;
    lever_mask_ |= 1 << i;
    laid = true;
  }
  return laid;
}

// Chosen ahead of the hardest swing on offer, and DELIBERATELY not a
// comparison: a lapsed wound lifts every swing after it for as long as it
// stands, against one swing in a hundred. A buff worth less than the swing it
// displaces would be over-cast here; the rate check that would guard it
// belongs with the first skill that needs one.
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
    // Never chased: it rides a press the fight would make for damage anyway,
    // and chasing it would spend Trickblade on 1.8 seconds of shelter.
    if (buff.needs_wound_form) {
      continue;
    }
    // Recharging, so there is no laying it this time.
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
    // A buff counting hits charges only while it is DOWN, as GMS does: its
    // uptime is bounded however fast the character fires.
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

// Puts a share of the pool back, never past full.
void CombatSim::RecoverHp(const CombatParams& params, double share) {
  if (share <= 0.0) {
    return;
  }
  player_hp_ = std::min(static_cast<double>(params.max_player_hp),
                        player_hp_ + share * params.max_player_hp);
}

void CombatSim::RunAutoCasts(const CombatParams& params, double dt) {
  // These clocks run only with something to hit: waiting on an empty map
  // earns a summon no free cast.
  const std::vector<AttackOption>& casts = AutoAttacks(params);
  auto_clocks_.resize(casts.size());
  for (int i = 0; i < static_cast<int>(casts.size()); ++i) {
    const AttackOption& cast = casts[i];
    AutoClock& clock = auto_clocks_[i];
    if (queue_.empty() || cast.interval_seconds <= 0.0) {
      continue;
    }
    // The phase is left alone rather than wound on, so the pulse does not
    // come due the instant its buff lands and again a moment later. The spent
    // count goes back with it: it is per raising.
    if (cast.needs_buff >= 0 && (buff_mask_ & (1 << cast.needs_buff)) == 0) {
      clock.pulses = 0;
      continue;
    }
    // Dismissed for another skill's summon: it comes back at the beat it
    // went out on.
    if (cast.silenced_by_buff >= 0 &&
        (buff_mask_ & (1 << cast.silenced_by_buff)) != 0) {
      continue;
    }
    // Only the form that went up pulses; the other waits out the window.
    if (cast.needs_buff_stance >= 0 &&
        buffs_[cast.needs_buff].stance != cast.needs_buff_stance) {
      clock.pulses = 0;
      continue;
    }
    // One that has spent its window falls silent for the rest of it, so
    // lengthening the buff behind it buys nothing.
    if (cast.max_pulses > 0 && clock.pulses >= cast.max_pulses) {
      continue;
    }
    clock.phase += dt;
    // A step wider than the interval owes every cast it covered.
    while (clock.phase >= cast.interval_seconds) {
      clock.phase -= cast.interval_seconds;
      ++clock.pulses;
      const AttackOption& landed =
          RepeatForm(FormToLand(clock.empowered_count, cast), clock.pulses);
      // Every strike lands in full: three sword strikes 60ms apart are one
      // moment here, each its own attack on its own enemies.
      for (int strike = 0; strike < cast.strikes_per_pulse; ++strike) {
        Strike(landed, {DamageOrigin::kOwnClock, i});
        // Per strike, as the damage is: Darkness Aura recovers for every
        // attack the aura makes.
        RecoverHp(params, landed.hp_recover_pct);
      }
      // Elquines freezes what it touches. It never SPENDS the pile --
      // ClearSwingRiders sees to that.
      CreditFreeze(params, landed);
      if (cast.max_pulses > 0 && clock.pulses >= cast.max_pulses) {
        // The poison goes off as it leaves: one more explosion at the top of
        // the ramp, landing with the last tick.
        if (cast.final_repeat_strike) {
          Strike(RepeatForm(cast, cast.max_pulses),
                 {DamageOrigin::kOwnClock, i});
        }
        // The scroll bursting as it leaves, landing with the last tick for
        // the same reason.
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
    // A half that runs only while its buff is down counts nothing while it
    // stands. The tally is kept: what was swung before is still swung.
    if (cast.silent_while_buff && cast.needs_buff >= 0 &&
        (buff_mask_ & (1 << cast.needs_buff)) != 0) {
      continue;
    }
    trigger_count_[i] += weight;
    // A swing worth more than the whole count fires the skill for each of
    // them. Nudged because a weight of a seventh has no exact double: 28 of
    // them land under the 4 they mean, firing one swing late every time.
    while (trigger_count_[i] + kCountEpsilon >= cast.attacks_per_cast) {
      trigger_count_[i] -= cast.attacks_per_cast;
      // Every strike lands in full, as a pulse's do: the afterimage's second
      // of shooting is one moment here.
      for (int strike = 0; strike < cast.strikes_per_pulse; ++strike) {
        Strike(cast, {DamageOrigin::kSwingClock, i});
      }
    }
  }
}

void CombatSim::CreditKills(const CombatParams& params) {
  // Taken before anything strikes: what these casts kill charges the NEXT
  // release.
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
    // A wide swing can bring down more than the whole count in one step, and
    // each owes a release. The remainder carries.
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
  // The plate names the form really being charged, or it would count down
  // the wrong clock.
  view_.attack_name =
      attack != nullptr
          ? (WoundFull(*attack) ? attack->wound_form->name : attack->name)
          : "";
  if (attack != nullptr) {
    // Settled once, when the swing is first aimed: a hold already running is
    // the player's key held down, and re-deciding it every step would
    // flicker.
    if (aimed_ != previous) {
      held_pulses_ =
          ChannelPulses(*attack, std::min(std::max(1, attack->max_enemies),
                                          static_cast<int>(queue_.size())));
      // The only place the bank shortens a hold: the chooser judges one on
      // its rate, which its length does not move.
      if (attack->channel.charge_seconds > 0.0) {
        held_pulses_ = std::min(held_pulses_, ChargedPulses(params, aimed_));
      }
    }
    // A cast reaches nobody, so the window keeps what the last swing set:
    // the mob bars must not collapse for the length of a cast.
    if (attack->heal_fraction <= 0.0) {
      reach_ = std::max(1, attack->max_enemies);
    }
    // Cached because the charge bar is drawn after the aim and has no attack
    // to ask. A pick changing mid-charge changes the clock under it, which is
    // the honest reading.
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
  // By index, not by pointer: a buff going up between two bolts moves the
  // fight into another attack table, where the index still holds.
  const AttackOption& attack = Attacks(params)[index];
  AttackClock& clock = attack_clocks_[index];
  clock.next_strike_seconds -= dt;
  // A step wider than the beat owes every strike it covered.
  while (clock.strikes_left > 0 && clock.next_strike_seconds <= 0.0) {
    clock.next_strike_seconds += attack.cast_interval_seconds;
    --clock.strikes_left;
    // A shock finding nothing standing is one the orb never spent, and GMS
    // hands its wait back.
    if (Reached(attack) <= 0) {
      clock.cooldown_left =
          std::max(0.0, clock.cooldown_left - attack.cooldown_refund_seconds);
      continue;
    }
    attributing_ = index;
    RecoverHp(params, Strike(attack, {DamageOrigin::kSwing, 0}));
    attributing_ = -1;
    CreditFreeze(params, attack);
    // For the buffs charged by hits. No weight: the wait a landed SWING
    // takes off was paid at the cast, and a bolt is not another swing.
    CreditBuffs(params, 0.0, attack.lines);
  }
}

void CombatSim::RunSwing(const CombatParams& params, double dt) {
  // Aimed against the queue as it stands, so the bar names the swing really
  // coming. Only the poke is re-aimed under a dying crowd.
  const AttackOption* attack = AimSwing(params);
  if (attack == nullptr) {
    return;
  }
  attack_phase_ += dt;
  // A step wider than the swing owes every swing it covered. Without this a
  // 120ms skill under a 150ms frame loses one swing in five and pins the
  // charge bar full, the phase never falling back under one swing.
  while (attack != nullptr && attack->swing_seconds > 0.0 &&
         attack_phase_ >= HeldSeconds(*attack)) {
    attack_phase_ -= HeldSeconds(*attack);
    LandSwing(params, *attack);
    // Re-aimed by the landing: the queue moved and the commitment is spent.
    attack = aimed_ >= 0 ? &Attacks(params)[aimed_] : nullptr;
  }
}

void CombatSim::LandSwing(const CombatParams& params,
                          const AttackOption& attack) {
  // Read before the strike, because aiming again below moves it.
  int swung = aimed_;
  // Everything this swing lands is the swing's, its side strike and its load
  // included: they ride it rather than happening beside it.
  attributing_ = swung;
  if (swung >= 0 && swung < static_cast<int>(swings_by_attack_.size())) {
    ++swings_by_attack_[swung];
  }
  if (attack.heal_fraction > 0.0) {
    player_hp_ =
        std::min(static_cast<double>(params.max_player_hp),
                 player_hp_ + attack.heal_fraction * params.max_player_hp);
  } else {
    // A buff GMS grants "upon use" goes up before its own swing lands, so
    // the strike is priced under it and the swing is re-read out of the set
    // the raising moved the fight into. Every other one waits for the
    // landing.
    const AttackOption* cast = &attack;
    if (LayBuffs(params, swung, /*on_cast=*/true)) {
      cast = &Attacks(params)[swung];
    }
    const AttackOption& landed =
        FormToLand(attack_clocks_[swung].empowered_count, *cast);
    // A wall of bolts strikes once per bolt, so the dead are cleared between
    // them and a bolt whose twelve are down falls on the next twelve.
    double proc_recovered =
        Strike(landed, {DamageOrigin::kSwing, 0}, held_pulses_);
    // Per strike, not per swing: each shock spends its own share, so the
    // pile drains across the barrage rather than all at its opening.
    CreditFreeze(params, landed);
    // The rest of a told-apart swing lands on its own beat while the player
    // swings on, each strike finding the crowd as it then stands.
    if (landed.strikes_in_sequence > 1 && landed.cast_interval_seconds > 0.0) {
      attack_clocks_[swung].strikes_left = landed.strikes_in_sequence - 1;
      attack_clocks_[swung].next_strike_seconds = landed.cast_interval_seconds;
    }
    // Read off the aimed attack, not off what landed: the strike belongs to
    // the skill, not to the form standing in for it. After the swing, so it
    // lands on what the swing left.
    if (cast->side != nullptr &&
        attack_clocks_[swung].side_cooldown_left <= 0.0) {
      Strike(*cast->side, {DamageOrigin::kSideStrike, swung});
      attack_clocks_[swung].side_cooldown_left = cast->side->cooldown_seconds;
    }
    // Read off the aimed attack for the same reason, and spent here: what is
    // charged is the press, not the load's own clock.
    if (cast->loaded != nullptr && cast->loaded_attack >= 0 &&
        attack_clocks_[cast->loaded_attack].charges_left > 0) {
      // A press finding fewer left than it would take spends what is there:
      // GMS's charms go out in twos to fours.
      int spent = std::min(cast->loaded->charges_per_swing,
                           attack_clocks_[cast->loaded_attack].charges_left);
      for (int i = 0; i < spent; ++i) {
        Strike(*cast->loaded, {DamageOrigin::kLoad, cast->loaded_attack});
      }
      attack_clocks_[cast->loaded_attack].charges_left -= spent;
    }
    // Recovery rides the hit, so a cast earns none. What LANDED pays it, and
    // the swing's own adds to the character's.
    double recovered =
        params.hp_recover_pct + landed.hp_recover_pct + proc_recovered;
    // A hold pays per pulse, so letting go early is worth less of the pool.
    // What the swing states is one pulse's.
    if (landed.channel.pulses > 0) {
      recovered += landed.channel.hp_recover_pct * held_pulses_;
    }
    RecoverHp(params, recovered);
    // The swing is over; a volley it sets off runs on a clock of its own.
    attributing_ = -1;
    // After the strike, so the volley lands on what the swing left standing.
    CreditSwing(params, cast->count_weight);
    // The opening strike's lines alone -- the rest of a barrage credits its
    // own as it lands -- but the whole press's WEIGHT, thirty bolts being one
    // press of the key.
    CreditBuffs(params, cast->count_weight, landed.lines);
    LayBuffs(params, swung, /*on_cast=*/false);
  }
  // GMS charges Trickblade 14 seconds cold and 20 on a wound, so the wait is
  // the FORM's where one stood in.
  double wait = WoundFull(attack) ? attack.wound_form->cooldown_seconds
                                  : attack.cooldown_seconds;
  if (wait > 0.0) {
    attack_clocks_[swung].cooldown_left = wait;
  }
  if (attack.charges > 0 && attack_clocks_[swung].charges_left > 0) {
    --attack_clocks_[swung].charges_left;
  }
  // A whole charge for part of one, as GMS spends a light per second held.
  // What is filled toward the next is kept, so the lights arrive on their own
  // clock rather than on the presses.
  const ChannelHold& hold = attack.channel;
  if (hold.charge_seconds > 0.0 && hold.pulses_per_charge > 0) {
    double spent =
        std::ceil(static_cast<double>(held_pulses_) / hold.pulses_per_charge);
    attack_clocks_[swung].hold_charges =
        std::max(0.0, attack_clocks_[swung].hold_charges - spent);
  }
  attributing_ = -1;  // nothing is left to credit to this swing
  aimed_ = -1;        // the swing landed, so the next one is chosen afresh
  AimSwing(params);
}

void CombatSim::MergeEngagedWindow(const CombatParams& params) {
  // One HP bar per type in the front window, in queue order, each averaging
  // its members' remaining HP.
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
  // Rounded up so a sliver still reads as 1 rather than as death.
  view_.player_hp = static_cast<int>(std::ceil(player_hp_));
  view_.player_max_hp = params.max_player_hp;
  view_.player_hp_fraction =
      params.max_player_hp > 0
          ? std::clamp(player_hp_ / params.max_player_hp, 0.0, 1.0)
          : 0.0;
  view_.respawns = params.respawn_seconds > 0.0;
  // Against the interval this wait began under, so the bar does not jump
  // when the totem goes up mid-cycle.
  view_.respawn_fraction =
      view_.respawns && respawn_interval_ > 0.0
          ? std::clamp(respawn_phase_ / respawn_interval_, 0.0, 1.0)
          : 0.0;
}

void CombatSim::PublishTarget(const CombatParams& params) {
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
  // Newest first: a cast ends when the swing clock climbs back to the mark
  // it was raised at, and the last raised reaches its mark first.
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

void CombatSim::Advance(const CombatParams& params, double elapsed_seconds) {
  active_ = params.active;
  measuring_ = params.measuring;
  view_.kills_this_step.assign(params.types.size(), 0);
  view_.damage_this_step = 0.0;
  view_.respawned_this_step = false;
  view_.died_this_step = false;
  ledger_.BeginStep(params.record_damage_lines);
  if (!CanFight(params)) {
    GoIdle();
    return;
  }
  // A large real-time gap is clamped to one swing, so the fight resumes
  // rather than jumping. Against the bare poke, which every character has --
  // the coming skill is not known until the swing is aimed, below. A
  // measurement gets the step it asks for: no player stalled there.
  double dt = measuring_ ? elapsed_seconds
                         : std::min(elapsed_seconds,
                                    params.attacks.front().swing_seconds);
  step_seconds_ = dt;

  BeginMapIfChanged(params);
  // A level-up widens the pool and fills it, as GMS does. The LEVEL is
  // watched, not the pool: a skill point, a scroll or a swapped hat widens it
  // too and none of those is a reason to be healed.
  if (params.player_level != player_level_) {
    player_hp_ = params.max_player_hp;
  }
  // A pool that shrank takes the player down with it.
  player_hp_ = std::min(player_hp_, static_cast<double>(params.max_player_hp));

  // Before anything swings, so the rate this step's casts are priced against
  // covers the fight up to here.
  fight_seconds_ += dt;
  // Before the hit that may need it.
  revive_left_ = std::max(0.0, revive_left_ - dt);
  RespawnBeat(params, dt);
  TakeMobHit(params, dt);
  // Grown before the buffs run: one going up now loads its magazine's swing
  // and needs that clock to exist.
  int had_clocks = static_cast<int>(attack_clocks_.size());
  attack_clocks_.resize(Attacks(params).size());
  // A bank starts FULL, as a cooldown starts ready.
  const std::vector<AttackOption>& fresh = Attacks(params);
  for (int i = had_clocks; i < static_cast<int>(attack_clocks_.size()); ++i) {
    attack_clocks_[i].hold_charges = fresh[i].channel.max_charges;
    attack_clocks_[i].charges_left = fresh[i].recharge_max;
  }
  damage_by_attack_.resize(Attacks(params).size(), 0.0);
  swings_by_attack_.resize(Attacks(params).size(), 0);
  final_attack_damage_by_attack_.resize(Attacks(params).size(), 0.0);
  burn_damage_by_attack_.resize(Attacks(params).size(), 0.0);
  // After the hit, so a buff raised now answers it with its heal, and before
  // anything attacks, so this step swings with it.
  RunBuffs(params, dt);
  // After the hit and before the swing, so a fountain pays on the step it
  // was needed.
  RunRegen(params, dt);
  // Before anything swings, so summons and the character pick targets off
  // one order, and the swing is CHOSEN against what it will hit.
  AimAtHealthiest(params);
  // Before any own clock fires: a rain that grows with the crowd reads the
  // swing that called it down, aimed last step.
  const std::vector<AttackOption>& options = Attacks(params);
  swing_enemies_ = aimed_ >= 0 && aimed_ < static_cast<int>(options.size())
                       ? Reached(options[aimed_])
                       : 0;
  RunAutoCasts(params, dt);
  // After the beat has topped the roster up, so a release charged by the
  // swing that emptied the map still finds something to fall on.
  CreditKills(params);
  // Before the swing, so a burn lit last step has ticked and a monster that
  // thawed this step is one this step's swing sees thawed.
  RunDots(dt);
  RunFreeze(dt);
  RunStun(dt);
  RunMark(dt);
  RunWound(dt);
  RunScar(dt);
  RunCooldowns(params, dt);
  // Before the swing, so a bolt in the air lands on the crowd this step
  // opened with.
  RunBarrage(params, dt);
  RunSwing(params, dt);

  player_level_ = params.player_level;
  // A measurement draws nothing, and the picture costs a string per monster
  // per step.
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
    // One waiting on a swing or on lines moves at a swing boundary.
    if (buff.laid_by_attack < 0 && buff.charge_lines <= 0 &&
        buff.duration_seconds > 0.0) {
      soonest = std::min(soonest, clock.cooldown_left);
    }
  }
  return soonest;
}

}  // namespace ms
