#include "src/combat/fight.h"

#include <algorithm>
#include <cmath>

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

}  // namespace

void CombatSim::TopUp(const CombatParams& params) {
  std::vector<int> standing(params.types.size(), 0);
  for (const QueuedMob& mob : queue_) {
    ++standing[mob.type];
  }
  int first_new = static_cast<int>(queue_.size());
  for (int i = 0; i < static_cast<int>(params.types.size()); ++i) {
    for (int k = standing[i]; k < params.types[i].simultaneous; ++k) {
      queue_.push_back({i, static_cast<double>(params.types[i].mob->max_hp())});
    }
  }
  // Interleave the newcomers so a swing does not face one whole type at a
  // time. Only they are shuffled: the mobs already in the queue are being
  // fought, and moving a wounded one out of the front window would hand back
  // the damage done to it.
  std::shuffle(queue_.begin() + first_new, queue_.end(), rng_);
}

int CombatSim::LeadTarget(const AttackOption& attack, int hit) const {
  if (attack.lead_damage.empty() || hit <= 0) {
    return -1;
  }
  int lead = 0;
  for (int j = 1; j < hit; ++j) {
    if (queue_[j].hp > queue_[lead].hp) {
      lead = j;
    }
  }
  return lead;
}

// What one swing of `attack` would land on the queue as it stands: its own
// damage to each mob it reaches, the opening hit on one of them, and the Final
// Attack that follows it onto every one of them.
double CombatSim::SwingDamage(const AttackOption& attack) const {
  int reach = std::max(1, attack.max_enemies);
  int hit = std::min(reach, static_cast<int>(queue_.size()));
  double total = 0.0;
  for (int j = 0; j < hit; ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.damage_per_hit.size())) {
      total += attack.damage_per_hit[type];
    }
  }
  int lead = LeadTarget(attack, hit);
  if (lead >= 0 &&
      queue_[lead].type < static_cast<int>(attack.lead_damage.size())) {
    total += attack.lead_damage[queue_[lead].type];
  }
  for (int j = 0; j < hit; ++j) {
    int type = queue_[j].type;
    if (type < static_cast<int>(attack.final_attack_damage.size())) {
      total += attack.final_attack_damage[type];
    }
  }
  // A swing with an empowered form lands it once in every N, so what the
  // attack is worth per swing is the average of the two. The rate has to say
  // so, or the attack would be weighed on the weaker of the two things it
  // does. The form has no form of its own, so this recurs exactly once.
  if (attack.empowered != nullptr && attack.empowered_every > 0) {
    if (!attack.brands_enemies) {
      total +=
          (SwingDamage(*attack.empowered) - total) / attack.empowered_every;
      return total;
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
  return total;
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
    if (i < static_cast<int>(cooldown_left_.size()) &&
        cooldown_left_[i] > 0.0) {
      continue;
    }
    // Per second, not per swing: a skill that hits half again as hard but takes
    // twice as long is worse, and only the rate says so.
    double rate = SwingDamage(attack) / attack.swing_seconds;
    if (rate > best_rate) {
      best_rate = rate;
      best = i;
    }
  }
  return best;
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
    if (i < static_cast<int>(cooldown_left_.size()) &&
        cooldown_left_[i] > 0.0) {
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
  cooldown_left_.resize(Attacks(params).size(), 0.0);
  for (double& left : cooldown_left_) {
    left = std::max(0.0, left - dt);
  }
}

void CombatSim::Strike(const AttackOption& attack) {
  // One strike hits the front mobs at once; each takes its own type's damage.
  // Overkill on any of them is wasted. Dead mobs leave the queue and the ones
  // behind slide into the window next time.
  int hit = std::min(std::max(1, attack.max_enemies),
                     static_cast<int>(queue_.size()));
  // Picked before anything lands, so the opening hit chooses by the HP the
  // mobs went into the swing with rather than what the spread left them on.
  int lead = LeadTarget(attack, hit);
  for (int j = 0; j < hit; ++j) {
    queue_[j].hp -= DamageToBranded(attack, j);
  }
  if (lead >= 0) {
    queue_[lead].hp -= attack.lead_damage[queue_[lead].type];
  }
  // A Final Attack rolls separately against every enemy the swing reached, so
  // in expectation each of them takes it.
  if (!attack.final_attack_damage.empty()) {
    for (int j = 0; j < hit; ++j) {
      queue_[j].hp -= attack.final_attack_damage[queue_[j].type];
    }
  }
  std::vector<QueuedMob> survivors;
  survivors.reserve(queue_.size());
  for (int j = 0; j < static_cast<int>(queue_.size()); ++j) {
    if (j < hit && queue_[j].hp <= 0.0) {
      ++kills_this_step_[queue_[j].type];
    } else {
      survivors.push_back(queue_[j]);
    }
  }
  queue_ = std::move(survivors);
}

double CombatSim::DamageToBranded(const AttackOption& attack, int index) {
  double ordinary = attack.damage_per_hit[queue_[index].type];
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
  return ordinary + attack.empowered->damage_per_hit[queue_[index].type];
}

const AttackOption& CombatSim::FormToLand(std::vector<int>& counts, int size,
                                          int index,
                                          const AttackOption& attack) {
  counts.resize(size, 0);
  // A form that marks enemies never stands in for the swing: the swing lands
  // as itself, and DamageToBranded decides mob by mob what goes off on top.
  if (attack.empowered == nullptr || attack.empowered_every <= 0 ||
      attack.brands_enemies || index < 0) {
    return attack;
  }
  // Counted before the test, so a period of four is three ordinary landings
  // and then this one -- not this one first and three after.
  if (++counts[index] < attack.empowered_every) {
    return attack;
  }
  counts[index] = 0;
  return *attack.empowered;
}

int CombatSim::player_hp() const {
  return static_cast<int>(std::ceil(player_hp_));
}

void CombatSim::GoIdle() {
  initialized_ = false;
  respawning_ = false;
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
  auto_phase_.clear();
  cooldown_left_.clear();
  aimed_ = -1;
}

void CombatSim::BeginMapIfChanged(const CombatParams& params) {
  // The queue holds indices into the map's types, and its HP values are that
  // map's mobs'. Carried to another map, both would describe the wrong
  // monsters.
  if (initialized_ && map_ == params.map) {
    return;
  }
  map_ = params.map;
  respawn_phase_ = 0.0;
  attack_phase_ = 0.0;
  hit_phase_ = 0.0;
  auto_phase_.assign(params.auto_attacks.size(), 0.0);
  auto_empowered_count_.assign(params.auto_attacks.size(), 0);
  cooldown_left_.assign(params.attacks.size(), 0.0);
  // The buff clocks are deliberately left alone: they belong to the character
  // rather than to the map, and walking somewhere else neither takes a buff
  // away nor hands one back early.
  // Another map's attacks were another map's indices, and nothing here is
  // part-way through a swing at it any more.
  aimed_ = -1;
  player_hp_ = params.max_player_hp;
  queue_.clear();
  TopUp(params);
  initialized_ = true;
}

void CombatSim::RespawnBeat(const CombatParams& params, double dt) {
  respawn_phase_ += dt;
  if (respawn_phase_ < params.respawn_seconds) {
    return;
  }
  respawn_phase_ -= params.respawn_seconds;
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
  double taken = params.types[queue_.front().type].damage_to_player;
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
  front.hp -= params.damage_reflect_pct * damage_taken;
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

const std::vector<AttackOption>& CombatSim::AutoAttacks(
    const CombatParams& params) const {
  return params.AutoAttacks(buff_mask_);
}

const std::vector<AttackOption>& CombatSim::TriggeredAttacks(
    const CombatParams& params) const {
  return params.TriggeredAttacks(buff_mask_);
}

void CombatSim::RunBuffs(const CombatParams& params, double dt) {
  int count = static_cast<int>(params.buffs.size());
  buff_left_.resize(count, 0.0);
  buff_cooldown_left_.resize(count, 0.0);
  buff_mask_ = 0;
  for (int i = 0; i < count; ++i) {
    const BuffOption& buff = params.buffs[i];
    buff_left_[i] = std::max(0.0, buff_left_[i] - dt);
    buff_cooldown_left_[i] = std::max(0.0, buff_cooldown_left_[i] - dt);
    // Put up the moment it comes round, and only with something to fight: one
    // spent on an empty map is one the player does not have when the mobs
    // land. Nothing is recast while it is still standing -- a player timing
    // these would not throw the tail of one away.
    //
    // A buff its own swing lays is not raised here at all: it waits for that
    // swing to land. See LayBuffs.
    if (buff.laid_by_attack < 0 && buff_left_[i] <= 0.0 &&
        buff_cooldown_left_[i] <= 0.0 && !queue_.empty() &&
        buff.duration_seconds > 0.0) {
      buff_left_[i] = buff.duration_seconds;
      buff_cooldown_left_[i] = buff.cooldown_seconds;
      player_hp_ =
          std::min(static_cast<double>(params.max_player_hp),
                   player_hp_ + buff.heal_fraction * params.max_player_hp);
    }
    if (buff_left_[i] > 0.0) {
      buff_mask_ |= 1 << i;
    }
  }
}

void CombatSim::LayBuffs(const CombatParams& params, int swung) {
  for (int i = 0; i < static_cast<int>(buff_left_.size()); ++i) {
    const BuffOption& buff = params.buffs[i];
    if (buff.laid_by_attack != swung) {
      continue;
    }
    // Refreshed rather than stacked, and its wait started from the swing that
    // laid it: what a second puncture leaves is one wound, not two.
    buff_left_[i] = buff.duration_seconds;
    buff_cooldown_left_[i] = buff.cooldown_seconds;
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
    if (i < static_cast<int>(buff_left_.size()) && buff_left_[i] > 0.0) {
      continue;  // still standing, so there is nothing to go and do
    }
    // The swing itself may be recharging, in which case there is no laying it
    // this time and the fight swings for damage instead.
    if (buff.laid_by_attack < static_cast<int>(cooldown_left_.size()) &&
        cooldown_left_[buff.laid_by_attack] > 0.0) {
      continue;
    }
    return buff.laid_by_attack;
  }
  return -1;
}

void CombatSim::CreditBuffs(const CombatParams& params, double weight) {
  for (int i = 0; i < static_cast<int>(buff_cooldown_left_.size()); ++i) {
    buff_cooldown_left_[i] =
        std::max(0.0, buff_cooldown_left_[i] -
                          params.buffs[i].cooldown_reduction_seconds * weight);
  }
}

void CombatSim::RunAutoCasts(const CombatParams& params, double dt) {
  // Their clocks run only while there is something to hit: a summon has
  // nothing to do on an empty map, and waiting there earns it no free cast.
  const std::vector<AttackOption>& casts = AutoAttacks(params);
  auto_phase_.resize(casts.size(), 0.0);
  for (int i = 0; i < static_cast<int>(casts.size()); ++i) {
    const AttackOption& cast = casts[i];
    if (queue_.empty() || cast.interval_seconds <= 0.0) {
      continue;
    }
    auto_phase_[i] += dt;
    if (auto_phase_[i] >= cast.interval_seconds) {
      auto_phase_[i] -= cast.interval_seconds;
      Strike(FormToLand(auto_empowered_count_, static_cast<int>(casts.size()),
                        i, cast));
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
      Strike(cast);
    }
  }
}

const AttackOption* CombatSim::AimSwing(const CombatParams& params) {
  aimed_ = ChooseAttack(params);
  const AttackOption* attack = aimed_ >= 0 ? &Attacks(params)[aimed_] : nullptr;
  attack_name_ = attack != nullptr ? attack->name : "";
  if (attack != nullptr) {
    // A cast reaches nobody, so it leaves the window on whatever the last
    // swing set: the mob bars must not collapse for the length of the cast.
    if (attack->heal_fraction <= 0.0) {
      reach_ = std::max(1, attack->max_enemies);
    }
    // Cached because the charge bar is drawn after the swing is aimed and has
    // no attack of its own to ask. A pick that changes mid-charge changes the
    // clock under it, which is the honest reading: the swing being charged is
    // the one that will land.
    swing_seconds_ = attack->swing_seconds;
  }
  return attack;
}

void CombatSim::RunSwing(const CombatParams& params, double dt) {
  // Aimed against the queue as it stands, so the charge bar names the swing
  // that is really coming. Only the poke is re-aimed as mobs die out from
  // under it; a skill winding up is committed to. Aimed again after the
  // strike, because the queue just moved and the commitment is discharged.
  const AttackOption* attack = AimSwing(params);
  if (attack == nullptr) {
    return;
  }
  attack_phase_ += dt;
  if (attack_phase_ < attack->swing_seconds) {
    return;
  }
  attack_phase_ -= attack->swing_seconds;
  // Read before the strike, because aiming again below moves it.
  int swung = aimed_;
  if (attack->heal_fraction > 0.0) {
    player_hp_ =
        std::min(static_cast<double>(params.max_player_hp),
                 player_hp_ + attack->heal_fraction * params.max_player_hp);
  } else {
    Strike(FormToLand(empowered_count_,
                      static_cast<int>(Attacks(params).size()), swung,
                      *attack));
    // Recovery rides the hit, so a cast does not earn it and neither does a
    // swing at nothing.
    player_hp_ =
        std::min(static_cast<double>(params.max_player_hp),
                 player_hp_ + params.hp_recover_pct * params.max_player_hp);
    // Credited after the strike, so the volley lands on what the swing left
    // standing rather than on mobs it was about to kill anyway. A healing cast
    // credits nothing: it is not an attack.
    CreditSwing(params, attack->count_weight);
    // Attacking is what brings a buff round sooner, so the same swing that
    // credits the volleys credits the buffs. A cast credits neither.
    CreditBuffs(params, attack->count_weight);
    LayBuffs(params, swung);
  }
  if (attack->cooldown_seconds > 0.0) {
    cooldown_left_[swung] = attack->cooldown_seconds;
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

void CombatSim::PublishTarget(const CombatParams& params) {
  engaged_groups_.clear();
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
  died_this_step_ = false;
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

  // Before the hit that may need it, so a wait that runs out this step is one
  // the player has the benefit of.
  revive_left_ = std::max(0.0, revive_left_ - dt);
  RespawnBeat(params, dt);
  TakeMobHit(params, dt);
  // After the hit, so a buff going up now answers it with its heal, and
  // before everything that attacks, so this step swings with it.
  RunBuffs(params, dt);
  // After the hit and before the swing, so a fountain is worth something on
  // the step it was needed rather than only on the next one.
  player_hp_ = std::min(
      static_cast<double>(params.max_player_hp),
      player_hp_ + params.regen_pct_per_second * params.max_player_hp * dt);
  RunAutoCasts(params, dt);
  RunCooldowns(params, dt);
  RunSwing(params, dt);

  player_max_hp_ = params.max_player_hp;
  player_level_ = params.player_level;
  player_hp_fraction_ =
      params.max_player_hp > 0
          ? std::clamp(player_hp_ / params.max_player_hp, 0.0, 1.0)
          : 0.0;
  PublishTarget(params);
}

}  // namespace ms
