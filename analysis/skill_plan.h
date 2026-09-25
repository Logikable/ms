/* Spends a character's skill points greedily: one at a time, into whichever
 * skill raises a measured rate the most per point. A 4th job at 130 has about
 * 150 points of a 200-point book, so the choice largely decides its damage.
 *
 * The caller chooses the rate. A boss sim measures the fight ahead and a climb
 * measures its current map, and that is the whole difference between a bossing
 * build and a farming one.
 */
#ifndef MS_ANALYSIS_SKILL_PLAN_H_
#define MS_ANALYSIS_SKILL_PLAN_H_

#include <functional>
#include <map>
#include <string>

#include "src/game_state.h"
#include "src/protos/skill.pb.h"

namespace ms {

// Value of the character's current setup, measured however the caller likes.
// Called many times during one allocation on a character already changed, so it
// must read the state passed in, not any state it captured.
using SkillRate = std::function<double(GameState&)>;

// The catalog keyed by the name skill requirements use: the display name, not
// the catalog key.
std::map<std::string, const Skill*> SkillsByName(const GameState& state);

// Raises `skill` by `levels`, buying its prerequisites first. Returns the
// points spent: more than `levels` when a prerequisite had to be bought, fewer
// when SP runs out. The caller restores the character afterwards.
int BuySkill(GameState& state, const Skill& skill,
             const std::map<std::string, const Skill*>& named, int levels);

// Spends the pool one point at a time where it measures best, stopping when
// nothing raises the rate. Points the book can't use are left unspent.
//
// The search is lazy; see SpendGreedily. This covers the SP book only: a V
// node's ladder costs 1 to 9 points a level. See SpendVMatrix.
void SpendBook(GameState& state, const SkillRate& rate);

// The toggles a character settled on, and the roster of toggles at the time.
// Kept by callers that ask more than once, since SpendBookWithToggles costs
// twice what SpendBook does each time it decides from scratch.
struct ToggleChoice {
  std::string roster;
};

// Spends the book with every toggle off and with every toggle on first, and
// keeps the better. A toggle changes what points are worth: with Righteously
// Indignant on, levels in Heal make an attack.
//
// Pass `choice` to remember the result: while the roster of toggles is
// unchanged, the current setting is kept. Pass null to decide from scratch.
void SpendBookWithToggles(GameState& state, const SkillRate& rate,
                          ToggleChoice* choice = nullptr);

// Spends the V Point pool on the matrix, best value per point first, stopping
// when nothing raises the rate.
//
// The matrix is first emptied back into the pool. A character below a boss's
// defence wall measures every damage lever at zero, so once gear lifts them
// over it the plan must be redone.
//
// Value is per point because node ladders aren't flat. Purchases step through
// the ladder's own bands, where a node's perks sit.
//
// With `replan` false, the matrix stays and only newly gained points are spent.
// The caller decides when nothing that could lift the character over a wall has
// changed.
void SpendVMatrix(GameState& state, const SkillRate& rate, bool replan = true);

}  // namespace ms

#endif  // MS_ANALYSIS_SKILL_PLAN_H_
