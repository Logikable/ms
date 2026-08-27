#include "src/multiplayer/party_fight.h"

#include <string>
#include <utility>
#include <vector>

#include "src/combat/fight_authority.h"
#include "src/multiplayer/client.h"
#include "src/protos/multiplayer.pb.h"

namespace ms {
namespace {

BossRunState StateOf(FightState::Stage stage) {
  switch (stage) {
    case FightState::COUNTDOWN:
      return BossRunState::kCountdown;
    case FightState::PHASE_GAP:
      return BossRunState::kPhaseGap;
    default:
      return BossRunState::kFighting;
  }
}

BossRunState StateOf(FightEnded::Outcome outcome) {
  switch (outcome) {
    case FightEnded::CLEARED:
      return BossRunState::kWon;
    case FightEnded::TIMED_OUT:
      return BossRunState::kTimedOut;
    default:
      return BossRunState::kAborted;
  }
}

// What did the damage. An origin the build does not know is read as a swing,
// which is the one every character has.
DamageSource SourceOf(const FightDamage& line) {
  DamageSource source;
  if (line.origin() >= static_cast<int>(DamageOrigin::kSwing) &&
      line.origin() <= static_cast<int>(DamageOrigin::kBurn)) {
    source.origin = static_cast<DamageOrigin>(line.origin());
  }
  source.index = line.source_index();
  return source;
}

}  // namespace

PartyFightAuthority::PartyFightAuthority(MultiplayerClient& client)
    : client_(&client) {
}

void PartyFightAuthority::Leave() {
  left_ = fight_id_;
  client_->LeaveFight();
}

void PartyFightAuthority::Forget() {
  fight_ = SharedFight();
  fighting_ = false;
  told_ = false;
  boss_key_.clear();
  difficulty_index_ = 0;
  fight_id_.clear();
}

void PartyFightAuthority::Advance(const std::string& account_id) {
  for (const ServerMessage& message : client_->TakeFightMessages()) {
    if (message.has_fight_state()) {
      TakeState(message.fight_state(), account_id);
    } else if (message.has_fight_ended()) {
      TakeEnd(message.fight_ended());
    }
  }
}

void PartyFightAuthority::TakeState(const FightState& state,
                                    const std::string& account_id) {
  if (!left_.empty() && state.fight_id() == left_) {
    return;
  }
  fight_id_ = state.fight_id();
  fighting_ = true;
  told_ = true;
  boss_key_ = state.boss_key();
  difficulty_index_ = state.difficulty_index();
  fight_.state = StateOf(state.stage());
  fight_.phase = state.phase();
  fight_.seconds_left = state.seconds_left();
  fight_.countdown_left = state.countdown_left();
  fight_.hp_fractions.assign(state.hp_fractions().begin(),
                             state.hp_fractions().end());
  // Everyone the fight began with is in here, gone or not, which is what a
  // clear is split by. FightEnded says the same number again at the end.
  fight_.share_count = state.players_size();
  fight_.players.clear();
  fight_.self = -1;
  for (int i = 0; i < state.players_size(); ++i) {
    const FightPlayerState& player = state.players(i);
    if (player.account_id() == account_id) {
      fight_.self = i;
    }
    fight_.players.push_back({player.account_id(), player.name(), player.spot(),
                              player.present(), player.attack_name(),
                              player.attack_fraction()});
    // The player's own lines are not passed back to them: they drew those as
    // they landed them.
    if (player.account_id() == account_id) {
      continue;
    }
    for (const FightDamage& line : player.lines()) {
      fight_.lines.push_back({i, line.slot(), line.event(), SourceOf(line),
                              line.damage(), line.crit()});
    }
  }
}

void PartyFightAuthority::TakeEnd(const FightEnded& ended) {
  told_ = true;
  fight_.state = StateOf(ended.outcome());
  fight_.share_count = ended.share_count();
  fight_.awards.clear();
  for (const FightAward& award : ended.awards()) {
    SharedAward won;
    if (award.has_equip()) {
      won.drop.set_equip(award.equip());
    } else {
      won.drop.set_item(award.item());
    }
    won.count = award.count();
    fight_.awards.push_back(std::move(won));
  }
}

bool PartyFightAuthority::Fetch(SharedFight& fight) {
  if (!told_) {
    return false;
  }
  fight = fight_;
  // Drawn once. A stack stays on screen for its own beat afterwards.
  fight_.lines.clear();
  return true;
}

void PartyFightAuthority::Report(const FightReport& report) {
  FightUpdate update;
  update.set_phase(report.phase);
  update.set_spot(report.spot);
  update.set_attack_name(report.attack_name);
  update.set_attack_fraction(report.attack_fraction);
  update.set_item_drop_pct(report.item_drop_pct);
  for (const SharedLine& line : report.lines) {
    FightDamage* sent = update.add_lines();
    sent->set_slot(line.slot);
    sent->set_event(line.event);
    sent->set_origin(static_cast<int>(line.source.origin));
    sent->set_source_index(line.source.index);
    sent->set_damage(line.damage);
    sent->set_crit(line.crit);
  }
  client_->SendFightUpdate(update);
}

}  // namespace ms
