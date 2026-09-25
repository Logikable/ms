/* Link skills: what the whole ACCOUNT has climbed, handed to one character.
 *
 * A link skill belongs to a job line rather than to a book. Every line a
 * character on the account has taken past level 70 pays a rung of its line's
 * skill, three at the top, and the rungs of the lines sharing a branch sum
 * into the one skill -- so an account with a Hero and two Dark Knights levels
 * the warriors' skill twice, not three times.
 *
 * The tally here is of the characters NOT being played: the one in play
 * speaks for their own level, which climbs mid-session. Same bargain as
 * CharacterInstance::account_max_level.
 */
#ifndef MS_SRC_CHARACTER_LINK_H_
#define MS_SRC_CHARACTER_LINK_H_

#include <map>

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

// The levels a link skill is handed a rung at. GMS's own.
inline constexpr int kLinkRungLevels[] = {70, 120, 210};
// Rungs one line can pay, and so the levels a link skill gains per line.
inline constexpr int kLinkRungsPerLine = 3;
// Link skills one character carries beside the one their own line hands
// them. GMS's twelve; nothing reaches it yet.
inline constexpr int kMaxEquippedLinkSkills = 12;
// The account level the gold trail to the Link Skills screen lights at: the
// top rung. The skills themselves are live from level 1.
inline constexpr int kLinkSkillsLevel = 210;

// What one character of `level` pays their line: 0 below the first rung.
int LinkRungsFor(int level);

// The skills `slot` carries, empty for a slot a character has never filled.
const LinkPreset& PresetOf(const LinkSkills& link, StatPreset slot);
// The same, growing the list to kNumStatPresets first -- the mirror of
// MigrateInnerAbility, and called for the same reason.
LinkPreset& PresetOf(LinkSkills& link, StatPreset slot);

// The best level reached on each job line, keyed by the line's second job --
// see LineOf. A line nobody has taken is absent.
class LinkTally {
 public:
  // Records a character, keeping the higher level where the line already has
  // one. A character with no second advancement pays nothing: their line is
  // undecided.
  void Record(Job job, int level);

  // The level `line`'s link skill stands at for this tally: the rungs of
  // every line of the same branch, summed. `line` is the skill's own
  // Skill.link_line, a branch's FIRST job.
  int LevelFor(Job line) const;

  // The tally with one more character in it, for folding in the character
  // being played without disturbing what was mirrored.
  LinkTally With(Job job, int level) const;

  // The whole of it, line by line. For putting a tally on the wire and
  // reading one back -- see PlayerInfo.link_lines.
  const std::map<Job, int>& best_by_line() const {
    return best_;
  }

  bool empty() const {
    return best_.empty();
  }

 private:
  std::map<Job, int> best_;
};

}  // namespace ms

#endif  // MS_SRC_CHARACTER_LINK_H_
