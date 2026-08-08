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
