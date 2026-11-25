#include "src/combat/pity.h"

#include <algorithm>

namespace ms {
namespace {

int Kills(const PityRecord& record, const std::string& boss,
          const std::string& key) {
  for (const BossPity& pity : record) {
    if (pity.boss() == boss && pity.drop() == key) {
      return pity.kills();
    }
  }
  return 0;
}

// The last kill pity pays.
int LastPityKill(const MobDrop& drop) {
  return drop.pity() + std::max(1, drop.pity_copies()) - 1;
}

}  // namespace

std::string DropKey(const MobDrop& drop) {
  return drop.has_equip() ? drop.equip() : drop.item();
}

bool PityDue(const PityRecord& record, const std::string& boss,
             const MobDrop& drop) {
  if (drop.pity() <= 0) {
    return false;
  }
  const int kill = Kills(record, boss, DropKey(drop)) + 1;
  return kill >= drop.pity() && kill <= LastPityKill(drop);
}

void RecordPity(PityRecord& record, const std::string& boss,
                const MobDrop& drop) {
  if (drop.pity() <= 0) {
    return;
  }
  const std::string key = DropKey(drop);
  for (BossPity& pity : record) {
    if (pity.boss() == boss && pity.drop() == key) {
      pity.set_kills(pity.kills() + 1);
      return;
    }
  }
  BossPity* pity = record.Add();
  pity->set_boss(boss);
  pity->set_drop(key);
  pity->set_kills(1);
}

std::optional<PityProgress> ProgressOf(const PityRecord& record,
                                       const std::string& boss,
                                       const MobDrop& drop) {
  if (drop.pity() <= 0) {
    return std::nullopt;
  }
  const int kills = Kills(record, boss, DropKey(drop));
  if (kills >= LastPityKill(drop)) {
    return std::nullopt;
  }
  return PityProgress{kills, LastPityKill(drop)};
}

}  // namespace ms
