/* A skill's placements: the books that list it, and its position in each.
 *
 * Almost every skill is in one book. A skill several jobs have (Maple Warrior,
 * Epic Adventure, Physical Training) names one placement per book and lives in
 * data/skills/shared, so there is one file to keep right instead of ten copies
 * to keep in sync.
 */
#ifndef MS_SRC_CHARACTER_SKILL_PLACEMENT_H_
#define MS_SRC_CHARACTER_SKILL_PLACEMENT_H_

#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// Puts `skill` in `book` at position `order`. Returns the new placement for
// callers that need to set more on it; most only need the book.
SkillPlacement* PlaceIn(Skill& skill, JobAdvancement book, int order = 1);

// Whether `book` lists `skill`.
bool ListedIn(const Skill& skill, JobAdvancement book);

// The position of `skill` in `book`, counting from 1. Zero if the book doesn't
// list it.
int SkillOrderIn(const Skill& skill, JobAdvancement book);

// The book a skill's levels are charged to. Every placement of a skill is at
// the same job stage (skill_test checks this), so any one works; this returns
// the first.
JobAdvancement BookOf(const Skill& skill);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_SKILL_PLACEMENT_H_
