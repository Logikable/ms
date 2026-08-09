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
  // Only the first one re-seats the stats, because it is the only one that
  // changes what the character is for: a Beginner's free 13 STR is worth
  // nothing to a Magician, so it is refunded and the new job's primary is
  // seated in its place. A 2nd job stays inside its own category and keeps
  // raising the same stat, so resetting there would take back twenty levels of
  // spending and hand the player a pile of AP to put where it already was.
  if (state.character.proto().job_stage() == 1) {
    state.character.ResetStatsForJob(job);
  }
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
