/* A skill's placements: the books that list it, and where it sits on each.
 *
 * Almost every skill names one book. A skill more than one job holds -- Maple
 * Warrior, Epic Adventure, Physical Training -- names one per book and lives
 * in data/skills/shared, so there is one file to keep right rather than ten
 * copies to keep in step.
 */
#ifndef MS_SRC_CHARACTER_SKILL_PLACEMENT_H_
#define MS_SRC_CHARACTER_SKILL_PLACEMENT_H_

#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

// Puts `skill` in `book`, at `order` on its page. The placement it makes is
// returned for a caller with more to say about it; nearly every one wants only
// the book.
SkillPlacement* PlaceIn(Skill& skill, JobAdvancement book, int order = 1);

// Whether `book` lists `skill`.
bool ListedIn(const Skill& skill, JobAdvancement book);

// Where `skill` sits on `book`'s page, counting from 1. Zero for a book that
// does not list it.
int SkillOrderIn(const Skill& skill, JobAdvancement book);

// The book to charge a skill's levels to. Every placement of one skill is at
// the same job stage -- skill_test holds it, since a skill is only ever shared
// between jobs standing on one rung of the ladder -- so any of them answers
// and this is the first. JOB_ADVANCEMENT_UNSPECIFIED for a skill no book
// lists.
JobAdvancement BookOf(const Skill& skill);

}  // namespace ms

#endif  // MS_SRC_CHARACTER_SKILL_PLACEMENT_H_
