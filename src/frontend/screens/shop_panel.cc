#include "src/frontend/screens/shop_panel.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/keys.h"
#include "src/frontend/widgets/marquee.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/item.h"
#include "src/item/shop.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {
namespace {

// Column widths. Name and level match the bag's Equip tab, so the same item
// looks the same in both. Type fits the longest weapon name, and cost fits the
// most expensive item on the shelf exactly.
constexpr int kNameWidth = 26;
constexpr int kTypeWidth = 16;
constexpr int kLevelWidth = 7;
constexpr int kCostWidth = 13;

// Stock rows on screen at once. Deep enough for most of a warrior's list, the
// longest of any class, and short enough that the window still fits a modest
// terminal.
constexpr int kVisibleRows = 15;

// Larger than any row index on one tab, so combining the tab and the row into
// one key can't make two selections collide.
constexpr int kNameClockTabStride = 4096;

// Every row and header ends one column before the border, and the scroll bar
// takes the column after that whether or not it is drawn. Held exactly, so the
// window is always one width: it is centred, and a price one digit longer than
// the last would otherwise shift the whole shop sideways.
constexpr int kContentWidth =
    2 + kNameWidth + 2 + kTypeWidth + 2 + kLevelWidth + kCostWidth + 1 + 1;

// The balance panel beside the shop: a space, the currency's mark, a space, and
// four digits for the count. Four is enough, since a token shelf charges tens
// per piece and nobody holds five digits of one.
constexpr int kTokenCountWidth = 4;
constexpr int kTokenPanelWidth = 1 + 1 + 1 + kTokenCountWidth + 1;
// The gap between the two windows, so their borders don't merge.
constexpr int kTokenPanelGap = 1;
// Rows inside the panel, matching the shop's: the two tab bars, their rule, the
// column header, its rule, and the stock. The same, so both windows end on the
// same line.
constexpr int kTokenPanelRows = 5 + kVisibleRows;
// The panel's columns are kept whether or not it is drawn, blank under a shelf
// that uses meso. The shop is centred, so a panel that came and went would
// shift the whole window sideways on every step along the pay bar.
constexpr int kTokenPanelBlock = kTokenPanelGap + kTokenPanelWidth + 2;

// A "<mark> <text>" cell, right-aligned in kCostWidth columns. A coin is two
// columns and a token's mark one, which PadLeft counts, so the cell is the same
// width whatever the number.
std::string MarkedCell(const std::string& mark, const std::string& text) {
  return PadLeft(mark + " " + text, kCostWidth);
}

std::string CoinCell(const std::string& text) {
  return MarkedCell("🪙", text);
}

// One row's cost cell. The mark keeps its own colour whatever the price does:
// red marks why a row is unaffordable, and the currency isn't the reason
// (colors.h).
ftxui::Element CostCell(const ItemPrototype* token, const std::string& text,
                        bool affordable) {
  if (token == nullptr) {
    return RedUnless(ftxui::text(CoinCell(text)), affordable);
  }
  ftxui::Element amount = RedUnless(ftxui::text(" " + text), affordable);
  // Padded by hand rather than by MarkedCell, since the mark is a separate
  // element so it can keep its own colour.
  int columns = TextColumns(token->currency_mark() + " " + text);
  return ftxui::hbox({
      ftxui::text(std::string(std::max(0, kCostWidth - columns), ' ')),
      ftxui::text(token->currency_mark()) |
          ftxui::color(MarkColor(token->currency_color())),
      std::move(amount),
  });
}

// Two leading spaces match the "  " / "> " cursor on the rows below. The cost
// column header shows the mark of its currency, and none where the shelf uses
// two, since every row shows its own and the header can't show both.
ftxui::Element ColumnHeader(const std::vector<const ItemPrototype*>& tokens) {
  std::string cost = CoinCell("Cost");
  if (tokens.size() == 1) {
    cost = MarkedCell(tokens.front()->currency_mark(), "Cost");
  } else if (!tokens.empty()) {
    cost = PadLeft("Cost", kCostWidth);
  }
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Type", kTypeWidth) + "  " +
                     PadRight("Level", kLevelWidth) + cost);
}

// The Etc shelf has no type or level to show, so the two columns between the
// name and the price become one: how many the player already owns. "Owned" is
// the word used for that everywhere, including the buy dialog.
ftxui::Element EtcColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Owned", kTypeWidth + 2 + kLevelWidth) +
                     CoinCell("Cost"));
}

// The buyback shelf has both kinds of item at once, so it shows what applies to
// only one of them: an equip comes back as the one item it was, and a stack
// comes back as many. The type is left to Inspect, since the player already
// knows an item they chose to sell.
ftxui::Element BuyBackColumnHeader() {
  return ftxui::text("  " + PadRight("Name", kNameWidth) + "  " +
                     PadRight("Qty", kTypeWidth + 2 + kLevelWidth) +
                     CoinCell("Cost"));
}

// What the type column shows for one item. An accessory has no equip type (the
// kind of ring doesn't matter), so it falls back to the slot, which is the
// column the bag shows for the same item.
std::string TypeCell(const EquipPrototype& proto) {
  std::string type = FormatEquipType(proto.equip_type());
  return type.empty() ? FormatSlot(proto.equip_slot()) : type;
}

// The level cell, e.g. "Lv30  ". An item with no level requirement shows level
// 1 instead of a blank, matching the bag.
std::string LevelCell(const EquipPrototype& proto) {
  int level = proto.required_level();
  if (level <= 0) {
    level = 1;
  }
  return PadRight("Lv" + std::to_string(level), kLevelWidth);
}

}  // namespace

ShopPanel::ShopPanel(const CharacterInstance& character,
                     const std::map<std::string, EquipPrototype>& equips,
                     const std::map<std::string, ItemPrototype>& items)
    : character_(character),
      equips_(equips),
      items_(items),
      menu_({"Inspect", "Buy", "Close"}) {
  Reset();
}

int ShopPanel::RowCount() const {
  if (tab_ == kShopBuyBackTab) {
    return character_.buy_backs().size();
  }
  return static_cast<int>(stock_.size());
}

void ShopPanel::Restock() {
  // Rebuilt rather than kept, because the shop stocks what this character's
  // class can use, and that changes when they advance. Cheap, since the catalog
  // is small and the screen opens on a keypress.
  stock_.clear();
  // The buyback shelf belongs to the character, not the shop. It is read where
  // it is stored, so a sale made while the screen is open appears without
  // restocking, and nothing here filters it: what a player sold is theirs to
  // buy back whatever their class or level is now.
  if (tab_ == kShopBuyBackTab) {
    return;
  }
  if (tab_ == kShopEtcTab) {
    stock_ = ShopEtcStock(items_);
    return;
  }
  Payment payment = pay_ == kShopTokenTab ? kPaidInTokens : kPaidInMeso;
  std::vector<std::string> shelf = tab_ == kShopEquipsTab
                                       ? ShopEquipStock(equips_, payment)
                                       : ShopWeaponStock(equips_, payment);
  for (const std::string& key : shelf) {
    if (character_.MeetsJob(equips_.at(key))) {
      stock_.push_back(key);
    }
  }
}

void ShopPanel::Reset() {
  tab_ = kShopWeaponTab;
  pay_ = kShopMesoTab;
  Restock();
  zone_ = kZoneList;
  selected_ = 0;
  first_visible_ = 0;
  menu_open_ = false;
}

void ShopPanel::StepTab(int direction) {
  int next = tab_ + direction;
  if (next < 0 || next >= kNumShopTabs) {
    return;  // the ends of the bar stop instead of wrapping
  }
  tab_ = next;
  Restock();
  selected_ = 0;
  first_visible_ = 0;
}

void ShopPanel::StepPayTab(int direction) {
  int next = pay_ + direction;
  if (next < 0 || next >= kNumShopPayTabs) {
    return;
  }
  pay_ = next;
  Restock();
  selected_ = 0;
  first_visible_ = 0;
}

bool ShopPanel::HasPayRow() const {
  return tab_ == kShopWeaponTab || tab_ == kShopEquipsTab;
}

void ShopPanel::MoveCursor(int delta) {
  int bars = HasPayRow() ? 2 : 1;
  int next = StepCursor(CursorStop(), delta, bars + RowCount());
  if (next < bars) {
    // The window stays where it is: the cursor has left the list rather than
    // moved within it, and it returns to the row it left.
    zone_ = next == 0 ? kZoneTabs : kZonePay;
    return;
  }
  zone_ = kZoneList;
  selected_ = next - bars;
  ScrollToCursor();
}

// The stops above the list: always the tab bar, plus the pay bar under the two
// tabs that have one.
int ShopPanel::CursorStop() const {
  if (zone_ == kZoneTabs) {
    return 0;
  }
  if (zone_ == kZonePay) {
    return 1;
  }
  return selected_ + (HasPayRow() ? 2 : 1);
}

void ShopPanel::ScrollToCursor() {
  first_visible_ = ScrollWindowStart(RowCount(), selected_, kVisibleRows);
}

void ShopPanel::OpenMenu() {
  if (zone_ != kZoneList) {
    // Nothing to open a menu on, since the cursor is on a bar, not an item.
    return;
  }
  if (selected_item() == nullptr && selected_stackable() == nullptr &&
      selected_buy_back() == nullptr) {
    return;
  }
  menu_.Reset();
  menu_open_ = true;
}

bool ShopPanel::menu_open() const {
  return menu_open_;
}

Screen ShopPanel::OnMenuEvent(ftxui::Event event) {
  if (IsBack(event)) {
    menu_open_ = false;
    return kShop;
  }
  if (event == ftxui::Event::ArrowUp) {
    menu_.Up();
    return kShopMenu;
  }
  if (event == ftxui::Event::ArrowDown) {
    menu_.Down();
    return kShopMenu;
  }
  if (IsForward(event)) {
    // Closed on the way out whichever entry was chosen, so the next screen
    // isn't drawn with the menu still over the list behind it.
    menu_open_ = false;
    if (menu_.selected() == kShopMenuInspect) {
      return kShopInspect;
    }
    if (menu_.selected() == kShopMenuBuy) {
      return kShopBuy;
    }
    return kShop;
  }
  // Consume everything else, since the menu is modal over the list.
  return kShopMenu;
}

const BuyBackEntry* ShopPanel::selected_buy_back() const {
  if (tab_ != kShopBuyBackTab || selected_ < 0 ||
      selected_ >= character_.buy_backs().size()) {
    return nullptr;
  }
  return &character_.buy_backs().Get(selected_);
}

const ItemPrototype* ShopPanel::RowToken(const EquipPrototype& proto) const {
  if (proto.token_item().empty()) {
    return nullptr;
  }
  std::map<std::string, ItemPrototype>::const_iterator it =
      items_.find(proto.token_item());
  return it == items_.end() ? nullptr : &it->second;
}

const ItemPrototype* ShopPanel::selected_token() const {
  // Checks the open shelf as well as the row, so the result always matches what
  // the header says, even if the two ever disagree.
  if (!HasPayRow() || pay_ != kShopTokenTab) {
    return nullptr;
  }
  const EquipPrototype* item = selected_item();
  return item == nullptr ? nullptr : RowToken(*item);
}

const EquipPrototype* ShopPanel::selected_item() const {
  if (tab_ == kShopEtcTab || tab_ == kShopBuyBackTab || selected_ < 0 ||
      selected_ >= static_cast<int>(stock_.size())) {
    return nullptr;
  }
  return &equips_.at(stock_[selected_]);
}

const ItemPrototype* ShopPanel::selected_stackable() const {
  if (tab_ != kShopEtcTab || selected_ < 0 ||
      selected_ >= static_cast<int>(stock_.size())) {
    return nullptr;
  }
  return &items_.at(stock_[selected_]);
}

bool ShopPanel::OnEvent(ftxui::Event event) {
  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
    MoveCursor(event == ftxui::Event::ArrowUp ? -1 : 1);
    return true;
  }
  // Left and Right belong to whichever bar has the cursor, and only while it is
  // on one. In the list they would quietly change the list under the cursor.
  if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
    int direction = event == ftxui::Event::ArrowLeft ? -1 : 1;
    if (zone_ == kZoneTabs) {
      StepTab(direction);
      return true;
    }
    if (zone_ == kZonePay) {
      StepPayTab(direction);
      return true;
    }
  }
  return false;
}

std::vector<const ItemPrototype*> ShopPanel::TabTokens() const {
  std::vector<const ItemPrototype*> tokens;
  if (!HasPayRow() || pay_ != kShopTokenTab) {
    return tokens;
  }
  // The unfiltered shelf, so the tab still knows its currencies even when the
  // class filter leaves nothing to show.
  std::vector<std::string> shelf =
      tab_ == kShopEquipsTab ? ShopEquipStock(equips_, kPaidInTokens)
                             : ShopWeaponStock(equips_, kPaidInTokens);
  for (const std::string& key : shelf) {
    std::map<std::string, ItemPrototype>::const_iterator it =
        items_.find(equips_.at(key).token_item());
    if (it == items_.end()) {
      continue;
    }
    const ItemPrototype* token = &it->second;
    if (std::find(tokens.begin(), tokens.end(), token) == tokens.end()) {
      tokens.push_back(token);
    }
  }
  return tokens;
}

ftxui::Element ShopPanel::RenderTabBar() const {
  // Chips are white while the bar has the cursor and theme blue otherwise,
  // which shows the player the arrow keys are on the bar.
  bool focused = zone_ == kZoneTabs;
  const std::vector<TabSpec> kTabs = {
      {"Weapon"}, {"Equips"}, {"Etc"}, {"Buy-Back"}};
  std::vector<ftxui::Element> chips;
  // No width limit: four fixed labels, and the shop's rows are far wider.
  chips.push_back(TabBar(kTabs, tab_, focused, /*width=*/0));
  // The counter goes in the space the chips leave rather than across the whole
  // row, since a centred counter would collide with a third chip. It always
  // shows meso: token balances are in the panel beside the window, where all
  // seven fit and a row of chips couldn't hold two.
  chips.push_back(ftxui::filler());
  chips.push_back(ftxui::text(FormatMeso(character_.meso())) |
                  ftxui::color(kTheme));
  chips.push_back(ftxui::filler());
  return ftxui::hbox(std::move(chips));
}

// A balance the player shops with: the currency's mark in its own colour, which
// is all that tells two currencies apart, and how many they have.
ftxui::Element ShopPanel::RenderTokenBalance(const ItemPrototype& token) const {
  return ftxui::hbox({
      ftxui::text(" "),
      ftxui::text(token.currency_mark()) |
          ftxui::color(MarkColor(token.currency_color())),
      ftxui::text(" " + PadLeft(std::to_string(character_.CountItem(token)),
                                kTokenCountWidth)) |
          ftxui::color(kTheme),
  });
}

ftxui::Element ShopPanel::RenderTokenPanel() const {
  std::vector<const ItemPrototype*> tokens = TabTokens();
  if (tokens.empty()) {
    return ftxui::text("") |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kTokenPanelBlock);
  }
  std::vector<ftxui::Element> rows;
  for (const ItemPrototype* token : tokens) {
    rows.push_back(RenderTokenBalance(*token));
  }
  while (static_cast<int>(rows.size()) < kTokenPanelRows) {
    rows.push_back(ftxui::text(""));
  }
  ftxui::Element body =
      ftxui::vbox(std::move(rows)) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kTokenPanelWidth);
  return ftxui::hbox({
      ftxui::text(std::string(kTokenPanelGap, ' ')),
      ThemedWindow(" Tokens ", std::move(body)),
  });
}

// Blank under a tab with nothing to choose, so the window is one height
// whichever tab is open. It is centred, and a row that came and went would move
// the whole shop up the screen.
ftxui::Element ShopPanel::RenderPayBar() const {
  if (!HasPayRow()) {
    return ftxui::text("");
  }
  const std::vector<TabSpec> kTabs = {{"Meso"}, {"Token"}};
  return TabBar(kTabs, pay_, zone_ == kZonePay, /*width=*/0);
}

// The price is red when the player can't pay it, so the list shows what they
// can buy without arithmetic on every row.
ftxui::Element ShopPanel::RenderEtcRow(
    const ItemPrototype& item, const std::string& cursor,
    std::chrono::steady_clock::duration elapsed) const {
  ftxui::Element cost =
      RedUnless(ftxui::text(CoinCell(FormatWithCommas(item.shop_price()))),
                item.shop_price() <= character_.meso());
  return ftxui::hbox({
      ftxui::text(cursor + ScrollingWindow(item.name(), kNameWidth, elapsed) +
                  "  " +
                  PadRight(FormatWithCommas(character_.CountItem(item)),
                           kTypeWidth + 2 + kLevelWidth)),
      std::move(cost),
      ftxui::text(" "),
  });
}

// The level is red by the bag's rule and in the bag's colour. There is no class
// to colour, since the list only has items for this character's class.
ftxui::Element ShopPanel::RenderEquipRow(
    const EquipPrototype& proto, const std::string& cursor,
    std::chrono::steady_clock::duration elapsed) const {
  ftxui::Element level =
      RedUnless(ftxui::text(LevelCell(proto)), character_.MeetsLevel(proto));
  // Each row is priced in its own currency: the shelf it is on says which, and
  // the item says how many.
  const ItemPrototype* token = RowToken(proto);
  int64_t price = token == nullptr ? proto.shop_price() : proto.token_price();
  int64_t held =
      token == nullptr ? character_.meso() : character_.CountItem(*token);
  ftxui::Element cost = CostCell(token, FormatWithCommas(price), price <= held);
  return ftxui::hbox({
      ftxui::text(cursor + ScrollingWindow(proto.name(), kNameWidth, elapsed) +
                  "  " +
                  // The type scrolls too: "Arrow for Crossbow" is wider than
                  // the column, and a cut type reads as a different item. This
                  // is the only other column that scrolls; the rest are sized
                  // to fit.
                  ScrollingWindow(TypeCell(proto), kTypeWidth, elapsed) + "  "),
      std::move(level),
      std::move(cost),
      ftxui::text(" "),
  });
}

ftxui::Element ShopPanel::RenderBuyBackRow(
    const BuyBackEntry& entry, const std::string& cursor,
    std::chrono::steady_clock::duration elapsed) const {
  // The name is the item's own, and a trace's name already says it is a trace.
  std::string name;
  std::string qty;
  if (entry.has_equip()) {
    const EquipPrototype* proto =
        FindEquipByName(equips_, entry.equip().equip_name());
    name = proto == nullptr ? entry.equip().equip_name() : proto->name();
    if (entry.equip().trace()) {
      name += " Trace";
    }
  } else {
    name = entry.stack().name();
    qty = FormatWithCommas(entry.stack().count());
  }
  ftxui::Element cost =
      RedUnless(ftxui::text(CoinCell(FormatWithCommas(entry.unit_price()))),
                entry.unit_price() <= character_.meso());
  return ftxui::hbox({
      ftxui::text(cursor + ScrollingWindow(name, kNameWidth, elapsed) + "  " +
                  PadRight(qty, kTypeWidth + 2 + kLevelWidth)),
      std::move(cost),
      ftxui::text(" "),
  });
}

ftxui::Element ShopPanel::RenderStock() const {
  std::vector<ftxui::Element> item_rows;
  if (RowCount() == 0) {
    // The game's standard word for an empty list. A shelf is empty for a reason
    // the player can already see: the tab they are on.
    item_rows.push_back(EmptyState("empty", /*gutter=*/2));
  }
  // The tab is part of the key along with the row, so the same row on another
  // tab counts as a different name and starts from the beginning.
  name_clock_.Follow(tab_ * kNameClockTabStride + selected_);
  int last = std::min(RowCount(), first_visible_ + kVisibleRows);
  for (int i = first_visible_; i < last; ++i) {
    bool selected = zone_ == kZoneList && i == selected_;
    std::string cursor = selected ? "> " : "  ";
    std::chrono::steady_clock::duration elapsed =
        selected ? name_clock_.Elapsed()
                 : std::chrono::steady_clock::duration::zero();
    // The band is applied here rather than in each of the three row functions:
    // the price is a name and a type away from the caret, and one rule covers
    // every shelf.
    ftxui::Element row;
    if (tab_ == kShopBuyBackTab) {
      row = RenderBuyBackRow(character_.buy_backs().Get(i), cursor, elapsed);
    } else if (tab_ == kShopEtcTab) {
      row = RenderEtcRow(items_.at(stock_[i]), cursor, elapsed);
    } else {
      row = RenderEquipRow(equips_.at(stock_[i]), cursor, elapsed);
    }
    item_rows.push_back(HighlightRow(std::move(row), selected));
  }
  // Padded to the full window, so the shop is one height whatever the tab
  // holds. It is centred, so a shelf two rows shorter than the last would
  // otherwise move the title, the bar and the column header up the screen.
  while (static_cast<int>(item_rows.size()) < kVisibleRows) {
    item_rows.push_back(ftxui::text(""));
  }
  return ftxui::hbox({
      ftxui::vbox(std::move(item_rows)),
      ScrollBar(RowCount(), first_visible_, kVisibleRows),
  });
}

ftxui::Element ShopPanel::Render() const {
  std::vector<ftxui::Element> rows;
  rows.push_back(RenderTabBar());
  rows.push_back(RenderPayBar());
  rows.push_back(ThemedSeparator());
  if (tab_ == kShopBuyBackTab) {
    rows.push_back(BuyBackColumnHeader());
  } else if (tab_ == kShopEtcTab) {
    rows.push_back(EtcColumnHeader());
  } else {
    rows.push_back(ColumnHeader(TabTokens()));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(RenderStock());
  // Held at one width for the same reason it is held at one height: a shelf
  // whose most expensive item has one more digit would widen the window, and a
  // centred window that changes width moves.
  ftxui::Element body = ftxui::vbox(std::move(rows)) |
                        ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth);
  // The balance panel's columns are part of the shop whichever shelf is open,
  // so the menu below still measures its column from the shop's own border.
  ftxui::Element window = ftxui::hbox({
      ThemedWindow(" Shop ", std::move(body)),
      RenderTokenPanel(),
  });
  if (!menu_open_) {
    return window;
  }
  // Placed relative to the panel rather than the terminal, since the shop is
  // centred and has no fixed position. kMenuCol clears the border and the name
  // column, so the menu covers the price rather than the item's name.
  constexpr int kMenuCol = 1 + 2 + kNameWidth;
  // Floated, so a menu opened on one of the last few items extends past the
  // bottom border instead of stretching the window. Moving it up to fit would
  // separate it from its item, making it look like a menu for another row.
  return ftxui::dbox({
      std::move(window),
      Floating(menu_.Render(MenuRow(), kMenuCol)),
  });
}

int ShopPanel::MenuRow() const {
  // +6 rows: the window's top border, the two tab rows, their separator, the
  // column header and its separator. Measured from the top of the window, so it
  // is the cursor's position in the scrolled view, not in the stock.
  constexpr int kFirstItemRow = 6;
  return kFirstItemRow + selected_ - first_visible_;
}

}  // namespace ms
