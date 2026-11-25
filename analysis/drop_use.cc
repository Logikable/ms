#include "analysis/drop_use.h"

#include <algorithm>
#include <limits>

#include "src/character/stat_preset.h"
#include "src/combat/drop.h"
#include "src/item/item.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

bool PricedIn(const EquipPrototype& proto, const std::string& token) {
  for (const TokenPrice& price : proto.token_prices()) {
    if (price.token_item() == token) {
      return true;
    }
  }
  return false;
}

// Whether a BoxContents box can give `proto`: a piece one of its tokens buys,
// in one of its slots when it names any.
bool BoxGives(const BoxContents& box, const EquipPrototype& proto) {
  bool priced = false;
  for (const std::string& token : box.token_items()) {
    priced = priced || PricedIn(proto, token);
  }
  if (!priced) {
    return false;
  }
  if (box.slots().empty()) {
    return true;
  }
  for (int slot : box.slots()) {
    if (slot == proto.equip_slot() || slot == BaseSlot(proto.equip_slot())) {
      return true;
    }
  }
  return false;
}

}  // namespace

DropUse::DropUse(const GameState& state) {
  for (const auto& [boss_key, boss] : state.bosses) {
    for (const BossDifficulty& difficulty : boss.difficulties()) {
      if (difficulty.coming_soon()) {
        continue;
      }
      for (const MobDrop& drop : difficulty.drops()) {
        const std::string name = DropName(state, drop);
        if (name.empty()) {
          continue;
        }
        Row row;
        if (drop.has_equip()) {
          std::map<std::string, EquipPrototype>::const_iterator equip =
              state.equips.find(drop.equip());
          if (equip == state.equips.end()) {
            continue;
          }
          row.kind = Kind::kPiece;
          row.key = drop.equip();
          row.required_level = equip->second.required_level();
        } else {
          std::map<std::string, ItemPrototype>::const_iterator item =
              state.items.find(drop.item());
          if (item == state.items.end()) {
            continue;
          }
          row.key = drop.item();
          if (item->second.has_box()) {
            row.kind = Kind::kBox;
            row.box = item->second.box();
          } else if (item->second.has_pick_box()) {
            row.kind = Kind::kPickBox;
            for (const std::string& piece : item->second.pick_box().equips()) {
              std::map<std::string, EquipPrototype>::const_iterator equip =
                  state.equips.find(piece);
              row.pieces.insert(
                  equip == state.equips.end() ? piece : equip->second.name());
            }
          } else if (item->second.has_ring_box()) {
            row.kind = Kind::kRingBox;
            row.ring_box = item->second.ring_box();
          } else {
            row.kind = Kind::kCurrency;
            for (const auto& [key, equip] : state.equips) {
              if (PricedIn(equip, drop.item())) {
                row.pieces.insert(equip.name());
              }
            }
            // An Etc drop nothing is bought with, like a soul shard.
            if (row.pieces.empty()) {
              continue;
            }
          }
        }
        std::map<std::string, Row>::iterator found = rows_.find(name);
        if (found == rows_.end()) {
          found = rows_.insert({name, row}).first;
        }
        found->second.fights.insert(boss_key + "/" + difficulty.name());
      }
    }
  }
}

void DropUse::Clear(const std::string& boss, const std::string& difficulty,
                    const BossReward& reward, double day) {
  std::vector<Row*> opened;
  for (auto& [name, row] : rows_) {
    if (row.fights.count(boss + "/" + difficulty) == 0) {
      continue;
    }
    if (!row.open) {
      opened.push_back(&row);
      row.last_use.name = name;
      row.open = true;
      row.start_day = day;
      row.low_drop = reward.item_drop_pct;
      row.high_drop = reward.item_drop_pct;
    }
    row.low_drop = std::min(row.low_drop, reward.item_drop_pct);
    row.high_drop = std::max(row.high_drop, reward.item_drop_pct);
  }
  for (const BossRewardItem& item : reward.items) {
    std::map<std::string, Row>::iterator row = rows_.find(item.name);
    if (row != rows_.end()) {
      if (row->second.dropped == 0 && item.count > 0) {
        row->second.first_day = day;
      }
      row->second.dropped += item.count;
    }
  }
  // Until the boss preset wears something it is for, a window holds only its
  // first clear: nothing it paid was needed yet.
  for (Row* row : opened) {
    DropUseResult& first = row->last_use;
    first.equipment = row->kind == Kind::kPiece;
    first.required_level = row->required_level;
    first.start_day = day;
    first.end_day = day;
    first.dropped = row->dropped;
    first.first_day = row->first_day;
    first.low_drop = row->low_drop;
    first.high_drop = row->high_drop;
  }
}

int DropUse::InUse(const GameState& state, const Row& row) const {
  int in_use = 0;
  for (const auto& [slot, piece] :
       state.character.equipped(AutoswapSlotFor(Activity::kBossing))) {
    const EquipPrototype& proto = piece->prototype();
    bool counts = false;
    switch (row.kind) {
      case Kind::kPiece:
        counts = proto.name() == state.equips.at(row.key).name();
        break;
      case Kind::kCurrency:
      case Kind::kPickBox:
        counts = row.pieces.count(proto.name()) > 0;
        break;
      case Kind::kBox:
        counts = BoxGives(row.box, proto);
        break;
      case Kind::kRingBox:
        counts = proto.has_equipment_skill() &&
                 std::find(row.ring_box.skills().begin(),
                           row.ring_box.skills().end(),
                           proto.equipment_skill().skill()) !=
                     row.ring_box.skills().end();
        break;
    }
    in_use += counts ? 1 : 0;
  }
  return in_use;
}

DropUseResult DropUse::Snapshot(
    const GameState& state, const std::string& name, const Row& row,
    const std::map<std::string, int>& held_spares_used, double day) const {
  DropUseResult result;
  result.name = name;
  result.equipment = row.kind == Kind::kPiece;
  result.required_level = row.required_level;
  result.start_day = row.start_day;
  result.end_day = day;
  result.dropped = row.dropped;
  result.first_day = row.first_day;
  result.low_drop = row.low_drop;
  result.high_drop = row.high_drop;
  result.worn = true;
  if (result.equipment) {
    std::map<std::string, int>::const_iterator spares =
        held_spares_used.find(name);
    result.used =
        row.most_worn + (spares == held_spares_used.end() ? 0 : spares->second);
  } else {
    result.used =
        std::max<int64_t>(0, row.dropped - state.character.CountItem(name));
  }
  return result;
}

void DropUse::Look(const GameState& state,
                   const std::map<std::string, int>& held_spares_used,
                   double day) {
  for (auto& [name, row] : rows_) {
    if (!row.open) {
      continue;
    }
    const int in_use = InUse(state, row);
    if (in_use == 0) {
      continue;
    }
    row.most_worn = std::max(row.most_worn, in_use);
    row.seen_in_use = true;
    row.last_use = Snapshot(state, name, row, held_spares_used, day);
  }
}

std::vector<DropUseResult> DropUse::Finish() const {
  std::vector<DropUseResult> results;
  for (const auto& [name, row] : rows_) {
    if (row.open) {
      results.push_back(row.last_use);
    }
  }
  return results;
}

}  // namespace ms
