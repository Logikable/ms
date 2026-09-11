#include "src/combat/damage_ledger.h"

#include <map>
#include <vector>

namespace ms {

void DamageLedger::BeginStep(bool recording) {
  recording_ = recording;
  lines_this_step_.clear();
  strikes_of_event_.clear();
}

void DamageLedger::OpenLandings(int mobs, int hit, DamageSource source) {
  if (!recording_) {
    return;
  }
  landing_source_ = source;
  landing_event_.assign(mobs, 0);
  for (int j = 0; j < hit && j < mobs; ++j) {
    landing_event_[j] = ++next_event_;
  }
}

Landing DamageLedger::LandingAt(int mob_id, int index, double scale) const {
  if (!recording_) {
    return {0, 0, {}, scale};
  }
  int event = index < static_cast<int>(landing_event_.size())
                  ? landing_event_[index]
                  : 0;
  return {mob_id, event, landing_source_, scale};
}

void DamageLedger::RecordLine(const Landing& landing, double damage,
                              bool crit) {
  if (!recording_) {
    return;
  }
  FileLine(landing, damage, crit, NextStrike(landing.event));
}

int DamageLedger::NextStrike(int event) {
  return strikes_of_event_[event]++;
}

void DamageLedger::FileLine(const Landing& landing, double damage, bool crit,
                            int strike) {
  lines_this_step_.push_back(
      {landing.mob_id, landing.event, strike, landing.source, damage, crit});
}

void DamageLedger::RecordRolls(const Landing& landing, double damage) {
  if (!recording_) {
    return;
  }
  // One strike for the whole roll: its lines fell together and are drawn
  // together.
  int strike = NextStrike(landing.event);
  for (const LineRoll& roll : line_rolls_) {
    FileLine(landing, damage * roll.share, roll.crit, strike);
  }
}

std::vector<LineRoll>* DamageLedger::LineSink() {
  return recording_ ? &line_rolls_ : nullptr;
}

}  // namespace ms
