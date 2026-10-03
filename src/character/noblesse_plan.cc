#include "src/character/noblesse_plan.h"

#include <map>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/protos/skill.pb.h"

namespace ms {

int SpendNoblesseSp(CharacterInstance& character,
                    const std::map<std::string, Skill>& skills,
                    const NoblesseRate& rate) {
  std::vector<const Skill*> noblesse;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    if (entry.second.guild() == GUILD_SKILL_NOBLESSE) {
      noblesse.push_back(&entry.second);
      character.UnlearnSkill(entry.second, character.skill_level(entry.second));
    }
  }
  int bought = 0;
  while (character.noblesse_sp() > 0) {
    const Skill* best = nullptr;
    double best_rate = 0.0;
    for (const Skill* skill : noblesse) {
      if (!character.LearnSkill(*skill)) {
        continue;
      }
      double tried = rate(character);
      character.UnlearnSkill(*skill);
      if (best == nullptr || tried > best_rate) {
        best = skill;
        best_rate = tried;
      }
    }
    if (best == nullptr) {
      break;
    }
    character.LearnSkill(*best);
    ++bought;
  }
  return bought;
}

}  // namespace ms
