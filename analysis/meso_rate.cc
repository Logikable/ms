#include "analysis/meso_rate.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "src/character/character_stats.h"
#include "src/combat/encounter.h"
#include "src/combat/loot.h"
#include "src/game_state.h"
#include "src/protos/item.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

double EtcPerKill(const std::map<std::string, ItemPrototype>& items,
                  const Mob& mob) {
  double total = 0.0;
  for (const MobDrop& drop : mob.drops()) {
    if (!drop.has_item()) {
      continue;
    }
    std::map<std::string, ItemPrototype>::const_iterator it =
        items.find(drop.item());
    if (it != items.end() && it->second.sell_price() > 0) {
      total += drop.per_kill() * it->second.sell_price();
    }
  }
  return total;
}

double MesoPerKill(const Mob& mob, double etc, double item_drop_pct) {
  return ExpectedMesoPerKill(mob, item_drop_pct) + etc * (1.0 + item_drop_pct);
}

Crowd Crowd::At(absl::Span<const double> rate) const {
  Crowd copy = *this;
  copy.kills_per_second.assign(rate.begin(), rate.end());
  copy.kills_per_second.resize(mobs.size(), 0.0);
  return copy;
}

Crowd CrowdFor(const GameState& state, const CombatParams& params,
               absl::Span<const double> kills_per_second) {
  Crowd crowd;
  for (std::size_t i = 0; i < params.types.size(); ++i) {
    const Mob* mob = params.types[i].mob;
    crowd.mobs.push_back(mob == nullptr ? Mob() : *mob);
    crowd.etc.push_back(mob == nullptr ? 0.0 : EtcPerKill(state.items, *mob));
    crowd.kills_per_second.push_back(
        i < kills_per_second.size() ? kills_per_second[i] : 0.0);
  }
  return crowd;
}

double MesoPerSecond(const Crowd& crowd, double meso_pct, double meso_mult,
                     double item_drop_pct) {
  double total = 0.0;
  for (std::size_t i = 0; i < crowd.mobs.size(); ++i) {
    if (crowd.mobs[i].boss()) {
      continue;
    }
    total += crowd.kills_per_second[i] *
             MesoPerKill(crowd.mobs[i], crowd.etc[i], item_drop_pct);
  }
  return total * (1.0 + meso_pct) * meso_mult;
}

double MesoPerSecondFor(const GameState& state, const Crowd& crowd) {
  DerivedStats derived = DerivedStatsFor(state.character, state.skills);
  return MesoPerSecond(crowd, MesoBonus(derived), derived.meso_final_mult,
                       derived.item_drop_pct);
}

}  // namespace ms
