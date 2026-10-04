#include "src/frontend/widgets/item_row.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "src/account.h"
#include "src/character/character.h"
#include "src/character/progression.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_columns.h"
#include "src/frontend/widgets/marquee.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

const std::string& ItemCells::Get(ItemColumn column) const {
  static_assert(kNumItemColumns == 10, "a new column needs a cell");
  const std::string* const cells[kNumItemColumns] = {
      &name,  &slot,      &level,           &job,  &stats, &scroll,
      &stars, &potential, &bonus_potential, &flame};
  return *cells[static_cast<int>(column)];
}

PotentialRank ItemRowText::RankOf(ItemColumn column) const {
  switch (column) {
    case ItemColumn::kPotential:
      return potential_rank;
    case ItemColumn::kBonusPotential:
      return bonus_potential_rank;
    default:
      return POTENTIAL_RANK_UNSPECIFIED;
  }
}

// The stat column of one row: the attack this job uses, then the stat its
// damage is based on.
std::string ItemStatsCell(Job job, const EquipStats& stats) {
  // One column for the stat this job's damage is based on. The character panel
  // and the AP reset ask the same question, so it is answered in one place
  // instead of switching over jobs here.
  StatField primary = PrimaryStatField(job);
  const DisplayStat* main = DisplayStatFor(primary);
  std::string main_str;
  if (main != nullptr && main->GetFrom(stats) > 0) {
    main_str = "+" + std::to_string(main->GetFrom(stats)) + " " + main->label;
  }
  // There is room for one attack figure, so show the one this job uses. A wand
  // carries both, and a magician's weapon attack never reaches the damage
  // formula. The choice follows the job's stat rather than a list of jobs, so a
  // new magician branch works without changes.
  std::string atk_str;
  bool magic = primary == STAT_FIELD_INT;
  if (!magic && stats.attack() > 0) {
    atk_str = "+" + std::to_string(stats.attack()) + " ATT";
  } else if (stats.magic_attack() > 0) {
    atk_str = "+" + std::to_string(stats.magic_attack()) + " MATT";
  } else if (stats.attack() > 0) {
    atk_str = "+" + std::to_string(stats.attack()) + " ATT";
  }
  // Attack comes first because it decides a weapon. The main stat comes second.
  return PadRight(atk_str, 10) + PadRight(main_str, 10);
}

ItemCells EquipUpgradeCells(const EquipPrototype& proto, const Equip& state,
                            Job job, const ItemColumns& columns) {
  ItemCells cells;
  // The slot count is included so a row shows how far the item can still go,
  // not only how far it has come.
  int slots = TotalUpgradeSlots(proto);
  cells.scroll = slots > 0 ? std::to_string(state.scroll_successes()) + "/" +
                                 std::to_string(slots)
                           : "-";
  cells.stars = Supports(proto, UPGRADE_STAR_FORCE)
                    ? std::to_string(state.stars()) + "★"
                    : "-";
  cells.potential = PotentialCell(
      state.main_potential(), proto.required_level(), PrimaryStatField(job),
      SecondaryStatField(job), columns.Width(ItemColumn::kPotential));
  cells.potential_rank = state.main_potential().rank();
  cells.bonus_potential = PotentialCell(
      state.bonus_potential(), proto.required_level(), PrimaryStatField(job),
      SecondaryStatField(job), columns.Width(ItemColumn::kBonusPotential));
  cells.bonus_potential_rank = state.bonus_potential().rank();
  cells.flame =
      FlameCell(state.flame(), proto, PrimaryStatField(job),
                SecondaryStatField(job), columns.Width(ItemColumn::kFlame));
  return cells;
}

namespace {

// Folds one row's effects into what its column needs.
void Measure(const std::vector<std::string>& effects, EffectWidths& needs) {
  needs.measured = true;
  if (needs.widths.size() < effects.size()) {
    needs.widths.resize(effects.size(), needs.For(needs.widths.size()));
  }
  for (int n = 1; n <= static_cast<int>(needs.widths.size()); ++n) {
    needs.widths[n - 1] =
        std::max(needs.widths[n - 1], JoinedEffectsWidth(effects, n));
  }
}

}  // namespace

ItemListOptions ItemListOptionsFor(
    const std::vector<const EquipTabItem*>& items, bool bag,
    const CharacterInstance& character, const AccountInstance& account) {
  ItemListOptions options;
  options.bag = bag;
  options.scrolling = Unlocked(Feature::kScrolling, character, account);
  options.star_force = Unlocked(Feature::kStarForce, character, account);
  options.potential = Unlocked(Feature::kPotential, character, account);
  options.bonus_potential =
      Unlocked(Feature::kBonusPotential, character, account);
  options.flame = Unlocked(Feature::kFlame, character, account);
  const Job job = character.proto().job();
  options.potential_effects.measured = true;
  options.bonus_potential_effects.measured = true;
  options.flame_effects.measured = true;
  for (const EquipTabItem* row : items) {
    const EquipPrototype& proto = row->prototype();
    const Equip* item = &row->equip_state();
    Measure(
        PotentialCellEffects(item->main_potential(), proto.required_level(),
                             PrimaryStatField(job), SecondaryStatField(job)),
        options.potential_effects);
    Measure(
        PotentialCellEffects(item->bonus_potential(), proto.required_level(),
                             PrimaryStatField(job), SecondaryStatField(job)),
        options.bonus_potential_effects);
    Measure(FlameCellEffects(item->flame(), proto, PrimaryStatField(job),
                             SecondaryStatField(job)),
            options.flame_effects);
    options.scrolling |= item->scroll_successes() > 0;
    options.star_force |= item->stars() > 0;
    options.potential |=
        item->main_potential().rank() != POTENTIAL_RANK_UNSPECIFIED;
    options.bonus_potential |=
        item->bonus_potential().rank() != POTENTIAL_RANK_UNSPECIFIED;
    options.flame |= !item->flame().empty();
  }
  return options;
}

ItemRowText FormatItemRow(const ItemColumns& columns, const ItemCells& cells,
                          std::chrono::steady_clock::duration elapsed) {
  ItemRowText row;
  row.potential_rank = cells.potential_rank;
  row.bonus_potential_rank = cells.bonus_potential_rank;
  for (int i = 0; i < kNumItemColumns; ++i) {
    ItemColumn column = static_cast<ItemColumn>(i);
    if (!columns.Shows(column)) {
      continue;
    }
    int start = static_cast<int>(row.text.size());
    // The name follows the cursor column, which serves as its gap.
    if (column != ItemColumn::kName) {
      row.text.append(kItemCellGap, ' ');
    }
    row.text += column == ItemColumn::kName
                    ? ScrollingWindow(cells.name, columns.name_width, elapsed)
                    : PadRight(cells.Get(column), columns.Width(column));
    row.span[i] = {start, static_cast<int>(row.text.size()) - start};
  }
  return row;
}

ftxui::Decorator PotentialCellColor(PotentialRank rank) {
  return rank == POTENTIAL_RANK_UNSPECIFIED ? ftxui::nothing
                                            : ftxui::color(RarityColor(rank));
}

ftxui::Element ItemRowElement(const std::string& cursor, const ItemRowText& row,
                              ftxui::Decorator name) {
  const std::string& text = row.text;
  CellSpan name_span = row.Span(ItemColumn::kName);
  size_t at = std::min(static_cast<size_t>(name_span.offset + name_span.bytes),
                       text.size());
  std::vector<ftxui::Element> parts = {
      ftxui::text(cursor + text.substr(0, at)) | name};
  for (ItemColumn column :
       {ItemColumn::kPotential, ItemColumn::kBonusPotential}) {
    CellSpan span = row.Span(column);
    if (span.bytes == 0) {
      continue;
    }
    size_t start = std::max(at, static_cast<size_t>(span.offset));
    size_t end = std::min(start + static_cast<size_t>(span.bytes), text.size());
    parts.push_back(ftxui::text(text.substr(at, start - at)));
    parts.push_back(ftxui::text(text.substr(start, end - start)) |
                    PotentialCellColor(row.RankOf(column)));
    at = end;
  }
  parts.push_back(ftxui::text(text.substr(at)));
  return ftxui::hbox(std::move(parts));
}

}  // namespace ms
