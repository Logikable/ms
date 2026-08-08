#include "src/character/job_advancement.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

std::vector<std::string> StarterEquipsFor(Job job) {
  // One weapon per job at the level the advancement happens, so an advancement
  // is immediately playable. The Rogue gets three: the stars are a slot of
  // their own, and which of the dagger or the claw is held decides which of
  // the two attack skills can be swung, so handing over only one would quietly
  // pick the job's build.
  //
  // A 2nd job is handed the weapon its Final Attack demands. Without it the
  // skill is learnable and silently worthless, and nothing on the screen says
  // the weapon they climbed to 30 with is the reason.
  switch (job) {
    case JOB_SWORDMAN:
      return {"long_sword"};
    case JOB_MAGICIAN:
      return {"wooden_staff"};
    case JOB_ARCHER:
      return {"war_bow"};
    case JOB_ROGUE:
      return {"subi_throwing_stars", "fruit_knife", "garnier"};
    case JOB_FIGHTER:
      return {"blue_axe"};
    case JOB_PAGE:
      return {"mithril_maul"};
    case JOB_SPEARMAN:
      return {"forked_spear"};
    case JOB_HUNTER:
      return {"ryden"};
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
