#include "src/character/skill_placement.h"

#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

SkillPlacement* PlaceIn(Skill& skill, JobAdvancement book, int order) {
  SkillPlacement* placement = skill.add_placement();
  placement->set_job_advancement(book);
  placement->set_skill_order(order);
  return placement;
}

bool ListedIn(const Skill& skill, JobAdvancement book) {
  return SkillOrderIn(skill, book) > 0;
}

int SkillOrderIn(const Skill& skill, JobAdvancement book) {
  for (const SkillPlacement& placement : skill.placement()) {
    if (placement.job_advancement() == book) {
      return placement.skill_order();
    }
  }
  return 0;
}

JobAdvancement BookOf(const Skill& skill) {
  return skill.placement().empty() ? JOB_ADVANCEMENT_UNSPECIFIED
                                   : skill.placement(0).job_advancement();
}

}  // namespace ms
