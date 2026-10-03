/* Guild skills: the guild the game assumes every player is in.
 *
 * There is no guild. The player is taken to be in a maxed-out one, so its
 * passives arrive with the account's level, and its Noblesse skills are bought
 * with points the account earns by soloing bosses: one per boss difficulty,
 * GMS's weekly guild ranking replaced by something a single player can do.
 */
#ifndef MS_SRC_CHARACTER_GUILD_H_
#define MS_SRC_CHARACTER_GUILD_H_

#include <map>
#include <string>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/protos/account.pb.h"
#include "src/protos/boss.pb.h"

namespace ms {

// The account level at which every character holds the guild passives.
inline constexpr int kGuildSkillsLevel = 255;

// The Noblesse SP `clears` have earned: one for every difficulty at or below
// one beaten, in the order the boss's file lists them. A coming-soon
// difficulty pays nothing, and neither does a clear the catalog no longer
// names.
int NoblesseSpEarned(
    const google::protobuf::RepeatedPtrField<SoloClear>& clears,
    const std::map<std::string, Boss>& bosses);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_GUILD_H_
