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
void SpendBook(GameState& state, const SkillRate& rate);

// The book spent both ways round: every switch off, and every switch thrown
// first. A switch costs no points and so is never one of the purchases above,
// but it changes what the points are WORTH -- with Righteously Indignant
// thrown, the levels in Heal are a six-enemy swing rather than a heal, and a
// chooser that never threw it would never buy them.
void SpendBookWithToggles(GameState& state, const SkillRate& rate);

}  // namespace ms

#endif  // MS_ANALYSIS_SKILL_PLAN_H_
