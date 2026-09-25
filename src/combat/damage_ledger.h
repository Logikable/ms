/* Records a fight's damage as it lands: how much hit which monster, grouped so
 * that one attack's lines show as one stack of numbers.
 *
 * It is separate from the fight because it is write-only from the fight's side.
 * Nothing recorded here affects how the fight plays out.
 */
#ifndef MS_SRC_COMBAT_DAMAGE_LEDGER_H_
#define MS_SRC_COMBAT_DAMAGE_LEDGER_H_

#include <map>
#include <string>
#include <vector>

#include "src/combat/damage.h"

namespace ms {

// What dealt the damage, for display. All of the character's own attacks share
// one source, whatever the skill. Everything else is its own source, so a
// summon's numbers never replace a burn's.
enum class DamageOrigin {
  kSwing,
  kOwnClock,    // a summon, or another skill on its own timer
  kSwingClock,  // a skill triggered by landed attacks rather than by time
  kKillClock,   // a skill triggered by kills
  kSideStrike,  // an extra strike an attack triggers alongside itself
  kBurn,
  kLoad,  // a charge another skill stored for this attack to spend
};

struct DamageSource {
  DamageOrigin origin = DamageOrigin::kSwing;
  // Which one, when the origin has several: which summon, which burn slot. 0
  // for the character's attacks.
  int index = 0;
};

// Two lines from the same source on the same monster go in the same stack.
inline bool operator==(const DamageSource& a, const DamageSource& b) {
  return a.origin == b.origin && a.index == b.index;
}

// Ordering so DamageSource can be a map key. The order itself means nothing.
inline bool operator<(const DamageSource& a, const DamageSource& b) {
  if (a.origin != b.origin) {
    return a.origin < b.origin;
  }
  return a.index < b.index;
}

// One damage line, for callers that draw the fight. Every line one attack puts
// on a monster shares an `event`, so an eight-line attack shows as one stack of
// eight.
struct DamageLine {
  int mob_id = 0;
  int event = 0;
  // Which hit of the event this line belongs to. Each roll is one hit, so
  // twelve slashes make twelve hits in one event, as do a Final Attack and the
  // attack that triggered it on the same monster. See DamageStack.
  int strike = 0;
  DamageSource source;
  double damage = 0.0;
  bool crit = false;
  // The skill this line is credited to in the damage breakdown (see
  // DamageLedger::credit_name), and the cast it belongs to. Every line from one
  // attack shares a cast, whichever monster it hits; a held skill gets a new
  // cast per pulse.
  int credit = -1;
  int cast = 0;
};

// Where a landing is recorded and how it is scaled: the monster, the event, the
// source, and any multiplier applied after the roll. Passed even when not
// recording; it is then simply ignored.
struct Landing {
  int mob_id = 0;
  int event = 0;
  DamageSource source;
  double scale = 1.0;
  int credit = -1;
  int cast = 0;
};

// When not recording, the ledger accepts everything and stores nothing, so the
// fight never needs to check.
class DamageLedger {
 public:
  // Starts a step. Callers see only the lines recorded after this.
  void BeginStep(bool recording);
  bool recording() const {
    return recording_;
  }
  // Every line recorded this step so far, in order.
  const std::vector<DamageLine>& lines_this_step() const {
    return lines_this_step_;
  }

  // Gives each of the first `hit` of `mobs` its own event, so all of one
  // attack's lines on a monster group together, and remembers the source.
  // Numbers `casts` casts starting from this landing: a five-pulse hold is five
  // casts.
  void OpenLandings(int mobs, int hit, DamageSource source,
                    const std::string& credit, int casts = 1);
  // The landing for the monster at `index`, scaled by `scale`, using the event
  // OpenLandings gave it.
  Landing LandingAt(int mob_id, int index, double scale) const;
  // A new event for a landing that stands alone. Counted even when not
  // recording, so event numbers are the same either way.
  int NextEvent() {
    return ++next_event_;
  }
  // A landing with its own event and cast, such as one tick of a burn.
  Landing StandAlone(int mob_id, DamageSource source, int credit);

  // The ID `skill` is credited under, or -1 when not recording. IDs are never
  // reused, so they stay valid across steps.
  int Credit(const std::string& skill);
  const std::string& credit_name(int credit) const;
  // Records one line of `damage`, already scaled, against `landing`. Each call
  // is one hit of the landing's event.
  void RecordLine(const Landing& landing, double damage, bool crit);
  // Records the lines from the last RollFactor as a landing of `damage`, each
  // line getting its share. Each call is one hit.
  void RecordRolls(const Landing& landing, double damage);
  // Where a roll writes its per-line shares: the scratch buffer, or null when
  // not recording.
  std::vector<LineRoll>* LineSink();

 private:
  // Whether lines are being recorded at all, from the params.
  bool recording_ = false;
  // Assigned to each landing and never reused within a step, which is as long
  // as anything keeps one.
  int next_event_ = 0;
  // Never reused at all, so the breakdown can spot a new cast by its ID alone.
  int next_cast_ = 0;
  std::vector<std::string> credit_names_;
  std::map<std::string, int> credit_of_name_;
  // The event for each queued mob's lines in the current landing, parallel to
  // the queue, plus the current source.
  std::vector<int> landing_event_;
  DamageSource landing_source_;
  int landing_credit_ = -1;
  int landing_cast_ = 0;
  // Hits taken per event, so one attack's rolls on one monster can be told
  // apart. A map rather than a counter, because an event is revisited after the
  // other monsters are hit.
  std::map<int, int> strikes_of_event_;
  std::vector<DamageLine> lines_this_step_;
  // Buffer RollFactor writes its per-line shares into. Reused every roll, so
  // recording allocates once instead of once per line.
  std::vector<LineRoll> line_rolls_;

  // Returns the next hit number for `event` and advances it.
  int NextStrike(int event);
  // Records one line under a hit number already taken.
  void FileLine(const Landing& landing, double damage, bool crit, int strike);
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_LEDGER_H_
