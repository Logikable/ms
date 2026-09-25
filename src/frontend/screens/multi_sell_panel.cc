#include "src/frontend/screens/multi_sell_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/progression.h"
#include "src/frontend/placement.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/item/item.h"

namespace ms {
namespace {

// The bag tabs Multi-Sell shows. The shop isn't one of them, since the player
// is already at the shop counter.
constexpr int kTabs[] = {kEquipTab, kEtcTab};

// Larger than any row index on one tab, so combining the tab and the row into
// one name-clock key can't make two selections collide. The bag has its own
// copy of this for the same reason.
constexpr int kNameClockTabStride = 4096;

// The mark column on the left, headed "Sell", and the price column on the
// right. The equip list is the widest thing on the screen, so it sets the
// window's width and every tab keeps the price in the same column.
constexpr int kMarkWidth = 6;
constexpr int kPriceWidth = 11;
constexpr int kEquipRowWidth = 82;
// Both columns keep a space inside the border, as every panel does.
constexpr int kContentWidth = kMarkWidth + kEquipRowWidth + 2 + kPriceWidth + 1;
// The window is centred, so a box that shrank to a short tab's contents would
// sit at a different height on every tab. It is held to one size instead, and a
// tab with few rows leaves the space below empty. This is the smallest
// terminal's height minus the window's two borders.
constexpr int kContentHeight = kMinTerminalRows - 2;

// The price cell: a two-column separator, the right-aligned value, and a blank
// column inside the border. It sits at a fixed offset rather than at the row's
// right edge, because the scrolling frame gives a row fewer columns than the
// header beside it, so the edge isn't a column two lists can share.
ftxui::Element TailCell(const std::string& text) {
  return ftxui::text("  " + PadLeft(text, kPriceWidth) + " ");
}

// The stacks the Etc tab lists, as indices into `character`'s stacks: all of
// them. Currencies aren't included, since they are kept in the purse rather
// than carried, and none are for sale.
std::vector<int> EtcRows(const CharacterInstance& character) {
  return AllRows(static_cast<int>(character.stackables().size()));
}

}  // namespace

const std::set<int>& SaleBasket::For(int tab) const {
  return tab == kEtcTab ? etc : equips;
}

std::set<int>& SaleBasket::For(int tab) {
  return tab == kEtcTab ? etc : equips;
}

bool SaleBasket::empty() const {
  return equips.empty() && etc.empty();
}

int64_t RowSellValue(const CharacterInstance& character, int tab, int item) {
  if (tab == kEquipTab) {
    if (item < 0 || item >= character.inventory().size()) {
      return 0;
    }
    // A trace records a destroyed item rather than being a copy of it, so it is
    // worth what the record is worth.
    if (character.inventory().equip_instance(item) == nullptr) {
      return 0;
    }
    return character.inventory()[item].prototype().sell_price();
  }
  const std::vector<StackableItem>& stacks = character.stackables();
  if (item < 0 || item >= static_cast<int>(stacks.size())) {
    return 0;
  }
  // The whole stack is sold, so the whole stack is its value.
  return static_cast<int64_t>(stacks[item].count()) *
         stacks[item].prototype().sell_price();
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
        int count = character.stackables()[*it].count();
        earned += character.SellStackable(*it, count);
      }
    }
  }
  return earned;
}

MultiSellPanel::MultiSellPanel(const CharacterInstance& character,
                               const AccountInstance& account)
    : character_(character), account_(account) {
}

void MultiSellPanel::Reset(int tab, int item) {
  basket_ = SaleBasket();
  active_tab_ = tab == kEtcTab ? kEtcTab : kEquipTab;
  selected_ = 0;
  if (active_tab_ == kEquipTab) {
    selected_ = std::max(0, item);
  } else {
    // The caller names a stack, and the cursor goes to the row that shows it.
    std::vector<int> rows = EtcRows(character_);
    std::vector<int>::iterator it = std::find(rows.begin(), rows.end(), item);
    if (it != rows.end()) {
      selected_ = static_cast<int>(it - rows.begin());
    }
  }
  zone_ = kZoneList;
  cancel_focused_ = false;
  confirm_.Close();
  if (Markable(selected_)) {
    basket_.For(active_tab_).insert(BasketKey(selected_));
  }
}

int MultiSellPanel::ListCount() const {
  if (active_tab_ == kEquipTab) {
    return character_.inventory().size();
  }
  return static_cast<int>(EtcRows(character_).size());
}

int MultiSellPanel::BasketKey(int row) const {
  if (active_tab_ == kEquipTab) {
    return row;
  }
  std::vector<int> rows = EtcRows(character_);
  if (row < 0 || row >= static_cast<int>(rows.size())) {
    return -1;
  }
  return rows[row];
}

// Every row can be marked, whatever it is worth: a trace of a destroyed item
// pays nothing, and selling it is how it leaves the bag.
bool MultiSellPanel::Markable(int row) const {
  return row >= 0 && row < ListCount();
}

void MultiSellPanel::ToggleMark() {
  if (!Markable(selected_)) {
    return;
  }
  std::set<int>& marks = basket_.For(active_tab_);
  int key = BasketKey(selected_);
  if (!marks.erase(key)) {
    marks.insert(key);
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
  // The bar has only the two tabs in kTabs (the bag's Token tab isn't here), so
  // a step moves through that list rather than the enum.
  const int* here = std::find(std::begin(kTabs), std::end(kTabs), active_tab_);
  const int* next = here + direction;
  if (next < std::begin(kTabs) || next >= std::end(kTabs)) {
    return;  // the ends of the bar stop, as in the bag
  }
  active_tab_ = *next;
  selected_ = 0;
  // A tab with nothing in it has no row to select, so the cursor waits on the
  // bar until the player moves to a tab with rows.
  if (zone_ == kZoneList && ListCount() == 0) {
    zone_ = kZoneTabs;
  }
}

int64_t MultiSellPanel::Total() const {
  return BasketTotal(character_, basket_);
}

ConfirmChoice MultiSellPanel::OnEvent(ftxui::Event event) {
  // The dialog's own Cancel closes only the dialog: the player is backing out
  // of the question, not the screen.
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
        // The cursor starts on Confirm. The player marked every item
        // themselves, so the dialog is a last look rather than a new question.
        confirm_.Open();
      }
    }
  }
  return ConfirmChoice::kPending;
}

ftxui::Element MultiSellPanel::MarkCell(int row) const {
  bool marked = basket_.For(active_tab_).count(BasketKey(row)) > 0;
  return ftxui::text(marked ? "   ✓  " : "      ") | ftxui::color(kTheme);
}

ftxui::Element MultiSellPanel::PriceCell(int row) const {
  int key = BasketKey(row);
  int64_t value = RowSellValue(character_, active_tab_, key);
  bool marked = basket_.For(active_tab_).count(key) > 0;
  ftxui::Element cell = TailCell(FormatWithCommas(value));
  // Gold on a row being sold, so the gold prices add up to the total in the
  // header and the marks and the money agree.
  return marked ? std::move(cell) | ftxui::color(kGold) : std::move(cell);
}

ftxui::Element MultiSellPanel::RenderHeader() const {
  // The bar is indexed by position in kTabs, not by the bag's tab id.
  std::vector<TabSpec> specs;
  int active = 0;
  for (int tab : kTabs) {
    if (tab == active_tab_) {
      active = static_cast<int>(specs.size());
    }
    specs.push_back({kInventoryTabLabels[tab], /*unseen=*/false});
  }
  ftxui::Element total =
      ftxui::text("+" + FormatWithCommas(Total())) | ftxui::color(kGold);
  return ftxui::vbox({
      ftxui::hbox({
          TabBar(specs, active, zone_ == kZoneTabs, /*width=*/0),
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
  ItemListOptions options;
  options.bag = true;
  options.scrolling = Unlocked(Feature::kScrolling, character_, account_);
  options.star_force = Unlocked(Feature::kStarForce, character_, account_);
  options.potential = Unlocked(Feature::kPotential, character_, account_);
  ItemColumns columns = FitItemColumns(kEquipRowWidth, options);
  rows_ = BuildEquipRows(character_, character_.inventory(), selected_,
                         name_clock_.Elapsed(), columns);
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
      EquipHeader(columns, ftxui::text(PadRight(" Sell", kMarkWidth)),
                  TailCell("Price"), kEquipRowWidth),
      ThemedSeparator(),
      ftxui::vbox(std::move(list)) | ftxui::vscroll_indicator | ftxui::yframe |
          ftxui::flex,
  });
}

ftxui::Element MultiSellPanel::RenderStackTab() {
  const std::vector<StackableItem>& stacks = character_.stackables();
  std::vector<int> rows = EtcRows(character_);
  std::vector<ftxui::Element> list;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    bool on_cursor = zone_ == kZoneList && i == selected_;
    ftxui::Element row = RenderStackRow(
        stacks[rows[i]], on_cursor,
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
  name_clock_.Follow(active_tab_ * kNameClockTabStride + selected_);
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
