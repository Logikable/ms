/* Spends a character's skill points greedily. Shared by every sim that gives a
 * character a skill book.
 *
 * Spending in catalog order is no plan at all. A 4th job at 130 has about 150
 * points of a 200-point book, so which skills get them largely decides the
 * character's damage. Catalog order follows file names, which starves any job
 * whose Maple Warrior file sorts late.
 *
 * So points are ranked instead: one at a time, into whichever skill raises a
 * measured rate the most per point. The caller chooses the rate. A boss sim
 * measures the fight ahead and a climb measures its current map, and that
 * choice is the whole difference between a bossing build and a farming one.
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
// points spent: more than `levels` when an unpaid prerequisite had to be
// bought, fewer when SP runs out. The caller restores the character afterwards,
// so a plan that can't be finished costs nothing.
int BuySkill(GameState& state, const Skill& skill,
             const std::map<std::string, const Skill*>& named, int levels);

// Spends the pool one point at a time where it measures best, stopping when
// nothing raises the rate. Points the book can't use are left unspent rather
// than put into a skill that does nothing.
//
// The search is lazy, as for the matrix; see SpendGreedily for the bound and
// its tradeoff. This covers the SP book only. A V node costs V Points on a
// ladder of 1 to 9 a level, and ranking it against SP skills at one point a
// level would misprice most of the matrix. See SpendVMatrix.
void SpendBook(GameState& state, const SkillRate& rate);

// The toggles a character settled on, and the roster of toggles at the time.
// Kept by callers that ask more than once, since SpendBookWithToggles costs
// twice what SpendBook does each time it decides from scratch.
struct ToggleChoice {
  std::string roster;
};

// Spends the book both ways, with every toggle off and with every toggle on
// first, and keeps the better. A toggle costs no points but changes what points
// are worth: with Righteously Indignant on, levels in Heal make a six-enemy
// attack instead of a heal.
//
// Running the allocation twice is the most expensive thing a climb does, so a
// caller may pass `choice` to remember the result. While the roster of toggles
// is unchanged, the current setting is kept. Pass null to decide from scratch.
void SpendBookWithToggles(GameState& state, const SkillRate& rate,
                          ToggleChoice* choice = nullptr);

// Spends the V Point pool on the matrix, best value per point first, stopping
// when nothing raises the rate.
//
// The matrix is first emptied back into the pool, so this is a fresh plan, not
// an addition. It has to be: a character below a boss's defence wall measures
// every damage lever at zero and buys whatever adds a hit, and once gear lifts
// them over the wall the plan must be redone. GMS charges nothing to reset a
// node.
//
// The search is lazy (see SpendBook). Value is per point because node ladders
// aren't flat: a boost node costs one point a level, and a common node's first
// level costs seven. Purchases step through the ladder's own bands, where a
// node's perks sit, so a perk at level 20 is one purchase away.
//
// With `replan` false, the matrix stays and only newly gained points are spent.
// The caller decides when that is safe: when nothing that could lift the
// character over a wall has changed since the last plan.
void SpendVMatrix(GameState& state, const SkillRate& rate, bool replan = true);

}  // namespace ms

#endif  // MS_ANALYSIS_SKILL_PLAN_H_
