#include "src/sacred_dailies.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/character/consumables.h"
#include "src/character/hyper_stats.h"
#include "src/character/progression.h"
#include "src/character/symbol.h"
#include "src/combat/clear_time.h"
#include "src/combat/constants.h"
#include "src/combat/encounter.h"
#include "src/game_state.h"
#include "src/save.h"

namespace ms {
namespace {

double TotemRespawn(ConsumableType type) {
  return ConsumableInfoFor(type)->respawn_seconds;
}

bool DropsFrom(const GameState& state, const MapData& map,
               const std::string& symbol_key) {
  for (const Spawn& spawn : map.spawns()) {
    auto mob = state.mobs.find(spawn.mob());
    if (mob == state.mobs.end()) {
      continue;
    }
    for (const MobDrop& drop : mob->second.drops()) {
      if (drop.equip() == symbol_key) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

std::string SacredSymbolMap(const GameState& state,
                            const std::string& symbol_key) {
  std::string best;
  int least = 0;
  for (const auto& [key, map] : state.maps) {
    if (map.sacred_power() <= 0 || !DropsFrom(state, map, symbol_key)) {
      continue;
    }
    if (best.empty() || map.sacred_power() < least) {
      best = key;
      least = map.sacred_power();
    }
  }
  return best;
}

double SacredShare(double clear_seconds, double speed_factor) {
  const double frenzy =
      TotemRespawn(CONSUMABLE_TYPE_FRENZY_TOTEM) * speed_factor;
  const double wild = TotemRespawn(CONSUMABLE_TYPE_WILD_TOTEM) * speed_factor;
  const double bare = kRespawnIntervalSeconds * speed_factor;
  if (clear_seconds <= frenzy) {
    return 1.0;
  }
  if (clear_seconds <= wild) {
    return 1.0 - 0.2 * (clear_seconds - frenzy) / (wild - frenzy);
  }
  if (clear_seconds <= bare) {
    return 0.8 - 0.3 * (clear_seconds - wild) / (bare - wild);
  }
  return 0.0;
}

std::vector<SacredArea> MeasureSacredAreas(const GameState& state,
                                           ClearStat stat) {
  std::vector<SacredArea> areas;
  std::vector<std::pair<const std::string*, const EquipPrototype*>> symbols;
  for (const auto& [key, proto] : state.equips) {
    if (IsSacredSymbol(proto)) {
      symbols.emplace_back(&key, &proto);
    }
  }
  std::sort(symbols.begin(), symbols.end(), [](const auto& a, const auto& b) {
    return a.second->equip_slot() < b.second->equip_slot();
  });
  const double speed = GameSpeedFactor(state.character.proto().level());
  for (const auto& [key, proto] : symbols) {
    SacredArea area;
    area.symbol = proto;
    area.map = SacredSymbolMap(state, *key);
    if (area.map.empty()) {
      continue;
    }
    area.clears = ClearTimes(ComputeCombatParams(state, area.map),
                             kClearWindowGameSeconds * speed);
    area.clear =
        ClearPercentile(area.clears, stat == ClearStat::kMedian ? 0.5 : 0.9);
    area.share = area.clears.empty() ? 0.0 : SacredShare(area.clear, speed);
    areas.push_back(area);
    if (area.share <= 0.0) {
      break;
    }
  }
  return areas;
}

std::map<std::string, double> SacredShares(
    const std::vector<SacredArea>& areas) {
  std::map<std::string, double> shares;
  for (const SacredArea& area : areas) {
    if (area.share > 0.0) {
      shares[area.symbol->name()] = area.share;
    }
  }
  return shares;
}

SacredDailiesWorker::~SacredDailiesWorker() {
  if (pending_.valid()) {
    pending_.wait();
  }
}

std::string SacredDailiesWorker::WhoIs(const GameState& state) {
  return std::to_string(state.played_slot) + ":" + state.character.username();
}

void SacredDailiesWorker::Collect() {
  if (!pending_.valid()) {
    return;
  }
  last_ = pending_.get();
  last_for_ = pending_for_;
}

void SacredDailiesWorker::Start(const GameState& state,
                                std::chrono::steady_clock::time_point now) {
  if (state.character.proto().job_stage() < kSixthJobStage) {
    return;
  }
  if (pending_.valid() &&
      pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    return;
  }
  Collect();
  std::string who = WhoIs(state);
  if (last_for_ == who && now - started_ < kSacredRemeasure) {
    return;
  }
  started_ = now;
  pending_for_ = who;
  // Copied here, on the thread that owns `state`; only the catalogs are read
  // from the other thread.
  std::string bytes = SaveGameToBytes(state);
  pending_ = std::async(std::launch::async, [&state, bytes]() {
    std::unique_ptr<GameState> copy = RestoreCopy(state, bytes);
    return copy == nullptr ? std::map<std::string, double>()
                           : SacredShares(MeasureSacredAreas(*copy));
  });
}

std::map<std::string, double> SacredDailiesWorker::Shares(
    const GameState& state, std::chrono::steady_clock::time_point now) {
  if (state.character.proto().job_stage() < kSixthJobStage) {
    return {};
  }
  std::string who = WhoIs(state);
  Collect();
  if (last_for_ != who) {
    last_ = SacredShares(MeasureSacredAreas(state));
    last_for_ = who;
    started_ = now;
  }
  return last_;
}

}  // namespace ms
