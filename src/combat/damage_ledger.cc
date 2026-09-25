#include "src/combat/damage_ledger.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace ms {

void DamageLedger::BeginStep(bool recording) {
  recording_ = recording;
  lines_this_step_.clear();
  strikes_of_event_.clear();
}

void DamageLedger::OpenLandings(int mobs, int hit, DamageSource source,
                                const std::string& credit, int casts) {
  if (!recording_) {
    return;
  }
  landing_source_ = source;
  landing_credit_ = Credit(credit);
  landing_cast_ = next_cast_ + 1;
  next_cast_ += std::max(1, casts);
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
  return {mob_id, event,           landing_source_,
          scale,  landing_credit_, landing_cast_};
}

Landing DamageLedger::StandAlone(int mob_id, DamageSource source, int credit) {
  int event = NextEvent();
  if (!recording_) {
    return {0, event, source, 1.0};
  }
  return {mob_id, event, source, 1.0, credit, ++next_cast_};
}

int DamageLedger::Credit(const std::string& skill) {
  if (!recording_) {
    return -1;
  }
  std::map<std::string, int>::const_iterator it = credit_of_name_.find(skill);
  if (it != credit_of_name_.end()) {
    return it->second;
  }
  int credit = static_cast<int>(credit_names_.size());
  credit_names_.push_back(skill);
  credit_of_name_.emplace(skill, credit);
  return credit;
}

const std::string& DamageLedger::credit_name(int credit) const {
  static const std::string* const kNone = new std::string();
  if (credit < 0 || credit >= static_cast<int>(credit_names_.size())) {
    return *kNone;
  }
  return credit_names_[credit];
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
  lines_this_step_.push_back({landing.mob_id, landing.event, strike,
                              landing.source, damage, crit, landing.credit,
                              landing.cast});
}

void DamageLedger::RecordRolls(const Landing& landing, double damage) {
  if (!recording_) {
    return;
  }
  // The whole roll is one hit: its lines landed together and are drawn
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
