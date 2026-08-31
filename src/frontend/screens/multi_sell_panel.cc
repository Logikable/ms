#include "src/frontend/screens/multi_sell_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/item/item.h"

namespace ms {
namespace {

// The tabs of the bag Multi-Sell shows. The shop is not one of them: this is
// the counter, and the player is already standing at it.
constexpr int kTabs[] = {kEquipTab, kUseTab, kEtcTab};

// The mark column on the left, headed "Sell", and the price column on the
// right. The equip list is the widest thing on the screen, so the window is
// sized by it and every tab keeps the price under the same column.
constexpr int kMarkWidth = 6;
constexpr int kPriceWidth = 11;
constexpr int kEquipRowWidth = 82;
// Both columns keep a space inside the border, as every panel does.
constexpr int kContentWidth = kMarkWidth + kEquipRowWidth + 2 + kPriceWidth + 1;
// The window is centred, so a box that shrank to a short tab's contents would
// hang at a different height on every tab. It is held to one size instead, and
// a tab with few rows leaves the space below them empty.
constexpr int kContentHeight = 30;

// The price cell: two columns of separator, the value right-aligned, and a
// column of clearance inside the border. It sits at a fixed offset rather than
// at the row's right edge -- the scrolling frame gives a row fewer columns
// than the header beside it, so an edge is not a column two lists can share.
ftxui::Element TailCell(const std::string& text) {
  return ftxui::text("  " + PadLeft(text, kPriceWidth) + " ");
}

}  // namespace

const std::set<int>& SaleBasket::For(int tab) const {
  if (tab == kUseTab) {
    return use;
  }
  if (tab == kEtcTab) {
    return etc;
  }
  return equips;
}

std::set<int>& SaleBasket::For(int tab) {
  if (tab == kUseTab) {
    return use;
  }
  if (tab == kEtcTab) {
    return etc;
  }
  return equips;
}

bool SaleBasket::empty() const {
  return equips.empty() && use.empty() && etc.empty();
}

int64_t RowSellValue(const CharacterInstance& character, int tab, int row) {
  if (tab == kEquipTab) {
    if (row < 0 || row >= character.inventory().size()) {
      return 0;
    }
    // A trace is the record of a destroyed item, not a copy of it, so it is
    // worth what the record is worth.
    if (character.inventory().equip_instance(row) == nullptr) {
      return 0;
    }
    return character.inventory()[row].prototype().sell_price();
  }
  const std::vector<StackableItem>& stacks =
      character.stackables(TabCategory(tab));
  if (row < 0 || row >= static_cast<int>(stacks.size())) {
    return 0;
  }
  // The whole stack goes, so the whole stack is what the row is worth.
  return static_cast<int64_t>(stacks[row].count()) *
         stacks[row].prototype().sell_price();
}

int64_t BasketTotal(const CharacterInstance& character,
                    const SaleBasket& basket) {
  int64_t total = 0;
  for (int tab : kTabs) {
    for (int row : basket.For(tab)) {
      total += RowSellValue(character, tab, row);
    }
  }
  return total;
}

int64_t SellBasket(CharacterInstance& character, const SaleBasket& basket) {
  int64_t earned = 0;
  for (int i = static_cast<int>(std::size(kTabs)) - 1; i >= 0; --i) {
    int tab = kTabs[i];
    const std::set<int>& rows = basket.For(tab);
    for (std::set<int>::const_reverse_iterator it = rows.rbegin();
         it != rows.rend(); ++it) {
      if (tab == kEquipTab) {
        earned += character.SellEquip(*it);
      } else {
        ItemCategory category = TabCategory(tab);
        int count = character.stackables(category)[*it].count();
        earned += character.SellStackable(category, *it, count);
      }
    }
  }
  return earned;
}

MultiSellPanel::MultiSellPanel(const CharacterInstance& character)
    : character_(character) {
}

void MultiSellPanel::Reset(int tab, int row) {
  basket_ = SaleBasket();
  active_tab_ = tab == kUseTab || tab == kEtcTab ? tab : kEquipTab;
  selected_ = std::max(0, row);
  zone_ = kZoneList;
  cancel_focused_ = false;
  confirm_.Close();
  if (Markable(selected_)) {
    basket_.For(active_tab_).insert(selected_);
  }
}

int MultiSellPanel::ListCount() const {
  if (active_tab_ == kEquipTab) {
    return character_.inventory().size();
  }
  return static_cast<int>(
      character_.stackables(TabCategory(active_tab_)).size());
}

bool MultiSellPanel::Markable(int row) const {
  if (row < 0 || row >= ListCount()) {
    return false;
  }
  if (active_tab_ == kEquipTab) {
    return true;  // a trace goes too, and pays nothing
  }
  // A stack the shop will not pay for is one it will not take either: spell
  // traces, boss soul shards and the Frozen tokens are currency, and selling
  // a currency is not a thing the counter does.
  return RowSellValue(character_, active_tab_, row) > 0;
}

void MultiSellPanel::ToggleMark() {
  if (!Markable(selected_)) {
    return;
  }
  std::set<int>& marks = basket_.For(active_tab_);
  if (!marks.erase(selected_)) {
    marks.insert(selected_);
  }
}

int MultiSellPanel::CursorStop() const {
  if (zone_ == kZoneTabs) {
    return 0;
  }
  if (zone_ == kZoneButtons) {
    return ListCount() + 1;
  }
  return selected_ + 1;
}

void MultiSellPanel::MoveCursor(int delta) {
  int next = StepCursor(CursorStop(), delta, ListCount() + 2);
  if (next == 0) {
    zone_ = kZoneTabs;
    return;
  }
  if (next == ListCount() + 1) {
    zone_ = kZoneButtons;
    return;
  }
  zone_ = kZoneList;
  selected_ = next - 1;
}

void MultiSellPanel::StepTab(int direction) {
  int next = active_tab_ + direction;
  if (next < kEquipTab || next > kEtcTab) {
    return;  // the ends of the bar are walls, as in the bag
  }
  active_tab_ = next;
  selected_ = 0;
  // A tab with nothing in it has no row to stand on, so the cursor waits on
  // the bar until the player steps onto a tab that has.
  if (zone_ == kZoneList && ListCount() == 0) {
    zone_ = kZoneTabs;
  }
}

int64_t MultiSellPanel::Total() const {
  return BasketTotal(character_, basket_);
}

ConfirmChoice MultiSellPanel::OnEvent(ftxui::Event event) {
  // The prompt's own Cancel closes the prompt and no more: the player is
  // backing out of the question, not out of the screen.
  if (confirm_.open()) {
    ConfirmChoice choice = confirm_.OnEvent(std::move(event));
    return choice == ConfirmChoice::kConfirmed ? choice
                                               : ConfirmChoice::kPending;
  }
  if (IsBack(event)) {
    return ConfirmChoice::kCancelled;
  }
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
  } else if (event == ftxui::Event::ArrowLeft ||
             event == ftxui::Event::ArrowRight) {
    int direction = event == ftxui::Event::ArrowLeft ? -1 : 1;
    if (zone_ == kZoneTabs) {
      StepTab(direction);
    } else if (zone_ == kZoneButtons) {
      cancel_focused_ = direction > 0;
    }
  } else if (IsForward(event)) {
    if (zone_ == kZoneList) {
      ToggleMark();
    } else if (zone_ == kZoneButtons) {
      if (cancel_focused_) {
        return ConfirmChoice::kCancelled;
      }
      if (!basket_.empty()) {
        // Confirm is where the cursor lands. The player marked every row in
        // the basket themselves, so the dialog is a last look rather than a
        // question they have not already answered.
        confirm_.Open();
      }
    }
  }
  return ConfirmChoice::kPending;
}

ftxui::Element MultiSellPanel::MarkCell(int row) const {
  bool marked = basket_.For(active_tab_).count(row) > 0;
  return ftxui::text(marked ? "   ✓  " : "      ") | ftxui::color(kTheme);
}

ftxui::Element MultiSellPanel::PriceCell(int row) const {
  if (!Markable(row)) {
    // A row the counter will not take says so where its price would be.
    return TailCell("-") | ftxui::dim;
  }
  int64_t value = RowSellValue(character_, active_tab_, row);
  bool marked = basket_.For(active_tab_).count(row) > 0;
  ftxui::Element cell = TailCell(FormatWithCommas(value));
  // Gold on a row that is going: the price column then adds up to the total in
  // the header, and the marks and the money say the same thing.
  return marked ? std::move(cell) | ftxui::color(kGold) : std::move(cell);
}

ftxui::Element MultiSellPanel::RenderHeader() const {
  std::vector<TabSpec> specs;
  for (int tab : kTabs) {
    specs.push_back({kInventoryTabLabels[tab], /*unseen=*/false});
  }
  ftxui::Element total =
      ftxui::text("+" + FormatWithCommas(Total())) | ftxui::color(kGold);
  return ftxui::vbox({
      ftxui::hbox({
          TabBar(specs, active_tab_, zone_ == kZoneTabs, /*width=*/0),
          ftxui::filler(),
          ftxui::text(FormatMeso(character_.meso())) | ftxui::color(kTheme),
          ftxui::text("   "),
          std::move(total),
          ftxui::text(" "),
      }),
      ThemedSeparator(),
  });
}

ftxui::Element MultiSellPanel::RenderEquipTab() {
  rows_ = BuildEquipRows(character_, selected_, name_clock_.Elapsed());
  std::vector<ftxui::Element> list;
  for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
    bool on_cursor = zone_ == kZoneList && i == selected_;
    ftxui::Element row = RenderEquipRow(rows_[i], on_cursor, MarkCell(i),
                                        PriceCell(i), kEquipRowWidth);
    if (i == selected_) {
      row = std::move(row) | ftxui::focus;
    }
    list.push_back(std::move(row));
  }
  return ftxui::vbox({
      EquipHeader(ftxui::text(PadRight(" Sell", kMarkWidth)), TailCell("Price"),
                  kEquipRowWidth),
      ThemedSeparator(),
      ftxui::vbox(std::move(list)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::flex,
  });
}

ftxui::Element MultiSellPanel::RenderStackTab() {
  const std::vector<StackableItem>& stacks =
      character_.stackables(TabCategory(active_tab_));
  std::vector<ftxui::Element> list;
  for (int i = 0; i < static_cast<int>(stacks.size()); ++i) {
    bool on_cursor = zone_ == kZoneList && i == selected_;
    ftxui::Element row = RenderStackRow(
        stacks[i], on_cursor,
        i == selected_ ? name_clock_.Elapsed()
                       : std::chrono::steady_clock::duration::zero(),
        MarkCell(i), PriceCell(i), kEquipRowWidth);
    if (i == selected_) {
      row = std::move(row) | ftxui::focus;
    }
    list.push_back(std::move(row));
  }
  return ftxui::vbox({
      StackHeader(ftxui::text(PadRight(" Sell", kMarkWidth)), TailCell("Price"),
                  kEquipRowWidth),
      ThemedSeparator(),
      ftxui::vbox(std::move(list)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::flex,
  });
}

ftxui::Element MultiSellPanel::RenderList() {
  if (ListCount() == 0) {
    return ftxui::vbox(
        {EmptyState("empty", /*gutter=*/kMarkWidth + 2), ftxui::filler()});
  }
  selected_ = std::min(selected_, ListCount() - 1);
  if (active_tab_ == kEquipTab) {
    return RenderEquipTab();
  }
  return RenderStackTab();
}

ftxui::Element MultiSellPanel::Render() {
  name_clock_.Follow(active_tab_ * kNumInventoryTabs + selected_);
  ftxui::Element body = RenderList();
  return ThemedWindow(
      " Multi-Sell ",
      ftxui::vbox({
          RenderHeader(),
          std::move(body) | ftxui::flex,
          ThemedSeparator(),
          ftxui::hbox({ButtonRow(
              "Confirm", "Cancel", zone_ == kZoneButtons && !cancel_focused_,
              zone_ == kZoneButtons && cancel_focused_, !basket_.empty())}) |
              ftxui::hcenter,
      }) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth) |
          ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kContentHeight));
}

ftxui::Element MultiSellPanel::RenderConfirm() const {
  return DialogWindow(
      " Confirm Sale ",
      {
          CenteredRow("Are you sure?"),
          CenteredRow(ftxui::text(FormatMeso(Total())) | ftxui::color(kGold)),
      },
      confirm_.Render());
}

}  // namespace ms
