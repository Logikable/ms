/* damage_ledger.h holds the record of a fight's damage as it lands: what fell
 * on which monster, grouped so that one attack's lines read as one stack of
 * numbers rather than a stack apiece.
 *
 * Kept apart from the fight because everything else only writes into it. The
 * fight tells it what landed; nothing about how the fight goes is decided
 * here, and nothing here is read back by the fight.
 */
#ifndef MS_SRC_COMBAT_DAMAGE_LEDGER_H_
#define MS_SRC_COMBAT_DAMAGE_LEDGER_H_

#include <map>
#include <string>
#include <vector>

#include "src/combat/damage.h"

namespace ms {

// What did the damage, for a caller drawing it. The character's own SWING is
// ONE source however many skills they swing; everything else is a source
// apiece, so a summon's numbers never displace a burn's.
enum class DamageOrigin {
  kSwing,
  kOwnClock,    // a summon, or a skill on a clock of its own
  kSwingClock,  // a skill fired by swings landed rather than by seconds
  kKillClock,   // a skill fired by enemies defeated
  kSideStrike,  // the strike a swing sets off beside itself
  kBurn,
  kLoad,  // the load another skill left for this press to spend
};

struct DamageSource {
  DamageOrigin origin = DamageOrigin::kSwing;
  // Which one, where the origin has more than one: which summon, which burn's
  // slot. 0 for the swing, which is one source.
  int index = 0;
};

// Two lines from the same source on the same monster belong to the same stack
// of numbers.
inline bool operator==(const DamageSource& a, const DamageSource& b) {
  return a.origin == b.origin && a.index == b.index;
}

// So a tally can be kept by source. The order means nothing to a reader; what
// it is for is a map key.
inline bool operator<(const DamageSource& a, const DamageSource& b) {
  if (a.origin != b.origin) {
    return a.origin < b.origin;
  }
  return a.index < b.index;
}

// One landed line, for a caller drawing the fight rather than only stepping
// it. `event` is shared by every line one attack put on that monster, so an
// eight-line swing reads as one stack of eight.
struct DamageLine {
  int mob_id = 0;
  int event = 0;
  // Which landing of the event this line belongs to. One roll is one strike,
  // so twelve slashes file twelve under the one event -- as do a Final Attack
  // and a lead hit onto the same monster. See DamageStack.
  int strike = 0;
  DamageSource source;
  double damage = 0.0;
  bool crit = false;
  // The skill it is filed under in a damage breakdown, as DamageLedger::
  // credit_name reads it, and the cast it belongs to. Every line of one swing
  // shares a cast whatever it lands on; a held swing takes one per pulse.
  int credit = -1;
  int cast = 0;
};

// Where a landing is filed and what scales it: the monster, the event, what
// did it, and whatever multiplies it after the roll. Passed even by a fight
// that is not recording, which files nothing whatever it is handed.
struct Landing {
  int mob_id = 0;
  int event = 0;
  DamageSource source;
  double scale = 1.0;
  int credit = -1;
  int cast = 0;
};

// A ledger that is not recording accepts everything and files nothing, so the
// fight never has to ask whether anybody is reading.
class DamageLedger {
 public:
  // Opens the step: the lines a caller reads are the ones filed since this.
  void BeginStep(bool recording);
  bool recording() const {
    return recording_;
  }
  // Every line landed during the step so far, in the order they landed.
  const std::vector<DamageLine>& lines_this_step() const {
    return lines_this_step_;
  }

  // Gives each of the front `hit` of `mobs` its own event, so the lines one
  // attack puts on one monster group together however many ways it reaches
  // them, and remembers what is doing the damage. `casts` are numbered from
  // the landing's own: a hold of five pulses is five casts.
  void OpenLandings(int mobs, int hit, DamageSource source,
                    const std::string& credit, int casts = 1);
  // Where the landing on the monster at `index` is filed, scaled by `scale`.
  // The event is the one OpenLandings gave it.
  Landing LandingAt(int mob_id, int index, double scale) const;
  // An event nothing else shares, for a landing that stands alone. Counted
  // whether or not anybody is recording, so the numbers mean the same.
  int NextEvent() {
    return ++next_event_;
  }
  // A landing nothing else shares, on an event and a cast of its own: one tick
  // of a burn.
  Landing StandAlone(int mob_id, DamageSource source, int credit);

  // The number `skill` is filed under, or -1 when nothing is being recorded.
  // Numbers are never reused, so one held across steps stays good.
  int Credit(const std::string& skill);
  const std::string& credit_name(int credit) const;
  // Files one line of `damage`, already scaled, against `landing`. One call
  // is one strike of the landing's event.
  void RecordLine(const Landing& landing, double damage, bool crit);
  // Files what the last RollFactor left in the sink as a landing of `damage`,
  // each line taking its share. One call is one strike.
  void RecordRolls(const Landing& landing, double damage);
  // Where a roll should write its per-line shares: the scratch buffer, or
  // nowhere at all when nobody is reading the record.
  std::vector<LineRoll>* LineSink();

 private:
  // Whether the lines are being filed at all, from the params.
  bool recording_ = false;
  // Stamped onto each landing and never reused within a step, which is as long
  // as anything holds one.
  int next_event_ = 0;
  // Never reused at all, so a breakdown counting casts can tell a new one by
  // its number alone.
  int next_cast_ = 0;
  std::vector<std::string> credit_names_;
  std::map<std::string, int> credit_of_name_;
  // The event each queued mob's lines are filed under for the landing being
  // worked out, parallel to the queue, and what is doing the damage.
  std::vector<int> landing_event_;
  DamageSource landing_source_;
  int landing_credit_ = -1;
  int landing_cast_ = 0;
  // Strikes each event has taken, so the rolls one attack lands on one
  // monster are told apart. Keyed rather than counted through: an event is
  // come back to once every other monster is hit.
  std::map<int, int> strikes_of_event_;
  std::vector<DamageLine> lines_this_step_;
  // Where RollFactor writes its per-line shares, reused every roll so a
  // recording fight allocates once rather than once a line.
  std::vector<LineRoll> line_rolls_;

  // The strike number the next roll against `event` takes, and one more taken.
  int NextStrike(int event);
  // Files one line under a strike already counted.
  void FileLine(const Landing& landing, double damage, bool crit, int strike);
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_LEDGER_H_
