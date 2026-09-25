/* Link skills: the account's progress, given to one character.
 *
 * A link skill belongs to a job line, not a book. Every line a character on the
 * account has taken past level 70 adds a level to its line's skill, up to
 * three, and the levels of lines in the same branch add up into the one skill.
 * So an account with a Hero and two Dark Knights levels the warriors' skill
 * twice, not three times.
 *
 * The tally covers the characters not being played. The one in play counts at
 * their own level, which changes mid-session;
 * CharacterInstance::account_max_level works the same way.
 */
#ifndef MS_SRC_CHARACTER_LINK_H_
#define MS_SRC_CHARACTER_LINK_H_

#include <map>

#include "src/character/stat_preset.h"
#include "src/protos/character.pb.h"

namespace ms {

// The levels at which a link skill gains a level, from GMS.
inline constexpr int kLinkRungLevels[] = {70, 120, 210};
// Levels one line can add, and so the levels a link skill gains per line.
inline constexpr int kLinkRungsPerLine = 3;
// Link skills one character can equip, besides the one their own line gives
// them. GMS's twelve; nobody can reach it yet.
inline constexpr int kMaxEquippedLinkSkills = 12;
// The account level at which the gold trail to the Link Skills screen appears:
// the last threshold. The skills themselves work from level 1.
inline constexpr int kLinkSkillsLevel = 210;

// Levels one character at `level` adds to their line: 0 below the first
// threshold.
int LinkRungsFor(int level);

// The skills in `slot`; empty for a slot the character has never filled.
const LinkPreset& PresetOf(const LinkSkills& link, StatPreset slot);
// The same, first padding the list to kNumStatPresets, like MigrateInnerAbility
// and for the same reason.
LinkPreset& PresetOf(LinkSkills& link, StatPreset slot);

// The best level reached in each job line, keyed by the line's 2nd job (see
// LineOf). Lines nobody has taken are absent.
class LinkTally {
 public:
  // Records a character, keeping the higher level if the line already has one.
  // A character with no 2nd advancement adds nothing, since their line is
  // undecided.
  void Record(Job job, int level);

  // The level of `line`'s link skill for this tally: the levels from every line
  // in the same branch, added up. `line` is the skill's Skill.link_line, which
  // is a branch's first job.
  int LevelFor(Job line) const;

  // The tally with one more character added, to include the character being
  // played without changing the stored tally.
  LinkTally With(Job job, int level) const;

  // The full tally, line by line. For sending a tally over the network and
  // reading one back; see PlayerInfo.link_lines.
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
