/* Chooses what a character spends Noblesse SP on.
 *
 * One level at a time, to whichever Noblesse skill raises the rating most. The
 * four skills multiply into each other, so a level's worth depends on what is
 * already bought, which is why it is measured step by step and not ranked
 * once. Points come back for free, so a plan is redone from nothing whenever
 * the pool grows.
 *
 * The caller supplies the rating, as for hyper_plan.h: the game rates by the
 * expected hit on the toughest boss unlocked, and //analysis by damage in a
 * measured fight.
 */
#ifndef MS_SRC_CHARACTER_NOBLESSE_PLAN_H_
#define MS_SRC_CHARACTER_NOBLESSE_PLAN_H_

#include <functional>
#include <map>
#include <string>

#include "src/character/character.h"
#include "src/protos/skill.pb.h"

namespace ms {

// What the character is worth. It is called on a character this file has just
// changed, so it must read the character it is given, not one it captured.
using NoblesseRate = std::function<double(CharacterInstance&)>;

// Gives back every Noblesse level, then spends the whole pool. Returns the
// levels bought.
int SpendNoblesseSp(CharacterInstance& character,
                    const std::map<std::string, Skill>& skills,
                    const NoblesseRate& rate);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_NOBLESSE_PLAN_H_
