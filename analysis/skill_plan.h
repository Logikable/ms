/* Spending a character's skill points: the greedy every sim that puts a book
 * behind a character shares.
 *
 * The catalog's own order is no allocation at all. A 4th job at 130 holds
 * about 150 points of a 200-point book, so which of them get spent is most of
 * what the character hits for -- and walking the catalog spends them in
 * whatever order the files happen to be named, which starves whichever job
 * files its Maple Warrior under a late letter.
 *
 * So the points are ranked instead: one at a time, into whichever skill lifts
 * a measured rate the most per point it costs. What that rate measures is the
 * caller's to say -- a boss sim prices the fight in front of it, a climb
 * prices the map it is standing on -- and the choice of target is the whole
 * difference between a boss build and a farming one.
 */
#ifndef MS_ANALYSIS_SKILL_PLAN_H_
#define MS_ANALYSIS_SKILL_PLAN_H_

#include <functional>
#include <map>
#include <string>

#include "src/game_state.h"
#include "src/protos/skill.pb.h"

namespace ms {

// What one point is worth, measured however the caller likes. Called many
// times over one allocation, on a character the greedy has already changed,
// so it must read the state it is handed rather than any it captured.
using SkillRate = std::function<double(GameState&)>;

// The catalog by the name a skill requirement calls it: the display name,
// which is not the key the catalog is filed under.
std::map<std::string, const Skill*> SkillsByName(const GameState& state);

// Raises `skill` by `levels`, buying whatever it demands first. Returns the
// points that went in, which is more than the levels asked for when the skill
// stands behind a requirement nothing has paid for yet, and fewer when the SP
// runs out. The caller restores the character afterwards, so a plan that
// cannot be finished costs nothing.
int BuySkill(GameState& state, const Skill& skill,
             const std::map<std::string, const Skill*>& named, int levels);

// Spends the pool where it measures best, a point at a time, stopping when
// nothing raises the rate. Points the book cannot use are left UNSPENT rather
// than dumped into a skill that pays nothing.
//
// Searched LAZILY, as the matrix is -- see SpendGreedily for the bound and
// what it trades. The SP book ALONE: a V node is bought with V Points on a
// ladder costing 1 to 9 a level, and ranking it beside an SP skill at a point
// a level prices most of the matrix wrong. See SpendVMatrix.
void SpendBook(GameState& state, const SkillRate& rate);

// The switches a character has settled on, and the roster they settled them
// against. Held by a caller that asks more than once -- see
// SpendBookWithToggles, which costs twice what SpendBook does every time it
// has to decide afresh.
struct ToggleChoice {
  std::string roster;
};

// The book spent both ways round: every switch off, and every switch thrown
// first. A switch costs no points but changes what the points are WORTH --
// with Righteously Indignant thrown, the levels in Heal are a six-enemy swing
// rather than a heal.
//
// That means the allocation runs TWICE, the dearest thing a climb does, so a
// caller may hand in a `choice` to remember what was settled: while the roster
// of switches has not moved, the throw already standing is kept. Pass null to
// decide afresh.
void SpendBookWithToggles(GameState& state, const SkillRate& rate,
                          ToggleChoice* choice = nullptr);

// Spends the V Point pool on the matrix, best value per POINT first, stopping
// when nothing raises the rate.
//
// The matrix is EMPTIED back into the pool first, so this is a plan and not a
// running total. It has to be: a character short of a boss's defence wall
// measures every damage lever at zero and buys whatever adds a HIT, and once
// the gear lifts them over it must re-decide rather than add to that. GMS
// charges nothing to reset one.
//
// Searched lazily -- see SpendBook. Priced per POINT, a node's ladder not
// being flat: a boost costs one a level and a common's first costs seven. The
// levels offered are the ladder's own bands, where a node's perks sit, so a
// perk at level 20 is reachable in one purchase.
//
// `replan` false keeps the matrix standing and spends only what the pool has
// gained on top of it: the caller's to choose, where nothing that could carry
// the character over a wall has moved since the last plan.
void SpendVMatrix(GameState& state, const SkillRate& rate, bool replan = true);

}  // namespace ms

#endif  // MS_ANALYSIS_SKILL_PLAN_H_
