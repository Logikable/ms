#include "src/character/job_advancement.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/item/equip_instance.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

std::vector<std::string> StarterEquipsFor(Job job) {
  // One level-10 weapon per job, so an advancement is immediately playable.
  // The Rogue gets three: the stars are a slot of their own, and which of the
  // dagger or the claw is held decides which of the two attack skills can be
  // swung, so handing over only one would quietly pick the job's build.
  switch (job) {
    case JOB_SWORDMAN:
      return {"long_sword"};
    case JOB_MAGICIAN:
      return {"wooden_wand"};
    case JOB_ARCHER:
      return {"war_bow"};
    case JOB_ROGUE:
      return {"subi_throwing_stars", "fruit_knife", "garnier"};
    default:
      return {};
  }
}

void PerformJobAdvancement(GameState& state, Job job) {
  state.character.AdvanceJob(job);
  state.character.ResetStatsForJob(job);
  for (const std::string& name : StarterEquipsFor(job)) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(name);
    if (it == state.equips.end()) {
      continue;  // job_advancement_test pins the catalog against this list
    }
    state.character.PickUp(std::make_unique<EquipInstance>(it->second));
  }
}

}  // namespace ms
