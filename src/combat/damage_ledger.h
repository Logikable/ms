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

#include <vector>

#include "src/combat/damage.h"

namespace ms {

// What did the damage, for a caller drawing it. The character's own SWING is
// one source however many skills they swing, so a new swing takes the place of
// the last whatever it was. Everything else is a source apiece, held apart so
// a summon's numbers never take the place of a burn's.
enum class DamageOrigin {
  kSwing,
  kOwnClock,    // a summon, or a skill on a clock of its own
  kSwingClock,  // a skill fired by swings landed rather than by seconds
  kKillClock,   // a skill fired by enemies defeated
  kSideStrike,  // the strike a swing sets off beside itself
  kBurn,
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

// One line of damage as it landed on one monster, for a caller drawing the
// fight rather than only stepping it. `event` is shared by every line one
// attack put on that monster, so an eight-line swing reads as one stack of
// eight rather than eight stacks of one.
struct DamageLine {
  int mob_id = 0;
  int event = 0;
  DamageSource source;
  double damage = 0.0;
  bool crit = false;
};

// Where a landing is being filed and what scales it, for that record: the
// monster it fell on, the event it belongs to, what did it, and whatever
// multiplies it after the roll -- the Freeze Stacks the swing spent, what an
// arrow gained as it travelled. Passed even by a fight that is not recording,
// which files nothing whatever it is handed.
struct Landing {
  int mob_id = 0;
  int event = 0;
  DamageSource source;
  double scale = 1.0;
};

// The record itself. A ledger that is not recording accepts everything and
// files nothing, so the fight never has to ask whether anybody is reading --
// the sims step millions of times and draw none of it.
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

  // Opens the landing about to happen: gives each of the front `hit` of `mobs`
  // its own event, so the lines one attack puts on one monster group together
  // however many ways the swing reaches it, and remembers what is doing the
  // damage.
  void OpenLandings(int mobs, int hit, DamageSource source);
  // Where the landing on the monster standing at `index`, whose id is
  // `mob_id`, is filed -- scaled by `scale`. The event is the one OpenLandings
  // gave that monster.
  Landing LandingAt(int mob_id, int index, double scale) const;
  // An event nothing else shares, for a landing that stands alone: a burn's
  // tick falls on its own clock, between the swings rather than with one.
  // Counted whether or not anybody is recording, so an event number means the
  // same thing either way.
  int NextEvent() {
    return ++next_event_;
  }

  // Files one line of `damage`, already scaled, against `landing`.
  void RecordLine(const Landing& landing, double damage, bool crit);
  // Files what the last RollFactor put in the sink as a landing of `damage`,
  // each line taking its own share of it.
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
  // The event each queued mob's lines are filed under for the landing being
  // worked out, parallel to the queue, and what is doing the damage.
  std::vector<int> landing_event_;
  DamageSource landing_source_;
  std::vector<DamageLine> lines_this_step_;
  // Where RollFactor writes its per-line shares, reused every roll so a
  // recording fight allocates once rather than once a line.
  std::vector<LineRoll> line_rolls_;
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_DAMAGE_LEDGER_H_
