/* Guild skills: the guild the game assumes every player is in.
 *
 * There is no guild. The player is taken to be in a maxed-out one, so its
 * passives arrive with the account's level, and its Noblesse skills are bought
 * with points the account earns by soloing bosses.
 */
#ifndef MS_SRC_CHARACTER_GUILD_H_
#define MS_SRC_CHARACTER_GUILD_H_

namespace ms {

// The account level at which every character holds the guild passives.
inline constexpr int kGuildSkillsLevel = 255;

}  // namespace ms

#endif  // MS_SRC_CHARACTER_GUILD_H_
