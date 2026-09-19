#include "src/frontend/widgets/inventory_list.h"

#include <algorithm>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/progression.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/item_row.h"
#include "src/frontend/widgets/marquee.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The columns each balance takes on a bar whose cursor can stand on it. Wide
// enough for the most either can hold -- a hundred billion meso and a million
// traces -- so the band is the same block however much is in it.
constexpr int kMesoCell = 20;
constexpr int kTraceCell = 14;

// The least the balances stand off the last tab chip. The bar is read left to
// right and the two run into each other without it: a count reads as part of
// the tab beside it.
constexpr int kBalanceGutter = 8;

// The row's cells hboxed together, with whichever affixes the caller brought.
ftxui::Element Row(ftxui::Element lead, std::vector<ftxui::Element> cells,
                   ftxui::Element tail, int body_width) {
  ftxui::Element body = ftxui::hbox(std::move(cells));
  if (body_width > 0) {
    body =
        std::move(body) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, body_width);
  }
  std::vector<ftxui::Element> row;
  if (lead != nullptr) {
    row.push_back(std::move(lead));
  }
  row.push_back(std::move(body));
  if (tail != nullptr) {
    row.push_back(std::move(tail));
  }
  return ftxui::hbox(std::move(row));
}

// The Token tab's four columns. The name widths are the longest the game
// ships -- "Frozen Secondary Token" and "Crimson Queen's" -- and two columns
// over, so the longest name keeps a gap before its count rather than running
// into it. The mark cell holds one glyph and the space after it.
constexpr int kCurrencyMarkWidth = 2;
constexpr int kTokenNameWidth = 24;
constexpr int kShardNameWidth = 17;
constexpr int kCurrencyCountWidth = 10;
// What separates the token half of a row from the shard half. Wider than the
// gap between ordinary columns: it is the seam between two lists, not between
// two columns of one.
constexpr char kCurrencyGap[] = "      ";

// Blanks as wide as a column pair, for the half of a row whose list ran out.
ftxui::Element BlankCurrencyCell(int mark_width, int name_width) {
  return ftxui::text(
      std::string(mark_width + name_width + kCurrencyCountWidth, ' '));
}

// A name and its count. The mark rides in front of a token and keeps its own
// colour, which is what says at a glance which piece the currency buys.
ftxui::Element CurrencyCell(const CurrencyAmount& held, bool marked,
                            int name_width) {
  const ItemPrototype& proto = held.prototype();
  std::string body =
      PadRight(ShortName(proto), name_width) +
      PadRight(FormatWithCommas(held.count()), kCurrencyCountWidth);
  if (!marked) {
    return ftxui::text(body);
  }
  return ftxui::hbox({
      ftxui::text(PadRight(proto.currency_mark(), kCurrencyMarkWidth)) |
          ftxui::color(MarkColor(proto.currency_color())),
      ftxui::text(body),
  });
}

}  // namespace

const char* const kInventoryTabLabels[kNumInventoryTabs] = {
    "Equip", "Token", "Etc", "Shop", "Bank"};

std::vector<int> AllRows(int count) {
  std::vector<int> rows(std::max(0, count));
  std::iota(rows.begin(), rows.end(), 0);
  return rows;
}

ftxui::Element CurrencyHeader() {
  return ftxui::text("  " + std::string(kCurrencyMarkWidth, ' ') +
                     PadRight("Token", kTokenNameWidth) +
                     PadRight("Quantity", kCurrencyCountWidth) + kCurrencyGap +
                     PadRight("Soul Shard", kShardNameWidth) + "Quantity");
}

ftxui::Element RenderCurrencyRow(const CurrencyAmount* token,
                                 const CurrencyAmount* shard) {
  return ftxui::hbox({
      ftxui::text("  "),
      token == nullptr ? BlankCurrencyCell(kCurrencyMarkWidth, kTokenNameWidth)
                       : CurrencyCell(*token, /*marked=*/true, kTokenNameWidth),
      ftxui::text(kCurrencyGap),
      shard == nullptr
          ? BlankCurrencyCell(0, kShardNameWidth)
          : CurrencyCell(*shard, /*marked=*/false, kShardNameWidth),
  });
}

std::vector<InventoryRowState> BuildEquipRows(
    const CharacterInstance& character, const InventoryInstance& items,
    int selected, std::chrono::steady_clock::duration elapsed,
    const ItemColumns& columns) {
  std::vector<InventoryRowState> rows;
  for (int i = 0; i < items.size(); ++i) {
    const EquipTabItem& item = items[i];
    const EquipPrototype& proto = item.prototype();
    int level = proto.required_level() > 0 ? proto.required_level() : 1;
    ItemCells cells =
        EquipUpgradeCells(proto, item.equip_state(), character.proto().job(),
                          columns.Width(ItemColumn::kPotential));
    cells.name = item.name();
    cells.slot = FormatSlot(proto.equip_slot());
    cells.level = "Lv" + std::to_string(level);
    cells.job = FormatJobCategories(proto);
    cells.stats = ItemStatsCell(character.proto().job(), item.stats());
    InventoryRowState row;
    // Only the selected row's name slides; the rest sit at their heads.
    std::chrono::steady_clock::duration slide =
        i == selected ? elapsed : std::chrono::steady_clock::duration::zero();
    row.label = FormatItemRow(columns, cells, slide);
    row.is_trace = item.is_trace();
    row.level_ok = character.MeetsLevel(proto);
    row.job_ok = character.MeetsJob(proto);
    rows.push_back(std::move(row));
  }
  return rows;
}

ftxui::Element EquipHeader(const ItemColumns& columns, ftxui::Element lead,
                           ftxui::Element tail, int body_width) {
  return Row(std::move(lead), {ftxui::text(ItemListHeader(columns))},
             std::move(tail), body_width);
}

ftxui::Element RenderEquipRow(const InventoryRowState& row, bool on_cursor,
                              ftxui::Element lead, ftxui::Element tail,
                              int body_width) {
  std::string cursor = on_cursor ? "> " : "  ";
  const ItemRowText& label = row.label;
  // A row nothing can be done with: too low for it, the wrong class for it, or
  // a trace, which is a record of an item rather than one. Dimmed whole, the
  // way the skills tab dims a skill that cannot be learned -- one answer for
  // "this row's action is shut", in both lists (colors.h).
  bool blocked = !row.level_ok || !row.job_ok || row.is_trace;
  if (!blocked) {
    return HighlightRow(Row(std::move(lead), {ftxui::text(cursor + label.text)},
                            std::move(tail), body_width),
                        on_cursor);
  }
  // The caret stays bright: it is the cursor, not part of the row.
  std::vector<ftxui::Element> cells = {ftxui::text(cursor)};
  for (int i = 0; i < kNumItemColumns; ++i) {
    ItemColumn column = static_cast<ItemColumn>(i);
    CellSpan span = label.Span(column);
    if (span.bytes == 0) {
      continue;
    }
    ftxui::Element cell =
        ftxui::text(label.text.substr(span.offset, span.bytes));
    // The cell that says WHY stays bright and red while the rest of the row
    // dims. Dimming it too would mute the one thing on the row worth reading.
    bool why = (column == ItemColumn::kLevel && !row.level_ok) ||
               (column == ItemColumn::kJob && !row.job_ok);
    cells.push_back(why ? std::move(cell) | ftxui::color(kRed)
                        : std::move(cell) | ftxui::dim);
  }
  return HighlightRow(
      Row(std::move(lead), std::move(cells), std::move(tail), body_width),
      on_cursor);
}

ftxui::Element StackHeader(ftxui::Element lead, ftxui::Element tail,
                           int body_width) {
  return Row(std::move(lead),
             {ftxui::text("  " + PadRight("Name", kItemNameWidth) +
                          PadRight("Quantity", 10))},
             std::move(tail), body_width);
}

ftxui::Element RenderStackRow(const StackableItem& stack, bool on_cursor,
                              std::chrono::steady_clock::duration elapsed,
                              ftxui::Element lead, ftxui::Element tail,
                              int body_width) {
  std::string cursor = on_cursor ? "> " : "  ";
  std::string text = cursor +
                     ScrollingWindow(stack.name(), kItemNameWidth, elapsed) +
                     PadRight(std::to_string(stack.count()), 10);
  return HighlightRow(
      Row(std::move(lead), {ftxui::text(text)}, std::move(tail), body_width),
      on_cursor);
}

ftxui::Element RenderBalances(int64_t meso, int64_t spell_traces,
                              const CharacterInstance& character,
                              const AccountInstance& account, int cursor) {
  const bool selectable = cursor != kNoBalance;
  std::vector<ftxui::Element> counters = {HighlightRow(
      ftxui::text(selectable ? PadRight(FormatMeso(meso), kMesoCell)
                             : FormatMeso(meso)) |
          ftxui::color(kTheme),
      cursor == kMesoBalance)};
  if (Unlocked(Feature::kShop, character, account)) {
    counters.push_back(ftxui::text("   "));
    counters.push_back(HighlightRow(
        ftxui::text(selectable
                        ? PadRight(FormatSpellTraces(spell_traces), kTraceCell)
                        : FormatSpellTraces(spell_traces)) |
            ftxui::color(kTheme),
        cursor == kTraceBalance));
  }
  return ftxui::hbox(std::move(counters));
}

// The least the balances stand off the last tab chip. The bar is read left to
// right and the two run into each other without it: a count reads as part of
// the tab beside it.

ftxui::Element RenderBagTabBar(const std::vector<TabSpec>& tabs, int active,
                               ftxui::Element balances, bool row_selected,
                               bool highlighted, ftxui::Element trailing,
                               int width, ftxui::Box& bar_box) {
  // Left to right: the chips, the balances, whatever trails them. No width
  // limit on the chips: a bag's tabs are a fixed set and all of them fit.
  ftxui::Element chips = TabBar(tabs, active, row_selected, /*width=*/0);
  int chips_width = ftxui::Dimension::Fit(chips).dimx;
  int balances_width = ftxui::Dimension::Fit(balances).dimx;
  // What the row has left once everything on it is drawn. The gutter gives
  // way to it rather than the other way round: a bar squeezed until the
  // balances fit is better than a number with digits cut off the end.
  int room = std::max(0, width - chips_width - balances_width -
                             ftxui::Dimension::Fit(trailing).dimx);
  int lead =
      std::max(kBalanceGutter, (width - balances_width) / 2 - chips_width);
  lead = std::min(lead, room);
  ftxui::Element tab_row = ftxui::hbox({
                               std::move(chips),
                               ftxui::text(std::string(lead, ' ')),
                               std::move(balances),
                               ftxui::filler(),
                               std::move(trailing),
                           }) |
                           ftxui::reflect(bar_box);
  return ftxui::vbox({
      std::move(tab_row),
      PanelSeparator(highlighted),
  });
}

ftxui::Element RenderStackList(const std::vector<StackableItem>& stacks,
                               const std::vector<int>& rows, int selected,
                               bool focused, ftxui::Box& cursor_box,
                               bool highlighted,
                               std::chrono::steady_clock::duration elapsed) {
  if (rows.empty()) {
    // No header over nothing, as on an empty Equip tab. Column names are there
    // to tell rows apart, and there are no rows to tell apart.
    return ftxui::vbox({EmptyState("empty", /*gutter=*/2), ftxui::filler()});
  }
  std::vector<ftxui::Element> drawn;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    // The cursor shows only while the list holds focus, but the selected row
    // is marked either way -- see below.
    ftxui::Element row = RenderStackRow(
        stacks[rows[i]], focused && i == selected,
        i == selected ? elapsed : std::chrono::steady_clock::duration::zero());
    if (i == selected) {
      // What the frame scrolls to. These rows are plain text rather than an
      // ftxui::Menu, so nothing else marks the cursor. Marked whether or not
      // the panel holds focus, so the view does not jump on the way back, and
      // reflected so the item menu knows the row to open beside.
      row = std::move(row) | ftxui::focus | ftxui::reflect(cursor_box);
    }
    drawn.push_back(std::move(row));
  }
  return ftxui::vbox({
      StackHeader(),
      PanelSeparator(highlighted),
      // Only the rows scroll; the header and its rule stay put.
      ftxui::vbox(std::move(drawn)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::flex,
  });
}

ftxui::Element RenderEquipList(const CharacterInstance& character,
                               const InventoryInstance& items,
                               const std::vector<int>& rows, int selected,
                               bool focused, const ItemColumns& columns,
                               ftxui::Box& cursor_box, bool highlighted,
                               std::chrono::steady_clock::duration elapsed) {
  if (rows.empty()) {
    return ftxui::vbox({EmptyState("empty", /*gutter=*/2), ftxui::filler()});
  }
  std::vector<InventoryRowState> built = BuildEquipRows(
      character, items,
      rows[std::clamp(selected, 0, static_cast<int>(rows.size()) - 1)], elapsed,
      columns);
  std::vector<ftxui::Element> drawn;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    ftxui::Element row =
        RenderEquipRow(built[rows[i]], focused && i == selected);
    if (i == selected) {
      row = std::move(row) | ftxui::focus | ftxui::reflect(cursor_box);
    }
    drawn.push_back(std::move(row));
  }
  return ftxui::vbox({
      EquipHeader(columns),
      PanelSeparator(highlighted),
      ftxui::vbox(std::move(drawn)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::flex,
  });
}

}  // namespace ms
