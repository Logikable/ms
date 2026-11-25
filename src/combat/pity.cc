#include "src/combat/pity.h"

#include <algorithm>

namespace ms {
namespace {

int PityCopies(const MobDrop& drop) {
  return std::max(1, drop.pity_copies());
}

const BossPity* Find(const PityRecord& record, const std::string& boss,
                     const std::string& key) {
  for (const BossPity& pity : record) {
    if (pity.boss() == boss && pity.drop() == key) {
      return &pity;
    }
  }
  return nullptr;
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
  const BossPity* pity = Find(record, boss, DropKey(drop));
  const int kills = pity == nullptr ? 0 : pity->kills();
  const int copies = pity == nullptr ? 0 : pity->copies();
  return copies < PityCopies(drop) && kills + 1 >= drop.pity();
}

void RecordPity(PityRecord& record, const std::string& boss,
                const MobDrop& drop, bool paid) {
  if (drop.pity() <= 0) {
    return;
  }
  const std::string key = DropKey(drop);
  BossPity* pity = nullptr;
  for (BossPity& entry : record) {
    if (entry.boss() == boss && entry.drop() == key) {
      pity = &entry;
    }
  }
  if (pity == nullptr) {
    pity = record.Add();
    pity->set_boss(boss);
    pity->set_drop(key);
  }
  if (paid) {
    pity->set_copies(pity->copies() + 1);
    pity->set_kills(0);
  } else {
    pity->set_kills(pity->kills() + 1);
  }
}

std::optional<PityProgress> ProgressOf(const PityRecord& record,
                                       const std::string& boss,
                                       const MobDrop& drop) {
  if (drop.pity() <= 0) {
    return std::nullopt;
  }
  const BossPity* pity = Find(record, boss, DropKey(drop));
  const int kills = pity == nullptr ? 0 : pity->kills();
  const int copies = pity == nullptr ? 0 : pity->copies();
  if (copies >= PityCopies(drop)) {
    return std::nullopt;
  }
  if (PityCopies(drop) > 1) {
    return PityProgress{copies, PityCopies(drop)};
  }
  return PityProgress{kills, drop.pity()};
}

}  // namespace ms
