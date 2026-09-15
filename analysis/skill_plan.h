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

// Spends the pool where it measures best, a point at a time, and stops when
// nothing left to buy raises the rate. Points the book cannot use are left
// unspent rather than dumped into a skill that pays nothing.
//
// Searched LAZILY, the way the matrix below is: pricing one skill plays a
// fight and a book holds hundreds, so a stale score is kept as an upper bound
// and only the leader is priced again after a purchase. Where a purchase makes
// another skill worth MORE the bound can seat the order wrongly -- the trade
// is made on purpose, and the note on it is in SpendGreedily.
//
// The SP book alone. A V Matrix node is bought with V Points, on a ladder
// where one level costs anything from 1 to 9, and ranking it beside an SP
// skill at a point a level prices most of the matrix wrong -- see
// SpendVMatrix, which is the pool's own allocator.
void SpendBook(GameState& state, const SkillRate& rate);

// The switches a character has settled on, and the roster they settled them
// against. Held by a caller that asks more than once -- see
// SpendBookWithToggles, which costs twice what SpendBook does every time it
// has to decide afresh.
struct ToggleChoice {
  std::string roster;
};

// The book spent both ways round: every switch off, and every switch thrown
// first. A switch costs no points and so is never one of the purchases above,
// but it changes what the points are WORTH -- with Righteously Indignant
// thrown, the levels in Heal are a six-enemy swing rather than a heal, and a
// chooser that never threw it would never buy them.
//
// Both ways round means the allocation runs TWICE, which is the dearest thing
// a climb does. So a caller may hand in a `choice` to remember what was
// settled: while the roster of switches the character could throw has not
// moved, the throw they are already standing in is kept and the book is spent
// once. Which way a switch falls is a shape-of-build decision that a star or a
// scroll does not flip -- and where it would, the next skill learned re-asks.
// Pass null to decide afresh every time.
void SpendBookWithToggles(GameState& state, const SkillRate& rate,
                          ToggleChoice* choice = nullptr);

// Spends the V Point pool on the matrix, best value per POINT first, and stops
// when nothing left to buy raises the rate. Nothing at all below the 5th job,
// which has no matrix to spend on.
//
// The matrix is emptied back into the pool first, so this is a PLAN and not a
// running total. It has to be: a character ranks nodes by what they take off
// the fight, and one still short of a boss's defence wall measures every
// damage lever at zero and buys whatever happens to add a HIT instead. Called
// again once the gear has lifted them over, it re-decides from nothing rather
// than adding to a ranking taken against somebody who could not scratch the
// boss. GMS charges nothing to reset one.
//
// Searched lazily -- see SpendBook, which shares the search.
//
// Priced per point rather than per level because a node's ladder is not flat:
// a boost node costs one a level and a common's first costs seven, so the same
// pool buys forty levels of one or one of the other. Which levels are offered
// is the ladder's own bands -- where the price changes is where a node's own
// perks sit -- plus whatever the pool could pay for outright, so a perk waiting
// at level 20 is reachable in one purchase rather than through nineteen that
// each pay nothing.
void SpendVMatrix(GameState& state, const SkillRate& rate);

}  // namespace ms

#endif  // MS_ANALYSIS_SKILL_PLAN_H_
